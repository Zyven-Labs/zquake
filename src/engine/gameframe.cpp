#include "engine/gameframe.hpp"
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

        float nv[3];
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

    int movetype = IntField(vm, e, f.movetype);
    if (movetype != MOVE_FLY && movetype != MOVE_FLYMISSILE)
        vel[2] -= 800.0f * dt;
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

// SV_Physics_Push for MOVETYPE_PUSH brushes (doors, plats): translate by
// velocity over dt (riding SOLID_BBOX entities are carried along), and stop
// (zero velocity) when the move is blocked.
void PhysicsPush(vm::ProgVM& vm, const BSPMap& map, const Fields& f, int e,
                 float dt, const SolidEntity* ents, int num_ents) {
    float vel[3];
    vm.EdictFieldVector(e, f.velocity, vel);
    bool moving = false;
    for (int i = 0; i < 3; i++)
        if (vel[i] != 0.0f) moving = true;
    if (!moving) return;

    float origin[3];
    vm.EdictFieldVector(e, f.origin, origin);
    float mn[3], mx[3];
    vm.EdictFieldVector(e, f.mins, mn);
    vm.EdictFieldVector(e, f.maxs, mx);
    float move[3] = { vel[0] * dt, vel[1] * dt, vel[2] * dt };

    // Reference SV_Push: a mover is NEVER blocked by world geometry - it always
    // advances to its intended position (doors recess into their built-in
    // pocket). It only pushes solid BOX entities (riders) that end up inside its
    // box. The old code traced the mover against the world with MoveBox and
    // zeroed its velocity on the first partial hit, so the door froze at ~1u and
    // then SUB_CalcMoveDone snapped it to finaldest -> visible "snap" open/close.
    float end[3] = { origin[0] + move[0], origin[1] + move[1], origin[2] + move[2] };
    vm.SetEdictFieldVector(e, f.origin, end);

    // Riders: dynamic box entities overlapping the mover's box at `end` get
    // carried by the same delta (unless that would trap them inside world
    // solid - the mover still advances, matching the reference).
    for (int j = 0; j < num_ents; j++) {
        const SolidEntity& se = ents[j];
        if (se.edict == e) continue;
        if (se.kind != SolidEntity::Kind::Box) continue;
        if (se.mins[0] == 0 && se.maxs[0] == 0) continue; // no extent
        bool overlap = true;
        for (int i = 0; i < 3; i++) {
            if (se.origin[i] + se.mins[i] > end[i] + mx[i] ||
                se.origin[i] + se.maxs[i] < end[i] + mn[i]) { overlap = false; break; }
        }
        if (!overlap) continue;
        float rorg[3];
        vm.EdictFieldVector(se.edict, f.origin, rorg);
        float rtest[3] = { rorg[0] + move[0], rorg[1] + move[1], rorg[2] + move[2] };
        if (!map.BoxInSolid(rtest, se.mins, se.maxs)) {
            vm.SetEdictFieldVector(se.edict, f.origin, rtest);
            int fg = f.groundentity;
            if (fg >= 0) vm.EdictFieldInt(se.edict, fg) = vm.EdictNumToProg(e);
        }
    }
}

} // namespace

GameTraceContext g_trace_ctx;

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

void RunGameFrame(vm::ProgVM& vm, const BSPMap& map, float dt,
                  const SolidEntity* ents, int num_ents, int client_edict) {
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

    for (int i = 1; i < 1024; i++) {
        if (vm.EdictFree(i)) continue;
        if (i == client_edict && client_edict > 0) continue;

        int movetype = IntField(vm, i, f.movetype);
        bool moving = movetype == MOVE_TOSS || movetype == MOVE_BOUNCE ||
                      movetype == MOVE_FLY || movetype == MOVE_FLYMISSILE ||
                      movetype == MOVE_STEP || movetype == MOVE_PUSH;
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
        } else if (movetype == MOVE_PUSH) {
            PhysicsPush(vm, map, f, i, dt, move_ents, move_num);
            // Reference SV_Physics_Pusher advances a pusher's ltime every frame
            // so SUB_CalcMove's `nextthink = ltime + traveltime` is measured from
            // the live clock. Without it the stale ltime makes SUB_CalcMoveDone
            // (snap-to-finaldest) fire early -> the door jumps the last part of
            // its travel instead of sliding the full distance.
            if (f.ltime >= 0) vm.EdictFieldFloat(i, f.ltime) = time;
        } else if (movetype == MOVE_STEP) {
            // Reference SV_Physics_Step: gravity only when NOT on ground at
            // frame start (wasonground); friction when on ground; then move;
            // then re-determine on-ground from the 4 bottom corners.
            bool wasonground = IntFlags(vm, i, f.flags) & FL_ONGROUND;
            bool inwater = false; // (Option A: no full water handling)
            if (!wasonground && !(IntFlags(vm, i, f.flags) & FL_FLY) &&
                !((IntFlags(vm, i, f.flags) & FL_SWIM) && inwater)) {
                float vel[3];
                vm.EdictFieldVector(i, f.velocity, vel);
                vel[2] -= 800.0f * dt;
                vm.SetEdictFieldVector(i, f.velocity, vel);
            }
            {
                float vel[3];
                vm.EdictFieldVector(i, f.velocity, vel);
                if (vel[0] || vel[1] || vel[2]) {
                    SetFlags(vm, i, f.flags, IntFlags(vm, i, f.flags) & ~FL_ONGROUND);
                    if (wasonground) {
                        // friction (reference: control=max(stopspeed,speed))
                        float speed = std::sqrt(vel[0]*vel[0] + vel[1]*vel[1]);
                        if (speed) {
                            float control = speed < 100.0f ? 100.0f : speed;
                            float newspeed = speed - dt * control * 4.0f;
                            if (newspeed < 0) newspeed = 0;
                            newspeed /= speed;
                            vel[0] *= newspeed; vel[1] *= newspeed;
                            vm.SetEdictFieldVector(i, f.velocity, vel);
                        }
                    }
                    FlyMoveEnt(vm, map, f, i, dt, move_ents, move_num, i);
                }
            }
            // Re-determine on-ground: any of the 4 bottom corners at mins[2]-1 inside
            // solid content (reference SV_Physics_Step). Clear it when NONE are
            // solid so a monster that steps off a ledge loses ground and falls
            // instead of keeping a stale FL_ONGROUND and hovering.
            {
                float o3[3], mn3[3], mx3[3];
                vm.EdictFieldVector(i, f.origin, o3);
                vm.EdictFieldVector(i, f.mins, mn3);
                vm.EdictFieldVector(i, f.maxs, mx3);
                float pt[3] = { 0, 0, o3[2] + mn3[2] - 1.0f };
                bool onground = false;
                for (int x = 0; x <= 1 && !onground; x++)
                    for (int y = 0; y <= 1; y++) {
                        pt[0] = o3[0] + (x ? mx3[0] : mn3[0]);
                        pt[1] = o3[1] + (y ? mx3[1] : mn3[1]);
                        if (map.PointContents(pt) == CONTENTS_SOLID) onground = true;
                    }
                if (onground)
                    SetFlags(vm, i, f.flags, IntFlags(vm, i, f.flags) | FL_ONGROUND);
                else
                    SetFlags(vm, i, f.flags, IntFlags(vm, i, f.flags) & ~FL_ONGROUND);
            }
        }

        // SV_RunThink (reference sv_phys.c: runs AFTER the physics at the end
        // of the frame, so the physics re-determines on-ground first).
        {
            int think = vm.EdictFieldInt(i, f.think);
            if (think < 0) think = 0;
            float next = vm.EdictFieldFloat(i, f.nextthink);
            if (think > 0 && next > 0 && next <= time + dt) {
                float t = next < time ? time : next;
                vm.SetTime(t);
                vm.SetSelfEdict(i);
                vm.SetOtherEdict(0);
                vm.EdictFieldFloat(i, f.nextthink) = 0;
                if (f.ltime >= 0) {
                    float nlt = t - vm.EdictFieldFloat(i, f.ltime);
                    vm.EdictFieldFloat(i, f.ltime) = t;
                    vm.SetFrametime(nlt);
                }
                if (!vm.ExecuteProgram(think)) continue;
                if (vm.EdictFree(i)) continue;
            }
        }
    }
    vm.SetTime(time);
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
