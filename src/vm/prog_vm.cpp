#include "vm/prog_vm.hpp"
#include "vm/quakeprog.hpp"
#include "core/logging/logger.hpp"
#include <bit>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace zq::vm {

namespace {

constexpr int MAX_RUNAWAY = 100000;

// progs.dat is little-endian. The reference engine unconditionally calls
// LittleLong/LittleShort, which are identity on little-endian hosts, so we
// only byte-swap when the host is big-endian.
inline int32_t swap32(int32_t v) {
    uint32_t x = static_cast<uint32_t>(v);
    return static_cast<int32_t>(((x & 0xFFu) << 24) | ((x & 0xFF00u) << 8) |
                                ((x & 0xFF0000u) >> 8) | ((x & 0xFF000000u) >> 24));
}
inline int16_t swap16(int16_t v) {
    uint16_t x = static_cast<uint16_t>(v);
    return static_cast<int16_t>(((x & 0xFFu) << 8) | ((x & 0xFF00u) >> 8));
}
inline bool HostBigEndian() { return std::endian::native == std::endian::big; }

inline float ff(int32_t i) { return *reinterpret_cast<float*>(&i); }
inline int32_t fi(float f) { int32_t x; memcpy(&x, &f, 4); return x; }

} // namespace

ProgVM::ProgVM() {
    builtins_.assign(MAX_BUILTINS, BuiltinHandler{});
    localstack_.reserve(LOCALSTACK_SIZE);
}

ProgVM::~ProgVM() { Clear(); }

void ProgVM::Clear() {
    progs_ = nullptr;
    statements_.clear();
    functions_.clear();
    globaldefs_.clear();
    fielddefs_.clear();
    strings_.clear();
    globals_.clear();
    edict_mem_.clear();
    edict_stride_ = 0;
    num_edicts_ = 0;
    pr_depth_ = 0;
    xfunction_ = 0;
    xstatement_ = 0;
    self_ = 0;
}

const char* ProgVM::StringAt(int offset) const {
    if (offset < 0 || offset >= static_cast<int>(strings_.size())) return "";
    return strings_.data() + offset;
}

void ProgVM::SetString(int global_ofs, const char* s) {
    if (!s) s = "";
    int len = static_cast<int>(strlen(s)) + 1;
    int off = static_cast<int>(strings_.size());
    strings_.resize(strings_.size() + len);
    memcpy(strings_.data() + off, s, len);
    globals_[global_ofs] = off;
}

int ProgVM::InternString(const char* s) {
    if (!s) s = "";
    int len = static_cast<int>(strlen(s)) + 1;
    int off = static_cast<int>(strings_.size());
    strings_.resize(strings_.size() + len);
    memcpy(strings_.data() + off, s, len);
    return off;
}

bool ProgVM::Load(const void* data, size_t size) {
    if (size < sizeof(dprograms_t)) {
        log::Error("progvm: progs.dat too small");
        return false;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(data);

    dprograms_t hdr_le;
    memcpy(&hdr_le, bytes, sizeof(hdr_le));
    dprograms_t hdr{};
    hdr.version        = hdr_le.version;
    hdr.crc            = hdr_le.crc;
    hdr.ofs_statements = hdr_le.ofs_statements;
    hdr.numstatements  = hdr_le.numstatements;
    hdr.ofs_globaldefs = hdr_le.ofs_globaldefs;
    hdr.numglobaldefs  = hdr_le.numglobaldefs;
    hdr.ofs_fielddefs  = hdr_le.ofs_fielddefs;
    hdr.numfielddefs   = hdr_le.numfielddefs;
    hdr.ofs_functions  = hdr_le.ofs_functions;
    hdr.numfunctions   = hdr_le.numfunctions;
    hdr.ofs_strings    = hdr_le.ofs_strings;
    hdr.numstrings     = hdr_le.numstrings;
    hdr.ofs_globals    = hdr_le.ofs_globals;
    hdr.numglobals     = hdr_le.numglobals;
    hdr.entityfields   = hdr_le.entityfields;

    if (hdr.version != PROG_VERSION) {
        log::Error("progvm: progs.dat has wrong version number");
        return false;
    }

    auto check = [&](int ofs, int len, size_t bound) -> bool {
        return ofs >= 0 && len >= 0 && (size_t)(ofs) <= bound &&
               (size_t)(ofs + len) <= bound;
    };

    // statements lump
    if (!check(hdr.ofs_statements, (int)(hdr.numstatements * sizeof(dstatement_t)), size))
        return false;
    statements_.resize(hdr.numstatements);
    memcpy(statements_.data(), bytes + hdr.ofs_statements,
           hdr.numstatements * sizeof(dstatement_t));
    if (HostBigEndian()) {
        for (auto& st : statements_) {
            st.op = (uint16_t)swap16((int16_t)st.op);
            st.a = swap16(st.a);
            st.b = swap16(st.b);
            st.c = swap16(st.c);
        }
    }

    // functions lump (dfunction_t = 36 bytes)
    if (!check(hdr.ofs_functions, (int)(hdr.numfunctions * sizeof(dfunction_t)), size))
        return false;
    functions_.resize(hdr.numfunctions);
    memcpy(functions_.data(), bytes + hdr.ofs_functions,
           hdr.numfunctions * sizeof(dfunction_t));
    if (HostBigEndian()) {
        for (auto& fn : functions_) {
            fn.first_statement = swap32(fn.first_statement);
            fn.parm_start = swap32(fn.parm_start);
            fn.locals = swap32(fn.locals);
            fn.s_name = swap32(fn.s_name);
            fn.s_file = swap32(fn.s_file);
            fn.numparms = swap32(fn.numparms);
        }
    }

    // globaldefs
    if (!check(hdr.ofs_globaldefs, (int)(hdr.numglobaldefs * sizeof(ddef_t)), size))
        return false;
    globaldefs_.resize(hdr.numglobaldefs);
    memcpy(globaldefs_.data(), bytes + hdr.ofs_globaldefs,
           hdr.numglobaldefs * sizeof(ddef_t));
    if (HostBigEndian()) {
        for (auto& d : globaldefs_) {
            d.type = (uint16_t)swap16((int16_t)d.type);
            d.ofs = (uint16_t)swap16((int16_t)d.ofs);
            d.s_name = swap32(d.s_name);
        }
    }

    // fielddefs
    if (!check(hdr.ofs_fielddefs, (int)(hdr.numfielddefs * sizeof(ddef_t)), size))
        return false;
    fielddefs_.resize(hdr.numfielddefs);
    memcpy(fielddefs_.data(), bytes + hdr.ofs_fielddefs,
           hdr.numfielddefs * sizeof(ddef_t));
    if (HostBigEndian()) {
        for (auto& d : fielddefs_) {
            d.type = (uint16_t)swap16((int16_t)d.type);
            d.ofs = (uint16_t)swap16((int16_t)d.ofs);
            d.s_name = swap32(d.s_name);
        }
    }

    // strings
    if (!check(hdr.ofs_strings, hdr.numstrings, size))
        return false;
    strings_.assign(bytes + hdr.ofs_strings, bytes + hdr.ofs_strings + hdr.numstrings);

    // globals
    if (!check(hdr.ofs_globals, (int)(hdr.numglobals * sizeof(int32_t)), size))
        return false;
    globals_.resize(hdr.numglobals);
    memcpy(globals_.data(), bytes + hdr.ofs_globals, hdr.numglobals * sizeof(int32_t));
    if (HostBigEndian()) {
        for (auto& g : globals_) g = swap32(g);
    }

    progs_ = &header_;
    progs_valid_ = true;
    header_ = hdr;

    // cache global/field offsets by name
    global_self_ = FindGlobal("self");
    global_time_ = FindGlobal("time");
    global_frametime_ = FindGlobal("frametime");
    global_v_forward_ = FindGlobal("v_forward");
    global_v_up_ = FindGlobal("v_up");
    global_v_right_ = FindGlobal("v_right");
    field_origin_ = FindField("origin");
    field_angles_ = FindField("angles");
    field_model_ = FindField("model");
    field_modelindex_ = FindField("modelindex");
    field_solid_ = FindField("solid");
    field_movetype_ = FindField("movetype");
    field_classname_ = FindField("classname");
    field_frame_ = FindField("frame");
    field_viewofs_ = FindField("viewofs");
    field_mins_ = FindField("mins");
    field_maxs_ = FindField("maxs");
    field_think_ = FindField("think");
    field_nextthink_ = FindField("nextthink");

    // self defaults to world edict
    if (global_self_ >= 0) globals_[global_self_] = 0;

    return true;
}

int ProgVM::FindGlobal(const char* name) const {
    for (size_t i = 0; i < globaldefs_.size(); i++) {
        if (strcmp(StringAt(globaldefs_[i].s_name), name) == 0)
            return globaldefs_[i].ofs;
    }
    return -1;
}

int ProgVM::FindField(const char* name) const {
    for (size_t i = 0; i < fielddefs_.size(); i++) {
        if (strcmp(StringAt(fielddefs_[i].s_name), name) == 0)
            return fielddefs_[i].ofs;
    }
    return -1;
}

int ProgVM::FindFunction(const char* name) const {
    for (size_t i = 0; i < functions_.size(); i++) {
        if (strcmp(StringAt(functions_[i].s_name), name) == 0) return (int)i;
    }
    return -1;
}

void ProgVM::AllocateEdicts(int world_edicts) {
    if (!progs_) return;
    edict_stride_ = progs_->entityfields * 4; // v area only; base overhead not modeled
    num_edicts_ = world_edicts;
    size_t bytes = (size_t)MAX_EDICTS * (size_t)edict_stride_;
    edict_mem_.assign(bytes / sizeof(int32_t), 0);
    edict_free_.assign(MAX_EDICTS, 1); // all free
    // edicts 0..world_edicts-1 are reserved (world + clients), never freed.
    for (int i = 0; i < world_edicts && i < MAX_EDICTS; i++) edict_free_[i] = 0;
}

bool ProgVM::EdictFree(int edict_num) const {
    if (edict_num < 0 || edict_num >= MAX_EDICTS) return true;
    return edict_free_[edict_num] != 0;
}

void ProgVM::SetEdictFree(int edict_num, bool free) {
    if (edict_num < 0 || edict_num >= MAX_EDICTS) return;
    edict_free_[edict_num] = free ? 1 : 0;
}

int ProgVM::AllocEdict() {
    for (int i = 1; i < MAX_EDICTS; i++) {
        if (EdictFree(i)) {
            SetEdictFree(i, false);
            if (i >= num_edicts_) num_edicts_ = i + 1;
            // clear the field area
            int32_t* cell = edict_mem_.data() + (size_t)i * (edict_stride_ / 4);
            int n = edict_stride_ / 4;
            for (int k = 0; k < n; k++) cell[k] = 0;
            return i;
        }
    }
    return -1;
}

void ProgVM::FreeEdict(int edict_num) {
    if (edict_num <= 0 || edict_num >= MAX_EDICTS) return;
    SetEdictFree(edict_num, true);
    int32_t* cell = edict_mem_.data() + (size_t)edict_num * (edict_stride_ / 4);
    int n = edict_stride_ / 4;
    for (int k = 0; k < n; k++) cell[k] = 0;
}

// ---- map entity loading (ED_LoadFromFile / ED_ParseEdict parity) ----

namespace {
// COM_Parse-like tokenizer: returns pointer to next token position (or
// nullptr at end). `token` receives the token (quoted strings unquoted).
const char* ParseToken(const char* data, std::string& token) {
    token.clear();
    while (true) {
        if (!*data) return nullptr;
        // skip whitespace
        while (*data == ' ' || *data == '\t' || *data == '\n' || *data == '\r')
            data++;
        // skip // comments
        if (data[0] == '/' && data[1] == '/') {
            while (*data && *data != '\n') data++;
            continue;
        }
        break;
    }
    if (!*data) return nullptr;

    if (*data == '"') {
        data++;
        while (*data && *data != '"') {
            token.push_back(*data++);
        }
        if (*data == '"') data++;
        return data;
    }
    while (*data && *data != ' ' && *data != '\t' && *data != '\n' && *data != '\r') {
        token.push_back(*data++);
    }
    return data;
}
} // namespace

void ProgVM::LoadEntities(const char* data) {
    if (!progs_ || edict_stride_ <= 0) {
        log::Error("progvm: LoadEntities called before setup");
        return;
    }

    std::string token;
    int edict_num = 0; // first entity becomes the world (edict 0)

    while (true) {
        data = ParseToken(data, token);
        if (!data) break;
        if (token != "{") {
            char b[128];
            snprintf(b, sizeof(b), "progvm: ED_LoadFromFile: found %s when expecting {", token.c_str());
            log::Error(b);
            break;
        }

        if (edict_num == 0) {
            // world edict 0 (never free)
            SetEdictFree(0, false);
        } else {
            edict_num = AllocEdict();
            if (edict_num < 0) { log::Error("progvm: no free edicts"); return; }
        }

        // ---- parse the edict pairs ----
        bool init = false;
        std::string keyname;
        while (true) {
            data = ParseToken(data, token);
            if (!data) break;
            if (token == "}") break;
            keyname = token;

            // "angle" -> "angles" hack
            bool anglehack = (keyname == "angle");
            if (anglehack) keyname = "angles";
            if (keyname == "light") keyname = "light_lev";

            // strip trailing spaces
            size_t n = keyname.size();
            while (n && keyname[n - 1] == ' ') { --n; }
            keyname.resize(n);

            data = ParseToken(data, token);
            if (!data || token == "}") break;
            std::string value = token;
            init = true;

            if (!keyname.empty() && keyname[0] == '_') continue; // utility comment

            int fieldofs = FindField(keyname.c_str());
            if (fieldofs < 0) {
                // unknown field: warn loudly (parity with the reference)
                log::Warn("progvm: '");
                log::Warn(keyname.c_str());
                log::Warn("' is not a field");
                continue;
            }
            int32_t type = ev_void;
            for (const auto& fd : fielddefs_) {
                if (fd.ofs == fieldofs) { type = fd.type & ~DEF_SAVEGLOBAL; break; }
            }

            if (anglehack) {
                std::string tmp = "0 " + value + " 0";
                value = tmp;
            }

            int32_t* dst = EdictFieldPtr(edict_num, fieldofs);
            if (!dst) continue;

            // Model submodel index (SV_SetModel parity): "*N" or "maps/b_N.bsp"
            // both map to BSP submodel N. Kept even when the progs later clears
            // the model string (consumed pickups), which is how the renderer
            // knows which brush to hide.
            if (keyname == "model" && field_modelindex_ >= 0) {
                int mi = 0;
                const char* v = value.c_str();
                if (v[0] == '*') mi = atoi(v + 1);
                else if (strncmp(v, "maps/", 5) == 0) {
                    const char* p = strstr(v, "b_");
                    if (p) mi = atoi(p + 2);
                }
                if (mi > 0) {
                    float f = (float)mi;
                    int32_t* midst = EdictFieldPtr(edict_num, field_modelindex_);
                    if (midst) memcpy(midst, &f, 4);
                }
            }

            switch (type) {
            case ev_string:
                dst[0] = InternString(value.c_str());
                break;
            case ev_float: {
                float v = (float)atof(value.c_str());
                memcpy(dst, &v, 4);
                break;
            }
            case ev_vector: {
                float comp[3] = {0, 0, 0};
                const char* p = value.c_str();
                for (int i = 0; i < 3 && p; i++) {
                    comp[i] = (float)atof(p);
                    p = strchr(p, ' ');
                    if (p) ++p;
                }
                for (int i = 0; i < 3; i++) memcpy(dst + i, &comp[i], 4);
                break;
            }
            case ev_entity: {
                int idx = atoi(value.c_str());
                dst[0] = EdictNumToProg(idx);
                break;
            }
            case ev_field: {
                int f = FindField(value.c_str());
                dst[0] = (f >= 0) ? f : 0;
                break;
            }
            case ev_function: {
                int fn = FindFunction(value.c_str());
                dst[0] = fn;
                break;
            }
            default:
                break;
            }
        }

        if (!init) {
            FreeEdict(edict_num);
            edict_num++; // keep advancing for the next entity
            continue;
        }

        int classfield = FindField("classname");
        if (classfield < 0) { FreeEdict(edict_num); edict_num++; continue; }
        const char* cn = EdictFieldString(edict_num, classfield);
        if (!cn || !cn[0]) {
            FreeEdict(edict_num);
            edict_num++;
            continue;
        }
        int fn = FindFunction(cn);
        if (fn < 0) {
            log::Warn("progvm: no spawn function for: ");
            log::Warn(cn);
            FreeEdict(edict_num);
            edict_num++;
            continue;
        }

        SetSelfEdict(edict_num);
        ExecuteProgram(fn);
        edict_num++;
    }
}

char* ProgVM::EdictBase() {
    return reinterpret_cast<char*>(edict_mem_.data());
}

int ProgVM::ProgToEdictNum(int prog_offset) const {
    if (prog_offset < 0 || edict_stride_ <= 0) return -1;
    int num = prog_offset / edict_stride_;
    if (num >= MAX_EDICTS) return -1;
    return num;
}

int32_t* ProgVM::EdictFieldPtr(int edict_num, int field_ofs) {
    if (edict_num < 0 || edict_num >= MAX_EDICTS || field_ofs < 0) {
        return nullptr;
    }
    int32_t* cell = edict_mem_.data() + (size_t)edict_num * (edict_stride_ / 4);
    return cell + field_ofs;
}

int32_t& ProgVM::EdictFieldInt(int edict_num, int field_ofs) {
    int32_t* p = EdictFieldPtr(edict_num, field_ofs);
    static int32_t dummy = 0;
    return p ? *p : dummy;
}

float& ProgVM::EdictFieldFloat(int edict_num, int field_ofs) {
    int32_t* p = EdictFieldPtr(edict_num, field_ofs);
    static float dummy = 0.0f;
    return p ? *reinterpret_cast<float*>(p) : dummy;
}

void ProgVM::EdictFieldVector(int edict_num, int field_ofs, float out[3]) {
    int32_t* p = EdictFieldPtr(edict_num, field_ofs);
    if (p) {
        for (int i = 0; i < 3; i++) out[i] = *reinterpret_cast<float*>(p + i);
    } else {
        out[0] = out[1] = out[2] = 0.0f;
    }
}

void ProgVM::SetEdictFieldVector(int edict_num, int field_ofs, const float v[3]) {
    int32_t* p = EdictFieldPtr(edict_num, field_ofs);
    if (p) {
        for (int i = 0; i < 3; i++) p[i] = fi(v[i]);
    }
}

const char* ProgVM::EdictFieldString(int edict_num, int field_ofs) {
    int32_t* p = EdictFieldPtr(edict_num, field_ofs);
    if (!p) return "";
    return StringAt(p[0]);
}

int ProgVM::EdictFieldEntity(int edict_num, int field_ofs) {
    int32_t* p = EdictFieldPtr(edict_num, field_ofs);
    if (!p) return -1;
    return ProgToEdictNum(p[0]);
}

void ProgVM::SetSelfEdict(int edict_num) {
    if (global_self_ >= 0) globals_[global_self_] = EdictNumToProg(edict_num);
    self_ = EdictNumToProg(edict_num);
}

float ProgVM::Time() const {
    return (global_time_ >= 0) ? ff(globals_[global_time_]) : 0.0f;
}

void ProgVM::SetTime(float t) {
    if (global_time_ >= 0) globals_[global_time_] = fi(t);
}

void ProgVM::SetFrametime(float t) {
    int ofs = FindGlobal("frametime");
    if (ofs >= 0) globals_[ofs] = fi(t);
}

void ProgVM::SetOtherEdict(int edict_num) {
    int ofs = FindGlobal("other");
    if (ofs >= 0) globals_[ofs] = EdictNumToProg(edict_num);
}

int ProgVM::FunctionIndex(const char* global_name) const {
    int ofs = FindGlobal(global_name);
    if (ofs < 0 || ofs >= (int)globals_.size()) return 0;
    return globals_[ofs];
}

void ProgVM::SetGlobalInt(const char* name, int32_t v) {
    int ofs = FindGlobal(name);
    if (ofs >= 0) globals_[ofs] = v;
}

void ProgVM::SetGlobalFloatG(const char* name, float v) {
    int ofs = FindGlobal(name);
    if (ofs >= 0) globals_[ofs] = fi(v);
}

void ProgVM::SetGlobalStringG(const char* name, const char* s) {
    int ofs = FindGlobal(name);
    if (ofs >= 0) globals_[ofs] = InternString(s);
}

// ---- builtin helpers ----

float ProgVM::ParmFloat(int i) {
    int ofs = OFS_PARM0 + i * 3;
    return (ofs + 1 < (int)globals_.size()) ? ff(globals_[ofs]) : 0.0f;
}
int32_t ProgVM::ParmInt(int i) {
    int ofs = OFS_PARM0 + i * 3;
    return (ofs < (int)globals_.size()) ? globals_[ofs] : 0;
}
const char* ProgVM::ParmString(int i) {
    int ofs = OFS_PARM0 + i * 3;
    if (ofs >= (int)globals_.size()) return "";
    return StringAt(globals_[ofs]);
}
int ProgVM::ParmEdictNum(int i) {
    int ofs = OFS_PARM0 + i * 3;
    if (ofs >= (int)globals_.size()) return 0;
    return ProgToEdictNum(globals_[ofs]);
}
void ProgVM::ParmVector(int i, float out[3]) {
    int ofs = OFS_PARM0 + i * 3;
    if (ofs + 2 >= (int)globals_.size()) { out[0] = out[1] = out[2] = 0; return; }
    for (int k = 0; k < 3; k++) out[k] = ff(globals_[ofs + k]);
}

void ProgVM::SetReturnFloat(float v) { globals_[OFS_RETURN] = fi(v); }
void ProgVM::SetReturnInt(int32_t v) { globals_[OFS_RETURN] = v; }
void ProgVM::SetReturnString(const char* s) {
    if (!s) s = "";
    int len = (int)strlen(s) + 1;
    int off = (int)strings_.size();
    strings_.resize(strings_.size() + len);
    memcpy(strings_.data() + off, s, len);
    globals_[OFS_RETURN] = off;
}
void ProgVM::SetReturnEdict(int edict_num) {
    globals_[OFS_RETURN] = EdictNumToProg(edict_num);
}
void ProgVM::SetReturnVector(const float v[3]) {
    for (int k = 0; k < 3; k++) globals_[OFS_RETURN + k] = fi(v[k]);
}

// ---- builtin dispatch ----

void ProgVM::RegisterBuiltin(int num, BuiltinHandler fn) {
    if (num >= 0 && num < MAX_BUILTINS) builtins_[num] = std::move(fn);
}

bool ProgVM::CallBuiltin(int num) {
    if (num < 0 || num >= MAX_BUILTINS || !builtins_[num]) {
        RunError("Bad builtin call number %i", num);
        return false;
    }
    builtins_[num](*this);
    return true;
}

// ---- call stack ----

void ProgVM::RunError(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    err_msg_ = buf;
    last_error_code_ = 1;
    pr_depth_ = 0;
    log::Error(buf);
    if (xfunction_ >= 0 && xfunction_ < (int)functions_.size()) {
        int name = functions_[xfunction_].s_name;
        log::Error("in function ");
        log::Error(StringAt(name));
    }
}

bool ProgVM::EnterFunction(const dfunction_t* f) {
    if (pr_depth_ >= MAX_STACK_DEPTH) { RunError("PR_ExecuteProgram: stack overflow"); return false; }
    int c = f->locals;
    if ((int)localstack_.size() + c > LOCALSTACK_SIZE) {
        RunError("PR_ExecuteProgram: locals stack overflow");
        return false;
    }
    // save off any locals that the new function steps on
    for (int i = 0; i < c; i++)
        localstack_.push_back(globals_[f->parm_start + i]);

    // copy parameters
    int o = f->parm_start;
    for (int i = 0; i < f->numparms; i++) {
        for (int j = 0; j < f->parm_size[i]; j++) {
            globals_[o] = globals_[OFS_PARM0 + i * 3 + j];
            o++;
        }
    }
    return true;
}

void ProgVM::LeaveFunction() {
    if (pr_depth_ <= 0) { RunError("prog stack underflow"); return; }
    if (xfunction_ < 0 || xfunction_ >= (int)functions_.size()) { RunError("bad function"); return; }
    const dfunction_t* f = &functions_[xfunction_];
    int c = f->locals;
    if ((int)localstack_.size() < c) { RunError("locals stack underflow"); return; }
    for (int i = 0; i < c; i++) {
        globals_[f->parm_start + i] = localstack_[localstack_.size() - c + i];
    }
    localstack_.resize(localstack_.size() - c);
    pr_depth_--;
}

bool ProgVM::ExecuteProgram(const char* name) {
    int f = FindFunction(name);
    if (f < 0) {
        log::Error("progvm: no such function: ");
        log::Error(name);
        return false;
    }
    return ExecuteProgram(f);
}

bool ProgVM::ExecuteProgram(int func_num) {
    last_error_code_ = 0;
    err_msg_.clear();

    if (func_num < 0 || func_num >= (int)functions_.size()) {
        RunError("PR_ExecuteProgram: NULL function");
        return false;
    }
    const dfunction_t* f = &functions_[func_num];

    int exitdepth = pr_depth_;

    pr_stack_[pr_depth_].s = xstatement_;
    pr_stack_[pr_depth_].function_num = xfunction_;
    pr_depth_++;
    if (pr_depth_ >= MAX_STACK_DEPTH) { RunError("stack overflow"); return false; }
    if (!EnterFunction(f)) return false;
    xfunction_ = func_num;
    int s = f->first_statement - 1;

    int runaway = MAX_RUNAWAY;
    trace_enabled_ = getenv("ZQ_TRACE") != nullptr;

#define GET(offs) (globals_[(offs)])
#define GETF(offs) (*reinterpret_cast<float*>(&globals_[(offs)]))

    while (true) {
        s++;
        if (s < 0 || s >= (int)statements_.size()) { RunError("statement out of range"); return false; }
        const dstatement_t& st = statements_[s];

        if (trace_enabled_) {
            const char* ops[] = {"DONE","MUL_F","MUL_V","MUL_FV","MUL_VF","DIV_F","DIV_V","DIV_FV","ADD_F","ADD_V","SUB_F","SUB_V","EQ_F","EQ_V","EQ_S","EQ_E","EQ_FNC","NE_F","NE_V","NE_S","NE_E","NE_FNC","LE","GE","LT","GT","LOAD_F","LOAD_ENT","LOAD_S","LOAD_FLD","LOAD_FNC","LOAD_V","STOREP_F","STOREP_ENT","STOREP_S","STOREP_FLD","STOREP_FNC","STOREP_V","STOREP_FE","STOREP_FV","STOREP_EF","STOREP_EV","STORE_F","STORE_ENT","STORE_S","STORE_FLD","STORE_FNC","STORE_V","STORE_FP","STORE_VP","BREAK","ADDRESS","BITAND","BITOR","IFNOT","IF","CALL0","CALL1","CALL2","CALL3","CALL4","CALL5","CALL6","CALL7","CALL8","STATE","GOTO","NOT"};
            const char* o = st.op < (int)(sizeof(ops)/sizeof(ops[0])) ? ops[st.op] : "?";
            fprintf(stderr, "TRACE [%d] <fn%d> %-11s a=%d b=%d c=%d  self=%d\n", s, xfunction_, o, st.a, st.b, st.c, ProgToEdictNum(self_));
        }

        if (!--runaway) { RunError("runaway loop error"); return false; }
        xstatement_ = s;

        switch (st.op) {
        case OP_ADD_F:
            globals_[st.c] = fi(GETF(st.a) + GETF(st.b));
            break;
        case OP_ADD_V:
            for (int i = 0; i < 3; i++) globals_[st.c + i] = fi(GETF(st.a + i) + GETF(st.b + i));
            break;
        case OP_SUB_F:
            globals_[st.c] = fi(GETF(st.a) - GETF(st.b));
            break;
        case OP_SUB_V:
            for (int i = 0; i < 3; i++) globals_[st.c + i] = fi(GETF(st.a + i) - GETF(st.b + i));
            break;
        case OP_MUL_F:
            globals_[st.c] = fi(GETF(st.a) * GETF(st.b));
            break;
        case OP_MUL_V: {
            float r = GETF(st.a) * GETF(st.b) + GETF(st.a + 1) * GETF(st.b + 1) +
                      GETF(st.a + 2) * GETF(st.b + 2);
            globals_[st.c] = fi(r);
            break;
        }
        case OP_MUL_FV:
            for (int i = 0; i < 3; i++) globals_[st.c + i] = fi(GETF(st.a) * GETF(st.b + i));
            break;
        case OP_MUL_VF:
            for (int i = 0; i < 3; i++) globals_[st.c + i] = fi(GETF(st.b) * GETF(st.a + i));
            break;
        case OP_DIV_F:
            globals_[st.c] = fi(GETF(st.a) / GETF(st.b));
            break;
        case OP_BITAND:
            globals_[st.c] = (int)GETF(st.a) & (int)GETF(st.b);
            break;
        case OP_BITOR:
            globals_[st.c] = (int)GETF(st.a) | (int)GETF(st.b);
            break;
        case OP_GE: globals_[st.c] = GETF(st.a) >= GETF(st.b); break;
        case OP_LE: globals_[st.c] = GETF(st.a) <= GETF(st.b); break;
        case OP_GT: globals_[st.c] = GETF(st.a) > GETF(st.b); break;
        case OP_LT: globals_[st.c] = GETF(st.a) < GETF(st.b); break;
        case OP_AND: globals_[st.c] = GETF(st.a) && GETF(st.b); break;
        case OP_OR: globals_[st.c] = GETF(st.a) || GETF(st.b); break;

        case OP_NOT_F: globals_[st.c] = !GETF(st.a); break;
        case OP_NOT_V:
            globals_[st.c] = !GETF(st.a) && !GETF(st.a + 1) && !GETF(st.a + 2);
            break;
        case OP_NOT_S: {
            const char* sp = StringAt(globals_[st.a]);
            globals_[st.c] = !globals_[st.a] || !sp[0];
            break;
        }
        case OP_NOT_FNC: globals_[st.c] = !globals_[st.a]; break;
        case OP_NOT_ENT:
            globals_[st.c] = (globals_[st.a] == 0);
            break;

        case OP_EQ_F: globals_[st.c] = GETF(st.a) == GETF(st.b); break;
        case OP_EQ_V:
            globals_[st.c] = (GETF(st.a) == GETF(st.b)) && (GETF(st.a + 1) == GETF(st.b + 1)) &&
                             (GETF(st.a + 2) == GETF(st.b + 2));
            break;
        case OP_EQ_S: globals_[st.c] = strcmp(StringAt(globals_[st.a]), StringAt(globals_[st.b])) == 0; break;
        case OP_EQ_E: globals_[st.c] = globals_[st.a] == globals_[st.b]; break;
        case OP_EQ_FNC: globals_[st.c] = globals_[st.a] == globals_[st.b]; break;

        case OP_NE_F: globals_[st.c] = GETF(st.a) != GETF(st.b); break;
        case OP_NE_V:
            globals_[st.c] = (GETF(st.a) != GETF(st.b)) || (GETF(st.a + 1) != GETF(st.b + 1)) ||
                             (GETF(st.a + 2) != GETF(st.b + 2));
            break;
        case OP_NE_S: globals_[st.c] = strcmp(StringAt(globals_[st.a]), StringAt(globals_[st.b])) != 0; break;
        case OP_NE_E: globals_[st.c] = globals_[st.a] != globals_[st.b]; break;
        case OP_NE_FNC: globals_[st.c] = globals_[st.a] != globals_[st.b]; break;

        case OP_STORE_F:
        case OP_STORE_ENT:
        case OP_STORE_FLD:
        case OP_STORE_S:
        case OP_STORE_FNC:
            globals_[st.b] = globals_[st.a];
            break;
        case OP_STORE_V:
            for (int i = 0; i < 3; i++) globals_[st.b + i] = globals_[st.a + i];
            break;

        case OP_STOREP_F:
        case OP_STOREP_ENT:
        case OP_STOREP_FLD:
        case OP_STOREP_S:
        case OP_STOREP_FNC: {
            int32_t* ptr = edict_mem_.data() + (globals_[st.b] / 4);
            ptr[0] = globals_[st.a];
            break;
        }
        case OP_STOREP_V: {
            int32_t* ptr = edict_mem_.data() + (globals_[st.b] / 4);
            for (int i = 0; i < 3; i++) ptr[i] = globals_[st.a + i];
            break;
        }

        case OP_ADDRESS: {
            int cell = ProgToEdictNum(globals_[st.a]);
            if (cell < 0) { RunError("OP_ADDRESS: bad edict"); return false; }
            int32_t* base = edict_mem_.data() + (size_t)cell * (edict_stride_ / 4);
            globals_[st.c] = (int32_t)((uint8_t*)(base + globals_[st.b]) - (uint8_t*)edict_mem_.data());
            break;
        }

        case OP_LOAD_F:
        case OP_LOAD_FLD:
        case OP_LOAD_ENT:
        case OP_LOAD_S:
        case OP_LOAD_FNC: {
            int cell = ProgToEdictNum(globals_[st.a]);
            if (cell < 0) { RunError("OP_LOAD: bad edict"); return false; }
            int32_t* addr = edict_mem_.data() + (size_t)cell * (edict_stride_ / 4) + globals_[st.b];
            globals_[st.c] = addr[0];
            break;
        }
        case OP_LOAD_V: {
            int cell = ProgToEdictNum(globals_[st.a]);
            if (cell < 0) { RunError("OP_LOAD_V: bad edict"); return false; }
            int32_t* addr = edict_mem_.data() + (size_t)cell * (edict_stride_ / 4) + globals_[st.b];
            for (int i = 0; i < 3; i++) globals_[st.c + i] = addr[i];
            break;
        }

        case OP_IFNOT:
            if (!globals_[st.a]) s += st.b - 1;
            break;
        case OP_IF:
            if (globals_[st.a]) s += st.b - 1;
            break;
        case OP_GOTO:
            s += st.a - 1;
            break;

        case OP_CALL0:
        case OP_CALL1:
        case OP_CALL2:
        case OP_CALL3:
        case OP_CALL4:
        case OP_CALL5:
        case OP_CALL6:
        case OP_CALL7:
        case OP_CALL8: {
            argc_ = st.op - OP_CALL0;
            if (!globals_[st.a]) { RunError("NULL function"); return false; }
            int newf_num = (int)globals_[st.a];
            if (newf_num < 0 || newf_num >= (int)functions_.size()) { RunError("function out of range"); return false; }
            const dfunction_t* newf = &functions_[newf_num];
            if (newf->first_statement < 0) {
                int i = -newf->first_statement;
                if (!CallBuiltin(i)) return false;
                break;
            }
            pr_stack_[pr_depth_].s = s;
            pr_stack_[pr_depth_].function_num = xfunction_;
            pr_depth_++;
            if (pr_depth_ >= MAX_STACK_DEPTH) { RunError("stack overflow"); return false; }
            if (!EnterFunction(newf)) return false;
            xfunction_ = newf_num;
            s = newf->first_statement - 1;
            break;
        }

        case OP_DONE:
        case OP_RETURN: {
            for (int i = 0; i < 3; i++) globals_[OFS_RETURN + i] = globals_[st.a + i];
            {
                int ret_s = pr_stack_[pr_depth_ - 1].s;
                int ret_fn = pr_stack_[pr_depth_ - 1].function_num;
                LeaveFunction();
                if (last_error_code_) return false;
                if (pr_depth_ == exitdepth) {
                    xfunction_ = ret_fn;
                    return true;
                }
                xfunction_ = ret_fn;
                s = ret_s;
            }
            break;
        }

        case OP_STATE: {
            int ed = ProgToEdictNum(self_);
            if (ed < 0) ed = 0;
            // nextthink = time + 0.1
            if (field_nextthink_ >= 0)
                EdictFieldFloat(ed, field_nextthink_) = Time() + 0.1f;
            if (field_frame_ >= 0)
                EdictFieldFloat(ed, field_frame_) = GETF(st.a);
            if (field_think_ >= 0)
                EdictFieldInt(ed, field_think_) = globals_[st.b];
            break;
        }

        default:
            RunError("Bad opcode %i", st.op);
            return false;
        }
    }
#undef GET
#undef GETF
}

} // namespace zq::vm