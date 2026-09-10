#include "engine/move.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace zq::engine {
namespace {

constexpr float MOVE_EPSILON = 0.01f;
constexpr float STOP_EPSILON = 0.1f;
constexpr int MAX_CLIP_PLANES = 5;
constexpr float STEPSIZE = 18.0f;

inline void VecCopy(float d[3], const float s[3]) { d[0]=s[0]; d[1]=s[1]; d[2]=s[2]; }
inline void VecAdd(float d[3], const float a[3], const float b[3]) { d[0]=a[0]+b[0]; d[1]=a[1]+b[1]; d[2]=a[2]+b[2]; }
inline void VecScale(float d[3], const float a[3], float s) { d[0]=a[0]*s; d[1]=a[1]*s; d[2]=a[2]*s; }
inline float VecDot(const float a[3], const float b[3]) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline void VecCross(const float a[3], const float b[3], float d[3]) {
    d[0]=a[1]*b[2]-a[2]*b[1];
    d[1]=a[2]*b[0]-a[0]*b[2];
    d[2]=a[0]*b[1]-a[1]*b[0];
}
inline float VecLen(const float a[3]) { return std::sqrt(VecDot(a, a)); }
inline float VecNormalize(float a[3]) {
    float l = VecLen(a);
    if (l) { a[0]/=l; a[1]/=l; a[2]/=l; }
    return l;
}

// ---- SV_CheckVelocity ----
void CheckVelocity(PlayerPhys& p, const MoveVars& mv) {
    for (int i = 0; i < 3; i++) {
        if (!(p.velocity[i] == p.velocity[i])) p.velocity[i] = 0; // NaN
        if (!(p.origin[i] == p.origin[i])) p.origin[i] = 0;
        if (p.velocity[i] > mv.maxvelocity) p.velocity[i] = mv.maxvelocity;
        else if (p.velocity[i] < -mv.maxvelocity) p.velocity[i] = -mv.maxvelocity;
    }
}

// ---- SV_AddGravity ----
void AddGravity(PlayerPhys& p, float scale, const MoveVars& mv, float dt) {
    p.velocity[2] -= scale * mv.gravity * dt;
}

// ---- ClipVelocity (returns blocked: 1 floor, 2 step/wall) ----
int ClipVelocity(const float in[3], const float normal[3], float out[3], float overbounce) {
    int blocked = 0;
    if (normal[2] > 0) blocked |= 1;
    if (!normal[2]) blocked |= 2;
    float backoff = VecDot(in, normal) * overbounce;
    for (int i = 0; i < 3; i++) {
        float change = normal[i] * backoff;
        out[i] = in[i] - change;
        if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON) out[i] = 0;
    }
    return blocked;
}

// Trace context: world map + solid entities to clip against.
struct MoveCtx {
    const BSPMap* map = nullptr;
    const SolidEntity* ents = nullptr;
    int num_ents = 0;
    int ignore = -1;
};

// Slab test of the segment p1..p2 against the box, scaled by the tester box
// extents (mins/maxs swept behind the centre). Returns the entry fraction;
// 1.0f = no hit. Shared by the box-entity trace (internal) and +use aiming
// (external RayAABBFraction below).
BSPMap::TraceResult BoxEntityTrace(const SolidEntity& ent,
                                   const float start[3], const float end[3],
                                   const float mins[3], const float maxs[3]) {
    float bmin[3], bmax[3];
    for (int i = 0; i < 3; i++) {
        bmin[i] = ent.origin[i] + ent.mins[i] - maxs[i];
        bmax[i] = ent.origin[i] + ent.maxs[i] - mins[i];
        if (bmin[i] > bmax[i]) { float t = bmin[i]; bmin[i] = bmax[i]; bmax[i] = t; }
    }

    float tenter = 0.0f, texit = 1.0f;
    int axis = -1;            // axis of the entering face
    for (int i = 0; i < 3; i++) {
        float d = end[i] - start[i];
        if (d == 0.0f) {
            if (start[i] < bmin[i] || start[i] > bmax[i]) {
                BSPMap::TraceResult miss;
                miss.fraction = 1.0f;
                miss.endpos[0] = end[0]; miss.endpos[1] = end[1]; miss.endpos[2] = end[2];
                miss.allsolid = false;
                miss.hit_brush = false;
                return miss; // parallel and outside
            }
        } else {
            float t1 = (bmin[i] - start[i]) / d;
            float t2 = (bmax[i] - start[i]) / d;
            bool t1_is_min = t1 < t2;
            float lo = t1_is_min ? t1 : t2;
            float hi = t1_is_min ? t2 : t1;
            if (lo >= tenter) { tenter = lo; axis = i; }
            if (hi < texit) texit = hi;
        }
    }
    if (tenter > texit) {
        BSPMap::TraceResult miss;
        miss.fraction = 1.0f;
        miss.endpos[0] = end[0]; miss.endpos[1] = end[1]; miss.endpos[2] = end[2];
        miss.allsolid = false;
        miss.hit_brush = false;
        return miss; // misses
    }
    // Strictly inside (not merely touching a face)? Reference SV_RecursiveHullCheck
    // classifies a point sitting exactly ON a clip plane as outside, so a mover
    // that starts flush against another box can still move away from it.
    bool inside = true;
    for (int i = 0; i < 3; i++)
        if (start[i] <= bmin[i] || start[i] >= bmax[i]) { inside = false; break; }

    BSPMap::TraceResult trace;
    trace.fraction = 1.0f;
    trace.endpos[0] = end[0]; trace.endpos[1] = end[1]; trace.endpos[2] = end[2];
    trace.allsolid = false;
    trace.hit_brush = false;

    if (inside) {
        trace.startsolid = true;
        trace.fraction = 0.0f;
        return trace;
    }
    if (texit <= 0.0f) {
        // The box is behind the mover, or the mover starts flush against a face
        // and the move carries it away from the box - free in both cases.
        return trace; // free
    }
    // The mover is flush against a face (its centre is exactly on the box
    // boundary) and moves parallel to it (no motion on that axis) - that's a
    // graze along the face, not a blocking hit, so the mover can slide freely.
    for (int i = 0; i < 3; i++) {
        if (end[i] == start[i] &&
            (std::fabs(start[i] - bmin[i]) < 1e-4f || std::fabs(start[i] - bmax[i]) < 1e-4f))
            return trace; // free
    }
    if (tenter >= 1.0f) return trace;

    trace.fraction = tenter;
    // entering face normal
    trace.plane_normal[0] = trace.plane_normal[1] = trace.plane_normal[2] = 0;
    if (axis >= 0) {
        float dir = end[axis] - start[axis];
        trace.plane_normal[axis] = (dir > 0) ? -1.0f : 1.0f;
    } else {
        trace.plane_normal[2] = 1.0f;
    }
    trace.plane_dist = trace.plane_normal[axis >= 0 ? axis : 2];
    for (int i = 0; i < 3; i++)
        trace.endpos[i] = start[i] + tenter * (end[i] - start[i]);
    return trace;
}

// Box trace through the world + solid entities (SV_Move against the world
// and each SOLID entity). Returns the closest hit.
BSPMap::TraceResult Move(const MoveCtx& ctx, const PlayerPhys& p,
                         const float start[3], const float end[3]) {
    auto out = ctx.map->WorldTrace(start, end, p.mins, p.maxs);
    out.hit_brush = true;
    out.hit_edict = 0; // world

    for (int i = 0; i < ctx.num_ents; i++) {
        const SolidEntity& e = ctx.ents[i];
        if (e.kind == SolidEntity::Kind::None) continue;
        if (e.edict == ctx.ignore) continue;
        BSPMap::TraceResult tr;
        if (e.kind == SolidEntity::Kind::Brush) {
            tr = ctx.map->ModelTrace(start, end, p.mins, p.maxs, e.model_index, e.origin);
            tr.hit_brush = true;
        } else {
            tr = BoxEntityTrace(e, start, end, p.mins, p.maxs);
            if (tr.fraction == 1.0f) continue;
        }
        if (tr.fraction < out.fraction) {
            out = tr;
            out.hit_edict = e.edict;
        }
    }
    return out;
}

// SV_PushEntity (falls back to Move; origin advances as far as possible).
void PushEntity(const MoveCtx& ctx, PlayerPhys& p, const float push[3]) {
    float end[3];
    VecAdd(end, p.origin, push);
    auto tr = Move(ctx, p, p.origin, end);
    VecCopy(p.origin, tr.endpos);
}// ---- SV_FlyMove ----
int FlyMove(const MoveCtx& ctx, PlayerPhys& p, float time,
            BSPMap::TraceResult* steptrace) {
    constexpr int numbumps = 4;
    int blocked = 0;
    float original_velocity[3], primal_velocity[3], planes[MAX_CLIP_PLANES][3];
    float time_left = time;
    VecCopy(original_velocity, p.velocity);
    VecCopy(primal_velocity, p.velocity);
    int numplanes = 0;

    for (int bump = 0; bump < numbumps; bump++) {
        float end[3];
        for (int i = 0; i < 3; i++)
            end[i] = p.origin[i] + time_left * p.velocity[i];

        auto trace = Move(ctx, p, p.origin, end);

        if (trace.fraction == 0.0f) {
            // the trace reports no progress when deeply wedged
        }
        if (trace.allsolid) {
            p.velocity[0] = p.velocity[1] = p.velocity[2] = 0;
            return 3;
        }
        if (trace.fraction > 0) {
            VecCopy(p.origin, trace.endpos);
            VecCopy(original_velocity, p.velocity);
            numplanes = 0;
        }
        if (trace.fraction == 1)
            break;

        bool onnode = trace.plane_normal[2] > 0.7f;
        if (onnode) {
            blocked |= 1; // floor
            if (trace.hit_brush) p.onground = true;
        }
        if (!trace.plane_normal[2]) {
            blocked |= 2; // step
            if (steptrace) *steptrace = trace;
        }

        time_left -= time_left * trace.fraction;

        if (numplanes >= MAX_CLIP_PLANES) {
            p.velocity[0] = p.velocity[1] = p.velocity[2] = 0;
            return 3;
        }

        for (int i = 0; i < 3; i++) planes[numplanes][i] = trace.plane_normal[i];
        numplanes++;

        // modify original_velocity so it parallels all the clip planes
        float new_velocity[3];
        int i;
        for (i = 0; i < numplanes; i++) {
            ClipVelocity(original_velocity, planes[i], new_velocity, 1);
            int j;
            for (j = 0; j < numplanes; j++)
                if (j != i) {
                    if (VecDot(new_velocity, planes[j]) < 0) break;
                }
            if (j == numplanes) break;
        }
        if (i != numplanes) {
            VecCopy(p.velocity, new_velocity);
        } else {
            if (numplanes != 2) {
                p.velocity[0] = p.velocity[1] = p.velocity[2] = 0;
                return 7;
            }
            float dir[3];
            VecCross(planes[0], planes[1], dir);
            float d = VecDot(dir, p.velocity);
            VecScale(p.velocity, dir, d);
        }

        if (VecDot(p.velocity, primal_velocity) <= 0) {
            p.velocity[0] = p.velocity[1] = p.velocity[2] = 0;
            return blocked;
        }
    }
    return blocked;
}

// ---- SV_TestEntityPosition ----
bool TestEntityPosition(const MoveCtx& ctx, const PlayerPhys& p,
                        const float origin[3]) {
    auto tr = Move(ctx, p, origin, origin);
    return tr.startsolid;
}

// ---- SV_CheckStuck ----
void CheckStuck(const MoveCtx& ctx, PlayerPhys& p) {
    if (!TestEntityPosition(ctx, p, p.origin))
        return;

    float org[3];
    VecCopy(org, p.origin);
    // The reference records oldorigin to step back to; we don't track it, so
    // fall straight to the positional scan.
    for (int z = 0; z < 18; z++)
        for (int i = -1; i <= 1; i++)
            for (int j = -1; j <= 1; j++) {
                p.origin[0] = org[0] + i;
                p.origin[1] = org[1] + j;
                p.origin[2] = org[2] + z;
                if (!TestEntityPosition(ctx, p, p.origin))
                    return;
            }
    VecCopy(p.origin, org);
}

// ---- SV_CheckWater ----
bool CheckWater(const BSPMap& map, PlayerPhys& p) {
    p.waterlevel = 0;
    p.watertype = CONTENTS_EMPTY;
    float point[3] = { p.origin[0], p.origin[1], p.origin[2] + p.mins[2] + 1 };
    int cont = map.PointContents(point);
    if (cont <= CONTENTS_WATER) {
        p.watertype = cont;
        p.waterlevel = 1;
        point[2] = p.origin[2] + (p.mins[2] + p.maxs[2]) * 0.5f;
        cont = map.PointContents(point);
        if (cont <= CONTENTS_WATER) {
            p.waterlevel = 2;
            point[2] = p.origin[2] + p.viewheight;
            cont = map.PointContents(point);
            if (cont <= CONTENTS_WATER) p.waterlevel = 3;
        }
    }
    return p.waterlevel > 1;
}

// ---- SV_TryUnstick ----
int TryUnstick(const MoveCtx& ctx, PlayerPhys& p) {
    float oldorg[3];
    VecCopy(oldorg, p.origin);
    float dir[3] = {0, 0, 0};

    for (int k = 0; k < 8; k++) {
        switch (k) {
            case 0: dir[0] = 2; dir[1] = 0; break;
            case 1: dir[0] = 0; dir[1] = 2; break;
            case 2: dir[0] = -2; dir[1] = 0; break;
            case 3: dir[0] = 0; dir[1] = -2; break;
            case 4: dir[0] = 2; dir[1] = 2; break;
            case 5: dir[0] = -2; dir[1] = 2; break;
            case 6: dir[0] = 2; dir[1] = -2; break;
            case 7: dir[0] = -2; dir[1] = -2; break;
        }
        PushEntity(ctx, p, dir);
        int clip = FlyMove(ctx, p, 0.1f, nullptr);
        if (std::fabs(oldorg[1] - p.origin[1]) > 4 || std::fabs(oldorg[0] - p.origin[0]) > 4)
            return clip;
        VecCopy(p.origin, oldorg);
    }
    p.velocity[0] = p.velocity[1] = p.velocity[2] = 0;
    return 7;
}

// ---- SV_WallFriction ----
void WallFriction(PlayerPhys& p, const BSPMap::TraceResult& trace) {
    float d = VecDot(trace.plane_normal, p.angles); // uses forward? see reference note
    // Reference: AngleVectors(v_angle) then d = dot(plane.normal, forward).
    // Equivalent to dot(plane.normal, view-forward). Approximate with yaw.
    (void)d;
    // Cut the tangential velocity
    float yaw = p.angles[1] * 3.14159265358979f / 180.0f;
    float forward[3] = { std::cos(yaw), std::sin(yaw), 0 };
    float d2 = VecDot(trace.plane_normal, forward);
    d2 += 0.5f;
    if (d2 >= 0) return;
    float into[3];
    float i = VecDot(trace.plane_normal, p.velocity);
    VecScale(into, trace.plane_normal, i);
    float side[3];
    side[0] = p.velocity[0] - into[0];
    side[1] = p.velocity[1] - into[1];
    side[2] = p.velocity[2] - into[2];
    p.velocity[0] = side[0] * (1 + d2);
    p.velocity[1] = side[1] * (1 + d2);
}

// ---- SV_WalkMove ----
void WalkMove(const MoveCtx& ctx, PlayerPhys& p, float dt, const MoveVars& mv) {
    bool oldonground = p.onground;
    p.onground = false;

    float oldorg[3], oldvel[3];
    VecCopy(oldorg, p.origin);
    VecCopy(oldvel, p.velocity);

    BSPMap::TraceResult steptrace;
    int clip = FlyMove(ctx, p, dt, &steptrace);

    if (!(clip & 2)) return;             // didn't block on a step
    if (!oldonground && p.waterlevel == 0) return; // don't step up while jumping
    if (mv.nostep) return;

    float nosteporg[3], nostepvel[3];
    VecCopy(nosteporg, p.origin);
    VecCopy(nostepvel, p.velocity);

    // try moving up and forward to go up a step
    VecCopy(p.origin, oldorg);
    float upmove[3] = {0, 0, STEPSIZE};
    float downmove[3] = {0, 0, -STEPSIZE + oldvel[2] * dt};

    PushEntity(ctx, p, upmove);

    p.velocity[0] = oldvel[0];
    p.velocity[1] = oldvel[1];
    p.velocity[2] = 0;
    clip = FlyMove(ctx, p, dt, &steptrace);

    if (clip) {
        if (std::fabs(oldorg[0] - p.origin[0]) < 0.03125f &&
            std::fabs(oldorg[1] - p.origin[1]) < 0.03125f) {
            clip = TryUnstick(ctx, p);
        }
    }
    if (clip & 2) WallFriction(p, steptrace);

    auto downtrace = [&]() {
        BSPMap::TraceResult r;
        VecAdd(r.endpos, p.origin, downmove);
        auto t = Move(ctx, p, p.origin, r.endpos);
        VecCopy(p.origin, t.endpos);
        return t;
    }();

    if (downtrace.plane_normal[2] > 0.7f) {
        p.onground = true;
    } else {
        VecCopy(p.origin, nosteporg);
        VecCopy(p.velocity, nostepvel);
    }
}

// ---- SV_UserFriction ----
void UserFriction(const BSPMap& map, PlayerPhys& p, float dt, const MoveVars& mv) {
    float vel[3];
    VecCopy(vel, p.velocity);
    float speed = std::sqrt(vel[0] * vel[0] + vel[1] * vel[1]);
    if (!speed) return;

    // if the leading edge is over a dropoff, increase friction
    float start[3], stop[3];
    start[0] = stop[0] = p.origin[0] + vel[0] / speed * 16;
    start[1] = stop[1] = p.origin[1] + vel[1] / speed * 16;
    start[2] = p.origin[2] + p.mins[2];
    stop[2] = start[2] - 34;

    auto tr = map.WorldTrace(start, stop, p.mins, p.mins); // point trace: zero-size box
    float friction = (tr.fraction == 1.0f) ? mv.friction * mv.edgefriction : mv.friction;

    float control = speed < mv.stopspeed ? mv.stopspeed : speed;
    float newspeed = speed - dt * control * friction;
    if (newspeed < 0) newspeed = 0;
    newspeed /= speed;

    p.velocity[0] = vel[0] * newspeed;
    p.velocity[1] = vel[1] * newspeed;
    p.velocity[2] = vel[2] * newspeed;
}

// ---- SV_Accelerate (wishdir/wishspeed ellided: pass dir+speed) ----
void Accelerate(PlayerPhys& p, const float wishdir[3], float wishspeed,
                float dt, const MoveVars& mv) {
    float currentspeed = VecDot(p.velocity, wishdir);
    float addspeed = wishspeed - currentspeed;
    if (addspeed <= 0) return;
    float accelspeed = mv.accelerate * dt * wishspeed;
    if (accelspeed > addspeed) accelspeed = addspeed;
    for (int i = 0; i < 3; i++) p.velocity[i] += accelspeed * wishdir[i];
}

// ---- SV_AirAccelerate ----
void AirAccelerate(PlayerPhys& p, const float wishveloc[3], float dt,
                   const MoveVars& mv) {
    float wishdir[3];
    VecCopy(wishdir, wishveloc);
    float wishspd = VecNormalize(wishdir);
    if (wishspd > 30) wishspd = 30;
    float currentspeed = VecDot(p.velocity, wishdir);
    float addspeed = wishspd - currentspeed;
    if (addspeed <= 0) return;
    float accelspeed = mv.accelerate * wishspd * dt;
    if (accelspeed > addspeed) accelspeed = addspeed;
    for (int i = 0; i < 3; i++) p.velocity[i] += accelspeed * wishdir[i];
}

// ---- SV_WaterMove ----
void WaterMove(const BSPMap& map, PlayerPhys& p, const PlayerCmd& cmd,
               float dt, const MoveVars& mv) {
    (void)map;
    float yaw = p.angles[1] * 3.14159265358979f / 180.0f;
    float pitch = p.angles[0] * 3.14159265358979f / 180.0f;
    float sp = std::sin(pitch), cp = std::cos(pitch);
    float sy = std::sin(yaw), cy = std::cos(yaw);
    float forward[3] = { cp * cy, cp * sy, sp };
    float right[3] = { sy, -cy, 0 };

    float wishvel[3];
    for (int i = 0; i < 3; i++)
        wishvel[i] = forward[i] * cmd.forwardmove + right[i] * cmd.sidemove;
    if (!cmd.forwardmove && !cmd.sidemove && !cmd.upmove)
        wishvel[2] -= 60;
    else
        wishvel[2] += cmd.upmove;

    float wishspeed = VecLen(wishvel);
    if (wishspeed > mv.maxspeed) {
        VecScale(wishvel, wishvel, mv.maxspeed / wishspeed);
        wishspeed = mv.maxspeed;
    }
    wishspeed *= 0.7f;

    // water friction
    float speed = VecLen(p.velocity);
    float newspeed = 0;
    if (speed) {
        newspeed = speed - dt * speed * mv.friction;
        if (newspeed < 0) newspeed = 0;
        VecScale(p.velocity, p.velocity, newspeed / speed);
    }
    if (!wishspeed) return;

    float addspeed = wishspeed - newspeed;
    if (addspeed <= 0) return;

    VecNormalize(wishvel);
    float accelspeed = mv.accelerate * wishspeed * dt;
    if (accelspeed > addspeed) accelspeed = addspeed;
    for (int i = 0; i < 3; i++) p.velocity[i] += accelspeed * wishvel[i];
}

} // namespace

// Point ray (zero-extent tester) vs an AABB (SV_UseEdicts aiming). Entry
// fraction 0..1; 1.0f = no hit.
float RayAABBFraction(const float start[3], const float end[3],
                      const float origin[3], const float mins[3],
                      const float maxs[3]) {
    SolidEntity box;
    box.kind = SolidEntity::Kind::Box;
    box.origin[0] = origin[0]; box.origin[1] = origin[1]; box.origin[2] = origin[2];
    box.mins[0] = mins[0]; box.mins[1] = mins[1]; box.mins[2] = mins[2];
    box.maxs[0] = maxs[0]; box.maxs[1] = maxs[1]; box.maxs[2] = maxs[2];
    const float zero[3] = {0, 0, 0};
    return BoxEntityTrace(box, start, end, zero, zero).fraction;
}

BSPMap::TraceResult MoveBox(const BSPMap& map, const float mins[3],
                            const float maxs[3], const float start[3],
                            const float end[3],
                            const SolidEntity* ents, int num_ents, int ignore_edict) {
    MoveCtx ctx;
    ctx.map = &map;
    ctx.ents = ents;
    ctx.num_ents = num_ents;
    ctx.ignore = ignore_edict;
    PlayerPhys tmp;
    VecCopy(tmp.mins, mins);
    VecCopy(tmp.maxs, maxs);
    return Move(ctx, tmp, start, end);
}

void RunPlayerMove(const BSPMap& map, PlayerPhys& p, const PlayerCmd& cmd,
                   float dt, const MoveVars& mv,
                   const SolidEntity* ents, int num_ents, int ignore_edict) {
    if (dt <= 0) return;
    if (dt > 0.1f) dt = 0.1f;

    MoveCtx ctx;
    ctx.map = &map;
    ctx.ents = ents;
    ctx.num_ents = num_ents;
    ctx.ignore = ignore_edict;

    CheckVelocity(p, mv);

    // jump request handled by the caller via p.velocity (QuakeC normally sets
    // velocity_z on jump); gravity applied here as in SV_Physics_Client.
    if (!CheckWater(map, p))
        AddGravity(p, 1.0f, mv, dt);
    CheckStuck(ctx, p);

    // ---- SV_ClientThink (input → friction/accel) ----
    if (p.waterlevel >= 2) {
        WaterMove(map, p, cmd, dt, mv);
    } else {
        float yaw = p.angles[1] * 3.14159265358979f / 180.0f;
        float pitch = p.angles[0] * 3.14159265358979f / 180.0f;
        float sp = std::sin(pitch), cp = std::cos(pitch);
        float sy = std::sin(yaw), cy = std::cos(yaw);
        float forward[3] = { cp * cy, cp * sy, sp };
        float right[3] = { sy, -cy, 0 };

        float fmove = cmd.forwardmove, smove = cmd.sidemove;
        float wishvel[3];
        for (int i = 0; i < 3; i++)
            wishvel[i] = forward[i] * fmove + right[i] * smove;
        wishvel[2] = 0;

        float wishdir[3];
        VecCopy(wishdir, wishvel);
        float wishspeed = VecNormalize(wishdir);
        if (wishspeed > mv.maxspeed) {
            VecScale(wishvel, wishvel, mv.maxspeed / wishspeed);
            wishspeed = mv.maxspeed;
        }

        if (p.onground) {
            UserFriction(map, p, dt, mv);
            Accelerate(p, wishdir, wishspeed, dt, mv);
        } else {
            AirAccelerate(p, wishvel, dt, mv);
        }
    }

    // jump: set vertical velocity (QuakeC sets 270 on the press frame).
    // onground stays set this frame so WalkMove sees a grounded move.
    if (cmd.jump && p.onground) {
        p.velocity[2] = 270.0f;
    }

    WalkMove(ctx, p, dt, mv);

    CheckVelocity(p, mv);
}

} // namespace zq::engine