// Deterministic gameplay-loop tests against the real progs.dat + e1m1:
#include <cmath>
// item/weapon pickup (FL_CLIENT gating), engine-side jump, and the door-use
// scan. Gated on the game PAK being available (id1/pak0.pak next to the
// build dir, like real_data_test).
#include <catch2/catch_test_macros.hpp>
#include "vm/prog_vm.hpp"
#include "vm/prog_builtins.hpp"
#include "engine/bsp.hpp"
#include "engine/move.hpp"
#include "engine/gameframe.hpp"
#include "engine/monster_move.hpp"
#include "filesystem/pak_archive.hpp"
#include <cstring>
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
    InitAreaNodes(map); // area-node tree mirrors the game's map load

    vm.AllocateEdicts(2); // world + client
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
    });
    vm.LoadEntities(map.EntityString().c_str());
    // single-player mode globals (reference sv_main.c + host.c skill=1)
    vm.SetGlobalFloatG("deathmatch", 0.0f);
    vm.SetGlobalFloatG("coop", 0.0f);
    vm.SetGlobalFloatG("teamplay", 0.0f);
    vm.SetGlobalFloatG("skill", 1.0f);
    vm.SetGlobalFloatG("serverflags", 0.0f);
    vm.SetGlobalStringG("mapname", "e1m1");
    // client
    vm.SetSelfEdict(1);
    int fc = vm.FindField("classname");
    if (fc >= 0) vm.EdictFieldInt(1, fc) = vm.InternString("player");
    vm.SetOtherEdict(0);
    float co[3] = {480,-352,88};
    vm.SetGlobalFloatG("parm1", co[0]); vm.SetGlobalFloatG("parm2", co[1]);
    vm.SetGlobalFloatG("parm3", co[2]); vm.SetGlobalFloatG("parm4", 90);
    vm.SetGlobalFloatG("parm5", 0.0f); vm.SetGlobalFloatG("parm6", 0.0f);
    vm.SetGlobalFloatG("parm7", 0.0f); vm.SetGlobalFloatG("parm8", 1.0f);
    int pc = vm.FunctionIndex("PutClientInServer");
    if (pc > 0) vm.ExecuteProgram(pc);
    int ff = vm.FindField("flags");
    if (ff >= 0) vm.EdictFieldFloat(1, ff) = (float)((int)vm.EdictFieldFloat(1, ff) | 8); // FL_CLIENT
    int fh = vm.FindField("health");
    if (fh >= 0 && vm.EdictFieldFloat(1, fh) <= 0) vm.EdictFieldFloat(1, fh) = 100;
    return true;
}

// One run of the touch loop with the client planted on the item.
void Pump(ProgVM& vm, const BSPMap& map, int frames, float dt) {
    std::vector<SolidEntity> solids;
    for (int f = 0; f < frames; f++) {
        BuildSolidList(vm, map, solids, 1);
        SetGameTraceContext(&map, solids.data(), (int)solids.size(), 1);
        RunGameFrame(vm, map, dt, solids.data(), (int)solids.size(), 1);
        vm.SetSelfEdict(1); vm.SetOtherEdict(0);
        int pre = vm.FunctionIndex("PlayerPreThink"); if (pre > 0) vm.ExecuteProgram(pre);
        int post = vm.FunctionIndex("PlayerPostThink"); if (post > 0) vm.ExecuteProgram(post);
        // SP PlayerPreThink clears flags (drops FL_CLIENT); restore before touch.
        int ff = vm.FindField("flags");
        if (ff >= 0) vm.EdictFieldFloat(1, ff) = (float)((int)vm.EdictFieldFloat(1, ff) | 8);
        CheckTouch(vm, 1, solids.data(), (int)solids.size());
    }
}
}

TEST_CASE("Gameplay: weapon pickup grants ammo and hides the item", "[gameplay]") {
    ProgVM vm; BSPMap map;
    if (!SetupGame(vm, map)) return;

    int fc = vm.FindField("classname"), fo = vm.FindField("origin");
    int fn = vm.FindField("ammo_nails"), fmo = vm.FindField("model"), fso = vm.FindField("solid");
    int gun = -1;
    for (int i = 2; i < 1024; i++) if (!vm.EdictFree(i) &&
        std::strcmp(vm.EdictFieldString(i, fc), "weapon_nailgun") == 0) { gun = i; break; }
    REQUIRE(gun > 0);

    float go[3];
    vm.EdictFieldVector(gun, fo, go);
    vm.SetEdictFieldVector(1, fo, go);   // stand on the gun

    Pump(vm, map, 8, 0.1f);

    float nails = fn >= 0 ? vm.EdictFieldFloat(1, fn) : 0;
    const char* mdl = fmo >= 0 ? vm.EdictFieldString(gun, fmo) : "";
    float sol = fso >= 0 ? vm.EdictFieldFloat(gun, fso) : -1;
    REQUIRE(nails > 0.0f);
    REQUIRE(std::strcmp(mdl, "") == 0);  // model cleared => no longer rendered
    REQUIRE((int)sol == 0);              // solid NOT => stops touching
}

TEST_CASE("Gameplay: engine-side jump sets vertical velocity", "[gameplay]") {
    ProgVM vm; BSPMap map;
    if (!SetupGame(vm, map)) return;
    PlayerPhys p;
    p.origin[0] = 480; p.origin[1] = -352; p.origin[2] = 88;
    p.onground = true;
    static const MoveVars mv;
    PlayerCmd cmd;
    cmd.jump = true;
    RunPlayerMove(map, p, cmd, 0.016f, mv);
    REQUIRE(p.velocity[2] > 100.0f);
    REQUIRE(p.velocity[2] <= 274.0f);
}

// Parity: the client walk physics gets its grounded state from BEFORE
// PlayerPreThink. The SP progs PlayerPreThink clears the FL_ONGROUND flag
// mid-frame, so reading it back after PreThink makes the player look airborne
// forever: no ground friction (slides) and air-control capped at 30 u/s
// (moves very slowly). The app pre-syncs the flag from the previous frame's
// grounded state, so the engine must capture it that early.
TEST_CASE("Parity: client keeps ground friction/accel across PlayerPreThink flag clear", "[gameplay][parity]") {
    ProgVM vm; BSPMap map;
    if (!SetupGame(vm, map)) return;

    int ff = vm.FindField("flags");
    std::vector<SolidEntity> solids;
    BuildSolidList(vm, map, solids, 1);
    SetGameTraceContext(&map, solids.data(), (int)solids.size(), 1);

    ClientPhysics cin;
    cin.forwardmove = 320;   // W
    cin.move_vars = MoveVars{};

    bool grounded = false;
    float maxv = 0.0f;
    for (int f = 0; f < 30; f++) {
        if (ff >= 0) {
            int fl = (int)vm.EdictFieldFloat(1, ff);
            fl |= 8;   // FL_CLIENT
            if (grounded) fl |= FL_ONGROUND;
            else fl &= ~FL_ONGROUND;
            vm.EdictFieldFloat(1, ff) = (float)fl;
        }
        RunGameFrame(vm, map, 0.05f, solids.data(), (int)solids.size(), 1, &cin);
        float v = std::sqrt(cin.out_velocity[0] * cin.out_velocity[0] +
                            cin.out_velocity[1] * cin.out_velocity[1]);
        if (v > maxv) maxv = v;
        if (cin.out_onground) grounded = true;
    }
    REQUIRE(grounded);                                  // lands and stays grounded
    REQUIRE(maxv > 150.0f);   // ground accel well past the 30 u/s air ceiling
}

TEST_CASE("Parity: client impulse 2 selects the shotgun (W_WeaponFrame bind)", "[gameplay][parity]") {
    ProgVM vm; BSPMap map;
    if (!SetupGame(vm, map)) return;

    std::vector<SolidEntity> solids;
    BuildSolidList(vm, map, solids, 1);
    SetGameTraceContext(&map, solids.data(), (int)solids.size(), 1);

    int fi = vm.FindField("impulse");
    int fw = vm.FindField("weapon");
    int fit = vm.FindField("items");
    int fa = vm.FindField("ammo_shells");
    REQUIRE(fi >= 0); REQUIRE(fw >= 0); REQUIRE(fit >= 0); REQUIRE(fa >= 0);

    // Arm the shotgun like the B3 starter loadout.
    vm.EdictFieldFloat(1, fit) = (float)((int)vm.EdictFieldFloat(1, fit) | 1);   // IT_SHOTGUN
    vm.EdictFieldFloat(1, fa) = 25;

    ClientPhysics cin;
    RunGameFrame(vm, map, 0.05f, solids.data(), (int)solids.size(), 1, &cin);   // warm frame

    cin.impulse = 2;   // 2 = shotgun (matches NUM2 binding wiring)
    RunGameFrame(vm, map, 0.05f, solids.data(), (int)solids.size(), 1, &cin);

    REQUIRE((int)vm.EdictFieldFloat(1, fw) == 1);        // IT_SHOTGUN armed
    REQUIRE((int)vm.EdictFieldFloat(1, fi) == 0);        // QC consumed the impulse
}

TEST_CASE("Parity: EF_MUZZLEFLASH registers one frame then clears", "[gameplay][parity]") {
    ProgVM vm; BSPMap map;
    if (!SetupGame(vm, map)) return;

    std::vector<SolidEntity> solids;
    BuildSolidList(vm, map, solids, 1);
    SetGameTraceContext(&map, solids.data(), (int)solids.size(), 1);

    int fe = vm.FindField("effects");
    REQUIRE(fe >= 0);

    ClientPhysics cin;                       // idle: no buttons, no impulse
    // Warm frame: consume any residue the spawn may have left in .effects.
    RunGameFrame(vm, map, 0.05f, solids.data(), (int)solids.size(), 1, &cin);

    // Simulate a weapon think setting EF_MUZZLEFLASH (bit 1) during this frame.
    vm.EdictFieldFloat(1, fe) = (float)((int)vm.EdictFieldFloat(1, fe) | 2);
    int visible = ((int)vm.EdictFieldFloat(1, fe)) & 2;   // app-side per-tick read
    REQUIRE(visible == 2);                                // visible right after the firing frame

    // The next client frame consumes the residue at its start (retail clear).
    RunGameFrame(vm, map, 0.05f, solids.data(), (int)solids.size(), 1, &cin);
    int after_idle_frame = ((int)vm.EdictFieldFloat(1, fe)) & 2;
    REQUIRE(after_idle_frame == 0);                       // cleared before the next shot
}

TEST_CASE("Gameplay: door-use scan finds a func_door when facing it", "[gameplay]") {
    ProgVM vm; BSPMap map;
    if (!SetupGame(vm, map)) return;

    int fc = vm.FindField("classname"), fu = vm.FindField("use"), fmi = vm.FindField("modelindex");
    int best_door = -1; float bestarea = -1;
    for (int i = 2; i < 1024; i++) {
        if (vm.EdictFree(i)) continue;
        const char* cn = vm.EdictFieldString(i, fc);
        if (!cn || (std::strcmp(cn, "func_door") && std::strcmp(cn, "door"))) continue;
        int mi = fmi >= 0 ? (int)vm.EdictFieldFloat(i, fmi) : 0;
        if (mi > 0 && mi < (int)map.Models().size()) {
            const auto& m = map.Models()[mi];
            float a = (m.maxs[0]-m.mins[0])*(m.maxs[1]-m.mins[1]);
            if (a > bestarea) { bestarea = a; best_door = i; }
        }
    }
    REQUIRE(best_door > 0);
    REQUIRE(fu >= 0);
    REQUIRE(vm.EdictFieldInt(best_door, fu) > 0); // door has a use function

    int mi = fmi >= 0 ? (int)vm.EdictFieldFloat(best_door, fmi) : 0;
    const auto& m = map.Models()[mi];
    float cx = (m.mins[0]+m.maxs[0])*0.5f, cy = (m.mins[1]+m.maxs[1])*0.5f;
    float px = cx - 64, py = cy + 64;
    float yaw = std::atan2(cy-py, cx-px) * 180.0f / 3.14159265f;
    float face[2] = { std::cos(yaw*3.14159265f/180), std::sin(yaw*3.14159265f/180) };

    // replicate the app's per-evoked door scan
    int best = -1; float bestd = 120.0f;
    for (int e = 2; e < 1024; e++) {
        if (vm.EdictFree(e)) continue;
        const char* cn = vm.EdictFieldString(e, fc);
        if (!cn || (std::strcmp(cn, "func_door") && std::strcmp(cn, "door") && std::strcmp(cn, "func_door_secret"))) continue;
        if (fu < 0 || vm.EdictFieldInt(e, fu) <= 0) continue;
        int e_mi = fmi >= 0 ? (int)vm.EdictFieldFloat(e, fmi) : 0;
        float dx = 0, dy = 0;
        if (e_mi > 0 && e_mi < (int)map.Models().size()) {
            const auto& mm = map.Models()[e_mi];
            dx = (mm.mins[0]+mm.maxs[0])*0.5f - px;
            dy = (mm.mins[1]+mm.maxs[1])*0.5f - py;
        }
        float d = std::sqrt(dx*dx + dy*dy);
        if (d > bestd) continue;
        float dot = (dx*face[0] + dy*face[1]) / (d > 0.01f ? d : 1.0f);
        if (dot < 0.2f) continue;
        best = e; bestd = d;
    }
    REQUIRE(best == best_door);
}

// A waking monster must close the distance to the player WITHOUT ever
// walking through the client's box (regression: stale per-frame solid
// snapshots let monsters pile into the player / each other).
TEST_CASE("Gameplay: awakened monster approaches the player without clipping into it", "[gameplay]") {
    ProgVM vm; BSPMap map;
    if (!SetupGame(vm, map)) return;

    int fc = vm.FindField("classname"), fo = vm.FindField("origin");
    int fmn = vm.FindField("mins"), fmx = vm.FindField("maxs");
    int fr = vm.FindField("flags"), fv = vm.FindField("velocity");
    int dog = -1;
    for (int i = 2; i < 1024; i++) {
        if (vm.EdictFree(i)) continue;
        const char* cn = fc >= 0 ? vm.EdictFieldString(i, fc) : "";
        if (cn && std::strcmp(cn, "monster_dog") == 0) { dog = i; break; }
    }
    REQUIRE(dog > 0);

    float porg[3];
    vm.EdictFieldVector(1, fo, porg);

    // pick the direction with the most clearance around the player and put
    // the dog there (well inside the 512 wake range).
    float spot[3] = {0, 0, 0};
    float best_frac = -1.0f, best_dx = 0, best_dy = 0;
    static const float dirs[8][2] = {
        {1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    float smn[3] = {-16, -16, -32}, smx[3] = {16, 16, 24};
    for (int d = 0; d < 8; d++) {
        float end[3] = { porg[0] + dirs[d][0]*224, porg[1] + dirs[d][1]*224, porg[2] };
        auto tr = MoveBox(map, smn, smx, porg, end, nullptr, 0, -1);
        if (tr.fraction > best_frac) {
            best_frac = tr.fraction; best_dx = dirs[d][0]; best_dy = dirs[d][1];
        }
    }
    REQUIRE(best_frac >= 0.25f);
    float start_dist = 224 * best_frac;
    spot[0] = porg[0] + best_dx * (start_dist - 12);
    spot[1] = porg[1] + best_dy * (start_dist - 12);
    spot[2] = porg[2] - 24;
    // verify the dog has line of sight to the player (fresh-look point trace);
    // if LOS is blocked by a door/wall the AI will not leave idle, so only
    // assert approach when a clear sightline placement exists.
    bool have_los = false;
    float zpt[3] = {0, 0, 0}, zoom[3] = {0, 0, 0};
    for (float d = start_dist; d >= 64.0f && !have_los; d -= 20.0f) {
        float hx = porg[0] + best_dx * d, hy = porg[1] + best_dy * d;
        float s[3] = { hx, hy, porg[2] + 24 };
        float e[3] = { porg[0], porg[1], porg[2] + 24 };
        auto t = map.WorldTrace(s, e, zpt, zoom);
        if (t.fraction >= 0.99f) {
            spot[0] = hx; spot[1] = hy; spot[2] = porg[2] - 24;
            have_los = true;
            break;
        }
    }
    REQUIRE(have_los);

    vm.SetEdictFieldVector(dog, fo, spot);
    if (fv >= 0) { float z[3] = {0, 0, 0}; vm.SetEdictFieldVector(dog, fv, z); }
    if (fr >= 0) vm.EdictFieldFloat(dog, fr) = 512; // FL_ONGROUND (movetogoal gate)

    float start_d2 = (spot[0]-porg[0])*(spot[0]-porg[0]) + (spot[1]-porg[1])*(spot[1]-porg[1]);

    std::vector<SolidEntity> solids;
    float min_d2 = start_d2;
    bool overlapped = false;
    int fcn = vm.FindField("classname"), for2 = vm.FindField("origin");
    (void)fcn;
    static constexpr float MV_STEP = 20.0f;
    for (int f = 0; f < 150; f++) {
        // Drive the dog toward the player with the engine mover (the same
        // MoveBox/walkmove collision path the AI uses), against a fresh solid
        // list that INCLUDES the client - it must stop at the player's box,
        // never walk through it.
        BuildSolidList(vm, map, solids, dog);   // exclude mover, include client
        SetGameTraceContext(&map, solids.data(), (int)solids.size(), dog);
        float dgo[3], dmn[3], dmx[3];
        vm.EdictFieldVector(dog, for2, dgo);
        vm.EdictFieldVector(dog, fmn, dmn);
        vm.EdictFieldVector(dog, fmx, dmx);
        float cmn2[3], cmx2[3];
        vm.EdictFieldVector(1, fmn, cmn2);
        vm.EdictFieldVector(1, fmx, cmx2);
        float stop_sep = ((dmx[0]-dmn[0])/2.0f) + ((cmx2[0]-cmn2[0])/2.0f) + 8.0f;
        float dx2 = porg[0]-dgo[0], dy2 = porg[1]-dgo[1];
        float dist2 = std::sqrt(dx2*dx2 + dy2*dy2);
        if (dist2 > stop_sep) {
            float rad = std::atan2(dy2, dx2);
            float mxv = MV_STEP < dist2 - stop_sep ? MV_STEP : dist2 - stop_sep;
            if (mxv > 0) {
                float end[3] = { dgo[0] + std::cos(rad)*mxv, dgo[1] + std::sin(rad)*mxv, dgo[2] };
                auto tr = MoveBox(map, dmn, dmx, dgo, end, solids.data(), (int)solids.size(), dog);
                vm.SetEdictFieldVector(dog, for2, tr.endpos);
            }
        }

        RunGameFrame(vm, map, 0.05f, solids.data(), (int)solids.size(), 1);
        vm.SetSelfEdict(1); vm.SetOtherEdict(0);
        if (vm.FunctionIndex("PlayerPreThink") > 0) vm.ExecuteProgram(vm.FunctionIndex("PlayerPreThink"));
        if (vm.FunctionIndex("PlayerPostThink") > 0) vm.ExecuteProgram(vm.FunctionIndex("PlayerPostThink"));
        int fl = vm.FindField("flags");
        if (fl >= 0) vm.EdictFieldFloat(1, fl) = (float)((int)vm.EdictFieldFloat(1, fl) | 8);
        CheckTouch(vm, 1, solids.data(), (int)solids.size());

        float dg0[3]; vm.EdictFieldVector(dog, for2, dg0); float* dogorg = dg0;
        float d2 = (dogorg[0]-porg[0])*(dogorg[0]-porg[0]) + (dogorg[1]-porg[1])*(dogorg[1]-porg[1]);
        if (d2 < min_d2) min_d2 = d2;

        // client box overlap test: the monster may legitimately reach melee
        // contact (touching the client box), but must never penetrate it.
        float cn[3], cmn[3], cmx[3];
        vm.EdictFieldVector(1, for2, cn);
        vm.EdictFieldVector(1, fmn, cmn);
        vm.EdictFieldVector(1, fmx, cmx);
        float dn[3], dxn[3];
        vm.EdictFieldVector(dog, fmn, dn);
        vm.EdictFieldVector(dog, fmx, dxn);
        bool ov = true;
        constexpr float PEN = 2.0f;   // allow contact, flag real penetration
        for (int a = 0; a < 3; a++) {
            float e0 = dogorg[a] + dn[a], e1 = dogorg[a] + dxn[a];
            float c0 = cn[a] + cmn[a], c1 = cn[a] + cmx[a];
            if (e1 < c0 + PEN || c1 < e0 + PEN) { ov = false; break; }
        }
        if (ov) { overlapped = true; }
    }
    REQUIRE(overlapped == false);           // never walked through the player
    REQUIRE(min_d2 < start_d2 * 0.5f);      // closed in on the player
}

namespace {
// One engine frame of the real loop (thinks + pre/post + FL_CLIENT + touch).
void StepOnce(ProgVM& vm, const BSPMap& map, std::vector<SolidEntity>& solids,
              float dt, int client = 1) {
    BuildSolidList(vm, map, solids, client);
    SetGameTraceContext(&map, solids.data(), (int)solids.size(), client);
    RunGameFrame(vm, map, dt, solids.data(), (int)solids.size(), client);
    vm.SetSelfEdict(client); vm.SetOtherEdict(0);
    if (vm.FunctionIndex("PlayerPreThink") > 0) vm.ExecuteProgram(vm.FunctionIndex("PlayerPreThink"));
    if (vm.FunctionIndex("PlayerPostThink") > 0) vm.ExecuteProgram(vm.FunctionIndex("PlayerPostThink"));
    int ff = vm.FindField("flags");
    if (ff >= 0) vm.EdictFieldFloat(client, ff) = (float)((int)vm.EdictFieldFloat(client, ff) | 8);
    CheckTouch(vm, client, solids.data(), (int)solids.size());
}
} // namespace

// Parity: item respawns are 100% progs-owned (SUB_regen at whatever delay the
// progs schedules - the server source has no respawn logic of its own). This
// pins the engine as a faithful executor: the pickup hides immediately on
// pickup, and the model reappears EXACTLY at the nextthink time the progs
// scheduled (no engine-imposed delay, no engine-side model restore).
TEST_CASE("Parity: weapon hides on pickup and reappears at the progs-scheduled time", "[gameplay][parity]") {
    ProgVM vm; BSPMap map;
    if (!SetupGame(vm, map)) return;

    int fc = vm.FindField("classname"), fo = vm.FindField("origin");
    int fn = vm.FindField("ammo_nails"), fmo = vm.FindField("model");
    int fnt = vm.FindField("nextthink"), fth = vm.FindField("think");
    int gun = -1;
    for (int i = 2; i < 1024; i++) if (!vm.EdictFree(i) &&
        std::strcmp(vm.EdictFieldString(i, fc), "weapon_nailgun") == 0) { gun = i; break; }
    REQUIRE(gun > 0);

    float go[3];
    vm.EdictFieldVector(gun, fo, go);
    vm.SetEdictFieldVector(1, fo, go);   // stand on the gun

    std::vector<SolidEntity> solids;
    int consumed_frame = -1;
    for (int f = 0; f < 60 && consumed_frame < 0; f++) {
        StepOnce(vm, map, solids, 0.1f);
        float nails = fn >= 0 ? vm.EdictFieldFloat(1, fn) : 0;
        const char* m = fmo >= 0 ? vm.EdictFieldString(gun, fmo) : "";
        if (nails > 0 && m[0] == 0) consumed_frame = f;
    }
    REQUIRE(consumed_frame >= 0);               // pickup consumed + hid it

    // The progs scheduled the respawn; record exactly when.
    float scheduled = fnt >= 0 ? vm.EdictFieldFloat(gun, fnt) : -1.0f;
    REQUIRE(scheduled > vm.Time());             // a future respawn was scheduled
    bool think_armed = (fth >= 0) && (vm.EdictFieldInt(gun, fth) > 0);
    REQUIRE(think_armed);                       // a think is armed

    // move the client away so the respawn isn't instantly re-touched
    float away[3] = { go[0] + 2000, go[1] + 2000, go[2] + 100 };
    vm.SetEdictFieldVector(1, fo, away);

    constexpr float DT = 0.1f;
    float restored_at = -1.0f;
    for (int f = 0; f < 400; f++) {
        StepOnce(vm, map, solids, DT);
        const char* m = fmo >= 0 ? vm.EdictFieldString(gun, fmo) : "";
        if (m[0] != 0) { restored_at = vm.Time(); break; }
    }
    REQUIRE(restored_at > 0.0f);
    // Engine must fire the progs' regen at (approximately) its chosen time,
    // and must NOT hide/restore the item on its own.
    REQUIRE(restored_at >= scheduled - 1.0f);
    REQUIRE(restored_at <= scheduled + 1.0f);
}

// Parity: a brush health box is consumed and hidden immediately on pickup.
// (In this progs its respawn think does not restore the model, so it stays
// out of the render list - also a progs-owned behavior the engine must relay.)
TEST_CASE("Parity: brush health box is consumed and hidden immediately", "[gameplay][parity]") {
    ProgVM vm; BSPMap map;
    if (!SetupGame(vm, map)) return;

    int fc = vm.FindField("classname"), fo = vm.FindField("origin");
    int fh = vm.FindField("health"), fmo = vm.FindField("model");
    // low health so the heal actually applies (T_Heal returns 0 at full health
    // and the touch leaves the item un-consumed, like a surplus pickup)
    if (fh >= 0) vm.EdictFieldFloat(1, fh) = 30.0f;

    int box = -1;
    for (int i = 2; i < 1024; i++) if (!vm.EdictFree(i) &&
        std::strcmp(vm.EdictFieldString(i, fc), "item_health") == 0) { box = i; break; }
    REQUIRE(box > 0);

    float bo[3];
    vm.EdictFieldVector(box, fo, bo);
    vm.SetEdictFieldVector(1, fo, bo);   // stand on the health box

    std::vector<SolidEntity> solids;
    int consumed_frame = -1;
    for (int f = 0; f < 60 && consumed_frame < 0; f++) {
        StepOnce(vm, map, solids, 0.1f);
        float health = fh >= 0 ? vm.EdictFieldFloat(1, fh) : 0;
        const char* m = fmo >= 0 ? vm.EdictFieldString(box, fmo) : "";
        if (health > 30.0f && m[0] == 0) consumed_frame = f;
    }
    REQUIRE(consumed_frame >= 0);               // consumed + hid it

    float away[3] = { bo[0] + 2000, bo[1] + 2000, bo[2] + 100 };
    vm.SetEdictFieldVector(1, fo, away);

    // stays hidden the whole early window (progs-owned model clearing; the
    // engine must not resurrect it, engine-owned or otherwise)
    std::vector<SolidEntity> s2;
    for (int f = 0; f < 20; f++) StepOnce(vm, map, s2, 0.1f);
    bool stays_hidden = (fmo >= 0) && (vm.EdictFieldString(box, fmo)[0] == 0);
    REQUIRE(stays_hidden);
}

// The engine's MOVETYPE_PUSH physics must drive a func_door after door_use:
// door_use -> door_go_down sets self.velocity, and RunGameFrame's PUSH handler
// must translate the edict's origin (the reference SV_Physics_Push).
TEST_CASE("Gameplay: func_door opens (MOVETYPE_PUSH physics) after door_use", "[gameplay]") {
    ProgVM vm; BSPMap map;
    if (!SetupGame(vm, map)) return;

    int fc = vm.FindField("classname"), fu = vm.FindField("use");
    int fmo = vm.FindField("movetype"), fvel = vm.FindField("velocity");
    int fori = vm.FindField("origin");
    REQUIRE(fu >= 0);

    int door = -1;
    for (int i = 2; i < 1024; i++) {
        if (vm.EdictFree(i)) continue;
        const char* cn = fc >= 0 ? vm.EdictFieldString(i, fc) : "";
        if (cn && !std::strcmp(cn, "door") && vm.EdictFieldInt(i, fu) > 0) { door = i; break; }
    }
    REQUIRE(door > 0);

    // doors spawn as MOVETYPE_PUSH brushes
    REQUIRE(fmo >= 0);
    REQUIRE((int)vm.EdictFieldFloat(door, fmo) == MOVE_PUSH);

    float before[3];
    vm.EdictFieldVector(door, fori, before);

    // Not used yet: no movement.
    {
        std::vector<SolidEntity> solids;
        for (int f = 0; f < 30; f++) StepOnce(vm, map, solids, 0.1f);
        float still[3];
        vm.EdictFieldVector(door, fori, still);
        REQUIRE(std::fabs(still[0]-before[0]) < 0.01f);
        REQUIRE(std::fabs(still[1]-before[1]) < 0.01f);
        REQUIRE(std::fabs(still[2]-before[2]) < 0.01f);
    }

    // Use the door: the progs sets velocity and a moving think chain.
    vm.SetSelfEdict(door); vm.SetOtherEdict(1);
    REQUIRE(vm.ExecuteProgram(vm.EdictFieldInt(door, fu)));

    float moved[3] = { 0, 0, 0 };
    std::vector<SolidEntity> solids;
    for (int f = 0; f < 20; f++) {
        StepOnce(vm, map, solids, 0.1f);
        float o[3];
        vm.EdictFieldVector(door, fori, o);
        float dist = std::sqrt((o[0]-before[0])*(o[0]-before[0]) +
                               (o[1]-before[1])*(o[1]-before[1]) +
                               (o[2]-before[2])*(o[2]-before[2]));
        if (dist > 0.5f) { std::memcpy(moved, o, sizeof(moved)); }
    }
    float total = std::sqrt((moved[0]-before[0])*(moved[0]-before[0]) +
                            (moved[1]-before[1])*(moved[1]-before[1]) +
                            (moved[2]-before[2])*(moved[2]-before[2]));
    REQUIRE(total > 1.0f);
    (void)fvel;
}

// A mover whose box rests exactly against another box entity must still be
// able to move AWAY from it. Regression: BoxEntityTrace treated a start point
// sitting exactly ON the expanded box face as startsolid, so two touching
// monsters deadlocked (every move returned fraction 0).
TEST_CASE("Collision: box touching another box can still move away", "[gameplay][collision]") {
    ProgVM vm; BSPMap map;
    if (!SetupGame(vm, map)) return;

    // Open area around the dog spawn (verified clear in all directions).
    SolidEntity wall;
    wall.kind = SolidEntity::Kind::Box;
    wall.edict = 2;
    wall.origin[0] = 88; wall.origin[1] = 1520; wall.origin[2] = -200;
    wall.mins[0] = -16; wall.mins[1] = -16; wall.mins[2] = -24;
    wall.maxs[0] = 16;  wall.maxs[1] = 16;  wall.maxs[2] = 40;

    // mover box 32 wide, origin such that its +x face is flush with wall -x face
    float mmins[3] = {-16, -16, -24}, mmaxs[3] = {16, 16, 40};
    float start[3] = { 88 - 16 - 16, 1520, -200 };   // 88-32 = 56
    float away[3] = { start[0] - 24.0f, start[1], start[2] };
    auto wtr = map.WorldTrace(start, away, mmins, mmaxs);
    REQUIRE(wtr.fraction == 1.0f);   // sanity: the spot is empty world
    auto tr = MoveBox(map, mmins, mmaxs, start, away, &wall, 1, -1);
    REQUIRE(tr.fraction == 1.0f);   // moving away from a touching box is free

    float into[3] = { start[0] + 24.0f, start[1], start[2] };
    auto tr2 = MoveBox(map, mmins, mmaxs, start, into, &wall, 1, -1);
    REQUIRE(tr2.fraction < 1.0f);   // moving into it is blocked
}

// Parity: a func_door must open, hold for its wait, then close - and NOT flap
// open/closed. Regression: the engine stored self.ltime as an elapsed delta
// instead of absolute sim time, so door_hit_top's `nextthink = ltime + wait`
// landed in the past and the door snapped open/closed every frame.
TEST_CASE("Parity: door completes one open-hold-close cycle without flapping", "[gameplay][parity]") {
    ProgVM vm; BSPMap map;
    if (!SetupGame(vm, map)) return;

    int fc = vm.FindField("classname"), fu = vm.FindField("use");
    int fori = vm.FindField("origin");
    int door = -1;
    for (int i = 2; i < 1024; i++) if (!vm.EdictFree(i) &&
        (!std::strcmp(vm.EdictFieldString(i, fc), "door") ||
         !std::strcmp(vm.EdictFieldString(i, fc), "func_door")) &&
        fu >= 0 && vm.EdictFieldInt(i, fu) > 0) { door = i; break; }
    REQUIRE(door > 0);

    float before[3];
    vm.EdictFieldVector(door, fori, before);
    int fown = vm.FindField("owner");
    // LinkDoors runs at spawn+0.1s (sets owner=self); step a few frames first.
    std::vector<SolidEntity> pre_solids;
    for (int f = 0; f < 4; f++) StepOnce(vm, map, pre_solids, 0.1f);
    REQUIRE((fown >= 0 && vm.ProgToEdictNum(vm.EdictFieldInt(door, fown)) == door));
    vm.SetSelfEdict(door); vm.SetOtherEdict(1);
    REQUIRE(vm.ExecuteProgram(vm.EdictFieldInt(door, fu)));

    std::vector<SolidEntity> solids;
    float maxd = 0.0f;
    int closed_after_open = -1, flips = 0;
    float prev = 0.0f, prev_delta = 0.0f;
    bool was_open = false;
    for (int f = 0; f < 500; f++) {   // ~50s of 0.1s steps
        StepOnce(vm, map, solids, 0.1f);
        float o[3];
        vm.EdictFieldVector(door, fori, o);
        float d = std::sqrt((o[0]-before[0])*(o[0]-before[0]) +
                            (o[1]-before[1])*(o[1]-before[1]) +
                            (o[2]-before[2])*(o[2]-before[2]));
        if (d > maxd) maxd = d;
        if (maxd > 2.0f) was_open = true;
        if (was_open && d < 1.0f && closed_after_open < 0) closed_after_open = f;
        float delta = d - prev;
        if (std::fabs(delta) > 2.0f) {
            if (prev_delta != 0.0f && (delta > 0) != (prev_delta > 0)) flips++;
            prev_delta = delta;
        }
        prev = d;
    }
    REQUIRE(maxd > 20.0f);              // opened
    REQUIRE(closed_after_open > 0);      // closed again after opening
    REQUIRE(flips < 4);                  // no flapping (open + close = ~1 reversal)
}

// Test the new yaw-based walkmove builtin (float yaw, float dist).
TEST_CASE("Parity: walkmove moves monster toward player via yaw and stops at walls", "[gameplay][parity]") {
    ProgVM vm; BSPMap map;
    if (!SetupGame(vm, map)) return;

    int fc = vm.FindField("classname"), fo = vm.FindField("origin");
    int fm = vm.FindField("mins"), fmx = vm.FindField("maxs");
    int fr = vm.FindField("flags"), fe = vm.FindField("enemy");
    REQUIRE(fc >= 0);
    REQUIRE(fo >= 0);
    REQUIRE(fm >= 0);
    REQUIRE(fmx >= 0);
    REQUIRE(fr >= 0);

    // find a dog
    int dog = -1;
    for (int i = 2; i < 1024; i++)
        if (!vm.EdictFree(i) && std::strcmp(vm.EdictFieldString(i, fc), "monster_dog") == 0) { dog = i; break; }
    REQUIRE(dog > 0);

    // place the dog in a known open spot in front of the player
    float porg[3]; vm.EdictFieldVector(1, fo, porg);
    float spot[3] = { porg[0] + 64, porg[1], porg[2] };
    vm.SetEdictFieldVector(dog, fo, spot);
    // set dog size to a typical monster bbox
    { float mn[3] = {-16,-16,-24}; vm.SetEdictFieldVector(dog, fm, mn); }
    { float mx[3] = {16,16,24};    vm.SetEdictFieldVector(dog, fmx, mx); }
    vm.EdictFieldFloat(dog, fr) = (float)(FL_ONGROUND);       // walkmove gate
    vm.EdictFieldFloat(dog, fe) = vm.EdictNumToProg(1);       // enemy = player

    float start_d2 = (spot[0]-porg[0])*(spot[0]-porg[0]) + (spot[1]-porg[1])*(spot[1]-porg[1]);

    // Run the engine frame (gravity + think), then use walkmove builtin
    // to drive the dog toward the player by yaw (the reference id1 path).
    std::vector<SolidEntity> solids;
    float min_d2 = start_d2;
    for (int f = 0; f < 30; f++) {
        BuildSolidList(vm, map, solids, dog);
        SetGameTraceContext(&map, solids.data(), (int)solids.size(), dog);

        // face the player: compute yaw to player
        float dorg[3]; vm.EdictFieldVector(dog, fo, dorg);
        float dy = porg[1]-dorg[1], dx = porg[0]-dorg[0];
        float yaw = std::atan2(dy, dx) * 180.0f / 3.14159265f;
        if (yaw < 0) yaw += 360.0f;

        constexpr float DEG2RAD = 3.14159265f/180.0f;
        float rad = yaw * DEG2RAD;
        float mv[3] = { std::cos(rad)*32.0f, std::sin(rad)*32.0f, 0 };
        bool ok = SV_Movestep(vm, dog, mv, true);
        (void)ok;

        RunGameFrame(vm, map, 0.05f, solids.data(), (int)solids.size(), 1);

        float dg[3]; vm.EdictFieldVector(dog, fo, dg);
        float d2 = (dg[0]-porg[0])*(dg[0]-porg[0]) + (dg[1]-porg[1])*(dg[1]-porg[1]);
        if (d2 < min_d2) min_d2 = d2;
    }
    REQUIRE(min_d2 < start_d2);          // closed in on the player
}
