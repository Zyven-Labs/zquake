#pragma once
#include "engine/bsp.hpp"

namespace zq::engine {

// Player motion state (the subset the reference physics touches).
struct PlayerPhys {
    float origin[3] = {0, 0, 0};
    float velocity[3] = {0, 0, 0};
    float angles[3] = {0, 0, 0};   // v_angle (pitch, yaw, roll)
    float mins[3] = {-16, -16, -24};
    float maxs[3] = {16, 16, 32};
    float viewheight = 28.0f;

    bool onground = false;
    int waterlevel = 0;
    int watertype = 0;
};

struct MoveVars {
    float gravity = 800.0f;
    float stopspeed = 100.0f;
    float maxspeed = 320.0f;
    float accelerate = 10.0f;
    float airaccelerate = 0.7f;
    float wateraccelerate = 10.0f;
    float friction = 4.0f;
    float edgefriction = 2.0f;
    float waterfriction = 4.0f;
    float maxvelocity = 2000.0f;
    bool nostep = false;
};

// Per-frame input intent (usercmd_t): wish speeds in units/sec along the
// view axes (keys already scaled to +/-320), and a jump request.
struct PlayerCmd {
    float forwardmove = 0;   // W/S
    float sidemove = 0;      // A/D  (+ = right)
    float upmove = 0;
    bool jump = false;
};

// A solid entity the moving box can collide with (the reference SV_ClipToEntity).
struct SolidEntity {
    enum class Kind { None, Brush, Box };
    Kind kind = Kind::None;
    int edict = -1;             // VM edict number (for ignore/touch)
    int model_index = -1;       // Brush: index into BSPMap Models() (submodel)
    float origin[3] = {0, 0, 0};// Brush: submodel origin offset; Box: entity origin
    float mins[3] = {0, 0, 0};  // Box entities
    float maxs[3] = {0, 0, 0};
};

// Runs one frame of faithful Quake player physics against the world BSP.
// Mirrors SV_ClientThink + SV_Physics_Client(MOVETYPE_WALK) + SV_WalkMove.
// `ents` (count `num_ents`) are traced in addition to the world; the entity
// with the `ignore_edict` tag is skipped (it is the mover itself).
void RunPlayerMove(const BSPMap& map, PlayerPhys& p, const PlayerCmd& cmd,
                   float dt, const MoveVars& movevars,
                   const SolidEntity* ents = nullptr, int num_ents = 0,
                   int ignore_edict = -1);

// Reusable SV_Move: closest hit against the world + solid entities.
BSPMap::TraceResult MoveBox(const BSPMap& map, const float mins[3],
                            const float maxs[3], const float start[3],
                            const float end[3],
                            const SolidEntity* ents = nullptr, int num_ents = 0,
                            int ignore_edict = -1);

// Entry fraction (0..1) of the segment start->end crossing the AABB
// origin+mins..maxs; 1.0 means no hit. Zero-size point trace (SV_Move).
float RayAABBFraction(const float start[3], const float end[3],
                      const float origin[3], const float mins[3],
                      const float maxs[3]);

} // namespace zq::engine