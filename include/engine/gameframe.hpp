#pragma once
#include "engine/bsp.hpp"
#include "engine/move.hpp"
#include "vm/prog_vm.hpp"
#include <vector>

namespace zq::engine {

// QuakeC movetype / solid / flag constants (QW defs.qc).
inline constexpr int MOVE_NONE = 0;
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
inline constexpr int FL_ONGROUND = 512;
inline constexpr int FL_WATERJUMP = 2048;

// Build the list of solid entities (for SV_Move entity clipping).
// Entries for the world (0) and `ignore_edict` are skipped. SOLID_BSP
// brush submodels become Brush entries using their modelindex; others are Box.
void BuildSolidList(vm::ProgVM& vm, const BSPMap& map,
                    std::vector<SolidEntity>& out, int ignore_edict);

// Runs one server frame of QuakeC logic (SV_Physics port): sets time /
// frametime, calls StartFrame, then for each non-client edict that is due to
// think, runs its think function, and advances TOSS/STEP/FLY physics.
void RunGameFrame(vm::ProgVM& vm, const BSPMap& map, float dt,
                  const SolidEntity* ents, int num_ents, int client_edict);

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

} // namespace zq::engine
