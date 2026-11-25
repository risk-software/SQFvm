#include "ops_dummy_nular.h"
#include "../runtime/runtime.h"
#include "../runtime/value.h"
#include "../runtime/type.h"
#include "../runtime/d_array.h"
#include "d_object.h"

using namespace sqf::runtime;
using namespace sqf::types;
void sqf::operators::ops_dummy_nular(sqf::runtime::runtime& runtime)
{
	using namespace sqf::runtime::sqfop;
	runtime.register_sqfop(nular("it_dumpcommands", "", [](sqf::runtime::runtime& runtime) -> value { runtime.__logmsg(logmessage::runtime::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "NOT IMPLEMENTED", "it_dumpcommands")); return {}; }));
	runtime.register_sqfop(nular("currentunit", "", [](sqf::runtime::runtime& runtime) -> value { runtime.__logmsg(logmessage::runtime::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "NOT IMPLEMENTED", "currentunit")); return {}; }));
	runtime.register_sqfop(nular("intercept_looptest", "", [](sqf::runtime::runtime& runtime) -> value { runtime.__logmsg(logmessage::runtime::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "NOT IMPLEMENTED", "intercept_looptest")); return {}; }));
	runtime.register_sqfop(nular("textnull", "", [](sqf::runtime::runtime& runtime) -> value { runtime.__logmsg(logmessage::runtime::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "NOT IMPLEMENTED", "textnull")); return {}; }));
	runtime.register_sqfop(nular("intercept_looptestscript", "", [](sqf::runtime::runtime& runtime) -> value { runtime.__logmsg(logmessage::runtime::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "NOT IMPLEMENTED", "intercept_looptestscript")); return {}; }));
	runtime.register_sqfop(nular("interceptonframe", "", [](sqf::runtime::runtime& runtime) -> value { runtime.__logmsg(logmessage::runtime::ErrorMessage(runtime.context_active().current_frame().diag_info_from_position(), "NOT IMPLEMENTED", "interceptonframe")); return {}; }));

}
