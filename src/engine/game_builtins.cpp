// engine/game_builtins.cpp — Gameplay QuakeC builtins (traceline, walkmove,
// movetogoal, checkbottom, changeyaw, aim, find, findradius, ...).
#include "engine/gameframe.hpp"
#include "engine/monster_move.hpp"
#include "engine/cvar_system.hpp"
#include "engine/particle.hpp"
#include "engine/muzzle_flash.hpp"
#include "core/logging/logger.hpp"
#include <cstddef>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace zq::engine {
namespace {

constexpr float PI = 3.14159265358979f;
constexpr int MOVE_TOSS = 6;

inline float AngMod(float a) {
    while (a < 0) a += 360;
    while (a >= 360) a -= 360;
    return a;
}

int SelfEnt(vm::ProgVM& vm) {
    int g = vm.FindGlobal("self");
    if (g < 0) return 0;
    return vm.ProgToEdictNum(vm.Global(g));
}

int Fld(vm::ProgVM& vm, const char* n) { return vm.FindField(n); }

inline int IntFlags(vm::ProgVM& vm, int e, int f) { return (int)vm.EdictFieldFloat(e, f); }
inline void SetFlags(vm::ProgVM& vm, int e, int f, int v) { vm.EdictFieldFloat(e, f) = (float)v; }

// ---- math helpers ----

// vtos-like: append to the return string
// (normalize #9)
void Buf_normalize(vm::ProgVM& vm) {
    float v[3];
    vm.ParmVector(0, v);
    float len = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    float o[3] = {0, 0, 0};
    if (len) { o[0] = v[0]/len; o[1] = v[1]/len; o[2] = v[2]/len; }
    vm.SetReturnVector(o);
}

// sound (#8) - no server audio yet
void Buf_sound(vm::ProgVM& vm) { (void)vm; }

// cvar (#45) / cvar_set (#72)
float CvarValue(vm::ProgVM&, const char* name) {
    CVar* c = CVarSystem::GetCVar(String(name));
    if (!c) {
        c = CVarSystem::RegisterCVar(String(name), String("0"));
    }
    return c->GetFloat();
}
void Buf_cvar(vm::ProgVM& vm) {
    const char* n = vm.ParmString(0);
    vm.SetReturnFloat(CvarValue(vm, n));
}
void Buf_cvar_set(vm::ProgVM& vm) {
    const char* n = vm.ParmString(0);
    float v = vm.ParmFloat(1);
    CVar* c = CVarSystem::GetCVar(String(n));
    if (!c) c = CVarSystem::RegisterCVar(String(n), String("0"));
    c->SetFloat(v);
}

// ---- trace globals ----
void SetTraceResult(vm::ProgVM& vm, const BSPMap::TraceResult& tr) {
    vm.SetGlobalFloatG("trace_fraction", tr.fraction);
    vm.SetGlobalFloatG("trace_allsolid", tr.allsolid ? 1.0f : 0.0f);
    vm.SetGlobalFloatG("trace_startsolid", tr.startsolid ? 1.0f : 0.0f);
    vm.SetGlobalFloatG("trace_inopen", tr.inopen ? 1.0f : 0.0f);
    vm.SetGlobalFloatG("trace_inwater", tr.inwater ? 1.0f : 0.0f);
    vm.SetGlobalInt("trace_ent", tr.hit_edict > 0 ? vm.EdictNumToProg(tr.hit_edict) : 0);
    vm.SetGlobalFloatG("trace_endpos_x", tr.endpos[0]);
    vm.SetGlobalFloatG("trace_endpos_y", tr.endpos[1]);
    vm.SetGlobalFloatG("trace_endpos_z", tr.endpos[2]);
    // trace_plane_normal (vector global) if present
    int pn = vm.FindGlobal("trace_plane_normal");
    if (pn >= 0) {
        for (int i = 0; i < 3; i++) {
            int32_t bits;
            memcpy(&bits, &tr.plane_normal[i], 4);
            vm.Global(pn + i) = bits;
        }
    }
}

// traceline (#16): point line through world + solid entity boxes
void Buf_traceline(vm::ProgVM& vm) {
    const GameTraceContext& g = GetGameTraceContext();
    float v1[3], v2[3];
    vm.ParmVector(0, v1);
    vm.ParmVector(1, v2);
    float zmin[3] = {0, 0, 0}, zmax[3] = {0, 0, 0};
    int ignore = -1;
    int forent = vm.ParmEdictNum(3);
    // nomonsters (parm2) != 0 disables checking monster/box entities
    if (vm.ParmFloat(2) != 0.0f) {
        auto tr = g.map ? g.map->WorldTrace(v1, v2, zmin, zmax) : BSPMap::TraceResult{};
        if (g.map) tr.hit_brush = true;
        SetTraceResult(vm, tr);
        return;
    }
    if (forent > 0) ignore = forent;
    auto tr = g.map ? MoveBox(*g.map, zmin, zmax, v1, v2, g.ents, g.num_ents, ignore)
                    : BSPMap::TraceResult{};
    if (getenv("ZQ_TRACE_LINES")) {
        int selfn = vm.SelfEdict();
        fprintf(stderr, "TL<fn=%s> traceline v1=(%.1f,%.1f,%.1f)->v2=(%.1f,%.1f,%.1f) ignore=%d hit=%d frac=%.3f\n",
                vm.CurrentFunctionName(), v1[0], v1[1], v1[2], v2[0], v2[1], v2[2],
                ignore, tr.hit_edict, tr.fraction);
    }
    SetTraceResult(vm, tr);
}

// checkclient (#17)
void Buf_checkclient(vm::ProgVM& vm) {
    vm.SetReturnEdict(GetGameTraceContext().client_edict);
}

// find (#18): (entity start, .string field, string match) - reference PF_find.
// WinQuake find -- PARM1 is the byte offset of a string field in each edict;
// return the first edict after start whose field-octet holds the match string.
void Buf_find(vm::ProgVM& vm) {
    int start = vm.ParmEdictNum(0);
    int field_offs = vm.ParmInt(1);
    const char* match = vm.ParmString(2);
    for (int e = start + 1; e < 1024; e++) {
        if (e == 0) continue;
        if (vm.EdictFree(e)) continue;
        const char* s = vm.EdictFieldString(e, field_offs);
        if (s && strcmp(s, match) == 0) { vm.SetReturnEdict(e); return; }
    }
    vm.SetReturnEdict(0);
}

// nextent (#47)
void Buf_nextent(vm::ProgVM& vm) {
    int start = vm.ParmEdictNum(0);
    for (int e = start + 1; e < 1024; e++) {
        if (vm.EdictFree(e)) continue;
        vm.SetReturnEdict(e);
        return;
    }
    vm.SetReturnEdict(0);
}

// findradius (#22): (vector org, float rad)
void Buf_findradius(vm::ProgVM& vm) {
    float org[3];
    vm.ParmVector(0, org);
    float rad = vm.ParmFloat(1);
    int fo = Fld(vm, "origin");
    for (int e = 1; e < 1024; e++) {
        if (vm.EdictFree(e)) continue;
        float o[3] = { org[0], org[1], org[2] };
        if (fo >= 0) vm.EdictFieldVector(e, fo, o);
        float dx = org[0]-o[0], dy = org[1]-o[1], dz = org[2]-o[2];
        if (dx*dx + dy*dy + dz*dz < rad*rad) {
            vm.SetReturnEdict(e);
            return;
        }
    }
    vm.SetReturnEdict(0);
}

// checkbottom (#40)
void Buf_checkbottom(vm::ProgVM& vm) {
    int self = SelfEnt(vm);
    int fo = Fld(vm, "origin"), fm = Fld(vm, "mins"), fx = Fld(vm, "maxs");
    if (self < 0 || fo < 0 || fm < 0 || fx < 0) { vm.SetReturnFloat(0); return; }
    float org[3];
    vm.EdictFieldVector(self, fo, org);
    float mn[3], mx[3];
    vm.EdictFieldVector(self, fm, mn);
    vm.EdictFieldVector(self, fx, mx);
    const GameTraceContext& g = GetGameTraceContext();
    float zmin[3] = {0,0,0}, zmax[3] = {0,0,0};

    for (int cx = 0; cx <= 1; cx++)
        for (int cy = 0; cy <= 1; cy++) {
            float p1[3] = { org[0] + (cx ? mx[0] : mn[0]),
                            org[1] + (cy ? mx[1] : mn[1]),
                            org[2] + mn[2] };
            float p2[3] = { p1[0], p1[1], p1[2] - 36 };
            auto tr = g.map ? MoveBox(*g.map, zmin, zmax, p1, p2, g.ents, g.num_ents, self)
                            : BSPMap::TraceResult{};
            if (tr.fraction == 1.0f) { vm.SetReturnFloat(0); return; }
            // must be within a step of the bottom
            if ((p1[2] - tr.endpos[2]) > 18.0f) { vm.SetReturnFloat(0); return; }
        }
    vm.SetReturnFloat(1);
}

// walkmove (#32): float(float yaw, float dist) — mirrors PF_walkmove in
// the reference (pr_cmds.c:1146). Returns 0 if the entity is not on ground,
// flying, or swimming.
void Buf_walkmove(vm::ProgVM& vm) {
    int self = SelfEnt(vm);
    if (self <= 0) { vm.SetReturnFloat(0); return; }
    int ff = Fld(vm, "flags");
    if (ff < 0) { vm.SetReturnFloat(0); return; }

    int flags = IntFlags(vm, self, ff);
    if (!(flags & (FL_ONGROUND | FL_FLY | FL_SWIM))) {
        vm.SetReturnFloat(0);
        return;
    }

    float yaw = vm.ParmFloat(0);
    float dist = vm.ParmFloat(1);

    constexpr float DEG2RAD = PI / 180.0f;
    float rad = yaw * DEG2RAD;
    float mv[3] = { std::cos(rad) * dist, std::sin(rad) * dist, 0 };

    vm.SetReturnFloat(SV_Movestep(vm, self, mv, true) ? 1.0f : 0.0f);
}

// movetogoal (#67): (entity goal) - walk toward the goal entity's origin.
void Buf_movetogoal(vm::ProgVM& vm) {
    SV_MoveToGoal(vm);
}

// changeyaw (#49)
void Buf_changeyaw(vm::ProgVM& vm) {
    int self = SelfEnt(vm);
    int fa = Fld(vm, "angles"), fi = Fld(vm, "ideal_yaw"), fs = Fld(vm, "yaw_speed");
    if (self < 0 || fa < 0 || fi < 0) return;
    float current = AngMod(vm.EdictFieldFloat(self, fa + 1));
    float ideal = vm.EdictFieldFloat(self, fi);
    if (current == ideal) return;
    // Reference PF_changeyaw: speed is yaw_speed directly (degrees per call),
    // NOT scaled by frametime. Turning is clamped to +/-speed, snapping to
    // ideal_yaw when the remaining turn is within speed.
    float speed = (fs >= 0) ? vm.EdictFieldFloat(self, fs) : 60.0f;
    speed = speed ? speed : 60.0f;
    float move = ideal - current;
    if (ideal > current) {
        if (move >= 180.0f) move -= 360.0f;
    } else {
        if (move <= -180.0f) move += 360.0f;
    }
    if (move > 0.0f) {
        if (move > speed) move = speed;
    } else {
        if (move < -speed) move = -speed;
    }
    vm.EdictFieldFloat(self, fa + 1) = AngMod(current + move);
}

// aim (#44): vector aiming at the enemy
void Buf_aim(vm::ProgVM& vm) {
    int self = SelfEnt(vm);
    int fo = Fld(vm, "origin"), fe = Fld(vm, "enemy"), fv = Fld(vm, "view_ofs");
    float org[3], target[3] = {0,0,0};
    if (self >= 0) vm.EdictFieldVector(self, fo, org);
    int en = self >= 0 ? vm.EdictFieldEntity(self, fe) : 0;
    if (en > 0) vm.EdictFieldVector(en, fo, target);
    float start[3] = { org[0], org[1], org[2] + 22 };
    if (self >= 0 && fv >= 0) vm.EdictFieldVector(self, fv, start);
    float v[3] = { target[0]-start[0], target[1]-start[1], target[2]-start[2] };
    float len = std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    float o[3] = {0,0,0};
    if (len) { o[0]=v[0]/len; o[1]=v[1]/len; o[2]=v[2]/len; }
    vm.SetReturnVector(o);
}

// pointcontents (#41): (vector) - SV_PointContents
void Buf_pointcontents(vm::ProgVM& vm) {
    const GameTraceContext& g = GetGameTraceContext();
    float p[3];
    vm.ParmVector(0, p);
    if (!g.map) { vm.SetReturnFloat(0); return; }
    vm.SetReturnFloat((float)g.map->PointContents(p));
}

// droptofloor (#34): (void) - SV_DropToFloor (returns 1 if not dropped off an edge)
void Buf_droptofloor(vm::ProgVM& vm) {
    const GameTraceContext& g = GetGameTraceContext();
    int self = SelfEnt(vm);
    int fo = Fld(vm, "origin"), fm = Fld(vm, "mins"), fx = Fld(vm, "maxs");
    if (self < 0 || !g.map || fo < 0 || fm < 0 || fx < 0) { vm.SetReturnFloat(0); return; }
    float org[3], mn[3], mx[3];
    vm.EdictFieldVector(self, fo, org);
    vm.EdictFieldVector(self, fm, mn);
    vm.EdictFieldVector(self, fx, mx);
    float end[3] = { org[0], org[1], org[2] - 256 };
    auto tr = MoveBox(*g.map, mn, mx, org, end, g.ents, g.num_ents, self);
    if (tr.fraction == 1.0f || tr.allsolid || tr.startsolid) { vm.SetReturnFloat(0); return; }
    vm.SetEdictFieldVector(self, fo, tr.endpos);
    vm.SetReturnFloat(1);
}

// ---- SVC_MUZZLEFLASH multicast capture (qw-qc/player.qc muzzleflash()) ----
// The QW progs signals a shot by writing a tiny message on the MSG_MULTICAST
// channel and sending it through multicast(). The network message itself is
// dropped, but we sniff it to raise MuzzleFlashSignal for the app's per-tick
// muzzle strobe (QW's EF_MUZZLEFLASH in .effects is never set).
constexpr int MSG_MULTICAST = 4;      // defs.qc
constexpr int SVC_MUZZLEFLASH = 39;   // defs.qc (qw svc_strings index)
unsigned char g_mc_buf[8];
int g_mc_len = 0;

void Mz_writebyte(vm::ProgVM& vm) {
    if ((int)vm.ParmFloat(0) == MSG_MULTICAST && g_mc_len < (int)sizeof(g_mc_buf))
        g_mc_buf[g_mc_len++] = (unsigned char)(int)vm.ParmFloat(1);
}

// WriteShort (#54): extends the buffer so later entity offsets stay aligned.
void Mz_writeshort(vm::ProgVM& vm) {
    if ((int)vm.ParmFloat(0) == MSG_MULTICAST && g_mc_len + 2 <= (int)sizeof(g_mc_buf)) {
        int v = (int)vm.ParmFloat(1);
        g_mc_buf[g_mc_len++] = (unsigned char)(v & 0xFF);
        g_mc_buf[g_mc_len++] = (unsigned char)((v >> 8) & 0xFF);
    }
}

// WriteEntity (#59): void(float to, entity s)
void Mz_writeentity(vm::ProgVM& vm) {
    if ((int)vm.ParmFloat(0) != MSG_MULTICAST || g_mc_len + 2 > (int)sizeof(g_mc_buf)) return;
    int ent = vm.ParmEdictNum(1);
    g_mc_buf[g_mc_len++] = (unsigned char)(ent & 0xFF);
    g_mc_buf[g_mc_len++] = (unsigned char)((ent >> 8) & 0xFF);
}

// multicast (#82): void(vector where, float set) — sends the built message.
void Buf_multicast(vm::ProgVM& vm) {
    if (g_mc_len >= 3 && g_mc_buf[0] == SVC_MUZZLEFLASH) {
        int ent = g_mc_buf[1] | (g_mc_buf[2] << 8);
        MuzzleFlashSignal::Signal(ent);
    }
    g_mc_len = 0;
    (void)vm;
}

// Write* (#52-59), centerprint (#73), ambientsound (#74)
void Buf_noop(vm::ProgVM& vm) { (void)vm; }

// particle (#48): void(vector org, vector dir, float color, float count) -
// feeds the engine particle store which the renderers consume (blood, sparks,
// bullet puffs fired by T_Blood / attacks).
void Buf_particle(vm::ProgVM& vm) {
    float org[3], dir[3];
    vm.ParmVector(0, org);
    vm.ParmVector(1, dir);
    float color = vm.ParmFloat(2);
    int count = (int)vm.ParmFloat(3);
    ParticleSystem::StartParticle(org, dir, count, (int)color);
}

// setspawnparms (#78)
void Buf_setspawnparms(vm::ProgVM& vm) { (void)vm; }

// infokey (#80) - no client info keys
void Buf_infokey(vm::ProgVM& vm) { vm.SetReturnString(""); }

// precache_*2 / precache_file (#68,75,76,77)
void Buf_precache(vm::ProgVM& vm) { vm.SetReturnString(vm.ParmString(0)); }

} // namespace

void RegisterGameBuiltins(vm::ProgVM& vm) {
    vm.RegisterBuiltin(8, Buf_sound);
    vm.RegisterBuiltin(9, Buf_normalize);
    vm.RegisterBuiltin(16, Buf_traceline);
    vm.RegisterBuiltin(17, Buf_checkclient);
    vm.RegisterBuiltin(18, Buf_find);
    vm.RegisterBuiltin(21, Buf_noop);          // stuffcmd
    vm.RegisterBuiltin(22, Buf_findradius);
    vm.RegisterBuiltin(32, Buf_walkmove);
    vm.RegisterBuiltin(34, Buf_droptofloor);
    vm.RegisterBuiltin(35, Buf_noop);          // lightstyle (visual only)
    vm.RegisterBuiltin(40, Buf_checkbottom);
    vm.RegisterBuiltin(41, Buf_pointcontents);
    vm.RegisterBuiltin(44, Buf_aim);
    vm.RegisterBuiltin(45, Buf_cvar);
    vm.RegisterBuiltin(46, Buf_noop);          // localcmd
    vm.RegisterBuiltin(47, Buf_nextent);
    vm.RegisterBuiltin(48, Buf_particle);
    vm.RegisterBuiltin(49, Buf_changeyaw);
    vm.RegisterBuiltin(52, Mz_writebyte);
    vm.RegisterBuiltin(53, Mz_writebyte);
    vm.RegisterBuiltin(54, Mz_writeshort);
    vm.RegisterBuiltin(55, Buf_noop);
    vm.RegisterBuiltin(56, Buf_noop);
    vm.RegisterBuiltin(57, Buf_noop);
    vm.RegisterBuiltin(58, Buf_noop);
    vm.RegisterBuiltin(59, Mz_writeentity);
    vm.RegisterBuiltin(67, Buf_movetogoal);
    vm.RegisterBuiltin(68, Buf_precache);      // precache_file
    vm.RegisterBuiltin(69, Buf_noop);          // makestatic
    vm.RegisterBuiltin(72, Buf_cvar_set);
    vm.RegisterBuiltin(73, Buf_noop);          // centerprint
    vm.RegisterBuiltin(74, Buf_noop);          // ambientsound
    vm.RegisterBuiltin(75, Buf_precache);      // precache_model2
    vm.RegisterBuiltin(76, Buf_precache);      // precache_sound2
    vm.RegisterBuiltin(77, Buf_precache);      // precache_file2
    vm.RegisterBuiltin(78, Buf_setspawnparms);
    vm.RegisterBuiltin(79, Buf_noop);          // logfrag
    vm.RegisterBuiltin(80, Buf_infokey);
    vm.RegisterBuiltin(82, Buf_multicast);     // multicast
}

// MuzzleFlashSignal: single pending edict, consumed by the app's battle poll.
std::atomic<int> MuzzleFlashSignal::pending_edict_{-1};

void MuzzleFlashSignal::Signal(int edict) {
    pending_edict_.store(edict, std::memory_order_relaxed);
}

bool MuzzleFlashSignal::Consume(int edict) {
    // exchange so a stale flash never re-fires on a later tick, and a nonzero
    // pending flash for a different edict is not lost to a later poll.
    return pending_edict_.exchange(-1, std::memory_order_relaxed) == edict;
}

} // namespace zq::engine
