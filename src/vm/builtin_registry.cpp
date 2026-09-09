#include "vm/builtin_registry.hpp"
#include "vm/vm.hpp"

namespace zq::vm {

static BuiltinFunc builtin_table[BuiltinRegistry::MAX_BUILTINS] = {nullptr};

void BuiltinRegistry::Register(int index, BuiltinFunc func) {
    if (index >= 0 && index < MAX_BUILTINS) {
        builtin_table[index] = func;
    }
}

BuiltinFunc BuiltinRegistry::Get(int index) {
    if (index >= 0 && index < MAX_BUILTINS) {
        return builtin_table[index];
    }
    return nullptr;
}

void BuiltinRegistry::Call(int index, VMState& state) {
    BuiltinFunc func = Get(index);
    if (func) {
        func(state);
    }
}

} // namespace zq::vm