#pragma once
#include <cstdint>
#include <cstddef>

namespace zq::vm {

// QuakeC progs.dat binary format (shared with qcc), from the Quake GPL
// release (pr_comp.h). All structs are little-endian on disk.

inline constexpr int32_t PROG_VERSION = 6;

inline constexpr int OFS_NULL = 0;
inline constexpr int OFS_RETURN = 1;
inline constexpr int OFS_PARM0 = 4;
inline constexpr int OFS_PARM1 = 7;
inline constexpr int OFS_PARM2 = 10;
inline constexpr int OFS_PARM3 = 13;
inline constexpr int OFS_PARM4 = 16;
inline constexpr int OFS_PARM5 = 19;
inline constexpr int OFS_PARM6 = 22;
inline constexpr int OFS_PARM7 = 25;
inline constexpr int RESERVED_OFS = 28;

inline constexpr int MAX_PARMS = 8;
inline constexpr int MAX_ARGVS = 8;

enum etype_t {
    ev_void,
    ev_string,
    ev_float,
    ev_vector,
    ev_entity,
    ev_field,
    ev_function,
    ev_pointer
};

// DEF_SAVEGLOBAL marker bit on the ddef_t type for save-global defs.
inline constexpr unsigned short DEF_SAVEGLOBAL = 1u << 15;

// Opcode numbering from pr_comp.h (three-address code, dstatement_t).
enum opcode_t {
    OP_DONE,
    OP_MUL_F,
    OP_MUL_V,
    OP_MUL_FV,
    OP_MUL_VF,
    OP_DIV_F,
    OP_ADD_F,
    OP_ADD_V,
    OP_SUB_F,
    OP_SUB_V,

    OP_EQ_F,
    OP_EQ_V,
    OP_EQ_S,
    OP_EQ_E,
    OP_EQ_FNC,

    OP_NE_F,
    OP_NE_V,
    OP_NE_S,
    OP_NE_E,
    OP_NE_FNC,

    OP_LE,
    OP_GE,
    OP_LT,
    OP_GT,

    OP_LOAD_F,
    OP_LOAD_V,
    OP_LOAD_S,
    OP_LOAD_ENT,
    OP_LOAD_FLD,
    OP_LOAD_FNC,

    OP_ADDRESS,

    OP_STORE_F,
    OP_STORE_V,
    OP_STORE_S,
    OP_STORE_ENT,
    OP_STORE_FLD,
    OP_STORE_FNC,

    OP_STOREP_F,
    OP_STOREP_V,
    OP_STOREP_S,
    OP_STOREP_ENT,
    OP_STOREP_FLD,
    OP_STOREP_FNC,

    OP_RETURN,
    OP_NOT_F,
    OP_NOT_V,
    OP_NOT_S,
    OP_NOT_ENT,
    OP_NOT_FNC,
    OP_IF,
    OP_IFNOT,
    OP_CALL0,
    OP_CALL1,
    OP_CALL2,
    OP_CALL3,
    OP_CALL4,
    OP_CALL5,
    OP_CALL6,
    OP_CALL7,
    OP_CALL8,
    OP_STATE,
    OP_GOTO,
    OP_AND,
    OP_OR,

    OP_BITAND,
    OP_BITOR
};

// statement_s -- the three-address instruction
struct alignas(4) dstatement_t {
    uint16_t op;
    int16_t a;
    int16_t b;
    int16_t c;
};

// ddef_t -- a global or field definition
struct alignas(4) ddef_t {
    uint16_t type;
    uint16_t ofs;
    int32_t s_name;
};

struct dfunction_t {
    int32_t first_statement; // negative numbers are builtins
    int32_t parm_start;
    int32_t locals;          // total ints of parms + locals
    int32_t profile;         // runtime
    int32_t s_name;
    int32_t s_file;          // source file defined in
    int32_t numparms;
    uint8_t parm_size[MAX_PARMS];
};

// dprograms_t -- the progs.dat header
struct dprograms_t {
    int32_t version;
    int32_t crc;

    int32_t ofs_statements;
    int32_t numstatements;

    int32_t ofs_globaldefs;
    int32_t numglobaldefs;

    int32_t ofs_fielddefs;
    int32_t numfielddefs;

    int32_t ofs_functions;
    int32_t numfunctions;

    int32_t ofs_strings;
    int32_t numstrings; // first string is a null string

    int32_t ofs_globals;
    int32_t numglobals;

    int32_t entityfields;
};

// eval_t -- the runtime value union (4-byte; vector is 12 bytes)
union eval_t {
    int32_t raw;
    float _float;
    float vector[3];
    int32_t string;
    int32_t function;
    int32_t edict;
};

// array layout of edicts: each edict is `entityfields` ints, plus a small
// header we do not model. The entity field area ("v") starts at the edict
// base. Field offsets are in int units from that base, matching pr_fielddefs.
// PROG edict values are BYTE offsets into the edict arena (like sv.edicts).

} // namespace zq::vm
