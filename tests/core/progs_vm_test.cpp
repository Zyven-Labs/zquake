// Real QuakeC VM tests against the actual qwprogs.dat bundled in the GPL
// sources. Proves the dprograms_t/dstatement_t loader + three-address
// interpreter + map-entity spawn path work on real Id bytecode.
#include <catch2/catch_test_macros.hpp>
#include "vm/prog_vm.hpp"
#include "vm/prog_builtins.hpp"
#include "engine/bsp.hpp"
#include "filesystem/pak_archive.hpp"
#include "core/logging/logger.hpp"
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef ZQ_TEST_PROGS_PATH
#define ZQ_TEST_PROGS_PATH "quake/qw-qc/qwprogs.dat"
#endif

using namespace zq::vm;

namespace {
std::vector<uint8_t> ReadFile(const char* path) {
    std::vector<uint8_t> out;
    FILE* f = std::fopen(path, "rb");
    if (!f) return out;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(sz);
    if (sz > 0) std::fread(out.data(), 1, sz, f);
    std::fclose(f);
    return out;
}
}

TEST_CASE("Progs: load real qwprogs.dat", "[progs]") {
    auto data = ReadFile(ZQ_TEST_PROGS_PATH);
    REQUIRE_FALSE(data.empty());

    ProgVM vm;
    REQUIRE(vm.Load(data.data(), data.size()));
    REQUIRE(vm.NumFunctions() > 100);
    REQUIRE(vm.NumStatements() > 1000);
    REQUIRE(vm.Header()->version == 6);
}

TEST_CASE("Progs: builtin table matches defs.qc numbering", "[progs]") {
    auto data = ReadFile(ZQ_TEST_PROGS_PATH);
    if (data.empty()) return; // no progs available
    ProgVM vm;
    REQUIRE(vm.Load(data.data(), data.size()));

    // builtin (sub-binary) function indices must match the QW numbering
    REQUIRE(vm.FunctionAt(1) != nullptr);
    REQUIRE(vm.FunctionAt(1)->first_statement == -1);   // makevectors
    REQUIRE(vm.FunctionAt(3)->first_statement == -3);   // setmodel
    REQUIRE(vm.FunctionAt(5)->first_statement == -6);   // break (gap)
    REQUIRE(vm.FunctionAt(13)->first_statement == -14); // spawn
    REQUIRE(vm.FunctionAt(19)->first_statement == -20); // precache_model
}

TEST_CASE("Progs: named function lookup", "[progs]") {
    auto data = ReadFile(ZQ_TEST_PROGS_PATH);
    if (data.empty()) return;
    ProgVM vm;
    REQUIRE(vm.Load(data.data(), data.size()));
    int fsub = vm.FindFunction("SUB_Null");
    REQUIRE(fsub > 0);
    REQUIRE(vm.FindFunction("worldspawn") > 0);
    REQUIRE(vm.FindFunction("does_not_exist") < 0);
}

TEST_CASE("Progs: execute real functions", "[progs]") {
    auto data = ReadFile(ZQ_TEST_PROGS_PATH);
    if (data.empty()) return;
    ProgVM vm;
    REQUIRE(vm.Load(data.data(), data.size()));
    RegisterDefaultBuiltins(vm);
    vm.AllocateEdicts(1);

    // trivial no-op function must run without error
    REQUIRE(vm.ExecuteProgram("SUB_Null"));
    REQUIRE(vm.LastError() == 0);

    // delay think sets self.nextthink = time+0.1 (via OP_STATE)
    REQUIRE(vm.ExecuteProgram("DelayThink"));
    REQUIRE(vm.LastError() == 0);
}

TEST_CASE("Progs: map entity spawn (e1m1)", "[progs]") {
    auto data = ReadFile(ZQ_TEST_PROGS_PATH);
    if (data.empty()) return;

    // real map: spawn the player start via QuakeC
    std::vector<uint8_t> mapdata;
    if (!ReadFile("id1/pak0.pak").empty()) {
        zq::fs::PAKArchive pak;
        if (!pak.Open("id1/pak0.pak")) return;
        for (auto& e : pak.GetEntries())
            if (std::strcmp(e.name, "maps/e1m1.bsp") == 0) {
                mapdata.resize(e.file_size);
                pak.ReadFile(e.name, mapdata.data(), mapdata.size());
                break;
            }
    }
    if (mapdata.empty()) return; // no game data available
    zq::engine::BSPMap map;
    REQUIRE(map.Load(mapdata.data(), mapdata.size()));

    ProgVM vm;
    REQUIRE(vm.Load(data.data(), data.size()));
    RegisterDefaultBuiltins(vm);
    vm.AllocateEdicts(1);
    vm.SetTime(0.0f);
    vm.LoadEntities(map.EntityString().c_str());

    // world edict 0 active; the information_player_start spawns correctly
    // (its origin parsed from the map, spawn function sets no model).
    REQUIRE_FALSE(vm.EdictFree(0));
    int origin_f = vm.FindField("origin");
    int class_f = vm.FindField("classname");

    bool found_start = false;
    float expect[3] = {480, -352, 88};
    for (int i = 1; i < 1024; i++) {
        if (vm.EdictFree(i)) continue;
        if (std::strcmp(vm.EdictFieldString(i, class_f), "info_player_start") == 0) {
            found_start = true;
            float o[3];
            vm.EdictFieldVector(i, origin_f, o);
            REQUIRE(o[0] == expect[0]);
            REQUIRE(o[1] == expect[1]);
            REQUIRE(o[2] == expect[2]);
        }
    }
    REQUIRE(found_start);
}
