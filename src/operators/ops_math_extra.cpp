#include "ops_math_extra.h"
#include "d_text.h"
#include "../runtime/value.h"
#include "../runtime/logging.h"
#include "../runtime/runtime.h"
#include "../runtime/sqfop.h"
#include "../runtime/d_array.h"
#include "../runtime/d_boolean.h"
#include "../runtime/d_scalar.h"
#include "../runtime/d_string.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <optional>
#include <random>
#include <string>
#include <cstring>
#include <tuple>

namespace err = logmessage::runtime;
using namespace sqf::runtime;
using namespace sqf::types;

namespace
{
    constexpr double pi = 3.14159265358979323846;

    sqf::runtime::diagnostics::diag_info position(runtime& runtime)
    {
        return runtime.context_active().current_frame().diag_info_from_position();
    }

    /// Reads a 2D or 3D vector; a missing z is 0, as the engine does since 2.00.
    std::optional<std::array<float, 3>> read_vector(value::cref source)
    {
        auto arr = source.data_try<d_array>();
        if (!arr || arr->size() < 2 || arr->size() > 3)
        {
            return {};
        }
        std::array<float, 3> out{ 0, 0, 0 };
        for (size_t i = 0; i < arr->size(); i++)
        {
            if (!arr->at(i).is<t_scalar>())
            {
                return {};
            }
            out[i] = arr->at(i).data<d_scalar, float>();
        }
        return out;
    }

    value make_vector(const std::array<float, 3>& vector)
    {
        return std::make_shared<d_array>(std::vector<value>{ vector[0], vector[1], vector[2] });
    }

    std::optional<std::vector<float>> read_numbers(runtime& runtime, value::cref source, size_t minimum, size_t maximum, const char* command)
    {
        auto arr = source.data<d_array>();
        if (arr->size() < minimum || arr->size() > maximum)
        {
            runtime.__logmsg(err::ExpectedArraySizeMissmatch(position(runtime), minimum, maximum, arr->size()));
            return {};
        }
        std::vector<float> numbers;
        for (size_t i = 0; i < arr->size(); i++)
        {
            if (arr->at(i).is<t_boolean>())
            {
                numbers.push_back(arr->at(i).data<d_boolean, bool>() ? 1.0f : 0.0f);
                continue;
            }
            if (!arr->at(i).is<t_scalar>())
            {
                runtime.__logmsg(err::ExpectedArrayTypeMissmatch(position(runtime), i, t_scalar(), arr->at(i).type()));
                return {};
            }
            numbers.push_back(arr->at(i).data<d_scalar, float>());
        }
        return numbers;
    }

    /// linearConversion [minFrom, maxFrom, value, minTo, maxTo, clip]
    value linearconversion_array(runtime& runtime, value::cref right)
    {
        auto n = read_numbers(runtime, right, 5, 6, "linearConversion");
        if (!n)
        {
            return {};
        }
        auto& v = *n;
        auto range = v[1] - v[0];
        auto t = range == 0 ? 0.0f : (v[2] - v[0]) / range;
        if (v.size() == 6 && v[5] != 0)
        {
            t = std::clamp(t, 0.0f, 1.0f);
        }
        return v[3] + (v[4] - v[3]) * t;
    }

    /// vectorLinearConversion [rangeFrom, rangeTo, rangeValue, vectorFrom, vectorTo, clip]
    value vectorlinearconversion_array(runtime& runtime, value::cref right)
    {
        auto arr = right.data<d_array>();
        if (arr->size() < 5 || arr->size() > 6 || !arr->at(0).is<t_scalar>() || !arr->at(1).is<t_scalar>() || !arr->at(2).is<t_scalar>())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "vectorLinearConversion", "expected [rangeFrom, rangeTo, rangeValue, vectorFrom, vectorTo, clip]"));
            return {};
        }
        auto from = read_vector(arr->at(3));
        auto to = read_vector(arr->at(4));
        if (!from || !to)
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "vectorLinearConversion", "vectorFrom and vectorTo must be 2D or 3D vectors"));
            return {};
        }
        auto range_from = arr->at(0).data<d_scalar, float>();
        auto range = arr->at(1).data<d_scalar, float>() - range_from;
        auto t = range == 0 ? 0.0f : (arr->at(2).data<d_scalar, float>() - range_from) / range;
        if (arr->size() == 6 && arr->at(5).data_try<d_boolean, bool>(false))
        {
            t = std::clamp(t, 0.0f, 1.0f);
        }
        std::array<float, 3> out{};
        for (size_t i = 0; i < 3; i++)
        {
            out[i] = (*from)[i] + ((*to)[i] - (*from)[i]) * t;
        }
        return make_vector(out);
    }

    value vectorfromto_array_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto from = read_vector(left);
        auto to = read_vector(right);
        if (!from || !to)
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "vectorFromTo", "expected two 2D or 3D vectors"));
            return {};
        }
        std::array<float, 3> diff{ (*to)[0] - (*from)[0], (*to)[1] - (*from)[1], (*to)[2] - (*from)[2] };
        auto length = std::sqrt(diff[0] * diff[0] + diff[1] * diff[1] + diff[2] * diff[2]);
        if (length == 0)
        {
            return make_vector({ 0, 0, 0 });
        }
        return make_vector({ diff[0] / length, diff[1] / length, diff[2] / length });
    }

    /// progress bezierInterpolation [p0, p1, ...]: De Casteljau's algorithm over numbers or vectors.
    value bezierinterpolation_scalar_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto t = left.data<d_scalar, float>();
        auto points = right.data<d_array>();
        if (points->size() == 0)
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "bezierInterpolation", "expected at least one control point"));
            return {};
        }
        bool scalars = points->at(0).is<t_scalar>();
        std::vector<std::array<float, 3>> work;
        for (size_t i = 0; i < points->size(); i++)
        {
            if (scalars)
            {
                if (!points->at(i).is<t_scalar>())
                {
                    runtime.__logmsg(err::ExpectedArrayTypeMissmatch(position(runtime), i, t_scalar(), points->at(i).type()));
                    return {};
                }
                work.push_back({ points->at(i).data<d_scalar, float>(), 0, 0 });
                continue;
            }
            auto point = read_vector(points->at(i));
            if (!point)
            {
                runtime.__logmsg(err::ErrorMessage(position(runtime), "bezierInterpolation", "positions must all be numbers or all be 2D/3D vectors"));
                return {};
            }
            work.push_back(*point);
        }
        for (size_t level = work.size() - 1; level > 0; level--)
        {
            for (size_t i = 0; i < level; i++)
            {
                for (size_t axis = 0; axis < 3; axis++)
                {
                    work[i][axis] = work[i][axis] + (work[i + 1][axis] - work[i][axis]) * t;
                }
            }
        }
        if (scalars)
        {
            return work[0][0];
        }
        return make_vector(work[0]);
    }

    value distancesqr_array_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto a = read_vector(left);
        auto b = read_vector(right);
        if (!a || !b)
        {
            return 1e10f;
        }
        float sum = 0;
        for (size_t i = 0; i < 3; i++)
        {
            sum += ((*a)[i] - (*b)[i]) * ((*a)[i] - (*b)[i]);
        }
        return sum;
    }
    /// a distance b (2.18): sqrt(a * a + b * b).
    value distance_scalar_scalar(runtime& runtime, value::cref left, value::cref right)
    {
        auto a = left.data<d_scalar, float>();
        auto b = right.data<d_scalar, float>();
        return std::sqrt(a * a + b * b);
    }
    /// a distanceSqr b (2.18): a * a + b * b.
    value distancesqr_scalar_scalar(runtime& runtime, value::cref left, value::cref right)
    {
        auto a = left.data<d_scalar, float>();
        auto b = right.data<d_scalar, float>();
        return a * a + b * b;
    }

    std::mt19937& generator()
    {
        static std::mt19937 instance(std::random_device{}());
        return instance;
    }

    /// random [min, mid, max]: a Bates distribution with n = 4, split at its midpoint and scaled so
    /// that its peak lies at mid, as the wiki describes.
    value random_array(runtime& runtime, value::cref right)
    {
        auto n = read_numbers(runtime, right, 3, 3, "random");
        if (!n)
        {
            return {};
        }
        std::uniform_real_distribution<float> uniform(0.0f, 1.0f);
        float bates = 0;
        for (int i = 0; i < 4; i++)
        {
            bates += uniform(generator());
        }
        bates /= 4.0f;
        auto [low, mid, high] = std::tuple{ (*n)[0], (*n)[1], (*n)[2] };
        if (bates < 0.5f)
        {
            return low + (mid - low) * (bates * 2.0f);
        }
        return mid + (high - mid) * ((bates - 0.5f) * 2.0f);
    }

    /// A deterministic value in [0, 1) for the seed and coordinates. The engine's generator is not
    /// documented, so the values differ from the game, but equal inputs always give equal results.
    float seeded_value(float seed, float x, float y)
    {
        uint64_t state = 0xcbf29ce484222325ull;
        for (float part : { seed, x, y })
        {
            uint32_t bits;
            std::memcpy(&bits, &part, sizeof(bits));
            state ^= bits;
            state *= 0x100000001b3ull;
        }
        state ^= state >> 33;
        state *= 0xff51afd7ed558ccdull;
        state ^= state >> 33;
        return static_cast<float>((state >> 40) / static_cast<double>(1ull << 24));
    }
    value random_scalar_scalar(runtime& runtime, value::cref left, value::cref right)
    {
        return seeded_value(left.data<d_scalar, float>(), 0, 0) * right.data<d_scalar, float>();
    }
    value random_scalar_array(runtime& runtime, value::cref left, value::cref right)
    {
        auto n = read_numbers(runtime, right, 2, 2, "random");
        if (!n)
        {
            return {};
        }
        return seeded_value(left.data<d_scalar, float>(), (*n)[0], (*n)[1]);
    }

    value finite_scalar(runtime& runtime, value::cref right)
    {
        return std::isfinite(right.data<d_scalar, float>());
    }
    /// tg is the tangent of an angle in degrees, like tan.
    value tg_scalar(runtime& runtime, value::cref right)
    {
        return static_cast<float>(std::tan(right.data<d_scalar, float>() * pi / 180.0));
    }

    value system_time(bool utc)
    {
        auto now = std::chrono::system_clock::now();
        auto seconds = std::chrono::system_clock::to_time_t(now);
        auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        std::tm parts{};
#if defined(_WIN32)
        if (utc) { gmtime_s(&parts, &seconds); } else { localtime_s(&parts, &seconds); }
#else
        if (utc) { gmtime_r(&seconds, &parts); } else { localtime_r(&seconds, &parts); }
#endif
        return std::make_shared<d_array>(std::vector<value>{
            static_cast<float>(parts.tm_year + 1900), static_cast<float>(parts.tm_mon + 1), static_cast<float>(parts.tm_mday),
            static_cast<float>(parts.tm_hour), static_cast<float>(parts.tm_min), static_cast<float>(parts.tm_sec),
            static_cast<float>(milliseconds) });
    }

    /// hashValue: FNV-1a 64 bit, Base64 encoded, as the wiki documents. The bytes the engine hashes
    /// are not documented; SQF-VM hashes the value's SQF text, so results can differ from the game.
    value hashvalue_any(runtime& runtime, value::cref right)
    {
        uint64_t hash = 0xcbf29ce484222325ull;
        for (unsigned char c : right.to_string_sqf())
        {
            hash ^= c;
            hash *= 0x100000001b3ull;
        }
        static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        unsigned char bytes[8];
        for (int i = 0; i < 8; i++)
        {
            bytes[i] = static_cast<unsigned char>(hash >> (8 * i));
        }
        std::string encoded;
        for (int i = 0; i < 8; i += 3)
        {
            uint32_t chunk = bytes[i] << 16;
            if (i + 1 < 8) chunk |= bytes[i + 1] << 8;
            if (i + 2 < 8) chunk |= bytes[i + 2];
            encoded += alphabet[(chunk >> 18) & 63];
            encoded += alphabet[(chunk >> 12) & 63];
            encoded += i + 1 < 8 ? alphabet[(chunk >> 6) & 63] : '=';
            encoded += i + 2 < 8 ? alphabet[chunk & 63] : '=';
        }
        return encoded;
    }

    /// formatText [format, arguments...]: like format, but returns structured text.
    value formattext_array(runtime& runtime, value::cref right)
    {
        auto arr = right.data<d_array>();
        if (arr->size() == 0 || !arr->at(0).is<t_string>())
        {
            runtime.__logmsg(err::ErrorMessage(position(runtime), "formatText", "expected [format, arguments...]"));
            return value(std::make_shared<d_text>());
        }
        auto format = arr->at(0).data<d_string, std::string>();
        std::string out;
        for (size_t i = 0; i < format.size(); i++)
        {
            if (format[i] == '%' && i + 1 < format.size() && std::isdigit(static_cast<unsigned char>(format[i + 1])))
            {
                size_t end = i + 1;
                while (end < format.size() && std::isdigit(static_cast<unsigned char>(format[end])))
                {
                    end++;
                }
                auto index = std::stoul(format.substr(i + 1, end - i - 1));
                if (index >= 1 && index < arr->size())
                {
                    auto& argument = arr->at(index);
                    out += argument.is<t_string>() ? argument.data<d_string, std::string>() : argument.data<sqf::runtime::data>()->to_string();
                }
                i = end - 1;
                continue;
            }
            out += format[i];
        }
        return value(std::make_shared<d_text>(out));
    }
}

void sqf::operators::ops_math_extra(::sqf::runtime::runtime& runtime)
{
    using namespace sqf::runtime::sqfop;
    runtime.register_sqfop(unary("linearConversion", t_array(), "linearConversion [minFrom, maxFrom, value, minTo, maxTo, clip]", linearconversion_array));
    runtime.register_sqfop(unary("vectorLinearConversion", t_array(), "vectorLinearConversion [rangeFrom, rangeTo, rangeValue, vectorFrom, vectorTo, clip]", vectorlinearconversion_array));
    runtime.register_sqfop(binary(4, "vectorFromTo", t_array(), t_array(), "Unit vector from the first position to the second.", vectorfromto_array_array));
    runtime.register_sqfop(binary(4, "bezierInterpolation", t_scalar(), t_array(), "Value on the Bezier curve through the control points at the given progress.", bezierinterpolation_scalar_array));
    runtime.register_sqfop(binary(4, "distanceSqr", t_array(), t_array(), "Squared distance between two positions.", distancesqr_array_array));
    runtime.register_sqfop(binary(4, "distance", t_scalar(), t_scalar(), "sqrt(a * a + b * b)", distance_scalar_scalar));
    runtime.register_sqfop(binary(4, "distanceSqr", t_scalar(), t_scalar(), "a * a + b * b", distancesqr_scalar_scalar));
    runtime.register_sqfop(unary("random", t_array(), "random [min, mid, max]: random number weighted towards mid.", random_array));
    runtime.register_sqfop(binary(4, "random", t_scalar(), t_scalar(), "seed random x: deterministic random number from 0 to x.", random_scalar_scalar));
    runtime.register_sqfop(binary(4, "random", t_scalar(), t_array(), "seed random [x, y]: deterministic noise value at the coordinates.", random_scalar_array));
    runtime.register_sqfop(unary("finite", t_scalar(), "True if the number is neither infinite nor NaN.", finite_scalar));
    runtime.register_sqfop(unary("tg", t_scalar(), "Tangent of an angle in degrees.", tg_scalar));
    runtime.register_sqfop(nular("systemTime", "Local time as [year, month, day, hour, minute, second, millisecond].",
        [](::sqf::runtime::runtime& runtime) -> value { return system_time(false); }));
    runtime.register_sqfop(nular("systemTimeUTC", "UTC time as [year, month, day, hour, minute, second, millisecond].",
        [](::sqf::runtime::runtime& runtime) -> value { return system_time(true); }));
    runtime.register_sqfop(unary("hashValue", t_any(), "FNV-1a 64-bit hash of the value, Base64 encoded.", hashvalue_any));
    runtime.register_sqfop(unary("formatText", t_array(), "formatText [format, arguments...]: like format, but returns structured text.", formattext_array));
}
