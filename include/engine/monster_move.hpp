#pragma once
// Monster movement helpers ported from quake/WinQuake/sv_move.c
// (SV_Movestep, SV_CheckBottom, SV_FixCheckBottom, SV_StepDirection,
//  SV_NewChaseDir, SV_CloseEnough). These operate on edicts via ProgVM
//  and depend on the current game trace context (SetGameTraceContext).
#include "vm/prog_vm.hpp"

namespace zq::engine {

// SV_Movestep: attempt to move `edict` by the horizontal `move` vector
// (stepping up/down stairs for walkable entities, direct for FL_FLY/FL_SWIM).
// Returns true if the move succeeded. When relink is true, calls
// SV_LinkEdict(edict, true) on success.
bool SV_Movestep(vm::ProgVM& vm, int edict, float move[3], bool relink);

// SV_CheckBottom: returns false if any part of the entity's base hangs off
// an edge that is not a staircase.
bool SV_CheckBottom(vm::ProgVM& vm, int edict);

// SV_FixCheckBottom: sets FL_PARTIALGROUND on the entity.
void SV_FixCheckBottom(vm::ProgVM& vm, int edict);

// SV_StepDirection: turn toward yaw and walk `dist` units if not blocked.
// Returns true if the step succeeded.
bool SV_StepDirection(vm::ProgVM& vm, int edict, float yaw, float dist);

// SV_NewChaseDir: pick a direction that avoids the current orientation and
// tries to walk toward the enemy's position.
void SV_NewChaseDir(vm::ProgVM& vm, int actor, int enemy, float dist);

// SV_CloseEnough: returns true if the entity's abs box is within `dist`
// units of the goal's abs box on all axes.
bool SV_CloseEnough(vm::ProgVM& vm, int ent, int goal, float dist);

// SV_MoveToGoal: top-level call matching the #67 builtin. Drives
//  self toward self.goalentity using SV_StepDirection / SV_NewChaseDir.
void SV_MoveToGoal(vm::ProgVM& vm);

} // namespace zq::engine
