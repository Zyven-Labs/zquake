#include "engine/host.hpp"
#include "engine/console.hpp"
#include "engine/cvar_system.hpp"
#include "engine/game.hpp"
#include "engine/input.hpp"
#include "engine/bsp.hpp"
#include "engine/mdl_model.hpp"
#include "engine/move.hpp"
#include "engine/gameframe.hpp"
#include "vulkan/vulkan_api.hpp"
#include "filesystem/pak_archive.hpp"
#include "core/math/mathf.hpp"
#include "app/world_builder.hpp"
#include "app/rt_scene.hpp"
#include "render/raytracer.hpp"
#include "core/logging/logger.hpp"
#include "vm/prog_vm.hpp"
#include "vm/prog_builtins.hpp"
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <vector>
#include <map>
#include <memory>
#include <array>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

using zq::mathf::Vec3;
using zq::mathf::Mat4;

// VM edict layout: world = 0, client player = 1, map entities = 2+.
inline constexpr int kClientEdict = 1;

// ===================== Helpers =====================

static bool ReadPakFile(zq::fs::PAKArchive& pak, const char* name,
                        std::vector<uint8_t>& out) {
    for (const auto& e : pak.GetEntries()) {
        if (strncmp(e.name, name, 55) == 0) {
            out.resize(e.file_size);
            if (e.file_size == 0) return true;
            return pak.ReadFile(name, out.data(), out.size());
        }
    }
    return false;
}

static void LoadPalette(zq::fs::PAKArchive& pak, uint8_t palette[768]) {
    std::vector<uint8_t> data;
    memset(palette, 0, 768);
    if (!ReadPakFile(pak, "gfx/palette.lmp", data) || data.size() < 768) {
        // Fallback grayscale palette
        for (int i = 0; i < 256; i++) {
            uint8_t v = (uint8_t)(i * 255 / 255);
            palette[i * 3 + 0] = v;
            palette[i * 3 + 1] = v;
            palette[i * 3 + 2] = v;
        }
        return;
    }
    memcpy(palette, data.data(), 768);
}

// ===================== Player =====================

struct Player {
    Vec3 pos;
    Vec3 vel;
    float yaw = 0.0f;   // degrees
    float pitch = 0.0f; // degrees
    bool on_ground = false;
};

static float DegToRad(float d) { return d * 0.0174532925f; }

// ===================== Locate pak =====================

static bool OpenGamePak(zq::fs::PAKArchive& pak, const char** candidates, int count) {
    for (int i = 0; i < count; i++) {
        if (pak.Open(candidates[i])) {
            zq::log::Info("Opened PAK: ");
            zq::log::Info(candidates[i]);
            return true;
        }
    }
    return false;
}

// ===================== Main =====================

static void HandleEvent(const SDL_Event& event, bool& running, bool& mouse_captured,
                        Player& player) {
    switch (event.type) {
        case SDL_QUIT:
            running = false;
            break;
        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_BACKQUOTE) {
                zq::engine::Console::Instance().Toggle();
                break;
            }
            if (zq::engine::Console::Instance().IsActive()) {
                SDL_Keycode sym = event.key.keysym.sym;
                if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER)
                    zq::engine::Console::Instance().HandleEnter();
                else if (sym == SDLK_BACKSPACE)
                    zq::engine::Console::Instance().HandleBackspace();
                else if (sym == SDLK_DELETE)
                    zq::engine::Console::Instance().HandleDelete();
                else if (sym == SDLK_UP)
                    zq::engine::Console::Instance().HistoryUp();
                else if (sym == SDLK_DOWN)
                    zq::engine::Console::Instance().HistoryDown();
                else if (sym == SDLK_LEFT)
                    zq::engine::Console::Instance().CursorLeft();
                else if (sym == SDLK_RIGHT)
                    zq::engine::Console::Instance().CursorRight();
                else if (sym == SDLK_TAB)
                    zq::engine::Console::Instance().HandleTab();
                else if (sym == SDLK_ESCAPE)
                    zq::engine::Console::Instance().Toggle();
            } else {
                zq::engine::Input::HandleKeyEvent(event.key.keysym.scancode, true);
            }
            break;
        case SDL_KEYUP:
            if (!zq::engine::Console::Instance().IsActive())
                zq::engine::Input::HandleKeyEvent(event.key.keysym.scancode, false);
            break;
        case SDL_TEXTINPUT:
            if (zq::engine::Console::Instance().IsActive()) {
                for (size_t i = 0; event.text.text[i]; i++) {
                    if (event.text.text[i] >= 32 && event.text.text[i] <= 126)
                        zq::engine::Console::Instance().HandleChar(event.text.text[i]);
                }
            }
            break;
        case SDL_MOUSEMOTION:
            if (!zq::engine::Console::Instance().IsActive() && mouse_captured) {
                // Mouse-right must turn the view right. Facing +X, +Y (north)
                // is left and -Y (south) is right, so a positive xrel must
                // DECREASE yaw (verified by the orientation render test).
                player.yaw -= event.motion.xrel * 0.15f;
                player.pitch -= event.motion.yrel * 0.15f;
                if (player.pitch > 89.0f) player.pitch = 89.0f;
                if (player.pitch < -89.0f) player.pitch = -89.0f;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            zq::engine::Input::HandleMouseButton(event.button.button, true);
            break;
        case SDL_MOUSEBUTTONUP:
            zq::engine::Input::HandleMouseButton(event.button.button, false);
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                // swapchain recreation handled next frame by VulkanAPI
            }
            break;
        default:
            break;
    }
}

// Game-frame: drive the QuakeC game logic each tick (SV_Physics port):
// entity thinks/physics, the client PreThink/move/PostThink, and touch.
struct GameFields {
    int origin=-1, velocity=-1, v_angle=-1, angle=-1, flags=-1, button0=-1,
        button2=-1, button3=-1;
    void Populate(zq::vm::ProgVM& vm) {
        origin = vm.FindField("origin");
        velocity = vm.FindField("velocity");
        v_angle = vm.FindField("v_angle");
        angle = vm.FindField("angles");
        flags = vm.FindField("flags");
        button0 = vm.FindField("button0");
        button2 = vm.FindField("button2");
        button3 = vm.FindField("button3");
    }
};

static void UpdateGameFrame(Player& p, float dt, const zq::engine::BSPMap& map,
                            zq::vm::ProgVM& progs, int client_edict) {
    using namespace zq::engine;
    static const MoveVars movevars;
    static GameFields F;

    std::vector<SolidEntity> solids;
    BuildSolidList(progs, map, solids, client_edict);
    SetGameTraceContext(&map, solids.data(), (int)solids.size(), client_edict);

    F.Populate(progs);
    // Pre-sync the client's origin/velocity into its edict BEFORE the game
    // frame runs, so monsters/AI that move this frame collide against the
    // player where they actually are (not last frame's spot), which stops
    // monsters from piling into/through the player.
    if (F.origin >= 0) {
        float o[3] = { p.pos.x, p.pos.y, p.pos.z };
        progs.SetEdictFieldVector(client_edict, F.origin, o);
    }
    if (F.velocity >= 0) {
        float v[3] = { p.vel.x, p.vel.y, p.vel.z };
        progs.SetEdictFieldVector(client_edict, F.velocity, v);
    }

    // 1) StartFrame + entity thinks/physics (SV_Physics; client excluded)
    RunGameFrame(progs, map, dt, solids.data(), (int)solids.size(), client_edict);

    // 2) sync client edict: origin, velocity, view angles, buttons
    if (F.origin >= 0) {
        float o[3] = { p.pos.x, p.pos.y, p.pos.z };
        progs.SetEdictFieldVector(client_edict, F.origin, o);
    }
    if (F.v_angle >= 0) {
        float a[3] = { p.pitch, p.yaw, 0 };
        progs.SetEdictFieldVector(client_edict, F.v_angle, a);
    }
    if (F.angle >= 0) {
        float a[3] = { p.pitch, p.yaw, 0 };
        progs.SetEdictFieldVector(client_edict, F.angle, a);
    }
    progs.SetSelfEdict(client_edict);
    progs.SetOtherEdict(0);
    if (F.button0 >= 0)
        progs.EdictFieldFloat(client_edict, F.button0) =
            (Input::IsKeyDown(Key::MOUSE1) || Input::IsKeyDown(Key::LCTRL)) ? 1.0f : 0.0f;
    if (F.button2 >= 0)
        progs.EdictFieldFloat(client_edict, F.button2) = Input::IsKeyDown(Key::SPACE) ? 1.0f : 0.0f;
    if (F.button3 >= 0)
        progs.EdictFieldFloat(client_edict, F.button3) = Input::IsKeyDown(Key::LSHIFT) ? 1.0f : 0.0f;

    // 3) PlayerPreThink: reads buttons (weapon fire, jump via button2)
    int pre = progs.FunctionIndex("PlayerPreThink");
    if (pre > 0) progs.ExecuteProgram(pre);

    // 4) engine movement (faithful SV_WalkMove against world + entities)
    PlayerPhys phys;
    phys.origin[0] = p.pos.x; phys.origin[1] = p.pos.y; phys.origin[2] = p.pos.z;
    phys.velocity[0] = p.vel.x; phys.velocity[1] = p.vel.y; phys.velocity[2] = p.vel.z;
    phys.angles[0] = p.pitch;
    phys.angles[1] = p.yaw;
    phys.angles[2] = 0;
    phys.onground = p.on_ground;

    PlayerCmd cmd;
    cmd.forwardmove = 0;
    if (Input::IsKeyDown(Key::WKEY)) cmd.forwardmove += 320;
    if (Input::IsKeyDown(Key::SKEY)) cmd.forwardmove -= 320;
    cmd.sidemove = 0;
    if (Input::IsKeyDown(Key::DKEY)) cmd.sidemove += 320;
    if (Input::IsKeyDown(Key::AKEY)) cmd.sidemove -= 320;
    cmd.upmove = 0;
    cmd.jump = Input::IsKeyDown(Key::SPACE); // engine-side jump (270 in RunPlayerMove)

    RunPlayerMove(map, phys, cmd, dt, movevars, solids.data(), (int)solids.size(),
                  client_edict);

    p.pos.x = phys.origin[0]; p.pos.y = phys.origin[1]; p.pos.z = phys.origin[2];
    p.vel.x = phys.velocity[0]; p.vel.y = phys.velocity[1]; p.vel.z = phys.velocity[2];
    p.on_ground = phys.onground;

    // synced velocity/onground back into the client edict
    if (F.velocity >= 0) {
        float v[3] = { p.vel.x, p.vel.y, p.vel.z };
        progs.SetEdictFieldVector(client_edict, F.velocity, v);
    }
    if (F.origin >= 0) {
        float o[3] = { p.pos.x, p.pos.y, p.pos.z };
        progs.SetEdictFieldVector(client_edict, F.origin, o);
    }
    if (F.flags >= 0) {
        int flags = (int)progs.EdictFieldFloat(client_edict, F.flags);
        flags |= 8; // FL_CLIENT
        if (p.on_ground) flags |= FL_ONGROUND; else flags &= ~FL_ONGROUND;
        progs.EdictFieldFloat(client_edict, F.flags) = (float)flags;
    }

    // 5) PlayerPostThink
    progs.SetSelfEdict(client_edict);
    int post = progs.FunctionIndex("PlayerPostThink");
    if (post > 0) progs.ExecuteProgram(post);

    // 6) touch detection (item/weapon pickup). The SP PlayerPreThink rewrites
    // the client's flags dropping FL_CLIENT, which the item touch functions
    // gate on — restore it here so pickups actually fire.
    if (F.flags >= 0) {
        int flags = (int)progs.EdictFieldFloat(client_edict, F.flags);
        flags |= 8; // FL_CLIENT
        if (p.on_ground) flags |= FL_ONGROUND;
        progs.EdictFieldFloat(client_edict, F.flags) = (float)flags;
    }
    CheckTouch(progs, client_edict, solids.data(), (int)solids.size());
}


int main(int argc, char** argv) {
    zq::log::Info("zquake starting...");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) < 0) {
        zq::log::Error("SDL2 initialization failed");
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "zquake", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1920, 1080, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!window) {
        zq::log::Error("Failed to create window");
        SDL_Quit();
        return 1;
    }

    zq::vk::VulkanAPI vulkan;
    if (!vulkan.Initialize(window)) {
        zq::log::Error("Vulkan initialization failed");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    zq::engine::Host::Instance().Initialize();
    zq::engine::Input::Init();
    SDL_StartTextInput();

    // ---- Load game data from PAK (optional) ----
    zq::fs::PAKArchive pak;
    const char* candidates[] = {
        "id1/pak0.pak",
        "quake/id1/pak0.pak",
        "pak0.pak",
        "id1/pak1.pak",
        "/usr/share/quake/id1/pak0.pak"
    };

    bool have_pak = OpenGamePak(pak, candidates, 5);
    if (!have_pak) {
        zq::log::Error("pak0.pak not found - place Quake data in ./id1/");
        zq::engine::Host::Instance().Shutdown();
        vulkan.Shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Palette (from pak)
    uint8_t palette[768];
    LoadPalette(pak, palette);

    // Map
    const char* map_name = "maps/e1m1.bsp";
    if (argc > 1) map_name = argv[1];
    std::vector<uint8_t> map_data;
    bool map_ok = ReadPakFile(pak, map_name, map_data);
    if (map_ok) {
        zq::log::Info("Loaded map from pak: ");
        zq::log::Info(map_name);
    }

    zq::engine::BSPMap map;
    if (!map_ok || !map.Load(map_data.data(), map_data.size())) {
        zq::log::Error("Failed to load/parse a map");
        zq::engine::Host::Instance().Shutdown();
        vulkan.Shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Player start. FindSpawnPoint guarantees a point that is inside the
    // level geometry (nudging embedded starts up, falling back to a point
    // above the world model), so the camera never spawns outside the map.
    Player player;
    float origin[3], angles[3];
    if (map.FindSpawnPoint(origin, angles)) {
        // Quake: the spawn origin IS the player origin; the eye sits at
        // origin + viewheight. Player falls the last bit to the floor.
        player.pos = Vec3{ origin[0], origin[1], origin[2] };
        player.yaw = angles[1];
    } else {
        player.pos = Vec3{ 0, 0, 60 };
    }

    // Build texture groups + upload textures
    auto groups = zq::app::BuildGroups(map);
    std::vector<zq::vk::VulkanImage*> group_textures(groups.size(), nullptr);
    for (size_t g = 0; g < groups.size(); g++) {
        int ti = groups[g].texture_index;
        if (ti >= 0 && ti < (int)map.Textures().size()) {
            const auto& tex = map.Textures()[ti];
            auto rgba = zq::app::IndexedToRGBA(tex, palette);
            group_textures[g] = vulkan.CreateTexture(tex.width, tex.height, rgba.data());
        }
        if (!group_textures[g]) {
            // Simple fallback texture
            std::vector<uint8_t> fallback(16 * 16 * 4);
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++) {
                    int idx = (x / 8 + y / 8) % 2;
                    uint8_t c = idx ? 180 : 90;
                    fallback[((y * 16) + x) * 4 + 0] = c;
                    fallback[((y * 16) + x) * 4 + 1] = c;
                    fallback[((y * 16) + x) * 4 + 2] = c;
                    fallback[((y * 16) + x) * 4 + 3] = 255;
                }
            group_textures[g] = vulkan.CreateTexture(16, 16, fallback.data());
        }
    }

    float white[4] = { 1, 1, 1, 1 };
    zq::vk::VulkanImage* lightmap = vulkan.CreateLightmap(white);

    // Upload vertex/index buffers per group
    struct Drawable {
        zq::vk::VulkanBuffer* vbuf;
        zq::vk::VulkanBuffer* ibuf;
        uint32_t index_count;
        zq::vk::VulkanImage* tex;
    };
    std::vector<Drawable> drawables;
    for (size_t g = 0; g < groups.size(); g++) {
        if (groups[g].indices.empty()) continue;
        auto& grp = groups[g];
        Drawable d;
        d.vbuf = vulkan.CreateVertexBuffer(grp.vertices.size() * sizeof(zq::app::WorldVertex));
        vulkan.UpdateBuffer(d.vbuf, grp.vertices.data(), grp.vertices.size() * sizeof(zq::app::WorldVertex));
        d.ibuf = vulkan.CreateIndexBuffer(grp.indices.size() * sizeof(uint32_t));
        vulkan.UpdateBuffer(d.ibuf, grp.indices.data(), grp.indices.size() * sizeof(uint32_t));
        d.index_count = (uint32_t)grp.indices.size();
        d.tex = group_textures[g];
        drawables.push_back(d);
    }

    // Register console commands
    zq::engine::Console::Instance().AddCommand("quit", [&](const zq::String&) {
        zq::engine::Host::Instance().Quit();
    });
    zq::engine::Console::Instance().AddCommand("pos", [&](const zq::String&) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "pos: %.1f %.1f %.1f (yaw %.1f)",
                      player.pos.x, player.pos.y, player.pos.z, player.yaw);
        zq::engine::Console::Instance().Println(zq::String(buf));
    });

    // ===================== QuakeC VM =====================
    // Load progs.dat from the PAK (id1/pak0.pak) and let the real QuakeC
    // spawn the map entities. This is the authoritative source of the
    // world's entity layout (ED_LoadFromFile).
    zq::vm::ProgVM progs;
    {
        std::vector<uint8_t> progs_data;
        bool have_progs = false;
        if (have_pak) {
            for (const auto& e : pak.GetEntries()) {
                if (strncmp(e.name, "progs.dat", 55) == 0) {
                    progs_data.resize(e.file_size);
                    if (e.file_size == 0 || pak.ReadFile(e.name, progs_data.data(), progs_data.size())) {
                        have_progs = true;
                        zq::log::Info("progs.dat loaded from PAK");
                    }
                    break;
                }
            }
        }
        if (have_progs && progs.Load(progs_data.data(), progs_data.size())) {
            zq::vm::RegisterDefaultBuiltins(progs);
            zq::engine::RegisterGameBuiltins(progs);
            zq::log::Info("QuakeC VM: progs.dat loaded, map entities spawned");
            char nb[96];
            std::snprintf(nb, sizeof(nb), "  functions=%d statements=%d",
                          progs.NumFunctions(), progs.NumStatements());
            zq::log::Info(nb);
            progs.RegisterBuiltin(34, [&map](zq::vm::ProgVM& vm) {
                // droptofloor: trace the entity's box straight down 256 units
                // (SV_Move); snap to the floor if it lands, return 0 if it
                // fell out of the level.
                int f_origin = vm.FindField("origin");
                int f_mins = vm.FindField("mins");
                int f_maxs = vm.FindField("maxs");
                int g_self = vm.FindGlobal("self");
                if (f_origin < 0 || f_mins < 0 || f_maxs < 0 || g_self < 0) {
                    vm.SetReturnFloat(0);
                    return;
                }
                int self = vm.ProgToEdictNum(vm.Global(g_self));
                if (self < 0) { vm.SetReturnFloat(0); return; }
                float o[3], mn[3], mx[3];
                vm.EdictFieldVector(self, f_origin, o);
                vm.EdictFieldVector(self, f_mins, mn);
                vm.EdictFieldVector(self, f_maxs, mx);
                float end[3] = {o[0], o[1], o[2] - 256};
                auto tr = zq::engine::MoveBox(map, mn, mx, o, end, nullptr, 0, -1);
                if (tr.fraction == 1.0f || tr.allsolid) {
                    vm.SetReturnFloat(0);
                } else {
                    vm.SetEdictFieldVector(self, f_origin, tr.endpos);
                    vm.SetReturnFloat(1);
                }
            });
            // pointcontents (#41): ask the BSP what is at this point.
            progs.RegisterBuiltin(41, [&map](zq::vm::ProgVM& vm) {
                float p[3];
                vm.ParmVector(0, p);
                vm.SetReturnFloat((float)map.PointContents(p));
            });
            progs.AllocateEdicts(2);            // world (0) + client (1)
            progs.SetTime(0.0f);
            progs.SetFrametime(0.0f);
            progs.SetBrushBounds([&map](int mi, float mn[3], float mx[3]) {
                if (mi < 0 || mi >= (int)map.Models().size()) return false;
                const auto& m = map.Models()[mi];
                mn[0] = m.origin[0] + m.mins[0]; mn[1] = m.origin[1] + m.mins[1]; mn[2] = m.origin[2] + m.mins[2];
                mx[0] = m.origin[0] + m.maxs[0]; mx[1] = m.origin[1] + m.maxs[1]; mx[2] = m.origin[2] + m.maxs[2];
                return true;
            });
            progs.LoadEntities(map.EntityString().c_str());

            // Single-player mode flags, exactly as the reference server sets
            // them (sv_main.c:1166-1176, host.c skill default "1"): the progs
            // branches on these globals/cvars for SP vs deathmatch behavior.
            progs.SetGlobalFloatG("deathmatch", 0.0f);
            progs.SetGlobalFloatG("coop", 0.0f);
            progs.SetGlobalFloatG("teamplay", 0.0f);
            progs.SetGlobalFloatG("skill", 1.0f);
            progs.SetGlobalFloatG("serverflags", 0.0f);
            progs.SetGlobalStringG("mapname", map_name);
            {
                if (zq::engine::CVar* c = zq::engine::CVarSystem::GetCVar(zq::String("skill"))) c->SetFloat(1.0f);
                else zq::engine::CVarSystem::RegisterCVar(zq::String("skill"), zq::String("1"));
                if (zq::engine::CVar* c = zq::engine::CVarSystem::GetCVar(zq::String("deathmatch"))) c->SetFloat(0.0f);
                else zq::engine::CVarSystem::RegisterCVar(zq::String("deathmatch"), zq::String("0"));
                if (zq::engine::CVar* c = zq::engine::CVarSystem::GetCVar(zq::String("coop"))) c->SetFloat(0.0f);
                else zq::engine::CVarSystem::RegisterCVar(zq::String("coop"), zq::String("0"));
                if (zq::engine::CVar* c = zq::engine::CVarSystem::GetCVar(zq::String("teamplay"))) c->SetFloat(0.0f);
                else zq::engine::CVarSystem::RegisterCVar(zq::String("teamplay"), zq::String("0"));
            }

            // Set up the client player edict (edict 1) via the game's own
            // PutClientInServer, seeded from the spawn parm globals.
            constexpr int CLIENT = kClientEdict;
            int f_class = progs.FindField("classname");
            if (f_class >= 0)
                progs.EdictFieldInt(CLIENT, f_class) = progs.InternString("player");
            progs.SetSelfEdict(CLIENT);
            progs.SetOtherEdict(0);
            progs.SetGlobalFloatG("parm1", origin[0]);
            progs.SetGlobalFloatG("parm2", origin[1]);
            progs.SetGlobalFloatG("parm3", origin[2]);
            progs.SetGlobalFloatG("parm4", angles[1]);
            // spawn parms 5-8: deathmatch, coop, teamplay, skill (pr_cmds
            // SetNewParms semantics; keeps the progs in single-player mode).
            progs.SetGlobalFloatG("parm5", 0.0f);   // deathmatch
            progs.SetGlobalFloatG("parm6", 0.0f);   // coop
            progs.SetGlobalFloatG("parm7", 0.0f);   // teamplay
            progs.SetGlobalFloatG("parm8", 1.0f);   // skill
            int picis = progs.FunctionIndex("PutClientInServer");
            if (picis > 0) {
                progs.ExecuteProgram(picis);
                zq::log::Info("PutClientInServer ran");
            }
            // health must be > 0 for the progs to treat the client as alive
            int f_health = progs.FindField("health");
            if (f_health >= 0 && progs.EdictFieldFloat(CLIENT, f_health) <= 0.0f)
                progs.EdictFieldFloat(CLIENT, f_health) = 100.0f;
            // mark the client edict as a client (FL_CLIENT) for touch gating
            int f_flags = progs.FindField("flags");
            if (f_flags >= 0)
                progs.EdictFieldFloat(CLIENT, f_flags) =
                    (float)((int)progs.EdictFieldFloat(CLIENT, f_flags) | 8);
            zq::log::Info("QuakeC VM: progs.dat loaded, map entities spawned");
        } else {
            zq::log::Warn("progs.dat not found - entity spawn via QuakeC disabled");
        }
    }

    // ===================== MDL Entities =====================
    // Renderable entities come from the QuakeC VM's spawned edicts (their
    // model string + origin + angles), falling back to the hardcoded
    // classname -> model table when no progs.dat is available.
    struct LoadedModel {
        zq::vk::VulkanBuffer* vbuf = nullptr;
        zq::vk::VulkanBuffer* ibuf = nullptr;
        std::vector<zq::vk::VulkanImage*> skins;   // per-skin textures
        uint32_t index_count = 0;
        uint32_t vertex_count = 0;
        std::unique_ptr<zq::engine::MdlModel> mdl;  // kept to rebuild frames
        int cur_frame = -1;                         // frame currently in vbuf
    };
    struct EntInstance {
        int model_index = -1;   // index into the `models` vector (avoids
                                // dangling pointers when the vector reallocates)
        float origin[3];
        float yaw = 0;
        float scale = 1.0f;
        int frame = 0;          // current animation frame from self.frame
        int skin = 0;           // current skin from self.skin
        int edict = -1;         // VM edict number (diagnostics)
    };

    auto classname_to_model = [](const std::string& cn) -> const char* {
        if (cn == "monster_dog") return "progs/dog.mdl";
        if (cn == "monster_army") return "progs/soldier.mdl";
        if (cn == "monster_ogre") return "progs/ogre.mdl";
        if (cn == "monster_demon") return "progs/demon.mdl";
        if (cn == "monster_wizard") return "progs/wizard.mdl";
        if (cn == "monster_knight") return "progs/knight.mdl";
        if (cn == "monster_hell_knight") return "progs/hknight.mdl";
        if (cn == "monster_zombie") return "progs/zombie.mdl";
        if (cn == "monster_shambler") return "progs/shambler.mdl";
        if (cn == "monster_enforcer") return "progs/enforcer.mdl";
        if (cn == "monster_fish") return "progs/fish.mdl";
        if (cn == "monster_boss") return "progs/boss.mdl";
        if (cn == "item_health") return "progs/health.mdl";
        if (cn == "item_armor1") return "progs/armor.mdl";
        if (cn == "item_armor2") return "progs/armor2.mdl";
        if (cn == "item_artifact_envirosuit") return "progs/suit.mdl";
        if (cn == "item_artifact_invulnerability") return "progs/invulner.mdl";
        if (cn == "item_artifact_super_damage") return "progs/sdamage.mdl";
        if (cn == "weapon_supershotgun") return "progs/g_shot.mdl";
        if (cn == "weapon_nailgun") return "progs/g_nail.mdl";
        if (cn == "weapon_supernailgun") return "progs/g_nail2.mdl";
        if (cn == "weapon_rocketlauncher") return "progs/g_rock.mdl";
        if (cn == "weapon_grenadelauncher") return "progs/g_rock2.mdl";
        if (cn == "weapon_lightning") return "progs/g_light.mdl";
        return nullptr;
    };

    std::vector<LoadedModel> models;
    std::vector<EntInstance> entities;
    std::vector<std::string> model_paths;
    // Per-entity vertex buffers: each MDL entity gets its own buffer so its
    // animation frame survives until the GPU actually executes the recorded
    // draws. A shared per-model buffer ends up holding only the LAST entity's
    // frame, so every monster of a model renders that same pose (e.g. all idle).
    std::map<int, zq::vk::VulkanBuffer*> mdl_ent_vbuf;
    std::map<int, int> mdl_ent_frame;

    // Build a frame's vertex data (positions + uv) from a loaded MDL. MDL
    // topology is identical across frames, so only this re-runs when the
    // current frame changes; the index buffer and VBO size stay fixed.
    auto build_mdl_frame = [](const zq::engine::MdlModel& mdl, int frame,
                              std::vector<zq::app::WorldVertex>& wverts) -> std::vector<uint32_t> {
        std::vector<float> verts, uv;
        std::vector<uint32_t> tris;
        mdl.BuildMesh(frame, verts, uv, tris);
        wverts.assign(verts.size() / 3, zq::app::WorldVertex{});
        for (size_t i = 0; i < wverts.size(); i++) {
            wverts[i].pos[0] = verts[i * 3 + 0];
            wverts[i].pos[1] = verts[i * 3 + 1];
            wverts[i].pos[2] = verts[i * 3 + 2];
            wverts[i].uv[0] = uv[i * 2 + 0];
            wverts[i].uv[1] = uv[i * 2 + 1];
            wverts[i].normal[0] = 0; wverts[i].normal[1] = 0; wverts[i].normal[2] = 1;
            wverts[i].lmuv[0] = 0.5f; wverts[i].lmuv[1] = 0.5f; wverts[i].lmuv[2] = 0;
        }
        return tris;
    };

    auto load_model = [&](const std::string& path) -> int {
        for (size_t i = 0; i < model_paths.size(); i++) {
            if (model_paths[i] == path) return (int)i;
        }
        std::vector<uint8_t> fdata;
        if (!ReadPakFile(pak, path.c_str(), fdata)) {
            std::fprintf(stderr, "MDL NOT IN PAK: %s\n", path.c_str());
            return -1;
        }
        auto mdl = std::make_unique<zq::engine::MdlModel>();
        if (!mdl->Load(fdata.data(), fdata.size())) return -1;
        if (mdl->NumFrames() < 1) return -1;

        // Build mesh for frame 0 (sizes the vertex buffer; topology is identical
        // for every frame, so the buffer is reused and only rewritten per frame).
        std::vector<zq::app::WorldVertex> wverts;
        std::vector<uint32_t> tris = build_mdl_frame(*mdl, 0, wverts);
        if (wverts.empty() || tris.empty()) return -1;

        // Upload every skin (monsters can select via self.skin).
        std::vector<zq::vk::VulkanImage*> skins;
        for (int si = 0; si < mdl->NumSkins(); si++) {
            const auto& skin = mdl->Skin(si);
            if (skin.empty()) continue;
            auto rgba = zq::app::IndexedToRGBA(skin.data(), skin.size(), palette);
            if (rgba.empty()) continue;
            zq::vk::VulkanImage* tex = vulkan.CreateTexture(mdl->SkinWidth(), mdl->SkinHeight(), rgba.data());
            if (tex) skins.push_back(tex);
        }
        if (skins.empty()) return -1;

        zq::vk::VulkanBuffer* vbuf = vulkan.CreateVertexBuffer(wverts.size() * sizeof(zq::app::WorldVertex));
        vulkan.UpdateBuffer(vbuf, wverts.data(), wverts.size() * sizeof(zq::app::WorldVertex));
        zq::vk::VulkanBuffer* ibuf = vulkan.CreateIndexBuffer(tris.size() * sizeof(uint32_t));
        vulkan.UpdateBuffer(ibuf, tris.data(), tris.size() * sizeof(uint32_t));

        LoadedModel lm;
        lm.vbuf = vbuf;
        lm.ibuf = ibuf;
        lm.skins = std::move(skins);
        lm.index_count = (uint32_t)tris.size();
        lm.vertex_count = (uint32_t)wverts.size();
        lm.mdl = std::move(mdl);
        lm.cur_frame = 0;
        model_paths.push_back(path);
        models.push_back(std::move(lm));
        return (int)(models.size() - 1);
    };

    const char* model_path = nullptr;
    auto spawn_edict = [&](const std::string& path, const float origin[3],
                           float yaw, float scale, int frame = 0, int skin = 0, int edict = -1) {
        if (path.empty()) return;
        if (path[0] == '*') return;
        if (path.compare(0, 5, "maps/") == 0) return;
        int mi = load_model(path);
        if (mi < 0) { /* model not loadable */ return; }
        EntInstance inst;
        inst.model_index = mi;
        inst.origin[0] = origin[0];
        inst.origin[1] = origin[1];
        inst.origin[2] = origin[2];
        inst.yaw = yaw;
        inst.scale = scale;
        inst.frame = frame;
        inst.skin = skin;
        inst.edict = edict;
        entities.push_back(inst);
    };

    // (Re)build the visible MDL entity list from the current VM edicts. Called
    // once at startup and every frame so picked-up items (model cleared to
    // "") stop rendering and moved entities follow their live origins.
    auto refresh_entities = [&]() {
        entities.clear();
        if (progs.Loaded()) {
            int f_model = progs.FindField("model");
            int f_origin = progs.FindField("origin");
            int f_angles = progs.FindField("angles");
            int f_scale = progs.FindField("scale");
            int f_frame = progs.FindField("frame");
            int f_skin = progs.FindField("skin");
            for (int i = 1; i < 1024; i++) {
                if (progs.EdictFree(i)) continue;
                if (i == kClientEdict) continue; // first person: don't render self
                const char* m = (f_model >= 0) ? progs.EdictFieldString(i, f_model) : "";
                if (!m[0] || m[0] == '*') continue;
                if (strncmp(m, "maps/", 5) == 0) continue;
                float origin[3] = {0, 0, 0};
                if (f_origin >= 0) progs.EdictFieldVector(i, f_origin, origin);
                float yaw = 0.0f;
                if (f_angles >= 0) {
                    float ang[3];
                    progs.EdictFieldVector(i, f_angles, ang);
                    yaw = ang[1];
                }
                float scale = 1.0f;
                if (f_scale >= 0) scale = progs.EdictFieldFloat(i, f_scale);
                int frame = f_frame >= 0 ? (int)progs.EdictFieldFloat(i, f_frame) : 0;
                int skin = f_skin >= 0 ? (int)progs.EdictFieldFloat(i, f_skin) : 0;
                if (frame < 0) frame = 0;
                if (skin < 0) skin = 0;
                spawn_edict(m, origin, yaw, scale, frame, skin, i);
            }
        } else {
            for (auto& e : map.ParseEntities()) {
                model_path = nullptr;
                if (!e.model.empty() && e.model[0] != '*') {
                    model_path = e.model.c_str();
                } else if (!e.classname.empty()) {
                    model_path = classname_to_model(e.classname);
                }
                if (!model_path) continue;
                spawn_edict(model_path, e.origin, e.angles[1], e.scale);
            }
        }
    };
    refresh_entities();
    {   // initial log
        char nb[64];
        std::snprintf(nb, sizeof(nb), "model entities: %zu", entities.size());
        zq::log::Info(nb);
    }

    // Brush submodels (doors, breaks, item/ammo boxes, etc.) are NOT part of
    // the static world mesh; they are drawn as separate drawables that are
    // gated on the live edict's model field, so a consumed pickup (model
    // cleared to "") stops rendering and disappears. Geometry/textures for
    // each submodel are built and cached once on first use.
    struct SubmodelCache {
        std::vector<Drawable> ds;
        // CPU vertex copies mirroring ds, kept so a moving push-brush (door)
        // can re-upload the VBOs translated by the edict's origin delta.
        std::vector<std::vector<zq::app::WorldVertex>> base;
        float applied[3] = { 0, 0, 0 }; // last re-upload offset
    };
    std::map<int, SubmodelCache> submodel_cache;
    std::vector<Drawable> brush_drawables;
    auto submodel_drawables = [&](int mi) -> const std::vector<Drawable>* {
        auto it = submodel_cache.find(mi);
        if (it != submodel_cache.end()) return &it->second.ds;
        if (mi <= 0 || mi >= (int)map.Models().size()) return nullptr;
        auto groups = zq::app::BuildSubmodelGroups(map, mi);
        std::vector<zq::vk::VulkanImage*> texs(groups.size(), nullptr);
        for (size_t g = 0; g < groups.size(); g++) {
            int ti = groups[g].texture_index;
            if (ti >= 0 && ti < (int)map.Textures().size()) {
                const auto& tex = map.Textures()[ti];
                auto rgba = zq::app::IndexedToRGBA(tex, palette);
                texs[g] = vulkan.CreateTexture(tex.width, tex.height, rgba.data());
            }
            if (!texs[g]) {
                std::vector<uint8_t> fallback(16 * 16 * 4, 255);
                texs[g] = vulkan.CreateTexture(16, 16, fallback.data());
            }
        }
        SubmodelCache cache;
        for (size_t g = 0; g < groups.size(); g++) {
            if (groups[g].indices.empty()) continue;
            Drawable d;
            d.vbuf = vulkan.CreateVertexBuffer(groups[g].vertices.size() * sizeof(zq::app::WorldVertex));
            vulkan.UpdateBuffer(d.vbuf, groups[g].vertices.data(), groups[g].vertices.size() * sizeof(zq::app::WorldVertex));
            d.ibuf = vulkan.CreateIndexBuffer(groups[g].indices.size() * sizeof(uint32_t));
            vulkan.UpdateBuffer(d.ibuf, groups[g].indices.data(), groups[g].indices.size() * sizeof(uint32_t));
            d.index_count = (uint32_t)groups[g].indices.size();
            d.tex = texs[g];
            cache.ds.push_back(d);
            cache.base.push_back(groups[g].vertices);
        }
        auto res = submodel_cache.emplace(mi, std::move(cache));
        return &res.first->second.ds;
    };
    // Shared brush-submodel state, computed on the game thread (reads progs) and
    // applied on the render thread (Vulkan). `brush_hidden` = consumed pickups;
    // `brush_deltas` = per-submodel translation for MOVETYPE_PUSH doors.
    std::vector<bool> brush_hidden;
    std::map<int, std::array<float,3>> brush_deltas;
    auto compute_brush_state = [&]() {
        brush_hidden.assign(map.Models().size(), false);
        brush_deltas.clear();
        if (!progs.Loaded()) return;
        int f_model = progs.FindField("model");
        int f_modelindex = progs.FindField("modelindex");
        int f_solid = progs.FindField("solid");
        int f_movetype = progs.FindField("movetype");
        int f_origin = progs.FindField("origin");
        for (int i = 1; i < 1024; i++) {
            if (progs.EdictFree(i)) continue;
            const char* m = (f_model >= 0) ? progs.EdictFieldString(i, f_model) : "";
            int mi = (f_modelindex >= 0) ? (int)progs.EdictFieldFloat(i, f_modelindex) : 0;
            if (mi <= 0 || mi >= (int)map.Models().size()) continue;
            int solid = f_solid >= 0 ? (int)progs.EdictFieldFloat(i, f_solid) : 0;
            // consumed pickup: model cleared + non-solid -> hide
            if (!m[0] && solid == 0) { brush_hidden[mi] = true; continue; }
            // moving door: MOVETYPE_PUSH brush -> translation delta
            if (solid == zq::engine::SOLID_BSP &&
                f_movetype >= 0 && (int)progs.EdictFieldFloat(i, f_movetype) == zq::engine::MOVE_PUSH &&
                f_origin >= 0 && (!f_model || (m && m[0] == '*'))) {
                float o[3]; progs.EdictFieldVector(i, f_origin, o);
                brush_deltas[mi] = { o[0] - map.Models()[mi].origin[0],
                                     o[1] - map.Models()[mi].origin[1],
                                     o[2] - map.Models()[mi].origin[2] };
            }
        }
    };
    // Render-thread brush application: assemble drawables (non-hidden) + upload
    // door translations. Owns submodel_cache / brush_drawables (Vulkan only).
    auto render_brush = [&](const std::vector<bool>& hidden,
                            const std::map<int,std::array<float,3>>& deltas) {
        brush_drawables.clear();
        for (int mi = 1; mi < (int)map.Models().size(); mi++) {
            if (hidden[mi]) continue;
            const std::vector<Drawable>* ds = submodel_drawables(mi);
            if (!ds) continue;
            brush_drawables.insert(brush_drawables.end(), ds->begin(), ds->end());
        }
        for (auto& kv : deltas) {
            int mi = kv.first;
            auto it = submodel_cache.find(mi);
            if (it == submodel_cache.end() || it->second.base.empty()) continue;
            SubmodelCache& sc = it->second;
            const float* delta = kv.second.data();
            if (delta[0] == sc.applied[0] && delta[1] == sc.applied[1] &&
                delta[2] == sc.applied[2]) continue;
            for (size_t g = 0; g < sc.base.size(); g++) {
                auto verts = sc.base[g];
                for (auto& v : verts)
                    for (int k = 0; k < 3; k++) v.pos[k] += delta[k];
                vulkan.UpdateBuffer(sc.ds[g].vbuf, verts.data(),
                                    verts.size() * sizeof(zq::app::WorldVertex));
            }
            std::memcpy(sc.applied, delta, sizeof(delta));
        }
    };

    // TEMP-DIAG: env-gated in-app self test (doors/pickups/monsters). Remove.
    if (getenv("ZQ_SELFTEST") && progs.Loaded()) {
        std::printf("[SELFTEST] begin\n");
        int fc = progs.FindField("classname"), fo = progs.FindField("origin");
        int fma = progs.FindField("model"), fso = progs.FindField("solid");
        int fu = progs.FindField("use");
        int fam = progs.FindField("ammo_nails"), fmo = progs.FindField("movetype");
        int fth = progs.FindField("think"), fnx = progs.FindField("nextthink");
        int fmd = progs.FindField("movedir");
        int items = 0, doors = 0, monsters = 0;
        for (int i = 1; i < 1024; i++) {
            if (progs.EdictFree(i)) continue;
            const char* cn = fc >= 0 ? progs.EdictFieldString(i, fc) : "";
            if (!cn || !cn[0]) continue;
            if (strncmp(cn, "monster_", 8) == 0) {
                monsters++;
                if (monsters <= 6) {
                    float o[3]; progs.EdictFieldVector(i, fo, o);
                    printf("[SELFTEST] monster %d cn=%s o=(%.0f %.0f %.0f) think=%d nextthink=%.1f\n",
                        i, cn, o[0], o[1], o[2],
                        fth >= 0 ? progs.EdictFieldInt(i, fth) : 0,
                        fnx >= 0 ? progs.EdictFieldFloat(i, fnx) : 0);
                }
            } else if (strncmp(cn, "item_", 5) == 0 || strncmp(cn, "weapon_", 7) == 0) {
                items++;
                if (items <= 5) {
                    const char* m = fma >= 0 ? progs.EdictFieldString(i, fma) : "";
                    printf("[SELFTEST] item %d cn=%s model='%s' solid=%d touch=%d\n",
                        i, cn, m ? m : "", fso >= 0 ? (int)progs.EdictFieldFloat(i, fso) : -9,
                        progs.FindField("touch") >= 0 ? progs.EdictFieldInt(i, progs.FindField("touch")) : 0);
                }
            } else if (strcmp(cn, "door") == 0 || strcmp(cn, "func_door") == 0) {
                doors++;
            }
        }
        printf("[SELFTEST] counts items=%d stones=%d doors=%d\n", items, monsters, doors);

        int gun = -1;
        for (int i = 1; i < 1024; i++) {
            if (progs.EdictFree(i)) continue;
            const char* cn = fc >= 0 ? progs.EdictFieldString(i, fc) : "";
            if (cn && strcmp(cn, "weapon_nailgun") == 0) { gun = i; break; }
        }
        float p0[3]; progs.EdictFieldVector(1, fo, p0);
        float gn[3] = {0, 0, 0};
        if (gun > 0) progs.EdictFieldVector(gun, fo, gn);
        printf("[SELFTEST] client o=(%.1f %.1f %.1f) gun=%d gun.o=(%.0f %.0f %.0f)\n",
            p0[0], p0[1], p0[2], gun, gn[0], gn[1], gn[2]);
        if (gun > 0) {
            player.pos = Vec3{ gn[0], gn[1], gn[2] };
            float nails_before = fam >= 0 ? progs.EdictFieldFloat(1, fam) : 0;
            zq::engine::Input::Poll();
            for (int f = 0; f < 10; f++) {
                UpdateGameFrame(player, 0.1f, map, progs, kClientEdict);
                int ff = progs.FindField("flags");
                if (ff >= 0) progs.EdictFieldFloat(1, ff) = (float)((int)progs.EdictFieldFloat(1, ff) | 8);
            }
            float nails_after = fam >= 0 ? progs.EdictFieldFloat(1, fam) : 0;
            const char* m = fma >= 0 ? progs.EdictFieldString(gun, fma) : "";
            printf("[SELFTEST] pickup nails %.0f -> %.0f, gun model='%s' solid=%d => %s\n",
                nails_before, nails_after, m, fso >= 0 ? (int)progs.EdictFieldFloat(gun, fso) : -9,
                (nails_after > nails_before) ? "PICKUP-OK" : "PICKUP-FAIL");
        }

        // door open test
        int door = -1;
        for (int i = 1; i < 1024; i++) {
            if (progs.EdictFree(i)) continue;
            const char* cn = fc >= 0 ? progs.EdictFieldString(i, fc) : "";
            if (cn && (strcmp(cn, "door") == 0 || strcmp(cn, "func_door") == 0) && fu >= 0 && progs.EdictFieldInt(i, fu) > 0) { door = i; break; }
        }
        if (door > 0) {
            // +use handler verification: place the player on the approach side
            // of the door, aim at its brush centre, and run the same trace the
            // E-key handler uses; it must find THIS door.
            float b[3]; progs.EdictFieldVector(door, fo, b);
            float md[3] = {0,0,0}; if (fmd>=0) progs.EdictFieldVector(door, fmd, md);
            int f_mi = progs.FindField("modelindex");
            int mi = f_mi >= 0 ? (int)progs.EdictFieldFloat(door, f_mi) : 0;
            float ctr[3] = { b[0], b[1], b[2] };
            if (mi > 0 && mi < (int)map.Models().size()) {
                const auto& mm = map.Models()[mi];
                for (int k = 0; k < 3; k++) ctr[k] = (mm.mins[k] + mm.maxs[k]) * 0.5f;
            }
            float px = ctr[0] - md[0] * 110.0f, py = ctr[1] - md[1] * 110.0f;
            player.pos = Vec3{ px, py, ctr[2] - 24.0f };
            player.yaw = (float)(std::atan2(ctr[1] - py, ctr[0] - px) * 180.0 / 3.14159265);
            player.pitch = 0;
            zq::engine::Input::Poll();
            // Walk back toward the door until a clear approach spot exists.
            {
                float zp[3] = {0, 0, 0}, zo[3] = {0, 0, 0};
                for (float dd = 110.0f; dd >= 40.0f; dd -= 10.0f) {
                    float sx = ctr[0] - md[0] * dd, sy = ctr[1] - md[1] * dd;
                    float s[3] = { sx, sy, ctr[2] + 4.0f };
                    auto t = map.WorldTrace(s, ctr, zp, zo);
                    if (t.fraction >= 0.99f) {
                        px = sx; py = sy;
                        break;
                    }
                }
                player.pos = Vec3{ px, py, ctr[2] - 24.0f };
                player.yaw = (float)(std::atan2(ctr[1] - py, ctr[0] - px) * 180.0 / 3.14159265);
                player.pitch = 0;
            }
            // ---- copy of the +use handler trace ----
            {
                float pitch = player.pitch * 3.14159265f / 180.0f;
                float yaw = player.yaw * 3.14159265f / 180.0f;
                float dir[3] = { std::cos(pitch) * std::cos(yaw),
                                 std::cos(pitch) * std::sin(yaw),
                                 std::sin(pitch) };
                float start[3] = { player.pos.x, player.pos.y, player.pos.z + 28.0f };
                float end[3];
                for (int k = 0; k < 3; k++) end[k] = start[k] + dir[k] * 128.0f;
                std::vector<zq::engine::SolidEntity> solids;
                zq::engine::BuildSolidList(progs, map, solids, kClientEdict);
                float best_frac = 1.0f; int best = -1;
                int f_solid = progs.FindField("solid");
                const float umin[3] = {-16, -16, -24}, umax[3] = {16, 16, 32};
                for (int e = 1; e < 1024; e++) {
                    if (progs.EdictFree(e)) continue;
                    if (f_solid >= 0 && (int)progs.EdictFieldFloat(e, f_solid) == 0) continue;
                    if (fu < 0 || progs.EdictFieldInt(e, fu) <= 0) continue;
                    float frac = 1.0f;
                    int emi = f_mi >= 0 ? (int)progs.EdictFieldFloat(e, f_mi) : 0;
                    if (emi > 0 && emi < (int)map.Models().size()) {
                        float o[3] = {0, 0, 0};
                        if (fo >= 0) progs.EdictFieldVector(e, fo, o);
                        auto tr = map.ModelTrace(start, end, umin, umax, emi, o);
                        frac = tr.startsolid ? 0.0f : tr.fraction;
                    }
                    if (frac < best_frac) { best_frac = frac; best = e; }
                }
                printf("[SELFTEST] +use trace: best=%d (aimed door=%d) frac=%.3f => %s\n",
                    best, door, best_frac, best == door ? "USE-FIND-OK" : "USE-FIND-MISS");
                if (best > 0) {
                    const char* bcn = fc >= 0 ? progs.EdictFieldString(best, fc) : "?";
                    int bmi = f_mi >= 0 ? (int)progs.EdictFieldFloat(best, f_mi) : 0;
                    bool isdoor = bcn && (!strcmp(bcn, "door") || !strcmp(bcn, "func_door"));
                    printf("[SELFTEST]   best ent: cn='%s' modelindex=%d => %s\n",
                        bcn ? bcn : "?", bmi, isdoor ? "DOOR-HIT-OK" : "DOOR-HIT-MISS");
                }
            }
            progs.SetSelfEdict(door); progs.SetOtherEdict(1);
            progs.ExecuteProgram(progs.EdictFieldInt(door, fu));
            zq::engine::Input::Poll();
            // Step the player OUT of the door's trigger field so it can complete
            // its open->hold->close cycle (a door correctly stays open / reopens
            // while someone stands in its field - that would mask the close test).
            {
                float sp[3], sa[3];
                if (map.FindSpawnPoint(sp, sa)) player.pos = Vec3{ sp[0], sp[1], sp[2] };
            }
            // Track the door over a full open/hold/close cycle; flapping (the
            // ltime bug) would show the travel distance oscillating rapidly.
            float prevd = 0.0f, maxd = 0.0f;
            int endclose_ok = -1, flap_count = 0;
            float dprev = -1.0f, prev_dir = 0.0f;
            (void)prevd;
            for (int f = 0; f < 600; f++) {   // ~9.6s
                UpdateGameFrame(player, 0.016f, map, progs, kClientEdict);
                int ff = progs.FindField("flags");
                if (ff >= 0) progs.EdictFieldFloat(1, ff) = (float)((int)progs.EdictFieldFloat(1, ff) | 8);
                float dcur[3]; progs.EdictFieldVector(door, fo, dcur);
                float dc = 0; for (int q = 0; q < 3; q++) dc += (dcur[q]-b[q])*(dcur[q]-b[q]);
                dc = std::sqrt(dc);
                if (dc > maxd) maxd = dc;
                // count real direction reversals (open then close), not travel frames
                float ddelta = dc - dprev;
                if (dprev >= 0 && std::fabs(ddelta) > 2.0f && f > 5 && f < 560) {
                    if (prev_dir != 0 && ((ddelta > 0) != (prev_dir > 0))) flap_count++;
                    prev_dir = ddelta;
                }
                // closed after having opened >= 94?
                if (maxd > 90.0f && dc < 2.0f && endclose_ok < 0) endclose_ok = f;
                dprev = dc;
            }
            float a[3]; progs.EdictFieldVector(door, fo, a);
            float d = 0; for (int q = 0; q < 3; q++) d += (a[q]-b[q])*(a[q]-b[q]);
            printf("[SELFTEST] door %d movetype=%d movedir=(%.2f %.2f %.2f) o=(%.1f %.1f %.1f)->(%.1f %.1f %.1f) dist=%.1f max=%.1f closedAt=%d flapped=%d => %s\n",
                door, fmo >= 0 ? (int)progs.EdictFieldFloat(door, fmo) : -9, md[0], md[1], md[2],
                b[0], b[1], b[2], a[0], a[1], a[2], (float)sqrt(d), maxd, endclose_ok, flap_count,
                (maxd > 90.0f && endclose_ok >= 0 && flap_count < 4) ? "DOOR-OK" : "DOOR-FAIL");
        } else printf("[SELFTEST] no usable door\n");

        // monster hunt test: teleport the player next to a monster_army (the
        // movetogoal-based chaser) and verify its AI closes in on the player
        // smoothly - no single-frame teleports ("snap"), no freeze.
        int mon = -1;
        for (int i = 1; i < 1024; i++) {
            if (progs.EdictFree(i)) continue;
            const char* cn = fc >= 0 ? progs.EdictFieldString(i, fc) : "";
            if (cn && strcmp(cn, "monster_army") == 0) { mon = i; break; }
        }
        if (mon > 0) {
            float mb[3], minit[3];
            progs.EdictFieldVector(mon, fo, mb);
            for (int k = 0; k < 3; k++) minit[k] = mb[k];
            int fEnemy = progs.FindField("enemy"), fState = progs.FindField("state");
            player.pos = Vec3{ mb[0] - 128.0f, mb[1] + 64.0f, mb[2] + 24.0f };
            zq::engine::Input::Poll();
            float prev[3] = { mb[0], mb[1], mb[2] };
            float maxjump = 0.0f;
            int rpm = -1;
            int ffr = progs.FindField("frame");
            int prev_frame = -1, distinct_frames = 0;
            printf("[SELFTEST] army %d start o=(%.0f %.0f %.0f) think=%d\n",
                mon, mb[0], mb[1], mb[2],
                progs.FindField("think") >= 0 ? progs.EdictFieldInt(mon, progs.FindField("think")) : 0);
            for (int f = 0; f < 150; f++) {
                UpdateGameFrame(player, 0.05f, map, progs, kClientEdict);
                int ff = progs.FindField("flags");
                if (ff >= 0) progs.EdictFieldFloat(1, ff) = (float)((int)progs.EdictFieldFloat(1, ff) | 8);
                float mo[3]; progs.EdictFieldVector(mon, fo, mo);
                int fr = ffr >= 0 ? (int)progs.EdictFieldFloat(mon, ffr) : 0;
                if (fr != prev_frame) { distinct_frames++; prev_frame = fr; }
                float jump = (mo[0]-prev[0])*(mo[0]-prev[0]) + (mo[1]-prev[1])*(mo[1]-prev[1]);
                jump = std::sqrt(jump);
                if (jump > maxjump) maxjump = jump;
                if (f % 30 == 0) {
                    printf("[SELFTEST] m%03d army o=(%.0f %.0f %.0f) frame=%d distinct=%d enemy=%d state=%d\n",
                        f, mo[0], mo[1], mo[2], fr, distinct_frames,
                        fEnemy >= 0 ? progs.EdictFieldInt(mon, fEnemy) : -9,
                        fState >= 0 ? progs.EdictFieldInt(mon, fState) : -9);
                }
                if (jump > 40.0f && rpm < 0) rpm = f;   // > 40u/frame = snap
                for (int k = 0; k < 3; k++) prev[k] = mo[k];
            }
            float ma[3]; progs.EdictFieldVector(mon, fo, ma);
            float moved = 0;
            for (int k = 0; k < 3; k++) moved += (ma[k]-minit[k])*(ma[k]-minit[k]);
            printf("[SELFTEST] army end o=(%.0f %.0f %.0f) moved=%.0f maxjump=%.0f frames=%d snap=%s => %s\n",
                ma[0], ma[1], ma[2], (float)std::sqrt(moved), maxjump, distinct_frames,
                rpm >= 0 ? "YES" : "NO",
                (moved > 4.0f && rpm < 0 && maxjump < 40.0f && distinct_frames > 3) ? "MONSTER-ALIVE" : "MONSTER-DEAD");
        } else printf("[SELFTEST] no monster_army\n");
        printf("[SELFTEST] done done\n");
        zq::engine::Host::Instance().Quit();
    }

    bool running = true;
    bool mouse_captured = true;
    if (SDL_SetRelativeMouseMode(SDL_TRUE) < 0) mouse_captured = true;
    zq::log::Info("Controls: WASD move, mouse look, space jump, ` console");
    zq::log::Info("Use 'quit' command to exit");

    // ---- Ray-traced path (ZQ_RT=1): a compute-kernel renderer ----
    const bool rt_enabled = getenv("ZQ_RT") != nullptr;
    const float rt_scale = 0.75f; // internal render resolution scale (blit upscales)
    zq::render::RayTracer rt;
    if (rt_enabled) {
        if (!rt.Initialize(vulkan.GetPhysicalDevice(), vulkan.GetDevice(),
                           vulkan.GetGraphicsQueueFamily(), vulkan.GetGraphicsQueue(),
                           vulkan.GetSwapchainFormat())) {
            zq::log::Error("raytracer: init failed; falling back to raster");
        }
    }
    bool rt_built = false;
    uint32_t rtW = 0, rtH = 0;
    // Swapchain size, written by the render thread (owns Vulkan), read by the
    // game thread for the camera aspect. Init before the render thread starts.
    std::atomic<int> swap_w{0}, swap_h{0};
    uint64_t rt_entity_ver = 0;
    std::vector<zq::render::RtLight> rt_lights_all;
    std::vector<zq::render::RtLight> rt_lights_selected;
    std::map<std::pair<int,int>, std::vector<uint8_t>> rt_skin_cache;
    std::map<std::pair<int,int>, std::uint32_t> rt_skin_tile;
    // Cache of per-(model, frame) MDL geometry so rebuild_rt_scene doesn't
    // regenerate identical animation-frame triangles every tick (was ~56ms).
    std::map<std::pair<int,int>, std::pair<std::vector<zq::app::WorldVertex>,
                                           std::vector<uint32_t>>> rt_frame_cache;
    // Brush-submodel texture -> atlas tile (mi, tex) so moving door submodels can
    // be re-added dynamically each rebuild via AddMeshWithTile.
    std::map<std::pair<int,int>, std::uint32_t> rt_brush_tile;
    std::vector<zq::render::RtTriangle> rt_static_tris;
    std::vector<uint8_t> rt_atlas;
    std::vector<zq::render::RtTileInfo> rt_tile_infos;
    uint32_t rt_atlas_w = 0, rt_atlas_h = 0;
    std::vector<zq::render::RtTriangle> rt_entity_tris;
    bool rt_static_done = false;
    
    // Assemble the ray-traced scene (world + brush submodels + MDL entities)
    // into a triangle list + texture atlas, then upload to the GPU.
    auto rebuild_rt_scene = [&]() -> bool {
        if (!rt.IsInitialized()) return false;
        zq::app::RtSceneBuilder builder;
        // Rebuild the dynamic MDL entity triangles (positions/frames), using the
        // cached skin->tile indices so they reference the static atlas.
        auto build_entity_rt_tris = [&]() {
            zq::app::RtSceneBuilder eb;
            for (auto& e : entities) {
                if (e.model_index < 0 || e.model_index >= (int)models.size()) continue;
                LoadedModel* lm = &models[e.model_index];
                if (!lm->mdl) continue;
                if (e.frame < 0 || e.frame >= lm->mdl->NumFrames()) continue;
                // Cached per-(model, frame) geometry (verts + indices in model
                // space); only the first use builds it.
                auto fkey = std::make_pair(e.model_index, e.frame);
                auto fc = rt_frame_cache.find(fkey);
                if (fc == rt_frame_cache.end()) {
                    std::vector<zq::app::WorldVertex> fv;
                    std::vector<uint32_t> fi;
                    fi = build_mdl_frame(*lm->mdl, e.frame, fv);
                    if (fv.empty() || fi.empty()) continue;
                    fc = rt_frame_cache.emplace(fkey, std::make_pair(std::move(fv), std::move(fi))).first;
                }
                const auto& verts = fc->second.first;
                const auto& ids = fc->second.second;
                Mat4 t = Mat4::Translate(Vec3{ e.origin[0], e.origin[1], e.origin[2] });
                Mat4 r = Mat4::RotationZ(DegToRad(e.yaw));
                Mat4 sm = Mat4::Identity();
                sm.m[0] = e.scale; sm.m[5] = e.scale; sm.m[10] = e.scale;
                Mat4 model = t * r * sm;
                auto key = std::make_pair(e.model_index, e.skin >= 0 ? e.skin : 0);
                auto ti = rt_skin_tile.find(key);
                if (ti == rt_skin_tile.end()) continue; // unknown skin (rare)
                eb.AddMeshWithTile(verts.data(), verts.size(), sizeof(zq::app::WorldVertex),
                                   ids.data(), ids.size(), ti->second, model.m);
            }
            // Moving brush submodels (MOVETYPE_PUSH doors): render at their current
            // origin, translated by (edict.origin - submodel.origin), using the
            // texture tiles recorded during the static build.
            if (progs.Loaded()) {
                int f_mi = progs.FindField("modelindex"), f_s = progs.FindField("solid"),
                    f_mt = progs.FindField("movetype"), f_model = progs.FindField("model"),
                    f_origin = progs.FindField("origin");
                for (int i = 1; i < 1024; i++) {
                    if (progs.EdictFree(i)) continue;
                    int mi2 = (int)progs.EdictFieldFloat(i, f_mi);
                    if (mi2 < 1 || mi2 >= (int)map.Models().size()) continue;
                    if ((int)progs.EdictFieldFloat(i, f_s) != zq::engine::SOLID_BSP) continue;
                    if ((int)progs.EdictFieldFloat(i, f_mt) != zq::engine::MOVE_PUSH) continue;
                    if (f_model >= 0) {
                        const char* m = progs.EdictFieldString(i, f_model);
                        if (!(m && m[0] == '*')) continue;
                    }
                    float o[3];
                    progs.EdictFieldVector(i, f_origin, o);
                    float dx = o[0] - map.Models()[mi2].origin[0];
                    float dy = o[1] - map.Models()[mi2].origin[1];
                    float dz = o[2] - map.Models()[mi2].origin[2];
                    Mat4 t = Mat4::Translate(Vec3{ dx, dy, dz });
                    auto bgroups = zq::app::BuildSubmodelGroups(map, mi2);
                    for (const auto& grp : bgroups) {
                        if (grp.indices.empty()) continue;
                        int ti2 = grp.texture_index;
                        auto tt = rt_brush_tile.find({ mi2, ti2 });
                        if (tt == rt_brush_tile.end()) continue;
                        eb.AddMeshWithTile(grp.vertices.data(), grp.vertices.size(),
                                           sizeof(zq::app::WorldVertex),
                                           grp.indices.data(), grp.indices.size(),
                                           tt->second, t.m);
                    }
                }
            }
return eb.Triangles();
        };

        if (!rt_static_done) {
            // ---- FIRST BUILD: static world+brush BVH (built once) + atlas ----
            for (const auto& grp : groups) {
                if (grp.indices.empty()) continue;
                int ti = grp.texture_index;
                if (ti < 0 || ti >= (int)map.Textures().size()) continue;
                auto rgba = zq::app::IndexedToRGBA(map.Textures()[ti], palette);
                builder.AddMesh(grp.vertices.data(), grp.vertices.size(), sizeof(zq::app::WorldVertex),
                                grp.indices.data(), grp.indices.size(), rgba.data(),
                                map.Textures()[ti].width, map.Textures()[ti].height, nullptr);
            }
            for (int mi = 1; mi < (int)map.Models().size(); mi++) {
                // Doors (brush submodels owned by MOVETYPE_PUSH edicts) are added
                // to the DYNAMIC entity BVH each tick so they render at their
                // current (open/closed) position. Exclude them from the static
                // scene so there's no phantom closed-door geometry left behind;
                // register their textures here so the dynamic rebuild can reuse
                // a stable tile.
                bool door = false;
                if (progs.Loaded()) {
                    int f_mi = progs.FindField("modelindex"), f_s = progs.FindField("solid"),
                        f_mt = progs.FindField("movetype"), f_model = progs.FindField("model");
                    for (int i = 1; i < 1024 && !door; i++) {
                        if (progs.EdictFree(i)) continue;
                        if ((int)progs.EdictFieldFloat(i, f_mi) != mi) continue;
                        if ((int)progs.EdictFieldFloat(i, f_s) != zq::engine::SOLID_BSP) continue;
                        if ((int)progs.EdictFieldFloat(i, f_mt) != zq::engine::MOVE_PUSH) continue;
                        if (f_model >= 0) {
                            const char* m = progs.EdictFieldString(i, f_model);
                            if (!(m && m[0] == '*')) continue;
                        }
                        door = true;
                    }
                }
                auto bgroups = zq::app::BuildSubmodelGroups(map, mi);
                for (const auto& grp : bgroups) {
                    if (grp.indices.empty()) continue;
                    int ti = grp.texture_index;
                    if (ti < 0 || ti >= (int)map.Textures().size()) continue;
                    auto rgba = zq::app::IndexedToRGBA(map.Textures()[ti], palette);
                    if (door) {
                        uint32_t tile = builder.RegisterTexture(rgba.data(),
                                        map.Textures()[ti].width, map.Textures()[ti].height);
                        rt_brush_tile[{mi, ti}] = tile;
                        continue;
                    }
                    builder.AddMesh(grp.vertices.data(), grp.vertices.size(), sizeof(zq::app::WorldVertex),
                                    grp.indices.data(), grp.indices.size(), rgba.data(),
                                    map.Textures()[ti].width, map.Textures()[ti].height, nullptr);
                }
            }
            rt_static_tris = builder.Triangles(); // world + brush (tex 0..W)
            // Register entity skins into the atlas so entity triangles can
            // reference them; record each skin's tile index.
            for (auto& e : entities) {
                if (e.model_index < 0 || e.model_index >= (int)models.size()) continue;
                LoadedModel* lm = &models[e.model_index];
                if (!lm->mdl) continue;
                auto key = std::make_pair(e.model_index, e.skin >= 0 ? e.skin : 0);
                if (rt_skin_tile.count(key)) continue;
                std::vector<uint8_t> skinRgba;
                auto it = rt_skin_cache.find(key);
                if (it != rt_skin_cache.end()) skinRgba = it->second;
                else if (!lm->mdl->Skin(key.second).empty()) {
                    skinRgba = zq::app::IndexedToRGBA(lm->mdl->Skin(key.second).data(), lm->mdl->Skin(key.second).size(), palette);
                    rt_skin_cache[key] = skinRgba;
                } else skinRgba.assign((size_t)lm->mdl->SkinWidth()*lm->mdl->SkinHeight()*4, 200);
                uint32_t tile = builder.RegisterTexture(skinRgba.data(), lm->mdl->SkinWidth(), lm->mdl->SkinHeight());
                rt_skin_tile[key] = tile;
            }
            rt_atlas = builder.AtlasRgba();
            rt_tile_infos = builder.TileInfos();
            rt_static_done = true;
            rtW = (uint32_t)(swap_w.load() * rt_scale);
            rtH = (uint32_t)(swap_h.load() * rt_scale);
            if (rtW < 1) rtW = 1;
            if (rtH < 1) rtH = 1;
            rt_atlas_w = builder.AtlasWidth();
            rt_atlas_h = builder.AtlasHeight();
        }
        // Produce the dynamic entity triangle list (MDL entities + moving doors).
        rt_entity_tris = build_entity_rt_tris();
        return true;
    };

    uint32_t last_ticks = SDL_GetTicks();
    int frame = 0;

    // ===================== Two-thread split =====================
    // Game thread (this loop): input, physics/tick, entity refresh, camera,
    // and building the RT triangle list + lights. Publishes a value snapshot.
    // Render thread: owns Vulkan/RayTracer, does the rebuild upload, the RT
    // dispatch (or raster draw) and present -- decoupled from the game tick.

    // Build the static RT scene data once on the game thread (CPU). The render
    // thread uploads it to the GPU on its first frame.
    swap_w = vulkan.GetSwapchainWidth();
    swap_h = vulkan.GetSwapchainHeight();
    if (rt_enabled) rebuild_rt_scene();

    struct FrameSnapshot {
        bool valid = false;
        bool rt_enabled = false;
        float proj[16] = {0}, view[16] = {0};
        float eye[3] = {0,0,0};
        int sw = 0, sh = 0;
        std::vector<zq::render::RtLight> lights;
        struct EntDraw { int edict = 0, model_index = -1, frame = -1, skin = -1;
                         float o[3] = {0,0,0}; float yaw = 0, scale = 1; };
        std::vector<EntDraw> entity_draws;
        std::vector<bool> brush_hidden;
        std::map<int, std::array<float,3>> brush_deltas;
    };
    FrameSnapshot snapshot;
    std::mutex frame_mutex;
    bool render_stop = false;
    uint64_t snapshot_gen = 0;
    std::vector<bool> raster_hidden_prev;
    bool raster_hidden_prev_init = false;

    // BVH worker: builds the entity BVH on its own thread so the game thread
    // never stalls on BuildBvh. Game -> (pending tris) -> worker -> (built
    // tris+nodes) -> render thread uploads.
    std::mutex bvh_mutex;
    std::condition_variable bvh_cv;
    std::vector<zq::render::RtTriangle> bvh_pending;
    bool bvh_pending_ready = false;
    bool bvh_stop = false;
    std::vector<zq::render::RtTriangle> bvh_built_tris;
    std::vector<zq::render::BvhNode> bvh_built_nodes;
    uint64_t bvh_built_gen = 0;

    auto bvh_thread = std::thread([&]() {
        while (true) {
            std::vector<zq::render::RtTriangle> tris;
            {
                std::unique_lock<std::mutex> lk(bvh_mutex);
                bvh_cv.wait(lk, [&]{ return bvh_pending_ready || bvh_stop; });
                if (bvh_stop) return;
                tris = std::move(bvh_pending);
                bvh_pending_ready = false;
            }
            auto nodes = zq::render::BuildBvh(tris);   // reorders tris in place
            {
                std::lock_guard<std::mutex> lk(bvh_mutex);
                bvh_built_tris = std::move(tris);
                bvh_built_nodes = std::move(nodes);
                bvh_built_gen++;
            }
        }
    });

    auto render_thread = std::thread([&]() {
        uint32_t r_frames = 0, r_start = SDL_GetTicks();
        uint64_t last_gen = 0;
        FrameSnapshot f;
        bool have = false;
        while (true) {
            {
                std::lock_guard<std::mutex> lk(frame_mutex);
                if (render_stop) return;
                if (snapshot_gen != last_gen) {
                    f = snapshot;         // copy the latest published snapshot
                    last_gen = snapshot_gen;
                    have = true;
                }
            }
            // Render EVERY iteration, even with no new snapshot: keep presenting
            // at the render thread's own cadence using the latest camera. Only
            // skip until the first snapshot exists.
            if (!have) { SDL_Delay(1); continue; }

            if (f.rt_enabled) {
                if (!rt_built && !rt_static_tris.empty()) {
                    rt_built = rt.BuildScene(rt_static_tris, rt_atlas, rt_atlas_w, rt_atlas_h,
                                             rt_tile_infos, rtW, rtH);
                }
                if (rt_built) {
                    // Upload the latest BVH worker output (if a build finished).
                    std::vector<zq::render::RtTriangle> bt;
                    std::vector<zq::render::BvhNode> bn;
                    static uint64_t last_bvh_gen = 0;
                    {
                        std::lock_guard<std::mutex> lk(bvh_mutex);
                        if (bvh_built_gen != last_bvh_gen) {
                            bt = bvh_built_tris;
                            bn = bvh_built_nodes;
                            last_bvh_gen = bvh_built_gen;
                        }
                    }
                    if (!bt.empty()) rt.UpdateEntities(bt, bn);
                }
                if (!rt_built) continue;
                rt.SetLights(f.lights);
                if (!vulkan.BeginFrame(false)) continue;
                vulkan.SetCamera(f.proj, f.view);
                rt.Dispatch(vulkan.GetActiveCommandBuffer(), f.proj, f.view,
                            vulkan.GetSwapchainImageView(vulkan.GetCurrentImageIndex()),
                            vulkan.GetSwapchainWidth(), vulkan.GetSwapchainHeight());
                vulkan.EndFrame();
                vulkan.WaitIdle();
            } else {
                // Raster path.
                if (!raster_hidden_prev_init || f.brush_hidden != raster_hidden_prev) {
                    render_brush(f.brush_hidden, f.brush_deltas);
                    raster_hidden_prev = f.brush_hidden;
                    raster_hidden_prev_init = true;
                } else {
                    render_brush(f.brush_hidden, f.brush_deltas);
                }
                if (!vulkan.BeginFrame()) continue;
                vulkan.SetCamera(f.proj, f.view);
                for (auto& d : drawables) {
                    zq::vk::VulkanAPI::Mesh m;
                    m.vertex_buffer = d.vbuf; m.index_buffer = d.ibuf;
                    m.index_count = d.index_count; m.texture = d.tex;
                    m.lightmap = lightmap; m.has_model = false;
                    vulkan.DrawMesh(m);
                }
                for (auto& d : brush_drawables) {
                    zq::vk::VulkanAPI::Mesh m;
                    m.vertex_buffer = d.vbuf; m.index_buffer = d.ibuf;
                    m.index_count = d.index_count; m.texture = d.tex;
                    m.lightmap = lightmap; m.has_model = false;
                    vulkan.DrawMesh(m);
                }
                std::vector<zq::app::WorldVertex> scratch;
                for (auto& e : f.entity_draws) {
                    if (e.model_index < 0 || e.model_index >= (int)models.size()) continue;
                    LoadedModel* lm = &models[e.model_index];
                    if (!lm->mdl) continue;
                    zq::vk::VulkanBuffer* vbuf = mdl_ent_vbuf[e.edict];
                    if (!vbuf) {
                        vbuf = vulkan.CreateVertexBuffer(lm->vertex_count * sizeof(zq::app::WorldVertex));
                        mdl_ent_vbuf[e.edict] = vbuf;
                        mdl_ent_frame[e.edict] = -1;
                    }
                    if (e.frame >= 0 && e.frame < lm->mdl->NumFrames() &&
                        e.frame != mdl_ent_frame[e.edict]) {
                        build_mdl_frame(*lm->mdl, e.frame, scratch);
                        if (!scratch.empty()) {
                            vulkan.UpdateBuffer(vbuf, scratch.data(),
                                                scratch.size() * sizeof(zq::app::WorldVertex));
                            mdl_ent_frame[e.edict] = e.frame;
                        }
                    }
                    Mat4 t = Mat4::Translate(Vec3{ e.o[0], e.o[1], e.o[2] });
                    Mat4 r = Mat4::RotationZ(DegToRad(e.yaw));
                    Mat4 sm = Mat4::Identity();
                    sm.m[0] = e.scale; sm.m[5] = e.scale; sm.m[10] = e.scale;
                    Mat4 model = t * r * sm;
                    zq::vk::VulkanAPI::Mesh m;
                    m.vertex_buffer = vbuf; m.index_buffer = lm->ibuf;
                    m.index_count = lm->index_count;
                    m.texture = (e.skin >= 0 && e.skin < (int)lm->skins.size())
                                    ? lm->skins[e.skin] : lm->skins.empty() ? nullptr : lm->skins[0];
                    m.lightmap = lightmap; m.has_model = true;
                    memcpy(m.model, model.m, sizeof(m.model));
                    vulkan.DrawMesh(m);
                }
                vulkan.EndFrame();
            }

            r_frames++;
            uint32_t now = SDL_GetTicks();
            float el = (float)(now - r_start) / 1000.0f;
            if (el >= 0.5f) {
                char title[96];
                std::snprintf(title, sizeof(title), "zquake  %.0f FPS", r_frames / el);
                SDL_SetWindowTitle(window, title);
                r_frames = 0; r_start = now;
            }
        }
    });

    // ===================== Game thread loop =====================
    while (running && zq::engine::Host::Instance().GetState() != zq::engine::Host::State::Shutdown) {
        uint32_t now = SDL_GetTicks();
        float dt = (float)(now - last_ticks) / 1000.0f;
        last_ticks = now;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.1f) dt = 0.1f;

        // Input::Poll must run BEFORE event handling so prev_keys_ holds last
        // frame's state; otherwise IsKeyJustPressed never fires.
        zq::engine::Input::Poll();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            HandleEvent(event, running, mouse_captured, player);
        }

        if (player.pitch > 89.0f) player.pitch = 89.0f;
        if (player.pitch < -89.0f) player.pitch = -89.0f;

        if (progs.Loaded()) {
            // E = use (reference SV_UseEdicts): trace 64 units forward from
            // the eyes and call use() on the first solid entity that is hit.
            if (zq::engine::Input::WasKeyPressed(zq::engine::Key::E)) {
                int f_use = progs.FindField("use");
                float pitch = player.pitch * 3.14159265f / 180.0f;
                float yaw = player.yaw * 3.14159265f / 180.0f;
                float dir[3] = { std::cos(pitch) * std::cos(yaw),
                                 std::cos(pitch) * std::sin(yaw),
                                 std::sin(pitch) };
                float start[3] = { player.pos.x, player.pos.y, player.pos.z + 28.0f };
                float end[3];
                for (int k = 0; k < 3; k++) end[k] = start[k] + dir[k] * 128.0f;
                std::vector<zq::engine::SolidEntity> solids;
                zq::engine::BuildSolidList(progs, map, solids, kClientEdict);
                float best_frac = 1.0f;
                int best = -1;
                int f_mi = progs.FindField("modelindex");
                int f_org = progs.FindField("origin");
                int f_solid = progs.FindField("solid");
                for (int e = 1; e < 1024; e++) {
                    if (progs.EdictFree(e)) continue;
                    if (f_solid >= 0 && (int)progs.EdictFieldFloat(e, f_solid) == 0) continue;
                    if (f_use < 0 || progs.EdictFieldInt(e, f_use) <= 0) continue;
                    float frac = 1.0f;
                    int mi = f_mi >= 0 ? (int)progs.EdictFieldFloat(e, f_mi) : 0;
                    if (mi > 0 && mi < (int)map.Models().size()) {
                        float o[3] = {0, 0, 0};
                        if (f_org >= 0) progs.EdictFieldVector(e, f_org, o);
                        const float umin[3] = {-16, -16, -24};
                        const float umax[3] = {16, 16, 32};
                        auto tr = map.ModelTrace(start, end, umin, umax, mi, o);
                        frac = tr.startsolid ? 0.0f : tr.fraction;
                    } else {
                        float o[3] = {0, 0, 0};
                        if (f_org >= 0) progs.EdictFieldVector(e, f_org, o);
                        float em[3] = {0, 0, 0}, ex[3] = {0, 0, 0};
                        int f_min = progs.FindField("mins"), f_max = progs.FindField("maxs");
                        if (f_min >= 0) progs.EdictFieldVector(e, f_min, em);
                        if (f_max >= 0) progs.EdictFieldVector(e, f_max, ex);
                        frac = zq::engine::RayAABBFraction(start, end, o, em, ex);
                    }
                    if (frac < best_frac) { best_frac = frac; best = e; }
                }
                if (best > 0) {
                    progs.SetSelfEdict(best);
                    progs.SetOtherEdict(kClientEdict);
                    progs.ExecuteProgram(progs.EdictFieldInt(best, f_use));
                    zq::log::Info("use");
                }
            }
            // Full reference game frame: entity/TOSS physics, client thinks,
            // touch/pickup, and the player movement with entity collision.
            UpdateGameFrame(player, dt, map, progs, kClientEdict);
            if (getenv("ZQ_SCALE") && frame == 45) {
                for (auto& e : entities) {
                    if (e.model_index < 0 || e.model_index >= (int)models.size()) continue;
                    LoadedModel* lm = &models[e.model_index];
                    if (!lm->mdl) continue;
                    const char* cn = progs.Loaded() ? progs.EdictFieldString(e.edict, progs.FindField("classname")) : "";
                    if (!(cn && strncmp(cn,"monster",7)==0)) continue;
                    const float* sc = lm->mdl->Scale();
                    const float* so = lm->mdl->ScaleOrigin();
                    std::printf("SCALE ent%d %s scale=(%.4f %.4f %.4f) scale_origin=(%.2f %.2f %.2f)\n",
                        e.edict, cn, sc[0],sc[1],sc[2], so[0],so[1],so[2]);
                    int fm=progs.FindField("mins"), fx=progs.FindField("maxs");
                    float mn[3],mx[3]; progs.EdictFieldVector(e.edict,fm,mn); progs.EdictFieldVector(e.edict,fx,mx);
                    std::printf("SCALE ent%d mins=(%.0f %.0f %.0f) maxs=(%.0f %.0f %.0f)\n", e.edict, mn[0],mn[1],mn[2], mx[0],mx[1],mx[2]);
                    break;
                }
            }
        } else {
            // No game logic loaded: fall back to bare movement.
            zq::engine::PlayerPhys phys;
            phys.origin[0] = player.pos.x; phys.origin[1] = player.pos.y; phys.origin[2] = player.pos.z;
            phys.velocity[0] = player.vel.x; phys.velocity[1] = player.vel.y; phys.velocity[2] = player.vel.z;
            phys.angles[0] = player.pitch; phys.angles[1] = player.yaw; phys.angles[2] = 0;
            phys.onground = player.on_ground;
            zq::engine::PlayerCmd cmd;
            cmd.forwardmove = (zq::engine::Input::IsKeyDown(zq::engine::Key::WKEY) ? 320 : 0) -
                              (zq::engine::Input::IsKeyDown(zq::engine::Key::SKEY) ? 320 : 0);
            cmd.sidemove = (zq::engine::Input::IsKeyDown(zq::engine::Key::DKEY) ? 320 : 0) -
                           (zq::engine::Input::IsKeyDown(zq::engine::Key::AKEY) ? 320 : 0);
            cmd.jump = zq::engine::Input::IsKeyDown(zq::engine::Key::SPACE);
            static const zq::engine::MoveVars mv;
            zq::engine::RunPlayerMove(map, phys, cmd, dt, mv);
            player.pos.x = phys.origin[0]; player.pos.y = phys.origin[1]; player.pos.z = phys.origin[2];
            player.vel.x = phys.velocity[0]; player.vel.y = phys.velocity[1]; player.vel.z = phys.velocity[2];
            player.on_ground = phys.onground;
        }

        // Rebuild the visible MDL list from the live progs entities.
        refresh_entities();
        compute_brush_state();

        // Build the RT entity triangle list when the dynamic scene changed,
        // every VM tick (not throttled).
        bool rt_dirty = false;
        if (rt_enabled && rt_static_done) {
            uint64_t v = entities.size();
            for (auto& e : entities)
                v = v*31 + (uint64_t)(int)(e.origin[0]*4) + (uint64_t)(int)(e.origin[1]*4)
                    + (uint64_t)(int)(e.origin[2]*4) + (uint64_t)e.frame;
            if (progs.Loaded()) {
                int f_s = progs.FindField("solid"),
                    f_mt = progs.FindField("movetype"), f_origin = progs.FindField("origin");
                for (int i = 1; i < 1024; i++) {
                    if (progs.EdictFree(i)) continue;
                    if ((int)progs.EdictFieldFloat(i, f_s) != zq::engine::SOLID_BSP) continue;
                    if ((int)progs.EdictFieldFloat(i, f_mt) != zq::engine::MOVE_PUSH) continue;
                    float o[3]; progs.EdictFieldVector(i, f_origin, o);
                    v = v*31 + (uint64_t)(int)(o[0]*4) + (uint64_t)(int)(o[1]*4) + (uint64_t)(int)(o[2]*4);
                }
            }
            if (v != rt_entity_ver) {
                rt_entity_ver = v;
                rebuild_rt_scene();       // game thread: cheap, cached tris only
                rt_dirty = true;
            }
        } else if (rt_enabled && !rt_static_done) {
            rebuild_rt_scene();           // first build
            rt_dirty = true;
        }

        // Populate lights each frame (light entities spawn a few ticks in).
        if (rt_enabled) {
            rt_lights_all.clear();
            if (progs.Loaded()) {
                int fcn = progs.FindField("classname"), fo = progs.FindField("origin");
                int fli = progs.FindField("light"), fco = progs.FindField("color");
                for (int i = 1; i < 1024; i++) {
                    if (progs.EdictFree(i)) continue;
                    const char* c = fcn >= 0 ? progs.EdictFieldString(i, fcn) : "";
                    if (!c || strncmp(c, "light", 5) != 0) continue;
                    zq::render::RtLight L;
                    if (fo >= 0) progs.EdictFieldVector(i, fo, L.pos);
                    float inten = fli >= 0 ? progs.EdictFieldFloat(i, fli) : 1.0f;
                    if (inten <= 0) inten = 1.0f;
                    if (fco >= 0) progs.EdictFieldVector(i, fco, L.color);
                    if (L.color[0]==0 && L.color[1]==0 && L.color[2]==0) { L.color[0]=L.color[1]=L.color[2]=1; }
                    L.intensity = inten * 2.0f;
                    L.radius = std::min(4000.0f, 1000.0f + inten * 8.0f);
                    rt_lights_all.push_back(L);
                }
            }
            // nearest-k to the eye
            int k = (int)std::min<size_t>(rt_lights_all.size(), 12);
            std::vector<zq::render::RtLight> near(k);
            for (int i = 0; i < k; i++) near[i] = rt_lights_all[i];
            auto dist = [&](const zq::render::RtLight& L){
                float dx=L.pos[0]-player.pos.x, dy=L.pos[1]-player.pos.y, dz=L.pos[2]-player.pos.z;
                return dx*dx+dy*dy+dz*dz;
            };
            for (size_t i = k; i < rt_lights_all.size(); i++) {
                int worst = 0;
                for (int j = 1; j < k; j++) if (dist(near[j]) > dist(near[worst])) worst = j;
                if (dist(rt_lights_all[i]) < dist(near[worst])) near[worst] = rt_lights_all[i];
            }
            rt_lights_selected = std::move(near);
        }

        // Camera.
        float pitch_rad = DegToRad(player.pitch);
        float yaw_rad = DegToRad(player.yaw);
        Vec3 cam_dir = {
            std::cos(pitch_rad) * std::cos(yaw_rad),
            std::cos(pitch_rad) * std::sin(yaw_rad),
            std::sin(pitch_rad)
        };
        Vec3 eye = player.pos;
        eye.z += 28.0f;
        Vec3 center = eye + cam_dir;
        float aspect = (float)vulkan.GetSwapchainWidth() / (float)vulkan.GetSwapchainHeight();
        if (aspect < 0.1f) aspect = 16.0f / 9.0f;
        Mat4 proj = Mat4::Perspective(75.0f, aspect, 0.1f, 4096.0f);
        Mat4 view = Mat4::LookAt(eye, center, Vec3{ 0, 0, 1 });

        // Raster entity draw snapshot.
        FrameSnapshot f;
        f.valid = true;
        f.rt_enabled = rt_enabled;
        for (int i = 0; i < 16; i++) { f.proj[i] = proj.m[i]; f.view[i] = view.m[i]; }
        f.eye[0] = eye.x; f.eye[1] = eye.y; f.eye[2] = eye.z;
        f.sw = vulkan.GetSwapchainWidth(); f.sh = vulkan.GetSwapchainHeight();
        if (rt_enabled) {
            f.lights = rt_lights_selected;
            // Hand the new triangle list to the BVH worker thread (not the game
            // thread); the render thread uploads the worker's built BVH.
            if (rt_dirty) {
                {
                    std::lock_guard<std::mutex> lk(bvh_mutex);
                    bvh_pending = rt_entity_tris;
                    bvh_pending_ready = true;
                }
                bvh_cv.notify_one();
            }
        }
        for (auto& e : entities) {
            FrameSnapshot::EntDraw ed;
            ed.edict = e.edict; ed.model_index = e.model_index; ed.frame = e.frame;
            ed.skin = e.skin; ed.yaw = e.yaw; ed.scale = e.scale;
            ed.o[0] = e.origin[0]; ed.o[1] = e.origin[1]; ed.o[2] = e.origin[2];
            f.entity_draws.push_back(ed);
        }
        f.brush_hidden = brush_hidden;
        f.brush_deltas = brush_deltas;

        {
            std::lock_guard<std::mutex> lk(frame_mutex);
            snapshot = std::move(f);
            snapshot_gen++;
        }

        // Pace the game thread to a target tick rate. It's decoupled from the
        // render thread (which may be slower); latest-wins snapshots keep the
        // render current without over-driving the game logic or burning CPU.
        uint32_t tick_elapsed = SDL_GetTicks() - now;
        int sleep_ms = 14 - (int)tick_elapsed;   // ~72 Hz
        if (sleep_ms < 1) sleep_ms = 1;
        SDL_Delay(sleep_ms);
        frame++;
    }

    // Shutdown the render thread.
    {
        std::lock_guard<std::mutex> lk(frame_mutex);
        render_stop = true;
    }
    render_thread.join();
    // Stop and join the BVH worker thread.
    {
        std::lock_guard<std::mutex> lk(bvh_mutex);
        bvh_stop = true;
    }
    bvh_cv.notify_all();
    bvh_thread.join();

    SDL_StopTextInput();
    zq::engine::Host::Instance().Shutdown();
    pak.Close();
    if (rt_enabled) rt.Shutdown();
    vulkan.Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
