#include <catch2/catch_test_macros.hpp>
#include "vm/prog_vm.hpp"
#include "vm/prog_builtins.hpp"
#include "engine/bsp.hpp"
#include "engine/move.hpp"
#include "engine/gameframe.hpp"
#include "filesystem/pak_archive.hpp"
#include <cstring>
#include <cstdio>
#include <vector>

using namespace zq::engine;
using zq::vm::ProgVM;

namespace {

struct World {
    ProgVM vm;
    BSPMap map;
    zq::fs::PAKArchive pak;

    bool Boot() {
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
        vm.AllocateEdicts(2); // world (0) + client (1), like the app
        vm.SetTime(0); vm.SetFrametime(0);
        vm.SetBrushBounds([&](int mi, float mn[3], float mx[3]) {
            const BSPModel& mod = map.Models()[mi];
            mn[0]=mod.origin[0]+mod.mins[0]; mn[1]=mod.origin[1]+mod.mins[1]; mn[2]=mod.origin[2]+mod.mins[2];
            mx[0]=mod.origin[0]+mod.maxs[0]; mx[1]=mod.origin[1]+mod.maxs[1]; mx[2]=mod.origin[2]+mod.maxs[2];
            return true;
        });
        // app-style droptofloor (#34) / pointcontents (#41) overrides that use
        // the loaded map directly (the game-builtin versions need a frame context).
        vm.RegisterBuiltin(34, [&](ProgVM& v) {
            int fo=v.FindField("origin"), mnf=v.FindField("mins"), mxf=v.FindField("maxs");
            int gs=v.FindGlobal("self");
            int s=v.ProgToEdictNum(v.Global(gs));
            if (fo<0||mnf<0||mxf<0||s<0){v.SetReturnFloat(0);return;}
            float o[3],mn[3],mx[3];
            v.EdictFieldVector(s,fo,o); v.EdictFieldVector(s,mnf,mn); v.EdictFieldVector(s,mxf,mx);
            float e[3]={o[0],o[1],o[2]-256};
            auto tr=MoveBox(map,mn,mx,o,e,nullptr,0,-1);
            if (tr.fraction==1.0f||tr.allsolid){v.SetReturnFloat(0);return;}
            v.SetEdictFieldVector(s,fo,tr.endpos);
            v.SetReturnFloat(1);
        });
        vm.RegisterBuiltin(41, [&](ProgVM& v) {
            float p[3]; v.ParmVector(0,p);
            v.SetReturnFloat((float)map.PointContents(p));
        });
        vm.LoadEntities(map.EntityString().c_str());

        // single-player globals + client setup, mirroring the app boot.
        vm.SetGlobalFloatG("deathmatch",0.0f); vm.SetGlobalFloatG("coop",0.0f);
        vm.SetGlobalFloatG("teamplay",0.0f);   vm.SetGlobalFloatG("skill",1.0f);
        vm.SetGlobalStringG("mapname","e1m1");
        vm.SetGlobalFloatG("parm1",0.0f); vm.SetGlobalFloatG("parm2",0.0f);
        vm.SetGlobalFloatG("parm3",0.0f); vm.SetGlobalFloatG("parm4",0.0f);
        vm.SetGlobalFloatG("parm5",0.0f); vm.SetGlobalFloatG("parm6",0.0f);
        vm.SetGlobalFloatG("parm7",0.0f); vm.SetGlobalFloatG("parm8",1.0f);
        int fc=vm.FindField("classname");
        if (fc>=0) vm.EdictFieldInt(1,fc)=vm.InternString("player");
        vm.SetSelfEdict(1); vm.SetOtherEdict(0);
        int picis=vm.FunctionIndex("PutClientInServer");
        if (picis>0) vm.ExecuteProgram(picis);
        int fh=vm.FindField("health");
        if (fh>=0&&vm.EdictFieldFloat(1,fh)<=0.0f) vm.EdictFieldFloat(1,fh)=100.0f;
        int ffl=vm.FindField("flags");
        if (ffl>=0) vm.EdictFieldFloat(1,ffl)=(float)((int)vm.EdictFieldFloat(1,ffl)|8);
        int fit=vm.FindField("items");
        if (fit>=0){int it=(int)vm.EdictFieldFloat(1,fit); vm.EdictFieldFloat(1,fit)=(float)(it|(1|4096));}
        int fa=vm.FindField("ammo_shells");
        if (fa>=0) vm.EdictFieldFloat(1,fa)=25.0f;
        int fw=vm.FindField("weapon");
        if (fw>=0) vm.EdictFieldFloat(1,fw)=1.0f;
        int fwsca=vm.FunctionIndex("W_SetCurrentAmmo");
        if (fwsca>0){vm.SetSelfEdict(1); vm.ExecuteProgram(fwsca);}
        return true;
    }

    int FirstMonster() {
        int fclass = vm.FindField("classname");
        for (int i=2;i<1024;i++) {
            if (vm.EdictFree(i)) continue;
            const char* c = fclass>=0?vm.EdictFieldString(i,fclass):"";
            if (c && std::strncmp(c,"monster_",8)==0) return i;
        }
        return 0;
    }

    void DumpMonster(int e) {
        int fclass=vm.FindField("classname"), fh=vm.FindField("health"),
            ftd=vm.FindField("takedamage"), fo=vm.FindField("origin");
        const char* cn = fclass>=0?vm.EdictFieldString(e,fclass):"?";
        float hp = fh>=0?vm.EdictFieldFloat(e,fh):-1;
        float td = ftd>=0?vm.EdictFieldFloat(e,ftd):-1;
        float o[3]={0,0,0};
        if (fo>=0) vm.EdictFieldVector(e,fo,o);
        printf("  edict %d %-24s hp=%.1f takedamage=%.0f o=(%.1f,%.1f,%.1f)\n",
               e, cn, hp, td, o[0],o[1],o[2]);
    }
};

int FieldInt(ProgVM& vm, int e, const char* name) {
    int f=vm.FindField(name);
    return f>=0?(int)vm.EdictFieldFloat(e,f):0;
}

// Link an entity like the engine's SV_LinkEdict so its box has size (the QC
// muzzle is src_z = absmin_z + size_z*0.7 and traces need a non-degenerate box).
void LinkEntity(ProgVM& vm, int e, float mn[3], float mx[3]) {
    int fo=vm.FindField("origin"), fmn=vm.FindField("mins"), fmx=vm.FindField("maxs"),
        fab=vm.FindField("absmin"), fax=vm.FindField("absmax"),
        fsz=vm.FindField("size"), foo=vm.FindField("oldorigin");
    if (fo<0) return;
    float o[3]; vm.EdictFieldVector(e,fo,o);
    float ab[3], ax[3], sz[3];
    for (int k=0;k<3;k++){ ab[k]=o[k]+mn[k]; ax[k]=o[k]+mx[k]; sz[k]=mx[k]-mn[k]; }
    if (fmn>=0) vm.SetEdictFieldVector(e,fmn,mn);
    if (fmx>=0) vm.SetEdictFieldVector(e,fmx,mx);
    if (fab>=0) vm.SetEdictFieldVector(e,fab,ab);
    if (fax>=0) vm.SetEdictFieldVector(e,fax,ax);
    if (fsz>=0) vm.SetEdictFieldVector(e,fsz,sz);
    if (foo>=0) vm.SetEdictFieldVector(e,foo,o);
}

} // namespace

// A fired shotgun must damage its target: 6 pellets x 4 damage when all hit.
TEST_CASE("SHOTGUN: fired at a monster drains its health", "[probe]") {
    World w;
    REQUIRE(w.Boot());

    int m = w.FirstMonster();
    REQUIRE(m > 0);

    int hp0 = FieldInt(w.vm, m, "health");
    // simulate an awake, hostile monster (boot spawns them dormant)
    int ftd = w.vm.FindField("takedamage");
    if (ftd>=0) w.vm.EdictFieldFloat(m, ftd)=2.0f;
    int ff2 = w.vm.FindField("flags");
    if (ff2>=0) w.vm.EdictFieldFloat(m, ff2)=(float)((int)w.vm.EdictFieldFloat(m,ff2)|32|512); // FL_MONSTER|FL_ONGROUND
    // Without any frames having run, the global time is 0, so a default 0
    // invincible_finished makes T_Damage read the monster as invincible.
    int finv = w.vm.FindField("invincible_finished");
    if (finv>=0) w.vm.EdictFieldFloat(m, finv)=-1.0f;

    // link monster + a sufficiently large hit box (matches its model footprint)
    {
        float mn[3]={-32,-32,-24}, mx[3]={32,32,40};
        LinkEntity(w.vm, m, mn, mx);
    }

    int fo = w.vm.FindField("origin");
    float corg[3], morg[3];
    w.vm.EdictFieldVector(m, fo, morg);

    // Park the client at the nearest clear point east of the monster so the
    // shotgun's lateral scatter (-0.04..0.04 over 2048u ~ 0.13u per unit of
    // travel) stays inside the target box.
    float best_sx = 0; bool found = false;
    float zz[3]={0,0,0};
    for (float sx = morg[0]-96.0f; sx >= morg[0]-512.0f; sx -= 16.0f) {
        float a0[3]={sx, morg[1], morg[2]+24}, a1[3]={morg[0]-8, morg[1], morg[2]+24};
        auto wtr = w.map.WorldTrace(a0,a1,zz,zz);
        if (wtr.fraction >= 1.0f) { best_sx = sx; found = true; break; }
    }
    REQUIRE(found);
    corg[0]=best_sx; corg[1]=morg[1]; corg[2]=morg[2];
    w.vm.SetEdictFieldVector(1, fo, corg);
    {
        float mn[3]={-16,-16,-24}, mx[3]={16,16,32};
        LinkEntity(w.vm, 1, mn, mx);
    }

    // aim at the monster (v_angle + enemy make W_FireShotgun aim at it)
    int fen = w.vm.FindField("enemy");
    if (fen>=0) w.vm.EdictFieldInt(1, fen) = w.vm.EdictNumToProg(m);
    int fva = w.vm.FindField("v_angle");
    if (fva>=0) { float va[3]={0,0,0}; w.vm.SetEdictFieldVector(1, fva, va); }
    w.vm.SetSelfEdict(1); w.vm.SetOtherEdict(0);

    // build the solid list so traceline can hit entities (mirrors the app)
    std::vector<SolidEntity> solids;
    BuildSolidList(w.vm, w.map, solids, 1);
    SetGameTraceContext(&w.map, solids.data(), (int)solids.size(), 1);

    int wa = w.vm.FindFunction("W_Attack");
    REQUIRE(wa > 0);
    w.vm.ExecuteProgram(wa);

    int hp1 = FieldInt(w.vm, m, "health");
    std::printf("post-shot monster health=%d (was %d)\n", hp1, hp0);
    // 6 pellets * 4 dmg, no armor: all six must have hit
    REQUIRE(hp1 < hp0);
    REQUIRE(hp1 == hp0 - 24);
    SUCCEED();
}

// Bug (2) regression: with the player standing still and no input at all,
// monsters must not die on their own.
TEST_CASE("SHOTGUN: monsters do not die with the player idle", "[probe]") {
    World w;
    REQUIRE(w.Boot());

    std::vector<int> monsters;
    {
        int fclass = w.vm.FindField("classname");
        for (int i=2;i<1024;i++) {
            if (w.vm.EdictFree(i)) continue;
            const char* c = fclass>=0?w.vm.EdictFieldString(i,fclass):"";
            if (c && std::strncmp(c,"monster_",8)==0) monsters.push_back(i);
        }
    }
    printf("initial monsters: %d\n", (int)monsters.size());
    int prev_alive = (int)monsters.size();

    std::vector<SolidEntity> solids;
    ClientPhysics cin;
    cin.move_vars = MoveVars{};

    for (int f = 0; f < 80; f++) {
        BuildSolidList(w.vm, w.map, solids, 1);
        SetGameTraceContext(&w.map, solids.data(), (int)solids.size(), 1);
        cin.button0 = false;
        RunGameFrame(w.vm, w.map, 0.05f, solids.data(), (int)solids.size(), 1, &cin);
        w.vm.SetSelfEdict(1); w.vm.SetOtherEdict(0);

        int alive = 0;
        for (int i : monsters)
            if (!w.vm.EdictFree(i)) alive++;
        if (alive < prev_alive)
            printf("frame %d: monsters lost (alive %d -> %d) WITHOUT input\n", f, prev_alive, alive);
        prev_alive = alive;
    }

    int last_alive = 0;
    for (int i : monsters)
        if (!w.vm.EdictFree(i)) { last_alive++; w.DumpMonster(i); }
    printf("final alive monsters: %d\n", last_alive);
    REQUIRE(last_alive == (int)monsters.size());
    SUCCEED();
}

// DIAGNOSTIC: place the player in front of a monster, let the monster attack,
// and watch BOTH the player and the monster health. Mirrors the report "when
// monsters shoot me, they die" (the shooter must not die on its own shot).
TEST_CASE("DIAG: monsters firing at the player", "[probe]") {
    World w;
    REQUIRE(w.Boot());

    // gather monsters + each one's attack-related function pointers
    struct Mon { int e; std::string cn; float hp0; int think, touch; };
    std::vector<Mon> mons;
    {
        int fclass = w.vm.FindField("classname"), ft = w.vm.FindField("think"),
            fx = w.vm.FindField("nextthink"), fh = w.vm.FindField("health"),
            fch = w.vm.FindField("touch");
        for (int i = 2; i < 1024; i++) {
            if (w.vm.EdictFree(i)) continue;
            const char* c = fclass >= 0 ? w.vm.EdictFieldString(i, fclass) : "";
            if (c && std::strncmp(c, "monster_", 8) == 0) {
                Mon m; m.e = i; m.cn = c;
                m.hp0 = fh >= 0 ? w.vm.EdictFieldFloat(i, fh) : -1;
                m.think = ft >= 0 ? w.vm.EdictFieldInt(i, ft) : -1;
                m.touch = fch >= 0 ? w.vm.EdictFieldInt(i, fch) : -1;
                w.vm.EdictFieldFloat(i, fx) = 0.01f; // wake them all up
                mons.push_back(m);
            }
        }
    }
    printf("DIAG: %zu monsters\n", mons.size());

    // one isolated monster per attempt: keep the monster AT ITS SPAWN (real
    // floor, clear geometry) and park the client at a worldtrace-cleared spot
    // east of it on the same z; silence every other monster so only the target
    // attacks. Then run the full frame loop and watch both healths.
    int fcl = w.vm.FindField("classname");
    int fo = w.vm.FindField("origin"), ft = w.vm.FindField("think"),
        fh = w.vm.FindField("health"), fen = w.vm.FindField("enemy");
    int fx = w.vm.FindField("nextthink");
    for (auto& m : mons) {
        // skip dormant mobs that boot without a think
        if (m.think <= 0) continue;
        if (w.vm.EdictFree(m.e)) continue;

        for (auto& o : mons) {
            if (o.e == m.e) continue;
            if (fo < 0) break;
            int tk = ft >= 0 ? w.vm.EdictFieldInt(o.e, ft) : 0;
            if (tk) w.vm.EdictFieldInt(o.e, ft) = 0;
            if (fx >= 0) w.vm.EdictFieldFloat(o.e, fx) = 0.0f;
        }

        float mo[3];
        w.vm.EdictFieldVector(m.e, fo, mo);
        float co[3] = { mo[0] - 160.0f, mo[1], mo[2] };
        {
            float zz[3] = {0, 0, 0};
            float a1[3] = {mo[0] - 8, mo[1], mo[2] + 24};
            for (float sx = mo[0] - 48.0f; sx >= mo[0] - 512.0f; sx -= 16.0f) {
                float a0[3] = {sx, mo[1], mo[2] + 24};
                auto wtr = w.map.WorldTrace(a0, a1, zz, zz);
                if (wtr.fraction >= 1.0f) { co[0] = sx; break; }
            }
        }
        w.vm.SetEdictFieldVector(1, fo, co);
        {
            float mn[3] = {-16, -16, -24}, mx[3] = {16, 16, 32};
            LinkEntity(w.vm, 1, mn, mx);
        }
        if (fcl >= 0) w.vm.EdictFieldInt(1, fcl) = w.vm.InternString("player");

        // give the monster the player as enemy/sight (if it has the fields)
        if (fen >= 0) w.vm.EdictFieldInt(m.e, fen) = w.vm.EdictNumToProg(1);
        int fis = w.vm.FindField("sight_entity");
        if (fis >= 0) w.vm.EdictFieldInt(m.e, fis) = w.vm.EdictNumToProg(1);

        const float pinv = -1.0f;
        int finv = w.vm.FindField("invincible_finished");
        if (finv >= 0) w.vm.EdictFieldFloat(1, finv) = pinv;

        float mhp0 = fh >= 0 ? w.vm.EdictFieldFloat(m.e, fh) : -1;
        float php0 = fh >= 0 ? w.vm.EdictFieldFloat(1, fh) : -1;

        float mhp = mhp0, php = php0;
        std::vector<SolidEntity> solids;
        ClientPhysics cin;
        cin.move_vars = MoveVars{};
        int mdead = -1, fshot = -1;
        float mhp_atshot = -1;
        for (int f = 0; f < 400; f++) {
            BuildSolidList(w.vm, w.map, solids, 1);
            SetGameTraceContext(&w.map, solids.data(), (int)solids.size(), 1);
            cin.button0 = false;
            RunGameFrame(w.vm, w.map, 0.05f, solids.data(), (int)solids.size(), 1, &cin);
            w.vm.SetSelfEdict(1);
            w.vm.SetOtherEdict(0);
            if (w.vm.EdictFree(m.e)) { if (mdead < 0) mdead = f; break; }
            mhp = fh >= 0 ? w.vm.EdictFieldFloat(m.e, fh) : -1;
            php = fh >= 0 ? w.vm.EdictFieldFloat(1, fh) : -1;
            if (php < php0 - 0.5f && fshot < 0 && mdead < 0) {
                fshot = f; mhp_atshot = mhp;
            }
            if (mhp < 1.0f) { if (mdead < 0) mdead = f; break; }
            if (f == 120) php = fh >= 0 ? w.vm.EdictFieldFloat(1, fh) : -1;
        }
        php = fh >= 0 ? w.vm.EdictFieldFloat(1, fh) : php;
        float phalf = fh >= 0 ? w.vm.EdictFieldFloat(1, fh) : -1;
        printf("DIAG monster %d %-22s hp %.0f -> %s player hp %.0f -> %.0f mid=%.0f dead@%d shot@%d mhp@shot=%.0f\n",
               m.e, m.cn.c_str(), mhp0,
               (mdead >= 0) ? "DIED" : "ok",
               php0, phalf, php, mdead, fshot, mhp_atshot);
    }
    SUCCEED();
}

// DIAGNOSTIC: drive an army monster to fire at the client for real (direct
// army_fire invocation inside a frame trace context) and watch BOTH healths.
// This reproduces the report "when monsters shoot me, they die".
TEST_CASE("DIAG: army shoots the player", "[probe]") {
    World w;
    REQUIRE(w.Boot());

    int m = w.FirstMonster();
    REQUIRE(m > 0);
    const char* cn0 = w.vm.EdictFieldString(m, w.vm.FindField("classname"));
    if (!cn0 || std::strncmp(cn0, "monster_army", 12) != 0) {
        // find an army specifically
        int fclass = w.vm.FindField("classname");
        int found = 0;
        for (int i = 2; i < 1024; i++) {
            if (w.vm.EdictFree(i)) continue;
            const char* c = fclass >= 0 ? w.vm.EdictFieldString(i, fclass) : "";
            if (c && std::strcmp(c, "monster_army") == 0) { m = i; found = 1; break; }
        }
        REQUIRE(found);
    }
    printf("DIAG shooting monster = edict %d\n", m);

    // wake/hostile setup (mirrors the shotgun test)
    int ftd = w.vm.FindField("takedamage");
    if (ftd >= 0) w.vm.EdictFieldFloat(m, ftd) = 2.0f;
    int ff2 = w.vm.FindField("flags");
    if (ff2 >= 0)
        w.vm.EdictFieldFloat(m, ff2) = (float)((int)w.vm.EdictFieldFloat(m, ff2) | 32 | 512);
    int finv = w.vm.FindField("invincible_finished");
    if (finv >= 0) w.vm.EdictFieldFloat(m, finv) = -1.0f;
    if (finv >= 0) w.vm.EdictFieldFloat(1, finv) = -1.0f;
    int fh = w.vm.FindField("health");
    float mhp0 = fh >= 0 ? w.vm.EdictFieldFloat(m, fh) : -1;
    float php0 = fh >= 0 ? w.vm.EdictFieldFloat(1, fh) : -1;

    {
        float mn[3] = {-32, -32, -24}, mx[3] = {32, 32, 40};
        LinkEntity(w.vm, m, mn, mx);
    }

    int fo = w.vm.FindField("origin");
    float morg[3];
    w.vm.EdictFieldVector(m, fo, morg);

    // park the client dead in front of the monster (east, clear of walls)
    float corg[3] = {morg[0] + 128, morg[1], morg[2]};
    w.vm.SetEdictFieldVector(1, fo, corg);
    {
        float mn[3] = {-16, -16, -24}, mx[3] = {16, 16, 32};
        LinkEntity(w.vm, 1, mn, mx);
    }

    // monster aims at the client: enemy + v_angle facing it
    int fen = w.vm.FindField("enemy");
    if (fen >= 0) w.vm.EdictFieldInt(m, fen) = w.vm.EdictNumToProg(1);
    int fva = w.vm.FindField("v_angle");
    if (fva >= 0) {
        float va[3] = {0, 0, 0};
        w.vm.SetEdictFieldVector(m, fva, va); // army_fire only calls ai_face
    }
    int fvis = w.vm.FindField("sight_entity");
    if (fvis >= 0) w.vm.EdictFieldInt(m, fvis) = w.vm.EdictNumToProg(1);

    // trace context that lets the bullet hit the client (ignore the shooter)
    std::vector<SolidEntity> fresh;
    BuildSolidList(w.vm, w.map, fresh, m);
    SetGameTraceContext(&w.map, fresh.data(), (int)fresh.size(), 1);
    int nEnts = 0;
    for (int i = 0; i < (int)fresh.size(); i++) printf("  solid e%d\n", fresh[i].edict);

    w.vm.SetSelfEdict(m);
    w.vm.SetOtherEdict(0);
    int af = w.vm.FindFunction("army_fire");
    REQUIRE(af > 0);
    bool ok = w.vm.ExecuteProgram(af);
    printf("DIAG army_fire returned %d (err=%s)\n", (int)ok, w.vm.ErrorMessage().c_str());

    int gte = w.vm.FindGlobal("trace_ent");
    int gtf = w.vm.FindGlobal("trace_fraction");
    int gte2 = gte >= 0 ? w.vm.ProgToEdictNum(w.vm.Global(gte)) : -1;
    printf("  trace_ent=%d trace_fraction=%.3f\n", gte2,
           gtf >= 0 ? w.vm.GlobalFloat(gtf) : -1);

    // Re-fire 19 more times (RunThinkLoop re-runs a stale-nextthink army_fire
    // every frame), watching BOTH healths each burst.
    float mhpr = fh >= 0 ? w.vm.EdictFieldFloat(m, fh) : -1;
    float phpr = fh >= 0 ? w.vm.EdictFieldFloat(1, fh) : -1;
    for (int b = 0; b < 20; b++) {
        w.vm.SetSelfEdict(m);
        w.vm.SetOtherEdict(0);
        w.vm.ExecuteProgram(af);
        if (w.vm.EdictFree(m)) { printf("DIAG burst %d: MONSTER FREED\n", b); break; }
        float mhp_ = fh >= 0 ? w.vm.EdictFieldFloat(m, fh) : -1;
        float php_ = fh >= 0 ? w.vm.EdictFieldFloat(1, fh) : -1;
        if (b < 3 || b == 9 || b == 19)
            printf("DIAG burst %2d: monster hp %.1f  player hp %.1f\n", b, mhp_, php_);
        if (php_ < phpr && (int)php_ != (int)phpr) phpr = php_;
        if (php_ <= 0.5f) break;
    }

    float mhp1 = fh >= 0 ? w.vm.EdictFieldFloat(m, fh) : -1;
    float php1 = fh >= 0 ? w.vm.EdictFieldFloat(1, fh) : -1;
    printf("DIAG army %d o=%.1f,%.1f,%.1f client o=%.1f,%.1f,%.1f\n",
           m, morg[0], morg[1], morg[2], corg[0], corg[1], corg[2]);
    printf("DIAG monster hp %.0f -> %.0f   player hp %.0f -> %.0f\n",
           mhp0, mhp1, php0, php1);
    SUCCEED();
}

// DIAGNOSTIC: 3 armies in a line all shooting the player. The army behind the
// player/second army fires through it. Reproduces "when monsters shoot me, they
// die" if the front shooter(s) eat friendly-fire bullets from the rear.
TEST_CASE("DIAG: army cluster friendly fire", "[probe]") {
    World w;
    REQUIRE(w.Boot());

    int fclass = w.vm.FindField("classname");
    std::vector<int> armies;
    for (int i = 2; i < 1024; i++) {
        if (w.vm.EdictFree(i)) continue;
        const char* c = fclass >= 0 ? w.vm.EdictFieldString(i, fclass) : "";
        if (c && std::strcmp(c, "monster_army") == 0) armies.push_back(i);
    }
    REQUIRE((int)armies.size() >= 3);

    int fo = w.vm.FindField("origin"), fh = w.vm.FindField("health"),
        fen = w.vm.FindField("enemy"), ft = w.vm.FindField("think"),
        fx = w.vm.FindField("nextthink");
    int finv = w.vm.FindField("invincible_finished");
    int ftd = w.vm.FindField("takedamage");
    int fvis = w.vm.FindField("sight_entity");
    int afx = w.vm.FindField("nextthink");

    // clear a corridor at an open spot using monster 12's spawn (known floor)
    float base[3];
    w.vm.EdictFieldVector(armies[0], fo, base);
    for (auto& a : armies)
        if (a != armies[0]) w.vm.EdictFieldVector(a, fo, base);

    // player at -128, armies at +0, +64, +128 ahead (so rear shoots through)
    float co[3] = {base[0] - 128, base[1], base[2]};
    {
        float zz[3] = {0, 0, 0};
        float a1[3] = {base[0] - 8, base[1], base[2] + 24};
        for (float sx = base[0] - 48.0f; sx >= base[0] - 512.0f; sx -= 16.0f) {
            float a0[3] = {sx, base[1], base[2] + 24};
            auto wtr = w.map.WorldTrace(a0, a1, zz, zz);
            if (wtr.fraction >= 1.0f) { co[0] = sx; co[1] = base[1]; co[2] = base[2]; break; }
        }
    }
    printf("DIAG cluster player at %.0f,%.0f,%.0f\n", co[0], co[1], co[2]);
    w.vm.SetEdictFieldVector(1, fo, co);
    {
        float mn[3] = {-16, -16, -24}, mx[3] = {16, 16, 32};
        LinkEntity(w.vm, 1, mn, mx);
    }
    for (int k = 0; k < 3; k++) {
        int m = armies[k];
        float mo[3] = {base[0] + 96.0f * k, base[1], base[2]};
        w.vm.SetEdictFieldVector(m, fo, mo);
        {
            float mn[3] = {-32, -32, -24}, mx[3] = {32, 32, 40};
            LinkEntity(w.vm, m, mn, mx);
        }
        if (fen >= 0) w.vm.EdictFieldInt(m, fen) = w.vm.EdictNumToProg(1);
        if (fvis >= 0) w.vm.EdictFieldInt(m, fvis) = w.vm.EdictNumToProg(1);
        if (finv >= 0) w.vm.EdictFieldFloat(m, finv) = -1.0f;
        if (finv >= 0) w.vm.EdictFieldFloat(1, finv) = -1.0f;
        if (ftd >= 0) w.vm.EdictFieldFloat(m, ftd) = 2.0f;
        // arm all three on army_fire with a stale nextthink: RunThinkLoop re-fires
        // it (army_fire never re-arms nextthink) -> continuous bullet spray.
        int af = w.vm.FindFunction("army_fire");
        if (af > 0 && ft >= 0) w.vm.EdictFieldInt(m, ft) = af;
        if (afx >= 0) w.vm.EdictFieldFloat(m, afx) = 0.010f + 0.01f * k;
        // aim down -x (toward the parked player) so the muzzle is correct
        int fva = w.vm.FindField("v_angle");
        if (fva >= 0) {
            float va[3] = {0, 270, 0};
            w.vm.SetEdictFieldVector(m, fva, va);
        }
    }
    for (int i = 2; i < 1024; i++) {
        if (w.vm.EdictFree(i)) continue;
        if (i == armies[0] || i == armies[1] || i == armies[2]) continue;
        int tk = ft >= 0 ? w.vm.EdictFieldInt(i, ft) : 0;
        if (tk) w.vm.EdictFieldInt(i, ft) = 0;
        if (fx >= 0) w.vm.EdictFieldFloat(i, fx) = 0.0f;
    }

    std::vector<SolidEntity> d0;
    BuildSolidList(w.vm, w.map, d0, 12);
    {
        int fo2 = w.vm.FindField("origin");
        int ffl = w.vm.FindField("flags");
        for (const auto& s : d0) {
            if (s.edict != 1 && s.edict != armies[0] && s.edict != armies[1] &&
                s.edict != armies[2])
                continue;
            float o[3];
            w.vm.EdictFieldVector(s.edict, fo2, o);
            printf("DIAG solidlist e%d o=%.0f,%.0f,%.0f flags=%d mn=(%.0f,%.0f,%.0f) mx=(%.0f,%.0f,%.0f)\n",
                   s.edict, o[0], o[1], o[2],
                   ffl >= 0 ? (int)w.vm.EdictFieldFloat(s.edict, ffl) : -1,
                   s.mins[0], s.mins[1], s.mins[2],
                   s.maxs[0], s.maxs[1], s.maxs[2]);
        }
    }
    fflush(stdout);

    float hp0[3], hp[3];
    for (int k = 0; k < 3; k++)
        hp0[k] = hp[k] = fh >= 0 ? w.vm.EdictFieldFloat(armies[k], fh) : -1;
    float php0 = fh >= 0 ? w.vm.EdictFieldFloat(1, fh) : -1;

    std::vector<SolidEntity> solids;
    ClientPhysics cin;
    cin.move_vars = MoveVars{};
    std::string diag;
    long t0 = 0;
    for (int f = 0; f < 1200 && diag.empty(); f++) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        long t = ts.tv_sec * 1000000L + ts.tv_nsec / 1000;
        if (f == 0) t0 = t;
        printf("frame %d start (%.3f ms)\n", f, (t - t0) / 1000.0);
        fflush(stdout);
        BuildSolidList(w.vm, w.map, solids, 1);
        SetGameTraceContext(&w.map, solids.data(), (int)solids.size(), 1);
        cin.button0 = false;
        RunGameFrame(w.vm, w.map, 0.05f, solids.data(), (int)solids.size(), 1, &cin);
        w.vm.SetSelfEdict(1);
        w.vm.SetOtherEdict(0);
        if (f < 5) {
            int gte = w.vm.FindGlobal("trace_ent");
            int gte2 = gte >= 0 ? w.vm.ProgToEdictNum(w.vm.Global(gte)) : -1;
            printf("after frame%d trace_ent=%d hps a0=%.0f a1=%.0f a2=%.0f p=%.0f\n", f, gte2,
                   fh >= 0 ? w.vm.EdictFieldFloat(armies[0], fh) : -1,
                   fh >= 0 ? w.vm.EdictFieldFloat(armies[1], fh) : -1,
                   fh >= 0 ? w.vm.EdictFieldFloat(armies[2], fh) : -1,
                   fh >= 0 ? w.vm.EdictFieldFloat(1, fh) : -1);
            fflush(stdout);
        }
        for (int k = 0; k < 3; k++) {
            if (w.vm.EdictFree(armies[k])) { diag = "freed"; break; }
            hp[k] = fh >= 0 ? w.vm.EdictFieldFloat(armies[k], fh) : -1;
        }
        if (!diag.empty()) break;
    }
    for (int k = 0; k < 3; k++) {
        const char* dead = w.vm.EdictFree(armies[k]) ? "DIED" : "ok";
        printf("DIAG army%d e%d hp %.0f -> %s\n", k, armies[k], hp0[k],
               w.vm.EdictFree(armies[k]) ? "DIED" : "ok");
        (void)dead;
    }
    float php1 = fh >= 0 ? w.vm.EdictFieldFloat(1, fh) : -1;
    printf("DIAG player hp %.0f -> %.0f\n", php0, php1);
    SUCCEED();
}

// DIAGNOSTIC: after boot (monsters already spawned through the VM), scan the
// raw edict memory to see WHICH field slot the bytecode actually wrote the
// think function into, and compare to FindField("think"). This resolves
// whether statement field operands are fielddefs.ofs or something else.
TEST_CASE("DIAG: raw edict layout vs FindField offsets", "[probe]") {
    World w;
    REQUIRE(w.Boot());
    int e = w.FirstMonster();
    REQUIRE(e > 0);
    int ft = w.vm.FindField("think");
    int think = w.vm.EdictFieldInt(e, ft);
    int want = w.vm.FindFunction("army_stand1");
    int stride4 = w.vm.EdictStride() / 4;
    char* base = w.vm.EdictBase() + (size_t)e * w.vm.EdictStride();
    printf("DIAG edict=%d think-read=%d\n", e, think);
    {
        int fcl = w.vm.FindField("classname");
        if (fcl >= 0) {
            int csi = w.vm.EdictFieldInt(e, fcl);
            printf("  classname string idx=%d '%s'\n", csi, w.vm.StringAt(csi));
        }
    }
    for (int k = 0; k < stride4; k++) {
        int32_t v;
        std::memcpy(&v, base + k * 4, 4);
        if (v == want) printf("  slot %d holds fn %d\n", k, want);
    }
    const char* names[] = {"size_x","size_y","size_z","touch","use","think",
                           "blocked","nextthink","groundentity","health",
                           "frags","weapon","weaponmodel","weaponframe"};
    for (int k = 39; k <= 52; k++) {
        int32_t v;
        std::memcpy(&v, base + k * 4, 4);
        printf("    %2d %-12s %d (float %.2f)\n", k, names[k - 39], v,
               *reinterpret_cast<float*>(&v));
    }
    SUCCEED();
}
// projectile can be created; log every entity frees so we can see WHO died.
TEST_CASE("DIAG: monster projectile ownership", "[probe]") {
    World w;
    REQUIRE(w.Boot());
    // Walk the entity list for anything holding a projectile touch function.
    int fcl = w.vm.FindField("classname"), fr = w.vm.FindField("touch"),
        fowner = w.vm.FindField("owner"), fo = w.vm.FindField("origin");
    printf("DIAG entities with touch set:\n");
    for (int i = 1; i < 1024; i++) {
        if (w.vm.EdictFree(i)) continue;
        int tt = fr >= 0 ? w.vm.EdictFieldInt(i, fr) : 0;
        if (!tt) continue;
        const char* c = fcl >= 0 ? w.vm.EdictFieldString(i, fcl) : "";
        int own = fowner >= 0 ? w.vm.EdictFieldEntity(i, fowner) : -1;
        printf("  e%d cn='%s' touch=%d owner=%d\n", i, c ? c : "?", tt, own);
    }
    SUCCEED();
}
// Drive the REAL attack chain th_missile=army_atk1 (STATE frames 81..89) with a
// lone army vs the player, and watch the animation, healths and bullet traces.
TEST_CASE("DIAG: army real attack chain", "[probe]") {
    World w;
    REQUIRE(w.Boot());
    int fclass = w.vm.FindField("classname");
    int army = -1;
    for (int i = 2; i < 1024; i++) {
        if (w.vm.EdictFree(i)) continue;
        const char* c = fclass >= 0 ? w.vm.EdictFieldString(i, fclass) : "";
        if (c && std::strcmp(c, "monster_army") == 0) { army = i; break; }
    }
    REQUIRE(army > 0);
    int fo = w.vm.FindField("origin"), fh = w.vm.FindField("health"),
        ft = w.vm.FindField("think"), fx = w.vm.FindField("nextthink"),
        ff = w.vm.FindField("frame"), fa = w.vm.FindField("angles"),
        fva = w.vm.FindField("v_angle"), fen = w.vm.FindField("enemy"),
        fis = w.vm.FindField("sight_entity"), fms = w.vm.FindField("th_missile");

    float base[3];
    w.vm.EdictFieldVector(army, fo, base);
    float co[3] = {base[0], base[1] - 192.0f, base[2]};
    {
        float zz[3] = {0, 0, 0};
        float a1[3] = {base[0], base[1] - 8, base[2] + 24};
        for (float sy = base[1] - 48.0f; sy >= base[1] - 512.0f; sy -= 16.0f) {
            float a0[3] = {base[0], sy, base[2] + 24};
            auto wtr = w.map.WorldTrace(a0, a1, zz, zz);
            if (wtr.fraction >= 1.0f) { co[1] = sy; break; }
        }
    }
    w.vm.SetEdictFieldVector(1, fo, co);
    {
        float mn[3] = {-16, -16, -24}, mx[3] = {16, 16, 32};
        LinkEntity(w.vm, 1, mn, mx);
    }
    if (fis >= 0) w.vm.EdictFieldInt(army, fis) = w.vm.EdictNumToProg(1);
    if (fen >= 0) w.vm.EdictFieldInt(army, fen) = w.vm.EdictNumToProg(1);
    int finv = w.vm.FindField("invincible_finished");
    if (finv >= 0) w.vm.EdictFieldFloat(1, finv) = -1.0f;
    int fatal = w.vm.FindField("attack_finished");
    int fstate = w.vm.FindField("attack_state");
    if (fstate >= 0) w.vm.EdictFieldFloat(army, fstate) = 1.0f; // AS_MISSILE
    int fatk = w.vm.FindFunction("army_atk1");
    REQUIRE(fatk > 0);
    if (ft >= 0) w.vm.EdictFieldInt(army, ft) = fatk;
    if (fx >= 0) w.vm.EdictFieldFloat(army, fx) = 0.01f;

    std::vector<SolidEntity> solids;
    ClientPhysics cin;
    cin.move_vars = MoveVars{};
    for (int f = 0; f < 120; f++) {
        BuildSolidList(w.vm, w.map, solids, 1);
        SetGameTraceContext(&w.map, solids.data(), (int)solids.size(), 1);
        cin.button0 = false;
        RunGameFrame(w.vm, w.map, 0.05f, solids.data(), (int)solids.size(), 1, &cin);
        w.vm.SetSelfEdict(1);
        w.vm.SetOtherEdict(0);
        if (w.vm.EdictFree(army)) { printf("DIAG army DIED at frame %d\n", f); break; }
        float mhp = fh >= 0 ? w.vm.EdictFieldFloat(army, fh) : -1;
        float php = fh >= 0 ? w.vm.EdictFieldFloat(1, fh) : -1;
        float frame = ff >= 0 ? w.vm.EdictFieldFloat(army, ff) : -1;
        int thinkNow = (ft >= 0 && !w.vm.EdictFree(army)) ? w.vm.EdictFieldInt(army, ft) : 0;
        const char* tn = "none";
        if (thinkNow > 0) tn = w.vm.FunctionNameFor(thinkNow);
        if (mhp < 30.0f) printf("DIAG army hp %.0f (was 30) at frame %d frame=%.0f think=%s\n",
                                mhp, f, frame, tn);
        if (f % 6 == 0 || php < 99.0f)
            printf("DIAG f=%03d army hp=%.0f frame=%.0f think=%s player hp=%.0f\n",
                   f, mhp, frame, tn, php);
    }
    float mhp2 = fh >= 0 ? w.vm.EdictFieldFloat(army, fh) : -1;
    float php2 = fh >= 0 ? w.vm.EdictFieldFloat(1, fh) : -1;
    printf("DIAG END army hp=%.0f player hp=%.0f\n", mhp2, php2);
    SUCCEED();
}
