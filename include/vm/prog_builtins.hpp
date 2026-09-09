#pragma once
#include "vm/prog_vm.hpp"

namespace zq::vm {

// Register the reference builtin set (QW numbering, 0..82) on a VM.
// Pure math/string/edict builtins are handled here; engine-needing ones
// (traceline, pointcontents, cvar, sound, ...) are registered as stubs that
// log and return 0 / empty, so the engine can override them later.
void RegisterDefaultBuiltins(ProgVM& vm);

} // namespace zq::vm
