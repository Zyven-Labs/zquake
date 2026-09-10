#include <catch2/catch_test_macros.hpp>
#include "vm/prog_vm.hpp"
#include "vm/prog_builtins.hpp"
#include "engine/bsp.hpp"
#include "engine/move.hpp"
#include "engine/gameframe.hpp"
#include "engine/monster_move.hpp"
#include "filesystem/pak_archive.hpp"
#include <cstring>
#include <cstdio>
#include <vector>

using namespace zq::engine;
using zq::vm::ProgVM;

namespace {
bool SetupGame(ProgVM& vm, BSPMap& map) {
    zq::fs::PAKArchive pak;
    if (!pak.Open("id1/pak0.pak")) return false;
    std::vector<uint8_t> pd, md;
    for (auto& e : pak.GetEntries()) {
        if (std::strcmp(e.name, "progs.dat") == 0) { pd.resize(e.file_size); pak.ReadFile(e.name, pd.data(), pd.size()); }
        else if (std::strcmp(e.name, "maps/e1m1.bsp") == 0) { md.resize(e.file_size); pak.ReadFile(e.name, md.data(), md.size()); }
    }
    if (pd.empty() || md.empty() || !vm.Load(pd.data(), pd.size())) return false;
    zq::vm::RegisterDefaultBuiltins(vm);
    RegisterGameBuiltins(vm);
    if (!map.Load(md.data(), md.size())) return false;
    InitAreaNodes(map);
    vm.AllocateEdicts(1024);
    vm.SetTime(0); vm.SetFrametime(0);
    vm.SetBrushBounds([&map](int mi, float mn[3], float mx[3]) {
        const zq::engine::BSPModel& mod = map.Models()[mi];
        mn[0] = mod.origin[0] + mod.mins[0]; mn[1] = mod.origin[1] + mod.mins[1]; mn[2] = mod.origin[2] + mod.mins[2];
        mx[0] = mod.origin[0] + mod.maxs[0]; mx[1] = mod.origin[1] + mod.maxs[1]; mx[2] = mod.origin[2] + mod.maxs[2];
        return true;
    });
    vm.RegisterBuiltin(34, [&map](ProgVM& v) {
        int fo = v.FindField("origin"), mnf = v.FindField("mins"), mxf = v.FindField("maxs");
        int gs = v.FindGlobal("self");
        int s = v.ProgToEdictNum(v.Global(gs));
        if (fo < 0 || mnf < 0 || mxf < 0 || s < 0) { v.SetReturnFloat(0); return; }
        float o[3], mn[3], mx[3];
        v.EdictFieldVector(s, fo, o);
        v.EdictFieldVector(s, mnf, mn);
        v.EdictFieldVector(s, mxf, mx);
        float e[3] = { o[0], o[1], o[2] - 256 };
        auto tr = MoveBox(map, mn, mx, o, e, nullptr, 0, -1);
        if (tr.fraction == 1.0f || tr.allsolid) { v.SetReturnFloat(0); return; }
        v.SetEdictFieldVector(s, fo, tr.endpos);
        v.SetReturnFloat(1);
    });
    vm.RegisterBuiltin(41, [&map](ProgVM& v) {
        float p[3]; v.ParmVector(0, p);
        v.SetReturnFloat((float)map.PointContents(p));
        return;
    });
    return true;
}
}

TEST_CASE("PROBE: shotgun fire dumps entities", "[probe]") {
    ProgVM vm; BSPMap map;
    if (!SetupGame(vm, map)) { FAIL("no game"); }

    int fit = vm.FindField("items"), fa = vm.FindField("ammo_shells");
    int fw = vm.FindField("weapon");
    if (fit >= 0) vm.EdictFieldFloat(1, fit) = (float)((int)vm.EdictFieldFloat(1, fit) | (1 | 4096));
    vm.EdictFieldFloat(1, fa) = 25.0f;
    if (fw >= 0) vm.EdictFieldFloat(1, fw) = 1.0f;
    int fsca = vm.FunctionIndex("W_SetCurrentAmmo");
    if (fsca > 0) { vm.SetSelfEdict(1); vm.ExecuteProgram(fsca); }

    std::vector<SolidEntity> solids;
    int ff = vm.FindField("flags");

    for (int f = 0; f < 10; f++) {
        if (ff >= 0)
            vm.EdictFieldFloat(1, ff) = (float)((int)vm.EdictFieldFloat(1, ff) | 8);
        BuildSolidList(vm, map, solids, 1);
        SetGameTraceContext(&map, solids.data(), (int)solids.size(), 1);
        ClientPhysics cin;
        cin.button0 = 1;
        cin.move_vars = MoveVars{};
        RunGameFrame(vm, map, 0.05f, solids.data(), (int)solids.size(), 1, &cin);
        vm.SetSelfEdict(1); vm.SetOtherEdict(0);
    }

    int fe = vm.FindField("effects"), fm = vm.FindField("model");
    int fclass = vm.FindField("classname"), fsrc = vm.FindField("origin");
    int fscale = vm.FindField("scale"), fmm = vm.FindField("modelindex");
    int fsolid = vm.FindField("solid"), fmt = vm.FindField("movetype");
    printf("=== entity dump ===\n");
    for (int i = 1; i < 128; i++) {
        if (vm.EdictFree(i)) continue;
        const char* m = (fm >= 0) ? vm.EdictFieldString(i, fm) : "";
        const char* cn = (fclass >= 0) ? vm.EdictFieldString(i, fclass) : "";
        float o[3] = {0,0,0};
        if (fsrc >= 0) vm.EdictFieldVector(i, fsrc, o);
        float sc = (fscale >= 0) ? vm.EdictFieldFloat(i, fscale) : 0;
        float mi = (fmm >= 0) ? vm.EdictFieldFloat(i, fmm) : 0;
        float so = (fsolid >= 0) ? vm.EdictFieldFloat(i, fsolid) : 0;
        float mt = (fmt >= 0) ? vm.EdictFieldFloat(i, fmt) : 0;
        printf("edict %d model='%s' class='%s' o=(%.1f,%.1f,%.1f) scale=%.2f modelindex=%.0f solid=%.0f movetype=%.0f\n",
               i, m, cn, o[0], o[1], o[2], sc, mi, so, mt);
    }
    printf("=== done ===\n");
}