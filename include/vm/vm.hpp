#pragma once
#include <cstdint>
#include <cstddef>

namespace zq::vm {

struct VMState {
    int32_t stack[65536];
    int32_t* stack_base;
    int32_t* stack_top;
    int32_t pc;
    int32_t function_num;
    int32_t *function_base;
    int32_t *string_base;
    int32_t *global_base;
    int32_t *function_table;
};

// QuakeC VM bytecode interpreter
// 32-bit int only, fixed-point math
class VM {
public:
    enum Opcode : uint8_t {
        OP_DONE,
        OP_ADD,
        OP_SUB,
        OP_MUL,
        OP_DIV,
        OP_MOD,
        OP_NEG,
        OP_EQ,
        OP_NE,
        OP_LT,
        OP_GT,
        OP_LE,
        OP_GE,
        OP_AND,
        OP_OR,
        OP_NOT,
        OP_STORE_F,
        OP_STORE,
        OP_LOAD,
        OP_LOAD_F,
        OP_ARG0, OP_ARG1, OP_ARG2, OP_ARG3, OP_ARG4,
        OP_GLOBAL,
        OP_SGLOBAL,
        OP_ENT,
        OP_SENT,
        OP_VECTOR,
        OP_PARAM,
        OP_SYSREQ_F,
        OP_JUMP,
        OP_JUMPFS,
        OP_CALL,
        OP_CALLB,
        OP_POP,
        OP_BREAK,
        OP_EQUAL,
        OP_NEQUAL,
        OP_GREATER,
        OP_LESS,
        OP_GREATEREQUAL,
        OP_LESSEQUAL,
        OP_STOREP,
        OP_LOADP,
        OP_STORE_F2,
        OP_LOAD_F2,
        OP_ADD_P,
        OP_SUB_P,
        OP_ADD_F,
        OP_SUB_F,
        OP_ADD_F2,
        OP_SUB_F2,
        OP_ADD_S,
        OP_SUB_S,
        OP_ANDP,
        OP_ORP,
        OP_BITAND,
        OP_BITOR,
        OP_JUMPNOT,
        OP_ENTFLOAT,
        OP_ENTINT,
        OP_CALLC,
        OP_NOT_STACK,
        OP_SENDFLOAT,
        OP_SENDINT,
        OP_SENDVECTOR,
        OP_SENDSTRING,
        OP_SENDENTITY,
        OP_COPY,
        OP_COPY2,
        OP_COPYN,
        OP_ZERO_GLOBALS,
        OP_ABORT,
        OP_RET,
        OP_RETF,
        OP_SWITCH,
        OP_JUMPNOTZERO0,
        OP_JUMPNOTZERO1,
        OP_JUMPNOTZERO2,
        OP_JUMPNOTZERO3,
        OP_JUMPNOTZERO4,
        OP_JUMPNOTZERO5,
        OP_JUMPNOTZERO6,
        OP_JUMPNOTZERO7,
        OP_INCR_P,
        OP_DECR_P,
        OP_MAX
    };
    
    static VMState state_;
    
    bool LoadBytecode(const void* bytecode_data, size_t bytecode_size,
                      const void* global_data, size_t global_size,
                      const void* function_data, size_t function_size,
                      const void* string_data, size_t string_size);
    void Run();
    void Stop();
    
    static int32_t GetGlobal(int index);
    static void SetGlobal(int index, int32_t value);
    static int32_t GetLocal(int index);
    static void SetLocal(int index, int32_t value);
    static const char* GetString(int offset);
};

}
