#include "engine/gameframe.hpp"
#include "engine/particle.hpp"
#include "core/logging/logger.hpp"
#include <cmath>
#include <cstring>

namespace zq::engine {
namespace {
// int-valued progs fields are stored as the plain float value (solid=4.0,
// movetype=7.0, modelindex=1.0, flags=512.0); read through the float.
inline int IntField(vm::ProgVM& vm, int e, int f) { return (int)vm.EdictFieldFloat(e, f); }
inline int IntFlags(vm::ProgVM& vm, int e, int f) { return (int)vm.EdictFieldFloat(e, f); }
inline void SetFlags(vm::ProgVM& vm, int e, int f, int v) { vm.EdictFieldFloat(e, f) = (float)v; }

// Lazily cached edict field offsets.
struct Fields {
    int classname = -1, origin = -1, angles = -1, velocity = -1, avelocity = -1;
    int mins = -1, maxs = -1, modelindex = -1, solid = -1, movetype = -1;
    int think = -1, nextthink = -1, touch = -1, flags = -1, groundentity = -1;
    int waterlevel = -1, watertype = -1;
    int v_angle = -1, model = -1, frame = -1, ltime = -1;
    int gravity = -1, blocked = -1, owner = -1, view_ofs = -1;
    int oldorigin = -1, punchangle = -1;
    int absmin = -1, absmax = -1;
    int button0 = -1, button2 = -1, button3 = -1;
    int forwardmove = -1, sidemove = -1, upmove = -1;
    int impulse = -1;
    int effects = -1;
    bool cached = false;
    void Populate(vm::ProgVM& vm) {
        if (cached) return;
        classname = vm.FindField("classname");
        origin = vm.FindField("origin");
        angles = vm.FindField("angles");
        velocity = vm.FindField("velocity");
        avelocity = vm.FindField("avelocity");
        mins = vm.FindField("mins");
        maxs = vm.FindField("maxs");
        modelindex = vm.FindField("modelindex");
        solid = vm.FindField("solid");
        movetype = vm.FindField("movetype");
        think = vm.FindField("think");
        nextthink = vm.FindField("nextthink");
        touch = vm.FindField("touch");
        flags = vm.FindField("flags");
        groundentity = vm.FindField("groundentity");
        waterlevel = vm.FindField("waterlevel");
        watertype = vm.FindField("watertype");
        v_angle = vm.FindField("v_angle");
        model = vm.FindField("model");
        frame = vm.FindField("frame");
        ltime = vm.FindField("ltime");
        gravity = vm.FindField("gravity");
        blocked = vm.FindField("blocked");
        owner = vm.FindField("owner");
        view_ofs = vm.FindField("view_ofs");
        oldorigin = vm.FindField("oldorigin");
        punchangle = vm.FindField("punchangle");
        absmin = vm.FindField("absmin");
        absmax = vm.FindField("absmax");
        button0 = vm.FindField("button0");
        button2 = vm.FindField("button2");
        button3 = vm.FindField("button3");
        forwardmove = vm.FindField("forwardmove");
        sidemove = vm.FindField("sidemove");
        upmove = vm.FindField("upmove");
        impulse = vm.FindField("impulse");
        effects = vm.FindField("effects");
        cached = true;
    }
};
Fields g_fields;

// ClipVelocity (reference): slide `in` off `normal` into `out`.
void ClipVelocity(const float in[3], const float normal[3], float out[3],
                  float overbounce) {
    constexpr float STOP_EPSILON = 0.1f;
    float backoff = (in[0]*normal[0] + in[1]*normal[1] + in[2]*normal[2]) * overbounce;
    for (int i = 0; i < 3; i++) {
        out[i] = in[i] - normal[i] * backoff;
        if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON) out[i] = 0;
    }
}

// Reference SV_AddGravity: velocity[2] -= ent_gravity * sv_gravity * dt,
// where ent_gravity comes from the optional per-entity "gravity" field
// (defaults to 1.0). WinQuake sv_gravity = 800.
float EntityGravity(vm::ProgVM& vm, const Fields& f, int e) {
    if (f.gravity >= 0) {
        float g = vm.EdictFieldFloat(e, f.gravity);
        if (g != 0.0f) return g;
    }
    return 1.0f;
}

void AddGravityEnt(vm::ProgVM& vm, const Fields& f, int e, float dt) {
    float vel[3];
    vm.EdictFieldVector(e, f.velocity, vel);
    vel[2] -= EntityGravity(vm, f, e) * 800.0f * dt;
    vm.SetEdictFieldVector(e, f.velocity, vel);
}

// Reference SV_Impact: two entities have touched, so run their touch
// functions (self=e1,other=e2, then self=e2,other=e1), preserving self/other
// so the touch never corrupts the entity globals for later thinks.
void RunImpact(vm::ProgVM& vm, const Fields& f, int e1, int e2) {
    int old_self = vm.SelfEdict();
    int old_other = vm.OtherEdict();
    auto run = [&](int who, int other) {
        if (who < 0 || who >= 1024 || vm.EdictFree(who)) return;
        int touch = vm.EdictFieldInt(who, f.touch);
        int solid = IntField(vm, who, f.solid);
        if (!touch || solid == SOLID_NOT) return;
        vm.SetTime(vm.Time());
        vm.SetSelfEdict(who);
        vm.SetOtherEdict(other);
        vm.ExecuteProgram(touch);
    };
    run(e1, e2);
    if (!vm.EdictFree(e1)) run(e2, e1);
    vm.SetSelfEdict(old_self);
    vm.SetOtherEdict(old_other);
}

// Simple multi-plane fly move for physics entities (SV_FlyMove lite).
int FlyMoveEnt(vm::ProgVM& vm, const BSPMap& map, const Fields& f, int e,
               float time, const SolidEntity* ents, int num_ents, int ignore) {
    float origin[3], vel[3], mins[3], maxs[3];
    vm.EdictFieldVector(e, f.origin, origin);
    vm.EdictFieldVector(e, f.velocity, vel);
    vm.EdictFieldVector(e, f.mins, mins);
    vm.EdictFieldVector(e, f.maxs, maxs);

    float planes[5][3];
    int numplanes = 0;
    float primal[3] = { vel[0], vel[1], vel[2] };
    float time_left = time;
    int blocked = 0;

    for (int bump = 0; bump < 4; bump++) {
        if (!vel[0] && !vel[1] && !vel[2]) break;   // reference early-out
        float end[3];
        for (int i = 0; i < 3; i++) end[i] = origin[i] + time_left * vel[i];
        auto tr = MoveBox(map, mins, maxs, origin, end, ents, num_ents, ignore);
        if (tr.allsolid) {
            { float z[3]={0,0,0}; vm.SetEdictFieldVector(e, f.velocity, z); }
            return 3;
        }
        if (tr.fraction > 0) {
            vm.SetEdictFieldVector(e, f.origin, tr.endpos);
            vm.EdictFieldVector(e, f.origin, origin);
            numplanes = 0;
        }
        if (tr.fraction == 1) break;

        if (tr.plane_normal[2] > 0.7f) {
            blocked |= 1;
            if (tr.hit_brush) {
                SetFlags(vm, e, f.flags, IntFlags(vm, e, f.flags) | FL_ONGROUND);
                // groundentity = the actual floor entity (reference), not world.
                vm.EdictFieldInt(e, f.groundentity) = vm.EdictNumToProg(tr.hit_edict);
            }
        }
        if (!tr.plane_normal[2]) blocked |= 2;

        // Run impact between the mover and whatever it hit (world has no touch,
        // so hitting a wall is a no-op; hitting an entity runs touches).
        RunImpact(vm, f, e, tr.hit_edict);
        if (vm.EdictFree(e)) break;

        time_left -= time_left * tr.fraction;
        if (numplanes >= 5) { { float z[3]={0,0,0}; vm.SetEdictFieldVector(e, f.velocity, z); } return 3; }
        for (int i = 0; i < 3; i++) planes[numplanes][i] = tr.plane_normal[i];
        numplanes++;

        float nv[3] = {0, 0, 0};
        int i;
        for (i = 0; i < numplanes; i++) {
            ClipVelocity(vel, planes[i], nv, 1.0f);
            int j;
            for (j = 0; j < numplanes; j++)
                if (j != i && (nv[0]*planes[j][0]+nv[1]*planes[j][1]+nv[2]*planes[j][2]) < 0)
                    break;
            if (j == numplanes) break;
        }
        if (i != numplanes) {
            vel[0] = nv[0]; vel[1] = nv[1]; vel[2] = nv[2];
        } else {
            if (numplanes != 2) { vel[0] = vel[1] = vel[2] = 0; return 7; }
            float dir[3];
            dir[0] = planes[0][1]*planes[1][2] - planes[0][2]*planes[1][1];
            dir[1] = planes[0][2]*planes[1][0] - planes[0][0]*planes[1][2];
            dir[2] = planes[0][0]*planes[1][1] - planes[0][1]*planes[1][0];
            float d = vel[0]*dir[0] + vel[1]*dir[1] + vel[2]*dir[2];
            vel[0] = dir[0]*d; vel[1] = dir[1]*d; vel[2] = dir[2]*d;
        }
        vm.SetEdictFieldVector(e, f.velocity, vel);
        if (vel[0]*primal[0] + vel[1]*primal[1] + vel[2]*primal[2] <= 0) {
            { float z[3]={0,0,0}; vm.SetEdictFieldVector(e, f.velocity, z); }
            return blocked;
        }
    }
    vm.SetEdictFieldVector(e, f.velocity, vel);
    vm.EdictFieldVector(e, f.velocity, vel);
    return blocked;
}

// Reference SV_PushEntity: advance origin by `push` (clipped against world +
// solid entities, skipping `ignore`), return the trace. Velocity unchanged.
BSPMap::TraceResult PushEntityEnt(vm::ProgVM& vm, const BSPMap& map, const Fields& f,
                                  int e, const float push[3],
                                  const SolidEntity* ents, int num_ents,
                                  int ignore) {
    float origin[3], mins[3], maxs[3];
    vm.EdictFieldVector(e, f.origin, origin);
    vm.EdictFieldVector(e, f.mins, mins);
    vm.EdictFieldVector(e, f.maxs, maxs);
    float end[3];
    for (int i = 0; i < 3; i++) end[i] = origin[i] + push[i];
    auto tr = MoveBox(map, mins, maxs, origin, end, ents, num_ents, ignore);
    vm.SetEdictFieldVector(e, f.origin, tr.endpos);
    return tr;
}

// Reference SV_TestEntityPosition: zero-length trace at the entity box's own
// origin. True if the box starts inside solid (world + SOLID_BSP entities).
bool TestInPlace(vm::ProgVM& vm, const BSPMap& map, const Fields& f, int e,
                 const SolidEntity* ents, int num_ents) {
    float origin[3], mins[3], maxs[3];
    vm.EdictFieldVector(e, f.origin, origin);
    vm.EdictFieldVector(e, f.mins, mins);
    vm.EdictFieldVector(e, f.maxs, maxs);
    auto tr = MoveBox(map, mins, maxs, origin, origin, ents, num_ents, -1);
    return tr.startsolid;
}

// SV_Physics_Toss for MOVETYPE_TOSS/BOUNCE/FLY/FLYMISSILE items & missiles.
void PhysicsToss(vm::ProgVM& vm, const BSPMap& map, const Fields& f, int e,
                 float dt, const SolidEntity* ents, int num_ents, int ignore) {
    if (IntFlags(vm, e, f.flags) & FL_ONGROUND) return;

    float vel[3], origin[3], avel[3], angles[3], mins[3], maxs[3];
    vm.EdictFieldVector(e, f.velocity, vel);
    vm.EdictFieldVector(e, f.origin, origin);
    vm.EdictFieldVector(e, f.avelocity, avel);
    vm.EdictFieldVector(e, f.angles, angles);
    vm.EdictFieldVector(e, f.mins, mins);
    vm.EdictFieldVector(e, f.maxs, maxs);

    // Reference SV_CheckVelocity: coerce each component into [-2000, 2000].
    for (int i = 0; i < 3; i++) {
        if (vel[i] > 2000.0f) vel[i] = 2000.0f;
        else if (vel[i] < -2000.0f) vel[i] = -2000.0f;
    }
    vm.SetEdictFieldVector(e, f.velocity, vel);

    int movetype = IntField(vm, e, f.movetype);
    if (movetype != MOVE_FLY && movetype != MOVE_FLYMISSILE)
        AddGravityEnt(vm, f, e, dt);
    for (int i = 0; i < 3; i++) {
        angles[i] += avel[i] * dt;
    }
    vm.SetEdictFieldVector(e, f.angles, angles);

    float move[3];
    for (int i = 0; i < 3; i++) move[i] = vel[i] * dt;
    float end[3];
    for (int i = 0; i < 3; i++) end[i] = origin[i] + move[i];
    auto tr = MoveBox(map, mins, maxs, origin, end, ents, num_ents, ignore);
    vm.SetEdictFieldVector(e, f.origin, tr.endpos);
    if (tr.fraction == 1) return;

    // Reference SV_Impact: fire both entities' touch (rockets -> RocketTouch,
    // nails -> NailTouch, grenades -> GrenadeTouch, so ranged damage works).
    RunImpact(vm, f, e, tr.hit_edict);
    if (vm.EdictFree(e)) return;

    float backoff = (movetype == MOVE_BOUNCE) ? 1.5f : 1.0f;
    ClipVelocity(vel, tr.plane_normal, vel, backoff);
    vm.SetEdictFieldVector(e, f.velocity, vel);

    if (tr.plane_normal[2] > 0.7f) {
        if (vel[2] < 60 || movetype != MOVE_BOUNCE) {
            SetFlags(vm, e, f.flags, IntFlags(vm, e, f.flags) | FL_ONGROUND);
            vm.EdictFieldInt(e, f.groundentity) = vm.EdictNumToProg(0);
            { float z[3]={0,0,0}; vm.SetEdictFieldVector(e, f.velocity, z); }
            { float z[3]={0,0,0}; vm.SetEdictFieldVector(e, f.avelocity, z); }
        }
    }
}

// Reference SV_PushMove for MOVETYPE_PUSH brushes (doors, plats): a pusher is
// never blocked by world geometry (it always advances into its built-in
// pocket). Solid box entities that start inside its final box - or stand on it
// - are pushed along; if any pushed rider is still embedded afterwards, the
// whole move is reverted and the pusher's "blocked" function (if any) runs.
void PhysicsPush(vm::ProgVM& vm, const BSPMap& map, const Fields& f, int e,
                 float dt, const SolidEntity* ents, int num_ents,
                 std::vector<int>* moved_riders = nullptr) {
    float vel[3];
    vm.EdictFieldVector(e, f.velocity, vel);
    if (!vel[0] && !vel[1] && !vel[2]) return;

    float origin[3], mn[3], mx[3];
    vm.EdictFieldVector(e, f.origin, origin);
    vm.EdictFieldVector(e, f.mins, mn);
    vm.EdictFieldVector(e, f.maxs, mx);
    float move[3] = { vel[0] * dt, vel[1] * dt, vel[2] * dt };
    float end[3] = { origin[0] + move[0], origin[1] + move[1], origin[2] + move[2] };
    float pushorig[3] = { origin[0], origin[1], origin[2] };

    // Move the pusher to its final position (reference: always advances).
    vm.SetEdictFieldVector(e, f.origin, end);

    // Solid list with the pusher at its final position, so rider blocking
    // tests include the freshly located brush (SV_LinkEdict parity).
    static thread_local std::vector<SolidEntity> fresh;
    BuildSolidList(vm, map, fresh, -1);
    const SolidEntity* fents = fresh.data();
    int fnum = (int)fresh.size();

    struct Moved { int edict; float from[3]; };
    Moved moved[1024];
    int num_moved = 0;
    int block = -1;

    for (int j = 0; j < num_ents; j++) {
        const SolidEntity& se = ents[j];
        if (se.edict == e) continue;
        if (se.kind != SolidEntity::Kind::Box) continue;
        int mt = IntField(vm, se.edict, f.movetype);
        if (mt == MOVE_PUSH || mt == MOVE_NONE || mt == MOVE_NOCLIP) continue;

        // standing on the pusher -> definitely moved
        bool standing = false;
        if (f.groundentity >= 0 &&
            (IntFlags(vm, se.edict, f.flags) & FL_ONGROUND)) {
            int g = vm.EdictFieldEntity(se.edict, f.groundentity);
            if (g == e) standing = true;
        }
        if (!standing) {
            // must overlap the pusher's final box...
            float eorg[3];
            vm.EdictFieldVector(se.edict, f.origin, eorg);
            if (eorg[0] + se.mins[0] >= end[0] + mx[0] ||
                eorg[0] + se.maxs[0] <= end[0] + mn[0] ||
                eorg[1] + se.mins[1] >= end[1] + mx[1] ||
                eorg[1] + se.maxs[1] <= end[1] + mn[1] ||
                eorg[2] + se.mins[2] >= end[2] + mx[2] ||
                eorg[2] + se.maxs[2] <= end[2] + mn[2])
                continue;
            // ...and already be embedded in that solid
            if (!TestInPlace(vm, map, f, se.edict, fents, fnum)) continue;
        }

        // remove the onground flag for non-players
        if (mt != MOVE_WALK)
            SetFlags(vm, se.edict, f.flags,
                     IntFlags(vm, se.edict, f.flags) & ~FL_ONGROUND);

        float from[3];
        vm.EdictFieldVector(se.edict, f.origin, from);
        moved[num_moved].edict = se.edict;
        moved[num_moved].from[0] = from[0];
        moved[num_moved].from[1] = from[1];
        moved[num_moved].from[2] = from[2];
        num_moved++;

        // try moving the contacted entity (pusher ignored during the push,
        // matching `pusher->v.solid = SOLID_NOT` around SV_PushEntity)
        PushEntityEnt(vm, map, f, se.edict, move, fents, fnum, e);

        // if it is still inside the pusher, block
        if (TestInPlace(vm, map, f, se.edict, fents, fnum)) {
            if (se.mins[0] == se.maxs[0]) continue;          // zero-size
            int sol = IntField(vm, se.edict, f.solid);
            if (sol == SOLID_NOT || sol == SOLID_TRIGGER) {  // corpse
                vm.EdictFieldFloat(se.edict, f.mins) = 0.0f;
                vm.EdictFieldFloat(se.edict, f.mins + 1) = 0.0f;
                float z[3] = {0, 0, 0};
                vm.SetEdictFieldVector(se.edict, f.maxs, z);
                continue;
            }
            // fail the move: remember the blocker, revert below.
            block = se.edict;
            break;
        }
        if (moved_riders) moved_riders->push_back(se.edict);
    }

    if (block >= 0) {
        if (moved_riders) moved_riders->clear();
        // Revert the pusher...
        vm.SetEdictFieldVector(e, f.origin, pushorig);
        if (f.ltime >= 0) vm.EdictFieldFloat(e, f.ltime) -= dt;

        // ...and every rider that was pushed (the blocker was the last push).
        for (int i = 0; i < num_moved; i++) {
            vm.SetEdictFieldVector(moved[i].edict, f.origin, moved[i].from);
        }

        // if the pusher has a "blocked" function, call it
        if (f.blocked >= 0 && vm.EdictFieldInt(e, f.blocked)) {
            int blocker = block;
            int old_self = vm.SelfEdict();
            int old_other = vm.OtherEdict();
            vm.SetSelfEdict(e);
            vm.SetOtherEdict(blocker);
            vm.ExecuteProgram(vm.EdictFieldInt(e, f.blocked));
            vm.SetSelfEdict(old_self);
            vm.SetOtherEdict(old_other);
        }
    }
}

} // namespace

GameTraceContext g_trace_ctx;
EntitySoundHook g_ent_sound_hook = nullptr;

void SetEntitySoundHook(EntitySoundHook hook) { g_ent_sound_hook = hook; }
EntitySoundHook GetEntitySoundHook() { return g_ent_sound_hook; }

// SV_CheckWaterTransition (reference sv_phys.c:1198). Samples the point
// contents at the entity's origin and plays misc/h2ohit1.wav when
// watertype crosses the empty<->water boundary.
void CheckWaterTransition(vm::ProgVM& vm, const BSPMap& map, const Fields& f, int e) {
    if (f.waterlevel < 0 || f.watertype < 0) return;
    int fo = vm.FindField("origin");
    float org[3];
    vm.EdictFieldVector(e, fo, org);
    int cont = map.PointContents(org);
    int wt   = (int)vm.EdictFieldFloat(e, f.watertype);
    if (!wt) {
        // just spawned — initialise
        vm.EdictFieldFloat(e, f.watertype) = (float)cont;
        vm.EdictFieldFloat(e, f.waterlevel) = 1.0f;
        return;
    }
    if (cont <= CONTENTS_WATER) {
        if (wt == CONTENTS_EMPTY && g_ent_sound_hook)
            g_ent_sound_hook(e, "misc/h2ohit1.wav");
        vm.EdictFieldFloat(e, f.watertype) = (float)cont;
        vm.EdictFieldFloat(e, f.waterlevel) = 1.0f;
    } else {
        if (wt != CONTENTS_EMPTY && g_ent_sound_hook)
            g_ent_sound_hook(e, "misc/h2ohit1.wav");
        vm.EdictFieldFloat(e, f.watertype) = (float)CONTENTS_EMPTY;
        vm.EdictFieldFloat(e, f.waterlevel) = (float)cont;
    }
}

AreaNodes g_area_nodes;

void InitAreaNodes(const BSPMap& map) {
    const BSPModel* wm = map.WorldModel();
    if (!wm) return;
    g_area_nodes.Init(wm->mins, wm->maxs);
}

// Reference SV_LinkEdict: compute the absolute box (with the FL_ITEM 15-unit
// pickup expansion and the 1-unit clip epsilon), write absmin/absmax, drop the
// entity into the area-node tree, and optionally run SV_TouchLinks.
void LinkEdict(vm::ProgVM& vm, int edict, bool touch_triggers) {
    if (edict <= 0 || vm.EdictFree(edict)) return;
    g_fields.Populate(vm);
    const Fields& f = g_fields;

    float o[3], mn[3], mx[3];
    vm.EdictFieldVector(edict, f.origin, o);
    vm.EdictFieldVector(edict, f.mins, mn);
    vm.EdictFieldVector(edict, f.maxs, mx);

    float amin[3], amax[3];
    for (int i = 0; i < 3; i++) {
        amin[i] = o[i] + mn[i];
        amax[i] = o[i] + mx[i];
    }

    int flags = IntFlags(vm, edict, f.flags);
    if (flags & FL_ITEM) {
        amin[0] -= 15; amin[1] -= 15;
        amax[0] += 15; amax[1] += 15;
    } else {
        amin[0] -= 1; amin[1] -= 1; amin[2] -= 1;
        amax[0] += 1; amax[1] += 1; amax[2] += 1;
    }

    if (f.absmin >= 0) vm.SetEdictFieldVector(edict, f.absmin, amin);
    if (f.absmax >= 0) vm.SetEdictFieldVector(edict, f.absmax, amax);

    int solid = IntField(vm, edict, f.solid);
    if (solid == SOLID_NOT) return;   // never linked into the tree
    g_area_nodes.Unlink(edict);
    g_area_nodes.Link(edict, amin, amax, solid == SOLID_TRIGGER);

    if (touch_triggers) {
        int old_self = vm.SelfEdict();
        int old_other = vm.OtherEdict();
        g_area_nodes.ForEachTrigger(amin, amax, [&](int touch) {
            if (touch == edict || vm.EdictFree(touch)) return;
            int func = vm.EdictFieldInt(touch, f.touch);
            if (!func) return;
            if (IntField(vm, touch, f.solid) != SOLID_TRIGGER) return;
            // Reference SV_TouchLinks: fire only on an exact box overlap
            // (the visited node candidates are put through an abs-box check
            // before the touch runs).
            float tamin[3], tamax[3];
            if (f.absmin >= 0 && f.absmax >= 0) {
                vm.EdictFieldVector(touch, f.absmin, tamin);
                vm.EdictFieldVector(touch, f.absmax, tamax);
            } else {
                float to[3], tmn[3], tmx[3];
                vm.EdictFieldVector(touch, f.origin, to);
                vm.EdictFieldVector(touch, f.mins, tmn);
                vm.EdictFieldVector(touch, f.maxs, tmx);
                for (int k = 0; k < 3; k++) {
                    tamin[k] = to[k] + tmn[k];
                    tamax[k] = to[k] + tmx[k];
                }
            }
            if (!AreaNodes::BoxOverlap(amin, amax, tamin, tamax)) return;
            vm.SetSelfEdict(touch);
            vm.SetOtherEdict(edict);
            vm.ExecuteProgram(func);
        });
        vm.SetSelfEdict(old_self);
        vm.SetOtherEdict(old_other);
    }
}

void SetGameTraceContext(const BSPMap* map, const SolidEntity* ents,
                         int num_ents, int client_edict) {
    g_trace_ctx.map = map;
    g_trace_ctx.ents = ents;
    g_trace_ctx.num_ents = num_ents;
    g_trace_ctx.client_edict = client_edict;
}

const GameTraceContext& GetGameTraceContext() { return g_trace_ctx; }

void BuildSolidList(vm::ProgVM& vm, const BSPMap& map, std::vector<SolidEntity>& out,
                    int ignore_edict) {
    g_fields.Populate(vm);
    const Fields& f = g_fields;
    out.clear();
    for (int i = 1; i < 1024; i++) {
        if (vm.EdictFree(i)) continue;
        if (i == ignore_edict) continue;
        int solid = IntField(vm, i, f.solid);
        if (solid == SOLID_NOT || solid == SOLID_TRIGGER) continue;
        int movetype = IntField(vm, i, f.movetype);
        if (movetype == MOVE_NONE || movetype == MOVE_NOCLIP) continue;

        SolidEntity se;
        se.edict = i;
        if (solid == SOLID_BSP) {
            se.kind = SolidEntity::Kind::Brush;
            se.model_index = IntField(vm, i, f.modelindex);
            if (se.model_index < 0 || se.model_index >= (int)map.Models().size())
                continue; // invalid submodel -> not blocking
            vm.EdictFieldVector(i, f.origin, se.origin);
        } else {
            se.kind = SolidEntity::Kind::Box;
            vm.EdictFieldVector(i, f.mins, se.mins);
            vm.EdictFieldVector(i, f.maxs, se.maxs);
            vm.EdictFieldVector(i, f.origin, se.origin);
        }
        out.push_back(se);
    }
}

// QW-style SV_RunThink (sv_phys.c): runs the think program while nextthink is
// inside the current frame window (clamped to sv.time so thinks never run in
// the past), looping so a think that re-arms nextthink runs again this frame.
// Returns false if the entity freed itself or the program failed to run.
bool RunThinkLoop(vm::ProgVM& vm, const Fields& f, int e, float time, float dt) {
    do {
        int think = vm.EdictFieldInt(e, f.think);
        if (think < 0) think = 0;
        float next = vm.EdictFieldFloat(e, f.nextthink);
        if (think <= 0 || next <= 0 || next > time + dt) return true;
        float t = next < time ? time : next;
        vm.SetTime(t);
        vm.SetSelfEdict(e);
        vm.SetOtherEdict(0);
        vm.EdictFieldFloat(e, f.nextthink) = 0;
        if (f.ltime >= 0) {
            float nlt = t - vm.EdictFieldFloat(e, f.ltime);
            vm.EdictFieldFloat(e, f.ltime) = t;
            vm.SetFrametime(nlt);
        }
        if (!vm.ExecuteProgram(think)) return false;
        if (vm.EdictFree(e)) return false;
    } while (1);
    return true;
}

// WinQuake SV_Physics_Client: writes the usercmd into the entity, runs
// PlayerPreThink, does the walk/noclip/fly move, re-links with touch so items
// and triggers fire against the moving player (this is how Quake picks up
// weapons/ammo), then PlayerPostThink. Clients are the lowest-numbered
// non-world edicts, so they move first each frame, exactly like SV_Physics.
void RunClientPhysics(vm::ProgVM& vm, const BSPMap& map, const Fields& f, int e,
                      float dt, const SolidEntity* ents, int num_ents,
                      ClientPhysics& c, float time) {
    // Reference SV_Physics clears EF_MUZZLEFLASH (bit 1) at the end of the
    // entity's frame so a muzzle flash registers for exactly one frame. We
    // consume the residue at the start of the next client frame; the app-side
    // per-tick read runs after RunGameFrame, catching the flag set by this
    // frame's weapon think.
    if (f.effects >= 0) {
        int eff = (int)vm.EdictFieldFloat(e, f.effects);
        vm.EdictFieldFloat(e, f.effects) = (float)(eff & ~2);
    }
    // usercmd -> entity fields (QuakeC PlayerPreThink reads these)
    if (f.button0 >= 0) vm.EdictFieldFloat(e, f.button0) = c.button0 ? 1.f : 0.f;
    if (f.button2 >= 0) vm.EdictFieldFloat(e, f.button2) = c.button2 ? 1.f : 0.f;
    if (f.button3 >= 0) vm.EdictFieldFloat(e, f.button3) = c.button3 ? 1.f : 0.f;
    if (f.forwardmove >= 0) vm.EdictFieldFloat(e, f.forwardmove) = c.forwardmove;
    if (f.sidemove >= 0) vm.EdictFieldFloat(e, f.sidemove) = c.sidemove;
    if (f.upmove >= 0) vm.EdictFieldFloat(e, f.upmove) = c.upmove;
    if (f.impulse >= 0) vm.EdictFieldFloat(e, f.impulse) = (float)c.impulse;

    // Capture the client state BEFORE PlayerPreThink: the SP progs clears the
    // flags mid-frame (including FL_ONGROUND), and the walk physics below needs
    // the grounded state left by last frame to pick ground-friction vs air
    // control (reference server gets this from the client-side pmove onground).
    int movetype = IntField(vm, e, f.movetype);
    int water_before = IntField(vm, e, f.waterlevel);
    bool onground = (IntFlags(vm, e, f.flags) & FL_ONGROUND) != 0;

    int pre = vm.FunctionIndex("PlayerPreThink");
    if (pre > 0) {
        vm.SetSelfEdict(e);
        vm.SetOtherEdict(0);
        vm.ExecuteProgram(pre);
    }

    // SV_CheckVelocity
    float vel[3];
    vm.EdictFieldVector(e, f.velocity, vel);
    for (int d = 0; d < 3; d++) {
        if (vel[d] > 2000.0f) vel[d] = 2000.0f;
        else if (vel[d] < -2000.0f) vel[d] = -2000.0f;
    }
    vm.SetEdictFieldVector(e, f.velocity, vel);

    switch (movetype) {
    case MOVE_NONE:
        RunThinkLoop(vm, f, e, time, dt);
        break;
    case MOVE_WALK: {
        if (!RunThinkLoop(vm, f, e, time, dt)) break;
        // SV_CheckWater + SV_AddGravity + SV_CheckStuck + SV_WalkMove via the
        // engine's RunPlayerMove (faithful, tested engine-side walk physics).
        PlayerPhys ph;
        vm.EdictFieldVector(e, f.origin, ph.origin);
        vm.EdictFieldVector(e, f.velocity, ph.velocity);
        if (f.v_angle >= 0) vm.EdictFieldVector(e, f.v_angle, ph.angles);
        else if (f.angles >= 0) vm.EdictFieldVector(e, f.angles, ph.angles);
        ph.onground = onground;   // grounded state captured before PlayerPreThink
        ph.waterlevel = water_before;

        PlayerCmd cmd;
        cmd.forwardmove = c.forwardmove;
        cmd.sidemove = c.sidemove;
        cmd.upmove = c.upmove;
        cmd.jump = c.jump;
        RunPlayerMove(map, ph, cmd, dt, c.move_vars, ents, num_ents, e);

        vm.SetEdictFieldVector(e, f.origin, ph.origin);
        vm.SetEdictFieldVector(e, f.velocity, ph.velocity);
        if (f.waterlevel >= 0) vm.EdictFieldFloat(e, f.waterlevel) = (float)ph.waterlevel;
        if (f.watertype >= 0) vm.EdictFieldFloat(e, f.watertype) = (float)ph.watertype;
        onground = ph.onground;
        break;
    }
    case MOVE_NOCLIP: {
        if (!RunThinkLoop(vm, f, e, time, dt)) break;
        float org[3];
        vm.EdictFieldVector(e, f.origin, org);
        for (int d = 0; d < 3; d++) org[d] += vel[d] * dt;
        vm.SetEdictFieldVector(e, f.origin, org);
        break;
    }
    case MOVE_FLY: {
        if (!RunThinkLoop(vm, f, e, time, dt)) break;
        FlyMoveEnt(vm, map, f, e, dt, ents, num_ents, e);
        break;
    }
    default:
        // unsupported client movetype: still think + relink (reference would
        // Sys_Error here; malicious/broken mods don't kill the engine).
        RunThinkLoop(vm, f, e, time, dt);
        break;
    }

    if (vm.EdictFree(e)) return;

    // SV_LinkEdict(ent, true): with touch, so triggers/items fire for the
    // client this frame. Re-assert FL_CLIENT first -- the SP PlayerPreThink
    // drops it mid-frame and the item touch functions gate on other.flags.
    if (f.flags >= 0) {
        int fl = IntFlags(vm, e, f.flags);
        fl |= FL_CLIENT;
        if (onground) fl |= FL_ONGROUND;
        else fl &= ~FL_ONGROUND;
        SetFlags(vm, e, f.flags, fl);
    }

    LinkEdict(vm, e, true);

    // water transition splash (misc/h2ohit1.wav) when the feet crossing
    // status flips into or out of water.
    int water_after = IntField(vm, e, f.waterlevel);
    if ((water_before > 1) != (water_after > 1) && g_ent_sound_hook)
        g_ent_sound_hook(e, "misc/h2ohit1.wav");

    int post = vm.FunctionIndex("PlayerPostThink");
    if (post > 0) {
        vm.SetSelfEdict(e);
        vm.SetOtherEdict(0);
        vm.ExecuteProgram(post);
    }

    // refresh the caller's copy of the player state
    vm.EdictFieldVector(e, f.origin, c.out_origin);
    vm.EdictFieldVector(e, f.velocity, c.out_velocity);
    c.out_onground = (IntFlags(vm, e, f.flags) & FL_ONGROUND) != 0;
    c.out_waterlevel = IntField(vm, e, f.waterlevel);
}

void RunGameFrame(vm::ProgVM& vm, const BSPMap& map, float dt,
                  const SolidEntity* ents, int num_ents, int client_edict,
                  ClientPhysics* client) {
    g_fields.Populate(vm);
    const Fields& f = g_fields;

    float time = vm.Time() + dt;
    vm.SetTime(time);
    vm.SetFrametime(dt);
    vm.SetOtherEdict(0);

    // SV_ProgStartFrame
    int sf = vm.FunctionIndex("StartFrame");
    if (sf > 0) {
        vm.SetSelfEdict(0);
        vm.ExecuteProgram(sf);
    }

    // Reference SV_Physics: when force_retouch is set (e.g. after a map
    // change), every entity is force re-linked so stationary trigger
    // overlaps still fire.
    int force_retouch_ofs = vm.FindGlobal("force_retouch");
    static thread_local std::vector<int> riders;

    // Keep the area-node tree in sync with every entity's current box before
    // the dispatch: zquake has no setorigin/setsize hook-in to link on spawn
    // (the reference links via SV_LinkEdict), so static triggers/items/brushes
    // are (re)linked here so the client relink + monster touches below can find
    // them wherever they are this frame. No touch is fired by this pass.
    for (int i = 1; i < 1024; i++)
        if (!vm.EdictFree(i)) LinkEdict(vm, i, false);

    for (int i = 1; i < 1024; i++) {
        if (vm.EdictFree(i)) continue;
        const bool is_client = (i == client_edict && client_edict > 0);
        if (is_client && !client) continue;

        int movetype = IntField(vm, i, f.movetype);
        int saved_mt = movetype;  // saved for post-think water transition
        bool moving = movetype == MOVE_TOSS || movetype == MOVE_BOUNCE ||
                      movetype == MOVE_FLY || movetype == MOVE_FLYMISSILE ||
                      movetype == MOVE_STEP || movetype == MOVE_PUSH;

        if (force_retouch_ofs >= 0 && vm.GlobalFloat(force_retouch_ofs) > 0.0f)
            LinkEdict(vm, i, true);   // force retouch even for stationary entities

        if (is_client) {
            RunClientPhysics(vm, map, f, i, dt, ents, num_ents, *client, time);
            continue;
        }

        const SolidEntity* move_ents = ents;
        int move_num = num_ents;
        if (moving) {
            // Rebuild the solid list against the CURRENT positions of every
            // other entity so movement/traceline see friends + client where
            // they are right now, not where they were at frame start. Without
            // this, sequentially-moving monsters clip into each other and the
            // player and pile up.
            static thread_local std::vector<SolidEntity> fresh;
            BuildSolidList(vm, map, fresh, i);
            // client_edict stays the real player: the ignore for the rebuilt
            // solid list is `i`, but checkclient() must still report the client.
            SetGameTraceContext(&map, fresh.data(), (int)fresh.size(), client_edict);
            move_ents = fresh.data();
            move_num = (int)fresh.size();
        }

        if (movetype == MOVE_TOSS || movetype == MOVE_BOUNCE ||
            movetype == MOVE_FLY || movetype == MOVE_FLYMISSILE) {
            PhysicsToss(vm, map, f, i, dt, move_ents, move_num, i);
            if (vm.EdictFree(i)) continue;
            LinkEdict(vm, i, true);   // SV_PushEntity links with touch
            CheckWaterTransition(vm, map, f, i);  // reference TOSS: before think
        } else if (movetype == MOVE_PUSH) {
            riders.clear();
            PhysicsPush(vm, map, f, i, dt, move_ents, move_num, &riders);
            for (int r : riders) LinkEdict(vm, r, true);
            LinkEdict(vm, i, false);   // SV_LinkEdict(pusher, false)
            // Reference SV_Physics_Pusher advances a pusher's ltime every frame
            // so SUB_CalcMove's `nextthink = ltime + traveltime` is measured from
            // the live clock. Without it the stale ltime makes SUB_CalcMoveDone
            // (snap-to-finaldest) fire early -> the door jumps the last part of
            // its travel instead of sliding the full distance.
            if (f.ltime >= 0) vm.EdictFieldFloat(i, f.ltime) = time;
        } else if (movetype == MOVE_NOCLIP) {
            // Reference SV_Physics_Noclip: think handled by the shared block
            // below; free flight + rotation.
            float avel[3], vel[3], ang[3], org[3];
            vm.EdictFieldVector(i, f.avelocity, avel);
            vm.EdictFieldVector(i, f.velocity, vel);
            vm.EdictFieldVector(i, f.angles, ang);
            vm.EdictFieldVector(i, f.origin, org);
            for (int c = 0; c < 3; c++) {
                ang[c] += avel[c] * dt;
                org[c] += vel[c] * dt;
            }
            vm.SetEdictFieldVector(i, f.angles, ang);
            vm.SetEdictFieldVector(i, f.origin, org);
            if (vm.EdictFree(i)) continue;
            LinkEdict(vm, i, false);
        } else if (movetype == MOVE_STEP) {
            // Reference SV_Physics_Step (WinQuake sv_phys.c): freefall when not
            // onground/flying/swimming, play land sound on hard impact, then
            // think. Monsters get their ground motion from QuakeC walkmove /
            // movetogoal builtins, not from this physics.
            bool hitsound_flag = false;
            if (!(IntFlags(vm, i, f.flags) &
                  (FL_ONGROUND | FL_FLY | FL_SWIM))) {
                float vel[3];
                vm.EdictFieldVector(i, f.velocity, vel);
                if (vel[2] < 800.0f * -0.1f) hitsound_flag = true;
                AddGravityEnt(vm, f, i, dt);
                // SV_CheckVelocity clamp
                vm.EdictFieldVector(i, f.velocity, vel);
                for (int c = 0; c < 3; c++) {
                    if (vel[c] > 2000.0f) vel[c] = 2000.0f;
                    else if (vel[c] < -2000.0f) vel[c] = -2000.0f;
                }
                vm.SetEdictFieldVector(i, f.velocity, vel);
                FlyMoveEnt(vm, map, f, i, dt, move_ents, move_num, i);
                // just hit ground
                if ((IntFlags(vm, i, f.flags) & FL_ONGROUND) && hitsound_flag && g_ent_sound_hook)
                    g_ent_sound_hook(i, "demon/dland2.wav");
            }
            if (vm.EdictFree(i)) continue;
            LinkEdict(vm, i, true);
        }

        // SV_RunThink (reference sv_phys.c: runs AFTER the physics at the end
        // of the frame, so the physics re-determines on-ground first). The QW
        // version loops so a think that re-arms nextthink is run again in the
        // same frame instead of being deferred.
        if (!RunThinkLoop(vm, f, i, time, dt)) continue;

        // Water transition for STEP entities (reference SV_Physics_Step: runs
        // AFTER SV_RunThink, playing misc/h2ohit1.wav on watertype crossing).
        if (saved_mt == MOVE_STEP)
            CheckWaterTransition(vm, map, f, i);
    }
    if (force_retouch_ofs >= 0 && vm.GlobalFloat(force_retouch_ofs) > 0.0f)
        vm.GlobalFloat(force_retouch_ofs) -= 1.0f;
    vm.SetTime(time);

    ParticleSystem::Step(dt);   // engine-side particle lifetimes (builtin #48)
}

void CheckTouch(vm::ProgVM& vm, int client_edict, const SolidEntity* ents,
                int num_ents) {
    (void)ents; (void)num_ents;
    g_fields.Populate(vm);
    const Fields& f = g_fields;

    float corg[3], cmins[3], cmaxs[3];
    vm.EdictFieldVector(client_edict, f.origin, corg);
    vm.EdictFieldVector(client_edict, f.mins, cmins);
    vm.EdictFieldVector(client_edict, f.maxs, cmaxs);

    // Save and restore self/other like reference SV_Impact, so running a touch
    // never leaks a stale self/other into a later monster think/melee (which
    // would make the melee damage the wrong target - e.g. the monster itself).
    int old_self = vm.SelfEdict();
    int old_other = vm.OtherEdict();

    for (int i = 1; i < 1024; i++) {
        if (vm.EdictFree(i)) continue;
        if (i == client_edict) continue;
        int touch = vm.EdictFieldInt(i, f.touch);
        int solid = IntField(vm, i, f.solid);
        if (!touch || solid == SOLID_NOT) continue;

        float eorg[3], emins[3], emaxs[3];
        vm.EdictFieldVector(i, f.origin, eorg);
        vm.EdictFieldVector(i, f.mins, emins);
        vm.EdictFieldVector(i, f.maxs, emaxs);

        bool overlap = true;
        for (int a = 0; a < 3; a++) {
            float e0 = eorg[a] + emins[a], e1 = eorg[a] + emaxs[a];
            float c0 = corg[a] + cmins[a], c1 = corg[a] + cmaxs[a];
            if (e1 < c0 || c1 < e0) { overlap = false; break; }
        }
        if (!overlap) continue;

        vm.SetTime(vm.Time());
        vm.SetSelfEdict(i);
        vm.SetOtherEdict(client_edict);
        vm.ExecuteProgram(touch);
        if (vm.EdictFree(i)) continue;
    }

    vm.SetSelfEdict(old_self);
    vm.SetOtherEdict(old_other);
}

} // namespace zq::engine
