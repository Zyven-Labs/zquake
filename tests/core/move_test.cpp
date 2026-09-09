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
