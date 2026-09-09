#pragma once
#include <cstdint>
#include <functional>

namespace zq::vm {

struct VMState;

// Builtin function type - takes VM state reference for arg/result access
using BuiltinFunc = void (*)(VMState& state);

class BuiltinRegistry {
public:
    static constexpr int MAX_BUILTINS = 256;
    
    static void Register(int index, BuiltinFunc func);
    static BuiltinFunc Get(int index);
    static void Call(int index, VMState& state);
};

}