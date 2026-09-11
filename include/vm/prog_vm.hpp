#pragma once
#include "vm/quakeprog.hpp"
#include <functional>
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace zq::vm {

// Builtin handlers are invoked with no args in the C sense; they read their
// arguments from the parameter globals (OFS_PARM0..) and write the return
// value through OFS_RETURN, exactly like the reference pr_cmds.c.
using BuiltinHandler = std::function<void(class ProgVM&)>;

// A QuakeC virtual machine that interprets real progs.dat bytecode
// (dprograms_t / dstatement_t), matching pr_exec.c semantics from the GPL
// release. Globals, strings, field defs and edicts follow the reference
// memory model exactly.
class ProgVM {
public:
    static constexpr int MAX_EDICTS = 1024;
    static constexpr int MAX_STACK_DEPTH = 32;
    static constexpr int LOCALSTACK_SIZE = 4096;
    static constexpr int MAX_BUILTINS = 256;

    ProgVM();
    ~ProgVM();

    // Loads progs.dat (host byte order assumed; this is the raw file bytes).
    // Returns false on parse error / bad version.
    bool Load(const void* data, size_t size);
    bool Loaded() const { return progs_ != nullptr; }
    void Clear();

    // ---- program tables ----
    const dprograms_t* Header() const { return progs_; }
    const dfunction_t* FunctionAt(int func_num) const {
        if (func_num < 0 || func_num >= (int)functions_.size()) return nullptr;
        return &functions_[func_num];
    }
    // String table access for diagnostics/builtins.
    const char* StringAt(int offset) const;
    // Name of the currently executing function (for diagnostics).
    const char* CurrentFunctionName() const {
        if (xfunction_ >= 0 && xfunction_ < (int)functions_.size())
            return StringAt(functions_[xfunction_].s_name);
        return "?";
    }
    // Name of a function table entry (for diagnostics).
    const char* FunctionNameFor(int func_num) const {
        if (func_num < 0 || func_num >= (int)functions_.size()) return "?";
        return StringAt(functions_[func_num].s_name);
    }
    void SetString(int global_ofs, const char* s);
    // Append a string to the runtime string table and return its offset.
    int InternString(const char* s);
    int NumFunctions() const { return (int)functions_.size(); }
    int NumStatements() const { return (int)statements_.size(); }
    int NumGlobals() const { return (int)globals_.size(); }

    // Raw program tables for external disassembly/tooling (read-only views).
    const dstatement_t* StatementsPtr() const { return statements_.data(); }
    const dfunction_t* FunctionsPtr() const { return functions_.data(); }

    // ---- globals ----
    // pr_globals is a flat int32 array; floats keep their IEEE bits.
    int32_t& Global(int ofs) { return globals_[ofs]; }
    float& GlobalFloat(int ofs) { return *reinterpret_cast<float*>(&globals_[ofs]); }

    // Look up a named global/field/function; returns -1 / nullptr if absent.
    int FindGlobal(const char* name) const;
    int FindField(const char* name) const;
    int FindFunction(const char* name) const;

    // ---- edicts ----
    // edict index <-> PROG byte offset (empty edict pointer == 0 == world).
    int EdictCount() const { return num_edicts_; }
    void AllocateEdicts(int world_edicts);
    bool EdictFree(int edict_num) const;
    void SetEdictFree(int edict_num, bool free);
    int AllocEdict();                       // finds a free edict (world stays 0)
    void FreeEdict(int edict_num);
    int ProgToEdictNum(int prog_offset) const;   // -1 if out of range
    int EdictNumToProg(int num) const { return num * edict_stride_; }
    char* EdictBase();                            // sv.edicts equivalent
    int EdictStride() const { return edict_stride_; }

    // Parses the map's entity text (the BSP "entities" lump) and runs each
    // classname spawn function with self set (ED_LoadFromFile parity).
    // Requires AllocateEdicts() to have been called first.
    void LoadEntities(const char* data);

    // ---- execution ----
    // Runs a function (non-negative; negative = invalid). Returns false on error.
    bool ExecuteProgram(int func_num);
    bool ExecuteProgram(const char* name); // by function name (log if missing)

    // ---- builtin dispatch ----
    void RegisterBuiltin(int num, BuiltinHandler fn);
    bool CallBuiltin(int num);
    bool HasBuiltin(int num) const {
        return num >= 0 && num < MAX_BUILTINS && static_cast<bool>(builtins_[num]);
    }

    // ---- builtin helpers (mirror G_*/G_FLOAT etc of progs.h) ----
    float ParmFloat(int i);            // G_FLOAT(OFS_PARM0 + i*3) -- scalars
    int32_t ParmInt(int i);
    const char* ParmString(int i);     // G_STRING
    int ParmEdictNum(int i);           // entity param -> edict index
    void ParmVector(int i, float out[3]);

    void SetReturnFloat(float v);
    void SetReturnInt(int32_t v);
    void SetReturnString(const char* s);
    void SetReturnEdict(int edict_num); // writes PROG offset
    void SetReturnVector(const float v[3]);

    // Edict field access (offsets resolved by FieldOffs).
    int32_t& EdictFieldInt(int edict_num, int field_ofs);
    float& EdictFieldFloat(int edict_num, int field_ofs);
    void EdictFieldVector(int edict_num, int field_ofs, float out[3]);
    void SetEdictFieldVector(int edict_num, int field_ofs, const float v[3]);
    const char* EdictFieldString(int edict_num, int field_ofs);
    int EdictFieldEntity(int edict_num, int field_ofs); // -> edict num

    // Global/self helpers.
    int Self() const { return self_; }
    int SelfEdict() const { return ProgToEdictNum(self_); }
    int OtherEdict() const;              // edict number of the `other` global
    void SetSelfEdict(int edict_num);
    float Time() const;
    void SetTime(float t);

    // Game-loop helpers (used by SV_Physics-style framing).
    void SetFrametime(float t);
    void SetOtherEdict(int edict_num);
    int FunctionIndex(const char* global_name) const;   // function stored in a global
    void SetGlobalInt(const char* name, int32_t v);     // writes a global by name
    void SetGlobalFloatG(const char* name, float v);    // writes a float global by name
    void SetGlobalStringG(const char* name, const char* s); // writes a string global by name

    // Debug/trace.
    bool trace_enabled() const { return trace_enabled_; }
    void EnableTrace(bool on) { trace_enabled_ = on; }
    int LastError() const { return last_error_code_; }
    const std::string& ErrorMessage() const { return err_msg_; }

    // Raise a VM error (aborts the running program, like PR_RunError).
    void RunError(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    // Engine-provided submodel bounds lookup (SV_ModelIndex / mod->mins,maxs).
    // The engine sets this once the map is loaded so the builtins (setmodel)
    // can compute mins/maxs/size like ED_SetModel. Returns false if unknown.
    using BrushBoundsFn = std::function<bool(int modelindex, float mins[3], float maxs[3])>;
    void SetBrushBounds(const BrushBoundsFn& f) { brush_bounds_ = f; }
    bool BrushSubmodelBounds(int modelindex, float mins[3], float maxs[3]) const {
        if (!brush_bounds_) return false;
        return brush_bounds_(modelindex, mins, maxs);
    }

    // Mutable world state hooks used by the engine: none here; the engine
    // reads/writes globals and edicts through the accessors above.

private:
    bool EnterFunction(const dfunction_t* f);
    void LeaveFunction();

    // raw byte pointer to an edict's field area (v equivalent)
    int32_t* EdictFieldPtr(int edict_num, int field_ofs);

    dprograms_t header_ = {};
    dprograms_t* progs_ = nullptr;
    bool progs_valid_ = false;
    std::vector<uint8_t> owned_;          // owns the file bytes if we had to swap
    std::vector<dstatement_t> statements_;
    std::vector<dfunction_t> functions_;
    std::vector<ddef_t> globaldefs_;
    std::vector<ddef_t> fielddefs_;
    std::vector<char> strings_;
    std::vector<int32_t> globals_;

    BrushBoundsFn brush_bounds_;

    // edict arena: stride bytes per edict (entityfields*4 + overhead)
    std::vector<int32_t> edict_mem_;
    std::vector<uint8_t> edict_free_;
    int edict_stride_ = 0;
    int num_edicts_ = 0;

    // call stack
    struct PrStack {
        int32_t s;
        int function_num;
    };
    PrStack pr_stack_[MAX_STACK_DEPTH];
    int pr_depth_ = 0;
    std::vector<int32_t> localstack_;

    int xfunction_ = 0;      // currently executing function number
    int xstatement_ = 0;     // last statement executed
    int argc_ = 0;

    // cached global/field offsets
    int global_self_ = -1;
    int global_time_ = -1;
    int global_frametime_ = -1;
    int global_v_forward_ = -1;
    int global_v_up_ = -1;
    int global_v_right_ = -1;
    int field_origin_ = -1;
    int field_angles_ = -1;
    int field_model_ = -1;
    int field_modelindex_ = -1;
    int field_solid_ = -1;
    int field_movetype_ = -1;
    int field_classname_ = -1;
    int field_frame_ = -1;
    int field_viewofs_ = -1;
    int field_mins_ = -1;
    int field_maxs_ = -1;
    int field_think_ = -1;
    int field_nextthink_ = -1;
    // ... add more as needed

    std::vector<BuiltinHandler> builtins_;
    bool trace_enabled_ = false;
    int last_error_code_ = 0;
    std::string err_msg_;

    int self_ = 0; // PROG offset of self (world base == 0)
};

} // namespace zq::vm