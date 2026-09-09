// Gameplay QuakeC builtins that need the world geometry / per-frame solid
// entity list. Ported from the reference pr_cmds.c / sv_* (find, findradius,
// walkmove, movetogoal, checkbottom, changeyaw, aim, traceline, ...).
#include "engine/gameframe.hpp"
#include "engine/cvar_system.hpp"
#include "core/logging/logger.hpp"
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
    SetTraceResult(vm, tr);
}

// checkclient (#17)
void Buf_checkclient(vm::ProgVM& vm) {
    vm.SetReturnEdict(GetGameTraceContext().client_edict);
}

// find (#18): (entity start, .string field, string match) - reference PF_find.
// The progs calls find(world, classname, "weapon_nailgun") etc.; the field
// argument is the field-def offset and only classname lookups occur in-game.
void Buf_find(vm::ProgVM& vm) {
    int start = vm.ParmEdictNum(0);
    const char* name = vm.ParmString(2);
    int fclass = vm.FindField("classname");
    for (int e = start + 1; e < 1024; e++) {
        if (e == 0) continue;
        if (vm.EdictFree(e)) continue;
        if (name && name[0]) {
            const char* c = fclass >= 0 ? vm.EdictFieldString(e, fclass) : "";
            if (c && strcmp(c, name) == 0) {
                vm.SetReturnEdict(e);
                return;
            }
        }
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

// walkmove (#32): walkstep `self` by up to `dist` units along the direction
// vector parm0 (QV progs pass self.movedir). Mirrors SV_movestep: try the
// direct move, else a step up and over small lips, dropping back to the floor.
// Returns 1 if the move succeeded, 0 if blocked.
void Buf_walkmove(vm::ProgVM& vm) {
    const GameTraceContext& g = GetGameTraceContext();
    int self = SelfEnt(vm);
    int fo = Fld(vm, "origin"), fm = Fld(vm, "mins"), fx = Fld(vm, "maxs");
    if (self < 0 || !g.map || fo < 0 || fm < 0 || fx < 0) { vm.SetReturnFloat(0); return; }

    float dir[3];
    vm.ParmVector(0, dir);
    float dist = vm.ParmFloat(1);
    float dlen = std::sqrt(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);
    float mv[3] = { 0, 0, 0 };
    if (dlen > 0) { mv[0] = dir[0]/dlen * dist; mv[1] = dir[1]/dlen * dist; }

    float org[3], mn[3], mx[3];
    vm.EdictFieldVector(self, fo, org);
    vm.EdictFieldVector(self, fm, mn);
    vm.EdictFieldVector(self, fx, mx);
    // monsters are block-sized; the VM keeps mins/maxs as world-extent boxes
    // centred on the origin (builders set them), so MoveBox matches directly.

    // 1) try the direct horizontal move
    float end[3] = { org[0]+mv[0], org[1]+mv[1], org[2] };
    auto tr = MoveBox(*g.map, mn, mx, org, end, g.ents, g.num_ents, self);
    if (tr.fraction == 1.0f) {
        // Reference SV_movestep: confirm the target still has floor underneath
        // (step-down check). If there is no floor within a step below, the
        // monster walked off an edge - do NOT let it fly; stop at the edge.
        constexpr float STEPSIZE = 18.0f;
        float raised[3] = { end[0], end[1], end[2] + STEPSIZE };
        float down[3] = { end[0], end[1], end[2] - STEPSIZE * 2 };
        auto d = MoveBox(*g.map, mn, mx, raised, down, g.ents, g.num_ents, self);
        if (d.allsolid || d.startsolid || d.fraction >= 1.0f) {
            vm.SetReturnFloat(0);   // walked off an edge: stop here
            return;
        }
        vm.SetEdictFieldVector(self, fo, end);
        SetFlags(vm, self, Fld(vm, "flags"), IntFlags(vm, self, Fld(vm, "flags")) | FL_ONGROUND);
        vm.SetReturnFloat(1);
        return;
    }
    if (tr.allsolid || tr.startsolid) { vm.SetReturnFloat(0); return; }

    // 2) step up to clear a small lip (STEPSIZE 18 like the reference)
    constexpr float STEPSIZE = 18.0f;
    float raised[3] = { org[0], org[1], org[2] + STEPSIZE };
    float forend[3] = { raised[0]+mv[0], raised[1]+mv[1], raised[2] };
    auto f = MoveBox(*g.map, mn, mx, raised, forend, g.ents, g.num_ents, self);
    if (f.allsolid || f.startsolid || f.fraction < 1.0f) { vm.SetReturnFloat(0); return; }

    // 3) drop back down to the floor
    float down[3] = { forend[0], forend[1], forend[2] - STEPSIZE - 32.0f };
    auto d = MoveBox(*g.map, mn, mx, forend, down, g.ents, g.num_ents, self);
    if (d.allsolid || d.fraction >= 1.0f) { vm.SetReturnFloat(0); return; }
    vm.SetEdictFieldVector(self, fo, d.endpos);
    SetFlags(vm, self, Fld(vm, "flags"), IntFlags(vm, self, Fld(vm, "flags")) | FL_ONGROUND);
    vm.SetReturnFloat(1);
}

// movetogoal (#67): (entity goal) - walk toward the goal entity's origin.
void Buf_movetogoal(vm::ProgVM& vm) {
    const GameTraceContext& g = GetGameTraceContext();
    int self = SelfEnt(vm);
    int fo = Fld(vm, "origin"), fm = Fld(vm, "mins"), fx = Fld(vm, "maxs");
    int fg = Fld(vm, "goalentity"), ff = Fld(vm, "flags"), fs = Fld(vm, "speed");
    if (self < 0 || !g.map || fo < 0) { vm.SetReturnFloat(0); return; }
    int goal = vm.ParmEdictNum(0);
    if (goal <= 0) goal = (fg >= 0) ? vm.EdictFieldEntity(self, fg) : 0;
    if (goal <= 0) { vm.SetReturnFloat(0); return; }
    if (fg >= 0) vm.EdictFieldInt(self, fg) = vm.EdictNumToProg(goal);

    float org[3], gorg[3];
    vm.EdictFieldVector(self, fo, org);
    vm.EdictFieldVector(goal, fo, gorg);
    float dx = gorg[0]-org[0], dy = gorg[1]-org[1];
    float dist = std::sqrt(dx*dx + dy*dy);
    if (dist < 1.0f) { vm.SetReturnFloat(1); return; }

    float speed = (fs >= 0) ? vm.EdictFieldFloat(self, fs) : 0.0f;
    if (speed <= 0) speed = 100.0f;
    float frametime = 0.016f;
    int gt = vm.FindGlobal("frametime");
    if (gt >= 0) memcpy(&frametime, &vm.Global(gt), 4);
    if (frametime <= 0) frametime = 0.016f;
    float step = speed * frametime;
    if (step > dist) step = dist;

    float mve[3] = { dx/dist * step, dy/dist * step, 0 };
    float mn[3] = {0,0,0}, mx[3] = {0,0,0};
    if (fm >= 0 && fx >= 0) {
        vm.EdictFieldVector(self, fm, mn);
        vm.EdictFieldVector(self, fx, mx);
    }

    auto doStep = [&](const float* start, const float* end,
                      BSPMap::TraceResult& out) -> bool {
        auto t = MoveBox(*g.map, mn, mx, start, end, g.ents, g.num_ents, self);
        if (t.allsolid || t.startsolid) return false;
        if (t.fraction == 1.0f) {
            // Reference SV_movestep: confirm floor below; walking off an edge
            // must not let the monster fly (no gravity while FL_ONGROUND).
            constexpr float STEPSIZE = 18.0f;
            float raised[3] = { end[0], end[1], end[2] + STEPSIZE };
            float down[3] = { end[0], end[1], end[2] - STEPSIZE * 2 };
            auto d = MoveBox(*g.map, mn, mx, raised, down, g.ents, g.num_ents, self);
            if (d.allsolid || d.startsolid || d.fraction >= 1.0f) return false;
            out = t; return true;
        }
        constexpr float STEPSIZE = 18.0f;
        float raised[3] = { start[0], start[1], start[2] + STEPSIZE };
        float fe[3] = { raised[0] + (end[0]-start[0]), raised[1] + (end[1]-start[1]), raised[2] };
        auto fr = MoveBox(*g.map, mn, mx, raised, fe, g.ents, g.num_ents, self);
        if (fr.allsolid || fr.startsolid || fr.fraction < 1.0f) out = t;
        else {
            float down[3] = { fe[0], fe[1], fe[2] - STEPSIZE - 32.0f };
            auto d = MoveBox(*g.map, mn, mx, fe, down, g.ents, g.num_ents, self);
            if (d.allsolid || d.fraction >= 1.0f) { out = t; return false; }
            out = d;
        }
        return true;
    };

    BSPMap::TraceResult tr;
    float dst[3] = { org[0]+mve[0], org[1]+mve[1], org[2] };
    bool ok = doStep(org, dst, tr);
    if (ok) vm.SetEdictFieldVector(self, fo, tr.endpos);
    if (ff >= 0) vm.EdictFieldFloat(self, ff) = (float)(IntFlags(vm, self, ff) | FL_ONGROUND);
    int fv = Fld(vm, "velocity");
    if (fv >= 0) {
        float v[3] = { dx/dist * speed, dy/dist * speed, 0 };
        vm.SetEdictFieldVector(self, fv, v);
    }
    if (Fld(vm, "ideal_yaw") >= 0) {
        float yaw = AngMod(std::atan2(dy, dx) * 180.0f / PI);
        vm.EdictFieldFloat(self, Fld(vm, "ideal_yaw")) = yaw;
    }
    vm.SetReturnFloat(ok ? 1.0f : 0.0f);
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

// Write* (#52-59), centerprint (#73), ambientsound (#74), multicast (#82)
void Buf_noop(vm::ProgVM& vm) { (void)vm; }

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
    vm.RegisterBuiltin(47, Buf_nextent);
    vm.RegisterBuiltin(49, Buf_changeyaw);
    vm.RegisterBuiltin(52, Buf_noop);
    vm.RegisterBuiltin(53, Buf_noop);
    vm.RegisterBuiltin(54, Buf_noop);
    vm.RegisterBuiltin(55, Buf_noop);
    vm.RegisterBuiltin(56, Buf_noop);
    vm.RegisterBuiltin(57, Buf_noop);
    vm.RegisterBuiltin(58, Buf_noop);
    vm.RegisterBuiltin(59, Buf_noop);
    vm.RegisterBuiltin(67, Buf_movetogoal);
    vm.RegisterBuiltin(68, Buf_precache);      // precache_file
    vm.RegisterBuiltin(72, Buf_cvar_set);
    vm.RegisterBuiltin(73, Buf_noop);          // centerprint
    vm.RegisterBuiltin(74, Buf_noop);          // ambientsound
    vm.RegisterBuiltin(75, Buf_precache);      // precache_model2
    vm.RegisterBuiltin(76, Buf_precache);      // precache_sound2
    vm.RegisterBuiltin(77, Buf_precache);      // precache_file2
    vm.RegisterBuiltin(78, Buf_setspawnparms);
    vm.RegisterBuiltin(79, Buf_noop);          // logfrag
    vm.RegisterBuiltin(80, Buf_infokey);
    vm.RegisterBuiltin(82, Buf_noop);          // multicast
}

} // namespace zq::engine
