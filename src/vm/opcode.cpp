#include "vm/opcode.hpp"
#include "vm/vm.hpp"
#include "core/logging/logger.hpp"

namespace zq::vm {

// Dispatch table for all opcodes
static void (*OpcodeTable[VM::OP_MAX])() = {
    Opcode_DONE,    // OP_DONE
    nullptr,        // OP_ADD (implemented in VM::Run for performance)
    nullptr,        // OP_SUB
    nullptr,        // OP_MUL
    nullptr,        // OP_DIV
    nullptr,        // OP_MOD
    nullptr,        // OP_NEG
    nullptr,        // OP_EQ
    nullptr,        // OP_NE
    nullptr,        // OP_LT
    nullptr,        // OP_GT
    nullptr,        // OP_LE
    nullptr,        // OP_GE
    nullptr,        // OP_AND
    nullptr,        // OP_OR
    nullptr,        // OP_NOT
    nullptr,        // OP_STORE_F
    nullptr,        // OP_STORE
    nullptr,        // OP_LOAD
    nullptr,        // OP_LOAD_F
    nullptr,        // OP_ARG0-4
    nullptr,        // OP_GLOBAL
    nullptr,        // OP_SGLOBAL
    nullptr,        // OP_ENT
    nullptr,        // OP_SENT
    nullptr,        // OP_VECTOR
    nullptr,        // OP_PARAM
    nullptr,        // OP_SYSREQ_F
    nullptr,        // OP_JUMP
    nullptr,        // OP_JUMPFS
    nullptr,        // OP_CALL
    nullptr,        // OP_CALLB
    nullptr,        // OP_POP
    nullptr,        // OP_BREAK
    nullptr,        // OP_EQUAL
    nullptr,        // OP_NEQUAL
    nullptr,        // OP_GREATER
    nullptr,        // OP_LESS
    nullptr,        // OP_GREATEREQUAL
    nullptr,        // OP_LESSEQUAL
    nullptr,        // OP_STOREP
    nullptr,        // OP_LOADP
    nullptr,        // OP_STORE_F2
    nullptr,        // OP_LOAD_F2
    nullptr,        // OP_ADD_P
    nullptr,        // OP_SUB_P
    nullptr,        // OP_ADD_F
    nullptr,        // OP_SUB_F
    nullptr,        // OP_ADD_F2
    nullptr,        // OP_SUB_F2
    nullptr,        // OP_ADD_S
    nullptr,        // OP_SUB_S
    nullptr,        // OP_ANDP
    nullptr,        // OP_ORP
    nullptr,        // OP_BITAND
    nullptr,        // OP_BITOR
    nullptr,        // OP_JUMPNOT
    nullptr,        // OP_ENTFLOAT
    nullptr,        // OP_ENTINT
    nullptr,        // OP_CALLC
    nullptr,        // OP_NOT_STACK
    nullptr,        // OP_SENDFLOAT
    nullptr,        // OP_SENDINT
    nullptr,        // OP_SENDVECTOR
    nullptr,        // OP_SENDSTRING
    nullptr,        // OP_SENDENTITY
    nullptr,        // OP_COPY
    nullptr,        // OP_COPY2
    nullptr,        // OP_COPYN
    nullptr,        // OP_ZERO_GLOBALS
    nullptr,        // OP_ABORT
};

void ExecuteOpcode(uint8_t opcode) {
    if (opcode < VM::OP_MAX && OpcodeTable[opcode]) {
        OpcodeTable[opcode]();
    }
}

void Opcode_DONE() {}
void Opcode_ADD() {}
void Opcode_SUB() {}
void Opcode_MUL() {}
void Opcode_DIV() {}
void Opcode_MOD() {}
void Opcode_NEG() {}
void Opcode_EQ() {}
void Opcode_NE() {}
void Opcode_LT() {}
void Opcode_GT() {}
void Opcode_LE() {}
void Opcode_GE() {}
void Opcode_AND() {}
void Opcode_OR() {}
void Opcode_NOT() {}
void Opcode_STORE() {}
void Opcode_LOAD() {}
void Opcode_JUMP() {}
void Opcode_CALL() {}
void Opcode_RET() {}
void Opcode_POP() {}
void Opcode_BREAK() {}
void Opcode_SWITCH() {}
void Opcode_ABORT() {
    log::Error("VM: ABORT opcode executed");
}

} // namespace zq::vm
