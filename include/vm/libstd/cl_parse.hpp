#pragma once

namespace zq::vm::libstd {

// Client-side builtins (CL_* functions)
void CL_Init();
void CL_ParseServerMessage();
void CL_ParseClientMessage();
void CL_WriteToConsole();

}
