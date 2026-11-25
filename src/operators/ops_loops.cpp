#include "ops_loops.h"
#include "ops_generic.h"
#include "ops_hashmap.h"
#include "../runtime/value.h"
#include "../runtime/logging.h"
#include "../runtime/runtime.h"
#include "../runtime/sqfop.h"
#include "../runtime/d_array.h"
#include "../runtime/d_boolean.h"
#include "../runtime/d_code.h"
#include "../runtime/d_scalar.h"

#include <chrono>
#include <optional>

namespace err = logmessage::runtime;
using namespace sqf::runtime;
using namespace sqf::types;

namespace
{
    // Shortest suspension when a wait has to retry, roughly one simulated frame.
    constexpr std::chrono::milliseconds minimum_wait_step(10);

    /// Pops every frame above the innermost loop frame and clears the loop frame's value stack.
    /// Returns false, after logging, when the command is not used inside a loop.
    bool unwind_to_innermost_loop(runtime& runtime, const char* command)
    {
        auto& context = runtime.context_active();
        size_t frames_above_loop = 0;
        bool found_loop = false;
        for (auto it = context.frames_rbegin(); it != context.frames_rend(); ++it, ++frames_above_loop)
        {
            if (it->is_loop())
            {
                found_loop = true;
                break;
            }
        }
        if (!found_loop)
        {
            runtime.__logmsg(err::ErrorMessage(context.current_frame().diag_info_from_position(), command, "used outside of a loop"));
            return false;
        }
        for (; frames_above_loop > 0; --frames_above_loop)
        {
            context.pop_frame();
        }
        context.clear_values();
        return true;
    }

    value break_(runtime& runtime)
    {
        if (unwind_to_innermost_loop(runtime, "break"))
        {
            runtime.context_active().pop_frame();
        }
        return {};
    }
    value breakwith_any(runtime& runtime, value::cref right)
    {
        if (!unwind_to_innermost_loop(runtime, "breakWith"))
        {
            return {};
        }
        runtime.context_active().pop_frame();
        return right;
    }
    value continue_(runtime& runtime)
    {
        if (unwind_to_innermost_loop(runtime, "continue"))
        {
            // Seeking to the end makes the loop's exit behavior run next, which starts the next iteration.
            runtime.context_active().current_frame().seek(0, frame::seekpos::end);
        }
        return {};
    }
    value continuewith_any(runtime& runtime, value::cref right)
    {
        if (!unwind_to_innermost_loop(runtime, "continueWith"))
        {
            return {};
        }
        runtime.context_active().current_frame().seek(0, frame::seekpos::end);
        return right;
    }

    value foreachreversed_code_array(runtime& runtime, value::cref left, value::cref right)
    {
        class behavior_foreachreversed_exit : public frame::behavior
        {
        private:
            std::shared_ptr<d_array> m_array;
            size_t m_index;
        public:
            behavior_foreachreversed_exit(std::shared_ptr<d_array> arr, size_t start) : m_array(arr), m_index(start) {}
            virtual result enact(sqf::runtime::runtime& runtime, sqf::runtime::frame& frame) override
            {
                // Deleting elements during the loop is allowed, so the index is clamped to the current size.
                if (m_index == 0 || m_array->size() == 0)
                {
                    return result::ok;
                }
                m_index = std::min(m_index - 1, m_array->size() - 1);
                runtime.context_active().clear_values();
                frame.clear_value_scope();
                frame["_forEachIndex"] = m_index;
                frame["_x"] = m_array->at(m_index);
                return result::seek_start;
            }
        };
        auto arr = right.data<d_array>();
        if (arr->size() == 0)
        {
            return {};
        }
        auto last = arr->size() - 1;
        frame f(runtime.default_value_scope(), left.data<d_code, instruction_set>(), std::make_shared<behavior_foreachreversed_exit>(arr, last));
        f.is_loop(true);
        f["_forEachIndex"] = last;
        f["_x"] = arr->at(last);
        runtime.context_active().push_frame(f);
        return {};
    }

    /// Snapshot of a HashMap's pairs, so the loop is not affected by changes to the map.
    std::vector<std::pair<value, value>> hashmap_pairs(std::shared_ptr<d_hashmap> map)
    {
        return { map->map().begin(), map->map().end() };
    }

    value foreach_code_hashmap(runtime& runtime, value::cref left, value::cref right)
    {
        class behavior_foreach_hashmap_exit : public frame::behavior
        {
        private:
            std::vector<std::pair<value, value>> m_pairs;
            size_t m_index;
        public:
            behavior_foreach_hashmap_exit(std::vector<std::pair<value, value>> pairs) : m_pairs(std::move(pairs)), m_index(0) {}
            virtual result enact(sqf::runtime::runtime& runtime, sqf::runtime::frame& frame) override
            {
                if (++m_index >= m_pairs.size())
                {
                    return result::ok;
                }
                runtime.context_active().clear_values();
                frame.clear_value_scope();
                frame["_forEachIndex"] = m_index;
                frame["_x"] = m_pairs[m_index].first;
                frame["_y"] = m_pairs[m_index].second;
                return result::seek_start;
            }
        };
        auto pairs = hashmap_pairs(right.data<d_hashmap>());
        if (pairs.empty())
        {
            return {};
        }
        auto first = pairs.front();
        frame f(runtime.default_value_scope(), left.data<d_code, instruction_set>(), std::make_shared<behavior_foreach_hashmap_exit>(pairs));
        f.is_loop(true);
        f["_forEachIndex"] = 0;
        f["_x"] = first.first;
        f["_y"] = first.second;
        runtime.context_active().push_frame(f);
        return {};
    }

    value apply_hashmap_code(runtime& runtime, value::cref left, value::cref right)
    {
        class behavior_apply_hashmap_exit : public frame::behavior
        {
        private:
            std::vector<std::pair<value, value>> m_pairs;
            std::vector<value> m_results;
            size_t m_index;
        public:
            behavior_apply_hashmap_exit(std::vector<std::pair<value, value>> pairs) : m_pairs(std::move(pairs)), m_index(0) {}
            virtual result enact(sqf::runtime::runtime& runtime, sqf::runtime::frame& frame) override
            {
                auto res = runtime.context_active().pop_value();
                m_results.push_back(res.has_value() ? *res : value{});
                if (++m_index >= m_pairs.size())
                {
                    runtime.context_active().clear_values();
                    runtime.context_active().push_value(value(std::make_shared<d_array>(m_results)));
                    return result::ok;
                }
                runtime.context_active().clear_values();
                frame.clear_value_scope();
                frame["_x"] = m_pairs[m_index].first;
                frame["_y"] = m_pairs[m_index].second;
                return result::seek_start;
            }
        };
        auto pairs = hashmap_pairs(left.data<d_hashmap>());
        if (pairs.empty())
        {
            return value(std::make_shared<d_array>());
        }
        auto first = pairs.front();
        frame f(runtime.default_value_scope(), right.data<d_code, instruction_set>(), std::make_shared<behavior_apply_hashmap_exit>(pairs));
        f.is_loop(true);
        f["_x"] = first.first;
        f["_y"] = first.second;
        runtime.context_active().push_frame(f);
        return {};
    }

    /// waitUntil [condition, timeout, interval] (2.22): returns the condition's value, or nil on timeout.
    value waituntil_array(runtime& runtime, value::cref right)
    {
        class behavior_waituntil_timeout_exit : public frame::behavior
        {
        private:
            std::chrono::system_clock::time_point m_deadline;
            bool m_has_deadline;
            std::chrono::milliseconds m_interval;
        public:
            behavior_waituntil_timeout_exit(float timeout, float interval) :
                m_deadline(std::chrono::system_clock::now() + std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<float>(timeout))),
                m_has_deadline(timeout >= 0),
                m_interval(std::max(minimum_wait_step, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<float>(interval))))
            {}
            virtual result enact(sqf::runtime::runtime& runtime, sqf::runtime::frame& frame) override
            {
                auto res = runtime.context_active().pop_value();
                if (res.has_value())
                {
                    auto condition = res->data_try<d_boolean, bool>();
                    if (!condition.has_value())
                    {
                        runtime.__logmsg(err::TypeMissmatch(frame.diag_info_from_position(), t_boolean(), res->type()));
                        runtime.context_active().clear_values();
                        runtime.context_active().push_value(value{});
                        return result::ok;
                    }
                    if (*condition)
                    {
                        runtime.context_active().clear_values();
                        runtime.context_active().push_value(*res);
                        return result::ok;
                    }
                }
                // The timeout is only checked after the condition, as in the engine.
                if (m_has_deadline && std::chrono::system_clock::now() >= m_deadline)
                {
                    // The result is nil, so that assigning it works as in the engine.
                    runtime.context_active().clear_values();
                    runtime.context_active().push_value(value{});
                    return result::ok;
                }
                runtime.context_active().suspend(m_interval);
                runtime.context_active().clear_values();
                frame.clear_value_scope();
                return result::seek_start;
            }
        };

        auto& context = runtime.context_active();
        auto arr = right.data<d_array>();
        if (arr->size() < 2)
        {
            runtime.__logmsg(err::ExpectedMinimumArraySizeMissmatch(context.current_frame().diag_info_from_position(), 2, arr->size()));
            return {};
        }
        if (!arr->at(0).is<t_code>() || !arr->at(1).is<t_scalar>() || (arr->size() >= 3 && !arr->at(2).is<t_scalar>()))
        {
            runtime.__logmsg(err::ErrorMessage(context.current_frame().diag_info_from_position(), "waitUntil", "expected [condition, timeout, interval]"));
            return {};
        }
        if (!context.can_suspend())
        {
            runtime.__logmsg(err::SuspensionInUnscheduledEnvironment(context.current_frame().diag_info_from_position()));
            return {};
        }
        auto timeout = arr->at(1).data<d_scalar, float>();
        auto interval = arr->size() >= 3 ? arr->at(2).data<d_scalar, float>() : 0.0f;
        frame f(runtime.default_value_scope(), arr->at(0).data<d_code, instruction_set>(), std::make_shared<behavior_waituntil_timeout_exit>(timeout, interval));
        context.push_frame(f);
        return {};
    }

    /// Suspends and runs the current waitUntil instruction again with `retry_argument` as its argument.
    value retry_wait(runtime& runtime, value retry_argument)
    {
        auto& context = runtime.context_active();
        context.current_frame().previous();
        context.suspend(minimum_wait_step);
        return retry_argument;
    }

    /// waitUntil scriptHandle (2.22). SQF-VM does not keep script results, so this returns nil.
    value waituntil_script(runtime& runtime, value::cref right)
    {
        auto& context = runtime.context_active();
        if (!context.can_suspend())
        {
            runtime.__logmsg(err::SuspensionInUnscheduledEnvironment(context.current_frame().diag_info_from_position()));
            return {};
        }
        if (right.data<d_script>()->done())
        {
            return {};
        }
        return retry_wait(runtime, right);
    }

    /// waitUntil [scriptHandle, timeout] (2.22). A hidden third element carries the deadline between retries.
    value waituntil_array_script(runtime& runtime, value::cref right)
    {
        auto& context = runtime.context_active();
        auto arr = right.data<d_array>();
        if (arr->size() < 2 || !arr->at(1).is<t_scalar>())
        {
            runtime.__logmsg(err::ErrorMessage(context.current_frame().diag_info_from_position(), "waitUntil", "expected [scriptHandle, timeout]"));
            return {};
        }
        if (!context.can_suspend())
        {
            runtime.__logmsg(err::SuspensionInUnscheduledEnvironment(context.current_frame().diag_info_from_position()));
            return {};
        }
        if (arr->at(0).data<d_script>()->done())
        {
            return {};
        }
        // Seconds since the VM started, like the time command, so a float keeps enough precision.
        using namespace std::chrono;
        auto now = duration_cast<milliseconds>(system_clock::now() - runtime.runtime_timestamp()).count() * 0.001f;
        auto deadline = arr->size() >= 3
            ? arr->at(2).data<d_scalar, float>()
            : now + arr->at(1).data<d_scalar, float>();
        if (arr->at(1).data<d_scalar, float>() >= 0 && now >= deadline)
        {
            return {};
        }
        std::vector<value> retry = { arr->at(0), arr->at(1), deadline };
        return retry_wait(runtime, value(std::make_shared<d_array>(retry)));
    }

    value waituntil_array_dispatch(runtime& runtime, value::cref right)
    {
        auto arr = right.data<d_array>();
        if (arr->size() >= 1 && arr->at(0).is<t_script>())
        {
            return waituntil_array_script(runtime, right);
        }
        return waituntil_array(runtime, right);
    }
}

void sqf::operators::ops_loops(::sqf::runtime::runtime& runtime)
{
    using namespace sqf::runtime::sqfop;
    runtime.register_sqfop(nular("break", "Skips all remaining loop iterations.", break_));
    runtime.register_sqfop(unary("breakWith", t_any(), "Skips all remaining loop iterations and returns the given value from the loop.", breakwith_any));
    runtime.register_sqfop(nular("continue", "Skips the rest of the current loop iteration and continues with the next one.", continue_));
    runtime.register_sqfop(unary("continueWith", t_any(), "Skips the rest of the current loop iteration, continues with the next one and uses the given value as the iteration's result.", continuewith_any));
    runtime.register_sqfop(binary(4, "forEachReversed", t_code(), t_array(), "Executes the code for every element of the array, from the last to the first. Elements may be deleted during the loop.", foreachreversed_code_array));
    runtime.register_sqfop(binary(4, "forEach", t_code(), t_hashmap(), "Executes the code for every key-value pair of the HashMap. The key is in _x and the value in _y.", foreach_code_hashmap));
    runtime.register_sqfop(binary(4, "apply", t_hashmap(), t_code(), "Executes the code for every key-value pair of the HashMap (_x key, _y value) and returns an array of the results.", apply_hashmap_code));
    runtime.register_sqfop(unary("waitUntil", t_array(), "waitUntil [condition, timeout, interval] or waitUntil [scriptHandle, timeout]: waits until the condition is true, the script is done or the timeout passes.", waituntil_array_dispatch));
    runtime.register_sqfop(unary("waitUntil", t_script(), "Waits until the script is done.", waituntil_script));
}
