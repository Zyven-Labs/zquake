// Reference player-physics tests on the real e1m1 map (loaded from the game
// PAK). Verifies the SV_ClientThink + SV_WalkMove / SV_FlyMove replication
// gives Quake-accurate behavior using the map's baked clip hulls:
//   - spawn box is clear of solid
//   - the player can actually move from spawn (the bug was wedging)
//   - strafe axes are correct (D = right of view)
//   - velocity builds and friction decays it
#include <catch2/catch_test_macros.hpp>
#include "filesystem/pak_archive.hpp"
#include "engine/bsp.hpp"
#include "engine/move.hpp"
#include "engine/gameframe.hpp"
#include "vm/prog_vm.hpp"
#include "vm/prog_builtins.hpp"
#include <cstring>
#include <cmath>
#include <vector>

using namespace zq::engine;

namespace {
// Load maps/e1m1.bsp from id1/pak0.pak (relative to the build dir).
bool LoadE1M1(BSPMap& map) {
    zq::fs::PAKArchive pak;
    if (!pak.Open("id1/pak0.pak")) return false;
    std::vector<uint8_t> md;
    for (auto& e : pak.GetEntries())
        if (std::strcmp(e.name, "maps/e1m1.bsp") == 0) {
            md.resize(e.file_size);
            pak.ReadFile(e.name, md.data(), md.size());
            break;
        }
    return !md.empty() && map.Load(md.data(), md.size());
}

PlayerPhys SpawnPlayer(BSPMap& map, float yaw) {
    MoveVars mv;
    PlayerPhys p;
    float o[3], a[3];
    if (!map.FindSpawnPoint(o, a)) {
        o[0] = o[1] = o[2] = 0;
    }
    p.origin[0] = o[0]; p.origin[1] = o[1]; p.origin[2] = o[2];
    p.angles[1] = yaw;
    p.onground = true;
    return p;
}
}

TEST_CASE("Physics: spawn box is clear of solid (e1m1)", "[physics]") {
    BSPMap map;
    if (!LoadE1M1(map)) return; // no game data
    auto p = SpawnPlayer(map, 0);
    REQUIRE_FALSE(map.BoxInSolid(p.origin, p.mins, p.maxs));
}

TEST_CASE("Physics: player moves from spawn (e1m1, W)", "[physics]") {
    BSPMap map;
    if (!LoadE1M1(map)) return;
    auto p = SpawnPlayer(map, 90.0f); // facing +Y
    float start[3] = { p.origin[0], p.origin[1], p.origin[2] };
    PlayerCmd cmd;
    cmd.forwardmove = 320;
    MoveVars mv;
    for (int i = 0; i < 30; i++)
        RunPlayerMove(map, p, cmd, 0.016f, mv);
    float moved = std::fabs(p.origin[0] - start[0]) + std::fabs(p.origin[1] - start[1]);
    // 30 frames of full-speed accel must move the player along north (+Y)
    REQUIRE(moved > 20.0f);
    REQUIRE(p.origin[1] > start[1] + 15.0f);
}

TEST_CASE("Physics: strafe axes correct (e1m1, D=right)", "[physics]") {
    BSPMap map;
    if (!LoadE1M1(map)) return;
    MoveVars mv;
    PlayerCmd cmd;
    {
        // Facing +Y (north) at the spawn: right is +X (east).
        auto p = SpawnPlayer(map, 90.0f);
        cmd.sidemove = 320; // D
        RunPlayerMove(map, p, cmd, 0.016f, mv);
        float dx = p.origin[0] - (p.origin[0]);
        (void)dx;
        float x0 = p.origin[0];
        for (int i = 0; i < 25; i++) RunPlayerMove(map, p, cmd, 0.016f, mv);
        REQUIRE(p.origin[0] > x0 + 10.0f); // moved east = right of north
    }
    {
        // A (sidemove -) moves left = -X (west)
        auto p = SpawnPlayer(map, 90.0f);
        cmd.sidemove = -320;
        float x0 = p.origin[0];
        for (int i = 0; i < 25; i++) RunPlayerMove(map, p, cmd, 0.016f, mv);
        REQUIRE(p.origin[0] < x0 - 10.0f);
    }
}

TEST_CASE("Physics: friction stops the player (e1m1)", "[physics]") {
    BSPMap map;
    if (!LoadE1M1(map)) return;
    auto p = SpawnPlayer(map, 90.0f);
    MoveVars mv;
    PlayerCmd cmd;
    cmd.forwardmove = 320;
    for (int i = 0; i < 40; i++) RunPlayerMove(map, p, cmd, 0.016f, mv);
    float at_release = std::sqrt(p.velocity[0]*p.velocity[0] + p.velocity[1]*p.velocity[1]);
    REQUIRE(at_release > 50.0f);
    cmd.forwardmove = 0;
    for (int i = 0; i < 90; i++) RunPlayerMove(map, p, cmd, 0.016f, mv);
    float still = std::sqrt(p.velocity[0]*p.velocity[0] + p.velocity[1]*p.velocity[1]);
    REQUIRE(still < at_release * 0.5f);
}

TEST_CASE("Physics: jump lifts off and returns to ground (e1m1)", "[physics]") {
    BSPMap map;
    if (!LoadE1M1(map)) return;
    auto p = SpawnPlayer(map, 0);
    // settle onto the floor first
    MoveVars mv;
    PlayerCmd cmd;
    for (int i = 0; i < 60; i++) RunPlayerMove(map, p, cmd, 0.016f, mv);
    float z0 = p.origin[2];
    cmd.jump = true;
    RunPlayerMove(map, p, cmd, 0.016f, mv);
    cmd.jump = false;
    REQUIRE(p.velocity[2] > 100.0f);
    float peak = p.origin[2];
    for (int i = 0; i < 200; i++) {
        RunPlayerMove(map, p, cmd, 0.016f, mv);
        if (p.origin[2] > peak) peak = p.origin[2];
    }
    REQUIRE(peak > z0 + 5.0f);
    REQUIRE(p.onground);
}

TEST_CASE("Physics: client runs through RunGameFrame dispatch (e1m1)", "[physics]") {
    BSPMap map;
    if (!LoadE1M1(map)) return;
    InitAreaNodes(map);
    auto p0 = SpawnPlayer(map, 0);   // facing +X

    // Minimal VM: load the real progs only for field/function indexes; no
    // worldspawn / entities so the single edict is the client.
    zq::fs::PAKArchive pak;
    if (!pak.Open("id1/pak0.pak")) return;
    std::vector<uint8_t> pd;
    for (auto& e : pak.GetEntries())
        if (std::strcmp(e.name, "progs.dat") == 0) {
            pd.resize(e.file_size);
            pak.ReadFile(e.name, pd.data(), pd.size());
            break;
        }
    if (pd.empty()) return;
    zq::vm::ProgVM vm;
    if (!vm.Load(pd.data(), pd.size())) return;
    zq::vm::RegisterDefaultBuiltins(vm);
    RegisterGameBuiltins(vm);
    vm.AllocateEdicts(2);
    vm.SetTime(0); vm.SetFrametime(0);

    const int fo = vm.FindField("origin"), fv = vm.FindField("velocity");
    const int fa = vm.FindField("v_angle"), fmn = vm.FindField("mins"), fmx = vm.FindField("maxs");
    const int fmt = vm.FindField("movetype"), fs = vm.FindField("solid"), ff = vm.FindField("flags");
    vm.SetEdictFieldVector(1, fo, p0.origin);
    float z[3] = {0, 0, 0}; vm.SetEdictFieldVector(1, fv, z);
    float va[3] = {0, 0, 0}; vm.SetEdictFieldVector(1, fa, va);
    float mn[3] = {-16, -16, -24}, mx[3] = {16, 16, 32};
    vm.SetEdictFieldVector(1, fmn, mn);
    vm.SetEdictFieldVector(1, fmx, mx);
    vm.EdictFieldFloat(1, fmt) = (float)MOVE_WALK;
    vm.EdictFieldFloat(1, fs) = (float)SOLID_SLIDEBOX;
    vm.EdictFieldFloat(1, ff) = (float)(FL_CLIENT | FL_ONGROUND);

    std::vector<SolidEntity> solids;
    BuildSolidList(vm, map, solids, 1);
    SetGameTraceContext(&map, solids.data(), (int)solids.size(), 1);

    ClientPhysics cin;
    cin.forwardmove = 320;   // W: +X at yaw 0
    cin.button2 = true;
    cin.move_vars = MoveVars{};

    bool grounded = false;
    float max_fwd = 0.0f;
    for (int i = 0; i < 30; i++) {
        RunGameFrame(vm, map, 0.016f, solids.data(), (int)solids.size(), 1, &cin);
        grounded = grounded || cin.out_onground;
        if (cin.out_velocity[0] > max_fwd) max_fwd = cin.out_velocity[0];
    }

    // moved forward (+X) and stays on the floor
    REQUIRE(cin.out_origin[0] > p0.origin[0] + 10.0f);
    REQUIRE(grounded);
    // the integration actually drove the player (full accel toward +X at some
    // frame; a wall can zero the velocity on the final clipped frame)
    REQUIRE(max_fwd > 50.0f);
    REQUIRE(cin.out_waterlevel >= 0);
    // identity + on-ground survive the frame (FL_CLIENT re-asserted)
    int fl = (int)vm.EdictFieldFloat(1, ff);
    REQUIRE((fl & FL_CLIENT) != 0);
    REQUIRE((fl & FL_ONGROUND) != 0);
    // the client edict itself moved
    float eo[3];
    vm.EdictFieldVector(1, fo, eo);
    REQUIRE(eo[0] > p0.origin[0]);
}
