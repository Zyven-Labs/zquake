#pragma once
#include "engine/area_nodes.hpp"
#include "engine/bsp.hpp"
#include "engine/move.hpp"
#include "vm/prog_vm.hpp"
#include <vector>

namespace zq::engine {

// QuakeC movetype / solid / flag constants (WinQuake server.h + defs.qc).
inline constexpr int MOVE_NONE = 0;
inline constexpr int MOVE_ANGLENOCLIP = 1;
inline constexpr int MOVE_ANGLECLIP = 2;
inline constexpr int MOVE_WALK = 3;
inline constexpr int MOVE_STEP = 4;
inline constexpr int MOVE_FLY = 5;
inline constexpr int MOVE_TOSS = 6;
inline constexpr int MOVE_PUSH = 7;
inline constexpr int MOVE_NOCLIP = 8;
inline constexpr int MOVE_FLYMISSILE = 9;
inline constexpr int MOVE_BOUNCE = 10;

inline constexpr int SOLID_NOT = 0;
inline constexpr int SOLID_TRIGGER = 1;
inline constexpr int SOLID_BBOX = 2;
inline constexpr int SOLID_SLIDEBOX = 3;
inline constexpr int SOLID_BSP = 4;

inline constexpr int FL_FLY = 1;
inline constexpr int FL_SWIM = 2;
inline constexpr int FL_CONVEYOR = 4;
inline constexpr int FL_CLIENT = 8;
inline constexpr int FL_INWATER = 16;
inline constexpr int FL_MONSTER = 32;
inline constexpr int FL_GODMODE = 64;
inline constexpr int FL_NOTARGET = 128;
inline constexpr int FL_ITEM = 256;
inline constexpr int FL_ONGROUND = 512;
inline constexpr int FL_PARTIALGROUND = 1024;
inline constexpr int FL_WATERJUMP = 2048;

// Build the list of solid entities (for SV_Move entity clipping).
// Entries for the world (0) and `ignore_edict` are skipped. SOLID_BSP
// brush submodels become Brush entries using their modelindex; others are Box.
void BuildSolidList(vm::ProgVM& vm, const BSPMap& map,
                    std::vector<SolidEntity>& out, int ignore_edict);

// Optional local-client input used to integrate the player into the entity
// dispatcher (WinQuake SV_Physics_Client). When RunGameFrame is passed a
// non-null ClientPhysics and the client edict is present, that edict is
// processed inside the server frame: usercmd is written to the edict, then
// PlayerPreThink -> walk/noclip physics -> water transition -> PlayerPostThink
// -> SV_LinkEdict(ent,false), exactly like SV_Physics_Client. With nullptr the
// client edict is skipped (non-interactive/test callers).
struct ClientPhysics {
    // input intent (usercmd_t)
    float forwardmove = 0;   // W/S (+/-320)
    float sidemove = 0;      // A/D (+/-320)
    float upmove = 0;
    bool jump = false;       // engine-side jump impulse (270 units/s)
    bool button0 = false;    // +attack
    bool button2 = false;    // +jump for QuakeC (button2 from space in SP)
    bool button3 = false;    // +speed
    int impulse = 0;         // weapon-select impulse (1-8), zeroed by progs

    // per-frame engine tuning (SV_Physics_Walk constants)
    MoveVars move_vars;

    // pose/state refreshed by RunGameFrame (read back after the frame)
    float out_origin[3] = {0, 0, 0};
    float out_velocity[3] = {0, 0, 0};
    bool out_onground = false;
    int out_waterlevel = 0;
};

// Runs one server frame of QuakeC logic (SV_Physics port): sets time /
// frametime, calls StartFrame, then for each non-client edict that is due to
// think, runs its think function, and advances TOSS/STEP/FLY physics.
void RunGameFrame(vm::ProgVM& vm, const BSPMap& map, float dt,
                  const SolidEntity* ents, int num_ents, int client_edict,
                  ClientPhysics* client = nullptr);

// Calls the touch function on any entity overlapping the client box
// (SV_Impact-style item/weapon pickup).
void CheckTouch(vm::ProgVM& vm, int client_edict, const SolidEntity* ents,
                int num_ents);

// Current game trace context (world + per-frame solid entities), read by the
// game builtins (traceline / walkmove / etc.).
struct GameTraceContext {
    const BSPMap* map = nullptr;
    const SolidEntity* ents = nullptr;
    int num_ents = 0;
    int client_edict = 1;
};
void SetGameTraceContext(const BSPMap* map, const SolidEntity* ents,
                         int num_ents, int client_edict);
const GameTraceContext& GetGameTraceContext();

// Registers the gameplay QuakeC builtins (numbers 0..82) that need the world
// geometry / trace context: traceline, walkmove, movetogoal, find, findradius,
// aim, changeyaw, checkbottom, sound, cvar, etc. Overrides the default stubs.
void RegisterGameBuiltins(vm::ProgVM& vm);

// Sound hook invoked by server physics when an event has an audio cue
// (monster landing "demon/dland2.wav", water enter/exit "misc/h2ohit1.wav").
// The engine has no audio subsystem yet, so this defaults to a no-op; the app
// can wire it up later. Signature: (entity that emitted the event, sound path).
using EntitySoundHook = void (*)(int ent_edict, const char* path);
void SetEntitySoundHook(EntitySoundHook hook);
EntitySoundHook GetEntitySoundHook();

// Area-node helpers (SV_ClearWorld / SV_LinkEdict / SV_TouchLinks parity).
// Initialises the tree from the world model bounds; call once after the map
// loads (or to re-link after a level change).
void InitAreaNodes(const BSPMap& map);

// Compute the entity's absolute bounding box (SV_LinkEdict abs box, including
// the FL_ITEM 15-unit pickup expansion and the 1-unit clip epsilon) and link
// it into the area-node tree. When `touch_triggers` is true, runs SV_TouchLinks
// for the entity as well.
void LinkEdict(vm::ProgVM& vm, int edict, bool touch_triggers);

// The global area-node tree instance shared with the server frame.
extern AreaNodes g_area_nodes;

} // namespace zq::engine
