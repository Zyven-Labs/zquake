#include "vm/vm.hpp"
#include "vm/builtin_registry.hpp"
#include "core/logging/logger.hpp"
#include "core/container/string.hpp"
#include <sstream>

namespace zq::vm {

VMState VM::state_;

bool VM::LoadBytecode(const void* bytecode_data, size_t bytecode_size,
                      const void* global_data, size_t global_size,
                      const void* function_data, size_t function_size,
                      const void* string_data, size_t string_size) {
    state_.global_base = const_cast<int32_t*>(static_cast<const int32_t*>(global_data));
    state_.function_table = const_cast<int32_t*>(static_cast<const int32_t*>(function_data));
    state_.string_base = const_cast<int32_t*>(static_cast<const int32_t*>(string_data));
    state_.pc = 0;
    state_.function_num = 0;
    
    std::ostringstream oss;
    oss << "VM bytecode loaded: " << bytecode_size << " bytes";
    log::Info(String(oss.str().c_str()));
    return true;
}

void VM::Run() {
    while (state_.pc >= 0) {
        uint8_t opcode = state_.function_table[state_.pc++];
        
        switch (opcode) {
            case OP_DONE:
            case OP_RET:
                return;
            case OP_RETF: {
                // Return with value already on stack
                return;
            }
            case OP_ABORT:
                log::Fatal("VM: ABORT");
                return;
            case OP_NEG: {
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = -a;
                break;
            }
            case OP_ADD: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = a + b;
                break;
            }
            case OP_SUB: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = a - b;
                break;
            }
            case OP_MUL: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = (int64_t)a * b / 256;
                break;
            }
            case OP_DIV: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = (b != 0) ? (a * 256) / b : 0;
                break;
            }
            case OP_MOD: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = (b != 0) ? a % b : 0;
                break;
            }
            case OP_EQ:
            case OP_EQUAL: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = (a == b) ? 1 : 0;
                break;
            }
            case OP_NE:
            case OP_NEQUAL: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = (a != b) ? 1 : 0;
                break;
            }
            case OP_LT:
            case OP_LESS: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = (a < b) ? 1 : 0;
                break;
            }
            case OP_GT:
            case OP_GREATER: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = (a > b) ? 1 : 0;
                break;
            }
            case OP_LE:
            case OP_LESSEQUAL: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = (a <= b) ? 1 : 0;
                break;
            }
            case OP_GE:
            case OP_GREATEREQUAL: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = (a >= b) ? 1 : 0;
                break;
            }
            case OP_AND:
            case OP_ANDP: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = (a && b) ? 1 : 0;
                break;
            }
            case OP_OR:
            case OP_ORP: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = (a || b) ? 1 : 0;
                break;
            }
            case OP_BITAND: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = a & b;
                break;
            }
            case OP_BITOR: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = a | b;
                break;
            }
            case OP_NOT:
            case OP_NOT_STACK: {
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = (a == 0) ? 1 : 0;
                break;
            }
            case OP_JUMP: {
                int32_t offset = *--state_.stack_top;
                state_.pc += offset;
                break;
            }
            case OP_JUMPFS:
            case OP_JUMPNOT: {
                int32_t offset = *--state_.stack_top;
                int32_t cond = *--state_.stack_top;
                if (!cond) {
                    state_.pc += offset;
                }
                break;
            }
            case OP_CALL: {
                int32_t func = *--state_.stack_top;
                state_.function_num = func;
                *state_.stack_top++ = state_.pc;
                state_.pc = state_.function_table[func];
                break;
            }
            case OP_CALLB: {
                int32_t builtin_num = *--state_.stack_top;
                int32_t ret_pc = *--state_.stack_top;
                BuiltinRegistry::Call(builtin_num, state_);
                state_.pc = ret_pc;
                break;
            }
            case OP_POP:
                state_.stack_top--;
                break;
            case OP_BREAK:
                log::Warn("VM: BREAK");
                return;
            case OP_STORE_F:
            case OP_STORE: {
                int32_t value = *--state_.stack_top;
                int32_t addr = *--state_.stack_top;
                state_.global_base[addr / 4] = value;
                break;
            }
            case OP_LOAD_F:
            case OP_LOAD: {
                int32_t addr = *--state_.stack_top;
                int32_t value = state_.global_base[addr / 4];
                *state_.stack_top++ = value;
                break;
            }
            case OP_STOREP: {
                int32_t value = *--state_.stack_top;
                int32_t* addr_ptr = reinterpret_cast<int32_t*>(*--state_.stack_top);
                if (addr_ptr) *addr_ptr = value;
                break;
            }
            case OP_LOADP: {
                int32_t* addr_ptr = reinterpret_cast<int32_t*>(*--state_.stack_top);
                *state_.stack_top++ = addr_ptr ? *addr_ptr : 0;
                break;
            }
            case OP_COPY: {
                int32_t dest = *--state_.stack_top;
                int32_t src = *--state_.stack_top;
                // Both are addresses relative to global base
                state_.global_base[dest / 4] = state_.global_base[src / 4];
                break;
            }
            case OP_COPY2: {
                int32_t dest = *--state_.stack_top;
                int32_t src = *--state_.stack_top;
                state_.global_base[dest / 4] = state_.global_base[src / 4];
                state_.global_base[dest / 4 + 1] = state_.global_base[src / 4 + 1];
                break;
            }
            case OP_COPYN: {
                int32_t n = *--state_.stack_top;
                int32_t dest = *--state_.stack_top;
                int32_t src = *--state_.stack_top;
                for (int32_t i = 0; i < n; i++) {
                    state_.global_base[(dest / 4) + i] = state_.global_base[(src / 4) + i];
                }
                break;
            }
            case OP_ARG0: case OP_ARG1: case OP_ARG2: case OP_ARG3: case OP_ARG4: {
                // Args are already on the stack from prior pushes
                // The function call will read them as locals
                break;
            }
            case OP_GLOBAL: {
                int32_t offset = *--state_.stack_top;
                // Push the offset of the global variable (as data offset, not pointer)
                *state_.stack_top++ = offset;
                break;
            }
            case OP_SGLOBAL: {
                int32_t offset = *--state_.stack_top;
                // Push the value of the global variable
                *state_.stack_top++ = state_.global_base[offset / 4];
                break;
            }
            case OP_ADD_P:
            case OP_ADD_F:
            case OP_ADD_F2:
            case OP_ADD_S: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = a + b;
                break;
            }
            case OP_SUB_P:
            case OP_SUB_F:
            case OP_SUB_F2:
            case OP_SUB_S: {
                int32_t b = *--state_.stack_top;
                int32_t a = *--state_.stack_top;
                *state_.stack_top++ = a - b;
                break;
            }
            case OP_ZERO_GLOBALS: {
                // Zero out all global variables
                for (int i = 0; i < state_.function_table[0] / 4; i++) {
                    state_.global_base[i] = 0;
                }
                break;
            }
            default: {
                std::ostringstream oss;
                oss << "VM: Unknown opcode: " << opcode;
                log::Warn(String(oss.str().c_str()));
                break;
            }
        }
    }
}

void VM::Stop() {
    state_.pc = -1;
}

int32_t VM::GetGlobal(int index) {
    return state_.global_base[index];
}

void VM::SetGlobal(int index, int32_t value) {
    state_.global_base[index] = value;
}

int32_t VM::GetLocal(int index) {
    return state_.stack_top[index];
}

void VM::SetLocal(int index, int32_t value) {
    state_.stack_top[index] = value;
}

const char* VM::GetString(int offset) {
    return reinterpret_cast<const char*>(state_.string_base) + offset;
}

}
