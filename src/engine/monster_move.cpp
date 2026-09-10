// Monster movement helpers ported from quake/WinQuake/sv_move.c.
// Operates on edicts via ProgVM + the current game trace context.
#include "engine/monster_move.hpp"
#include "engine/gameframe.hpp"
#include "engine/move.hpp"
#include <cmath>
#include <algorithm>

namespace zq::engine {
namespace {

constexpr float PI = 3.14159265358979f;
constexpr float STEPSIZE = 18.0f;
constexpr float DI_NODIR = -1.0f;

inline int IntField(vm::ProgVM& vm, int e, int f) { return (int)vm.EdictFieldFloat(e, f); }
inline int IntFlags(vm::ProgVM& vm, int e, int f) { return (int)vm.EdictFieldFloat(e, f); }
inline void SetFlags(vm::ProgVM& vm, int e, int f, int v) { vm.EdictFieldFloat(e, f) = (float)v; }

inline int Fld(vm::ProgVM& vm, const char* n) { return vm.FindField(n); }

inline float AngMod(float a) {
    a = std::fmod(a, 360.0f);
    if (a < 0) a += 360.0f;
    return a;
}

} // namespace

// ---------------------------------------------------------------------------
// SV_CheckBottom
// ---------------------------------------------------------------------------
bool SV_CheckBottom(vm::ProgVM& vm, int edict) {
    int fo = Fld(vm, "origin"), fmn = Fld(vm, "mins"), fmx = Fld(vm, "maxs");
    if (fo < 0 || fmn < 0 || fmx < 0) return true;
    float org[3], mn[3], mx[3];
    vm.EdictFieldVector(edict, fo, org);
    vm.EdictFieldVector(edict, fmn, mn);
    vm.EdictFieldVector(edict, fmx, mx);

    const GameTraceContext& g = GetGameTraceContext();
    if (!g.map) return true;

    float absmin[3], absmax[3];
    for (int i = 0; i < 3; i++) { absmin[i] = org[i] + mn[i]; absmax[i] = org[i] + mx[i]; }

    // fast path: if all 4 corners under the base are solid world, accept.
    float start[3];
    start[2] = absmin[2] - 1;
    for (int x = 0; x <= 1; x++) {
        for (int y = 0; y <= 1; y++) {
            start[0] = x ? absmax[0] : absmin[0];
            start[1] = y ? absmax[1] : absmin[1];
            if (g.map->PointContents(start) != CONTENTS_SOLID)
                goto realcheck;
        }
    }
    return true;

realcheck:
    // midpoint check
    float stop[3];
    start[2] = absmin[2];
    start[0] = stop[0] = (absmin[0] + absmax[0]) * 0.5f;
    start[1] = stop[1] = (absmin[1] + absmax[1]) * 0.5f;
    stop[2] = start[2] - 2 * STEPSIZE;
    float zero3[3] = {0, 0, 0};
    auto tr = MoveBox(*g.map, zero3, zero3, start, stop, g.ents, g.num_ents, edict);
    if (tr.fraction == 1.0f) return false;
    float mid = tr.endpos[2];
    float bottom = mid;

    for (int x = 0; x <= 1; x++) {
        for (int y = 0; y <= 1; y++) {
            start[0] = stop[0] = x ? absmax[0] : absmin[0];
            start[1] = stop[1] = y ? absmax[1] : absmin[1];
            auto c = MoveBox(*g.map, zero3, zero3, start, stop, g.ents, g.num_ents, edict);
            if (c.fraction != 1.0f && c.endpos[2] > bottom)
                bottom = c.endpos[2];
            if (c.fraction == 1.0f || mid - c.endpos[2] > STEPSIZE)
                return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// SV_FixCheckBottom
// ---------------------------------------------------------------------------
void SV_FixCheckBottom(vm::ProgVM& vm, int edict) {
    int ff = Fld(vm, "flags");
    if (ff >= 0)
        SetFlags(vm, edict, ff, IntFlags(vm, edict, ff) | FL_PARTIALGROUND);
}

// ---------------------------------------------------------------------------
// SV_Movestep  (reference: sv_move.c SV_movestep)
// ---------------------------------------------------------------------------
bool SV_Movestep(vm::ProgVM& vm, int edict, float move[3], bool relink) {
    int fo  = Fld(vm, "origin");
    int fmn = Fld(vm, "mins");
    int fmx = Fld(vm, "maxs");
    int ff  = Fld(vm, "flags");
    int fen = Fld(vm, "enemy");
    int fge = Fld(vm, "groundentity");
    if (fo < 0 || fmn < 0 || fmx < 0 || ff < 0) return false;

    const GameTraceContext& g = GetGameTraceContext();
    if (!g.map) return false;

    float oldorg[3], neworg[3];
    vm.EdictFieldVector(edict, fo, oldorg);
    for (int i = 0; i < 3; i++) neworg[i] = oldorg[i] + move[i];

    int flags = IntFlags(vm, edict, ff);

    // ---- flying/swimming monsters: direct move, optional enemy height tracking ----
    if (flags & (FL_FLY | FL_SWIM)) {
        int edict_num = vm.EdictFieldEntity(edict, fen);
        for (int i = 0; i < 2; i++) {
            for (int k = 0; k < 3; k++) neworg[k] = oldorg[k] + move[k];
            if (i == 0 && edict_num > 0 && !vm.EdictFree(edict_num)) {
                float eorg[3];
                vm.EdictFieldVector(edict_num, fo, eorg);
                float dz = oldorg[2] - eorg[2];
                if (dz > 40) neworg[2] -= 8;
                if (dz < 30) neworg[2] += 8;
            }
            float mn[3], mx[3];
            vm.EdictFieldVector(edict, fmn, mn);
            vm.EdictFieldVector(edict, fmx, mx);
            auto tr = MoveBox(*g.map, mn, mx, oldorg, neworg, g.ents, g.num_ents, edict);
            if (tr.fraction == 1.0f) {
                if ((flags & FL_SWIM) && g.map->PointContents(tr.endpos) == CONTENTS_EMPTY)
                    return false; // swimmer left water
                vm.SetEdictFieldVector(edict, fo, tr.endpos);
                if (relink) LinkEdict(vm, edict, true);
                return true;
            }
            if (edict_num <= 0 || vm.EdictFree(edict_num)) break;
        }
        return false;
    }

    // ---- walking: step up / drop down ----
    float step_end[3] = { neworg[0], neworg[1], neworg[2] + STEPSIZE };
    float drop_end[3] = { step_end[0], step_end[1], step_end[2] - 2 * STEPSIZE };
    float mn[3], mx[3];
    vm.EdictFieldVector(edict, fmn, mn);
    vm.EdictFieldVector(edict, fmx, mx);

    auto tr = MoveBox(*g.map, mn, mx, step_end, drop_end, g.ents, g.num_ents, edict);
    if (tr.allsolid) return false;

    if (tr.startsolid) {
        step_end[2] -= STEPSIZE;
        drop_end[2] = step_end[2] - 2 * STEPSIZE;
        tr = MoveBox(*g.map, mn, mx, step_end, drop_end, g.ents, g.num_ents, edict);
        if (tr.allsolid || tr.startsolid) return false;
    }

    if (tr.fraction == 1.0f) {
        // walked off an edge
        if (flags & FL_PARTIALGROUND) {
            // ground was pulled out: allow the move and drop ONGROUND
            vm.SetEdictFieldVector(edict, fo, neworg);
            SetFlags(vm, edict, ff, flags & ~FL_ONGROUND);
            if (relink) LinkEdict(vm, edict, true);
            return true;
        }
        return false;
    }

    // landed — confirm floor underneath
    vm.SetEdictFieldVector(edict, fo, tr.endpos);
    if (!SV_CheckBottom(vm, edict)) {
        if (flags & FL_PARTIALGROUND) {
            // entity had floor mostly pulled out; accept the move
            if (relink) LinkEdict(vm, edict, true);
            return true;
        }
        vm.SetEdictFieldVector(edict, fo, oldorg);
        return false;
    }

    if (flags & FL_PARTIALGROUND)
        SetFlags(vm, edict, ff, flags & ~FL_PARTIALGROUND);

    if (fge >= 0) {
        int ent_num = 0;
        // trace.ent is the edict that blocked the move (0 = world)
        // The reference stores the BSP trace ent; for simplicity, store world (0)
        // when the blocking surface is the BSP (hit_edict == 0).
        // MoveBox reports hit_edict = -1 for BSP world; 0 = world edict in QuakeC.
        ent_num = (tr.hit_edict >= 0) ? tr.hit_edict : 0;
        vm.EdictFieldInt(edict, fge) = vm.EdictNumToProg(ent_num);
    }

    if (relink) LinkEdict(vm, edict, true);
    return true;
}

// ---------------------------------------------------------------------------
// SV_StepDirection  (reference: sv_move.c SV_StepDirection)
// ---------------------------------------------------------------------------
bool SV_StepDirection(vm::ProgVM& vm, int edict, float yaw, float dist) {
    int fiy = Fld(vm, "ideal_yaw"), fa = Fld(vm, "angles");
    if (fiy >= 0) vm.EdictFieldFloat(edict, fiy) = yaw;

    // run changeyaw (same logic as PF_changeyaw, inline)
    if (fa >= 0) {
        float current = AngMod(vm.EdictFieldFloat(edict, fa + 1));
        int fis = Fld(vm, "yaw_speed");
        float speed = (fis >= 0) ? vm.EdictFieldFloat(edict, fis) : 60.0f;
        if (!speed) speed = 60.0f;
        float ideal = (fiy >= 0) ? vm.EdictFieldFloat(edict, fiy) : yaw;
        float move = ideal - current;
        if (move > 180) move -= 360;
        else if (move < -180) move += 360;
        if (move > 0) move = std::min(move, speed);
        else move = std::max(move, -speed);
        vm.EdictFieldFloat(edict, fa + 1) = AngMod(current + move);
    }

    float rad = yaw * PI / 180.0f;
    float mv[3] = { std::cos(rad) * dist, std::sin(rad) * dist, 0 };

    float oldorg[3];
    vm.EdictFieldVector(edict, Fld(vm, "origin"), oldorg);

    bool ok = SV_Movestep(vm, edict, mv, false);
    if (ok) {
        float new_yaw = AngMod(vm.EdictFieldFloat(edict, Fld(vm, "angles") + 1));
        float ideal_yaw = vm.EdictFieldFloat(edict, Fld(vm, "ideal_yaw"));
        float delta = new_yaw - ideal_yaw;
        if (delta > 180) delta -= 360;
        else if (delta < -180) delta += 360;
        if (delta > 45 && delta < 315) {
            // turned too far — reject the step
            vm.SetEdictFieldVector(edict, Fld(vm, "origin"), oldorg);
        }
    }
    LinkEdict(vm, edict, true);
    return ok;
}

// ---------------------------------------------------------------------------
// SV_NewChaseDir  (reference: sv_move.c SV_NewChaseDir)
// ---------------------------------------------------------------------------
void SV_NewChaseDir(vm::ProgVM& vm, int actor, int enemy, float dist) {
    int fo  = Fld(vm, "origin");
    int fiy = Fld(vm, "ideal_yaw");
    int fa  = Fld(vm, "angles");

    float aorg[3], eorg[3];
    vm.EdictFieldVector(actor, fo, aorg);
    vm.EdictFieldVector(enemy, fo, eorg);
    float deltax = eorg[0] - aorg[0];
    float deltay = eorg[1] - aorg[1];

    float d[3];
    d[1] = (deltax >  10) ? 0 :
           (deltax < -10) ? 180 : DI_NODIR;
    d[2] = (deltay < -10) ? 270 :
           (deltay >  10) ? 90  : DI_NODIR;

    float olddir = AngMod(std::round(vm.EdictFieldFloat(actor, fa + 1) / 45.0f) * 45.0f);
    float turnaround = AngMod(olddir - 180);

    auto trydir = [&](float tdir) -> bool {
        if (tdir == DI_NODIR) return false;
        if (tdir == turnaround) return false;
        return SV_StepDirection(vm, actor, tdir, dist);
    };

    // try direct route
    if (d[1] != DI_NODIR && d[2] != DI_NODIR) {
        float tdir = (d[1] == 0) ? (d[2] == 90 ? 45 : 315)
                                  : (d[2] == 90 ? 135 : 215);
        if (trydir(tdir)) return;
    }

    // try other directions (random tie-break)
    if (((std::rand() & 3) & 1) || std::abs(deltay) > std::abs(deltax)) {
        std::swap(d[1], d[2]);
    }

    if (trydir(d[1])) return;
    if (trydir(d[2])) return;
    if (trydir(olddir)) return;

    // sweep 45-degree steps
    float step = (std::rand() & 1) ? 45 : -45;
    float tdir = (step > 0) ? 0 : 315;
    for (int i = 0; i < 8; i++, tdir += step) {
        if (trydir(tdir)) return;
    }
    if (trydir(turnaround)) return;

    // couldn't move — set ideal_yaw back and check ground
    if (fiy >= 0) vm.EdictFieldFloat(actor, fiy) = olddir;
    if (!SV_CheckBottom(vm, actor))
        SV_FixCheckBottom(vm, actor);
}

// ---------------------------------------------------------------------------
// SV_CloseEnough  (reference: sv_move.c SV_CloseEnough)
// ---------------------------------------------------------------------------
bool SV_CloseEnough(vm::ProgVM& vm, int ent, int goal, float dist) {
    int fa_min = Fld(vm, "absmin"), fa_max = Fld(vm, "absmax");
    if (fa_min < 0 || fa_max < 0) return false;
    for (int i = 0; i < 3; i++) {
        if (vm.EdictFieldFloat(goal, fa_min + i) > vm.EdictFieldFloat(ent, fa_max + i) + dist) return false;
        if (vm.EdictFieldFloat(goal, fa_max + i) < vm.EdictFieldFloat(ent, fa_min + i) - dist) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// SV_MoveToGoal  (reference: sv_move.c SV_MoveToGoal, builtin #67)
// ---------------------------------------------------------------------------
void SV_MoveToGoal(vm::ProgVM& vm) {
    int self_ent = 0;
    int g_self = vm.FindGlobal("self");
    if (g_self >= 0) self_ent = vm.ProgToEdictNum(vm.Global(g_self));
    if (self_ent <= 0) { vm.SetReturnFloat(0); return; }

    int fg  = Fld(vm, "goalentity");
    int ff  = Fld(vm, "flags");
    int fen = Fld(vm, "enemy");

    int goal = (fg >= 0) ? vm.EdictFieldEntity(self_ent, fg) : 0;
    if (goal <= 0) { vm.SetReturnFloat(0); return; }

    float dist = vm.ParmFloat(0);

    int flags = (ff >= 0) ? IntFlags(vm, self_ent, ff) : 0;
    if (!(flags & (FL_ONGROUND | FL_FLY | FL_SWIM))) {
        vm.SetReturnFloat(0);
        return;
    }

    // if close enough to the enemy (not the goal), stop
    int enemy = (fen >= 0) ? vm.EdictFieldEntity(self_ent, fen) : 0;
    if (enemy > 0 && !vm.EdictFree(enemy) && SV_CloseEnough(vm, self_ent, enemy, dist)) {
        vm.SetReturnFloat(0);
        return;
    }

    // try current direction, else pick a new one
    if ((std::rand() & 3) == 1 ||
        !SV_StepDirection(vm, self_ent,
                          (Fld(vm, "ideal_yaw") >= 0) ? vm.EdictFieldFloat(self_ent, Fld(vm, "ideal_yaw")) : 0,
                          dist))
    {
        SV_NewChaseDir(vm, self_ent, goal, dist);
    }
    vm.SetReturnFloat(1);
}

} // namespace zq::engine
