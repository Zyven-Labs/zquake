#pragma once
#include "vm/vm.hpp"

namespace zq::vm {

// Opcode dispatcher
void ExecuteOpcode(uint8_t opcode);

// Implementation of core opcodes
void Opcode_DONE();
void Opcode_ADD();
void Opcode_SUB();
void Opcode_MUL();
void Opcode_DIV();
void Opcode_MOD();
void Opcode_NEG();
void Opcode_EQ();
void Opcode_NE();
void Opcode_LT();
void Opcode_GT();
void Opcode_LE();
void Opcode_GE();
void Opcode_AND();
void Opcode_OR();
void Opcode_NOT();
void Opcode_STORE();
void Opcode_LOAD();
void Opcode_JUMP();
void Opcode_CALL();
void Opcode_RET();
void Opcode_POP();
void Opcode_BREAK();
void Opcode_SWITCH();
void Opcode_ABORT();

}
