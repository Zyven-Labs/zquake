#pragma once

namespace zq::vm::libstd {

// Server-side builtins (SV_* functions)
void SV_Init();
void SV_SpawnEntity();
void SV_StartParticle();
void SV_SetModel();
void SV_Err_Error();
void SV_Printf();

}
