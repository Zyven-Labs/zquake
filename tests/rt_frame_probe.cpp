// Headless GPU render probe: reproduces the app's exact ray-traced "shotgun
// just fired" frame (first-person v_shot viewmodel + orange muzzle glow quad +
// spike pellet) and writes PPMs so the muzzle-flash / projectile visuals can be
// inspected without a display. Not a (pass/fail) test.
#include "vulkan/vulkan_api.hpp"
#include "render/raytracer.hpp"
#include "render/ray_scene.hpp"
#include "app/rt_scene.hpp"
#include "app/world_builder.hpp"
#include "engine/mdl_model.hpp"
#include "filesystem/pak_archive.hpp"
#include "core/math/mathf.hpp"
#include "core/logging/logger.hpp"
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <memory>

using zq::mathf::Vec3;

namespace {

struct Kernel {
    zq::render::RayTracer rt;
    std::vector<uint8_t> palette;
};

void LoadPalette(zq::fs::PAKArchive& pak, std::vector<uint8_t>& pal) {
    pal.assign(768, 0);
    std::vector<uint8_t> d(768);
    if (pak.ReadFile("gfx/palette.lmp", d.data(), d.size())) {
        pal = d;
    } else {
        for (int i = 0; i < 256; i++) {
            uint8_t v = (uint8_t)((i < 64) ? 0 : (i*4-64));
            pal[i*3+0] = v; pal[i*3+1] = v; pal[i*3+2] = v;
        }
    }
}

bool LoadMdl(zq::fs::PAKArchive& pak, const char* path, zq::engine::MdlModel& mdl) {
    for (auto& e : pak.GetEntries()) {
        if (std::strncmp(e.name, path, 55) == 0) {
            std::vector<uint8_t> d(e.file_size);
            if (!pak.ReadFile(path, d.data(), d.size())) return false;
            return mdl.Load(d.data(), d.size());
        }
    }
    return false;
}

void MdlToWorld(const zq::engine::MdlModel& mdl, int frame,
                std::vector<zq::app::WorldVertex>& verts,
                std::vector<uint32_t>& ids) {
    std::vector<float> v, uv;
    mdl.BuildMesh(frame, v, uv, ids);
    verts.assign(v.size() / 3, zq::app::WorldVertex{});
    for (size_t i = 0; i < verts.size(); i++) {
        verts[i].pos[0] = v[i*3+0]; verts[i].pos[1] = v[i*3+1]; verts[i].pos[2] = v[i*3+2];
        verts[i].uv[0] = uv[i*2+0]; verts[i].uv[1] = uv[i*2+1];
        verts[i].normal[0] = 0; verts[i].normal[1] = 0; verts[i].normal[2] = 1;
        verts[i].lmuv[0] = 0.5f; verts[i].lmuv[1] = 0.5f; verts[i].lmuv[2] = 0;
    }
}

// Mirrors entry_point.cpp addGlowQuad exactly.
void AddGlowQuad(zq::app::RtSceneBuilder& eb,
                 const float c[3], const float basisR[3], const float basisU[3],
                 float halfR, float halfU, std::uint32_t tile) {
    zq::app::WorldVertex q[4];
    for (int k = 0; k < 4; k++) {
        q[k].pos[0] = c[0]; q[k].pos[1] = c[1]; q[k].pos[2] = c[2];
        q[k].uv[0] = (k == 1 || k == 2) ? 1.0f : 0.0f;
        q[k].uv[1] = (k >= 2) ? 1.0f : 0.0f;
        q[k].normal[0] = 0; q[k].normal[1] = 0; q[k].normal[2] = 1;
        q[k].lmuv[0] = 0.5f; q[k].lmuv[1] = 0.5f; q[k].lmuv[2] = 0;
    }
    float dx0 = -basisR[0]*halfR, dy0 = -basisR[1]*halfR, dz0 = -basisR[2]*halfR;
    float dx1 = +basisR[0]*halfR, dy1 = +basisR[1]*halfR, dz1 = +basisR[2]*halfR;
    float du0 = -basisU[0]*halfU, dv0 = -basisU[1]*halfU, dw0 = -basisU[2]*halfU;
    float du1 = +basisU[0]*halfU, dv1 = +basisU[1]*halfU, dw1 = +basisU[2]*halfU;
    q[0].pos[0] += dx0 + du0; q[0].pos[1] += dy0 + dv0; q[0].pos[2] += dz0 + dw0;
    q[1].pos[0] += dx1 + du0; q[1].pos[1] += dy1 + dv0; q[1].pos[2] += dz1 + dw0;
    q[2].pos[0] += dx1 + du1; q[2].pos[1] += dy1 + dv1; q[2].pos[2] += dz1 + dw1;
    q[3].pos[0] += dx0 + du1; q[3].pos[1] += dy0 + dv1; q[3].pos[2] += dz0 + dw1;
    const std::uint32_t ids[6] = { 0, 1, 2, 0, 2, 3 };
    eb.AddMeshWithTile(q, 4, sizeof(zq::app::WorldVertex), ids, 6, tile, nullptr, true);
}

int RenderFrame(zq::render::RayTracer& rt, uint32_t W, uint32_t H,
                zq::engine::MdlModel& vshot, zq::engine::MdlModel& spike,
                const std::vector<uint8_t>& pal, const char* outPpm,
                bool oldIndexedSkin, bool spikeOnly) {
    zq::app::RtSceneBuilder b;

    // Viewmodel + spike skins. oldIndexedSkin == true uses the OLD opaque
    // IndexedToRGBA (palette index 0 stays opaque black), mirroring the
    // pre-fix appearance; false uses IndexedToRGBA_Model (index 0 -> alpha 0).
    auto sshot = oldIndexedSkin
        ? zq::app::IndexedToRGBA(vshot.Skin(0).data(), vshot.Skin(0).size(), pal.data())
        : zq::app::IndexedToRGBA_Model(vshot.Skin(0).data(), vshot.Skin(0).size(), pal.data());
    uint32_t tshot = b.RegisterTexture(sshot.data(), vshot.SkinWidth(), vshot.SkinHeight());
    auto sspike = oldIndexedSkin
        ? zq::app::IndexedToRGBA(spike.Skin(0).data(), spike.Skin(0).size(), pal.data())
        : zq::app::IndexedToRGBA_Model(spike.Skin(0).data(), spike.Skin(0).size(), pal.data());
    uint32_t tspike = b.RegisterTexture(sspike.data(), spike.SkinWidth(), spike.SkinHeight());

    // Muzzle + particle soft-dot tiles (copied from the app).
    std::vector<uint8_t> dot(64 * 64 * 4);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            float dx = (x + 0.5f - 32.0f) / 32.0f;
            float dy = (y + 0.5f - 32.0f) / 32.0f;
            float r = std::sqrt(dx*dx + dy*dy);
            uint8_t a = (uint8_t)(std::max(0.0f, 1.0f - r) * 255.0f);
            uint8_t* p = &dot[(y * 64 + x) * 4];
            p[0] = 200; p[1] = 200; p[2] = 200; p[3] = a;
        }
    uint32_t tpart = b.RegisterTexture(dot.data(), 64, 64);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            float dx = (x + 0.5f - 32.0f) / 32.0f;
            float dy = (y + 0.5f - 32.0f) / 32.0f;
            float r = std::sqrt(dx*dx + dy*dy);
            uint8_t a = (uint8_t)(std::max(0.0f, 1.0f - r) * 255.0f);
            uint8_t* p = &dot[(y * 64 + x) * 4];
            p[0] = 255; p[1] = 140; p[2] = 46; p[3] = a;
        }
    uint32_t tmuzzle = b.RegisterTexture(dot.data(), 64, 64);

    // Camera + gun (standing, no bob) exactly as the app computes them.
    float eye_f[3] = {1,0,0}, eye_r[3] = {0,-1,0}, eye_u[3] = {0,0,1};
    float eye_pos[3] = {0,0,28};
    const float kFudge = 2.0f;
    float gun_x = eye_pos[0] + eye_f[0]*0.0f - 1.0f/32.0f;
    float gun_y = eye_pos[1] + eye_f[1]*0.0f - 1.0f/32.0f;
    float gun_z = eye_pos[2] + eye_f[2]*0.0f + kFudge - 1.0f/32.0f;
    float muzzle_pos[3] = { gun_x + eye_f[0]*38.0f, gun_y + eye_f[1]*38.0f, gun_z + eye_f[2]*38.0f };
    float muzzle_light_pos[3] = { gun_x + eye_f[0]*22.0f, gun_y + eye_f[1]*22.0f, gun_z + eye_f[2]*22.0f + 16.0f };

    // Viewmodel mesh (frame 2: firing pose), vm matrix replicating the app.
    std::vector<zq::app::WorldVertex> gunV; std::vector<uint32_t> gunI;
    if (!spikeOnly) {
        MdlToWorld(vshot, 2, gunV, gunI);
        float M[16] = {0};
        M[0] = eye_f[0]; M[1] = eye_f[1]; M[2] = eye_f[2];
        M[4] = -eye_r[0]; M[5] = -eye_r[1]; M[6] = -eye_r[2];
        M[8] = eye_u[0]; M[9] = eye_u[1]; M[10] = eye_u[2];
        M[12] = gun_x; M[13] = gun_y; M[14] = gun_z; M[15] = 1;
        b.AddMeshWithTile(gunV.data(), gunV.size(), sizeof(zq::app::WorldVertex),
                          gunI.data(), gunI.size(), tshot, M);
    }

    // Muzzle glow quad (arms frame: timer=0.10 -> t=1 -> half=6).
    if (!spikeOnly) {
        float half = 2.0f + 4.0f * 1.0f;
        AddGlowQuad(b, muzzle_pos, eye_r, eye_u, half, half, tmuzzle);
    }

    // Pellet: spike.mdl mid-flight just ahead of the eye (scale 1 so the
    // geometry is visible; the real QC sets .scale=0 which collapses it).
    std::vector<zq::app::WorldVertex> pV; std::vector<uint32_t> pI;
    MdlToWorld(spike, 0, pV, pI);
    float py = spikeOnly ? 0.0f : 20.0f;
    float pz = spikeOnly ? 22.0f : 16.0f;
    zq::mathf::Mat4 t = zq::mathf::Mat4::Translate(Vec3{ 50.0f, py, pz });
    b.AddMeshWithTile(pV.data(), pV.size(), sizeof(zq::app::WorldVertex),
                      pI.data(), pI.size(), tspike, t.m);

    // Floor so the scene has geometry to light.
    struct V { float pos[3]; float uv[2]; };
    if (!spikeOnly) {
        std::vector<uint8_t> floorTex(64*64*4, 255);
        for (size_t k = 0; k < floorTex.size(); k += 4) {
            int ix = (int)((k/4) % 64), iy = (int)((k/4) / 64);
            uint8_t g = (uint8_t)(70 + 30*(((ix/8)+(iy/8))%2));
            floorTex[k]=g; floorTex[k+1]=g; floorTex[k+2]=g; floorTex[k+3]=255;
        }
        V verts[4] = {
            {{-200, -200, 0},  {0,0}},
            {{ 200, -200, 0},  {1,0}},
            {{ 200,  200, 0},  {1,1}},
            {{-200,  200, 0},  {0,1}},
        };
        std::uint32_t idx[6] = {0,1,2,0,2,3};
        b.AddMesh(verts, 4, sizeof(V), idx, 6, floorTex.data(), 64, 64, nullptr);
    }

    // Floor light from the viewmodel light keeps the scene from being pitch black.
    std::vector<zq::render::RtLight> lights;
    {
        zq::render::RtLight vL;
        if (spikeOnly) {
            vL.pos[0] = 50.0f; vL.pos[1] = 20.0f; vL.pos[2] = 16.0f;
            vL.color[0] = 1.0f; vL.color[1] = 1.0f; vL.color[2] = 1.0f;
            vL.intensity = 3.0f;
            vL.radius = 300.0f;
            lights.push_back(vL);
        } else {
        zq::render::RtLight mL;
        mL.pos[0] = muzzle_light_pos[0]; mL.pos[1] = muzzle_light_pos[1]; mL.pos[2] = muzzle_light_pos[2];
        mL.color[0] = 1.0f; mL.color[1] = 0.5f; mL.color[2] = 0.15f;
        mL.intensity = 3.0f * 1.3f * 1.0f;
        mL.radius = 565.0f;
        lights.push_back(mL);
        zq::render::RtLight vL;
        vL.pos[0] = muzzle_pos[0]; vL.pos[1] = muzzle_pos[1]; vL.pos[2] = muzzle_pos[2];
        vL.color[0] = 0.42f; vL.color[1] = 0.40f; vL.color[2] = 0.36f;
        vL.intensity = 0.55f;
        vL.radius = 200.0f;
        lights.push_back(vL);
        }
    }

    auto tris = b.Triangles();
    float proj[16], view[16];
    auto pm = zq::mathf::Mat4::Perspective(75.0f, (float)W/H, 0.1f, 4096.0f);
    Vec3 eye = { 0, 0, 28 };
    Vec3 center = { 2, 0, 28 }; // eye + fwd
    auto vm = zq::mathf::Mat4::LookAt(eye, center, Vec3{ 0, 0, 1 });
    std::memcpy(proj, pm.m, sizeof(proj));
    std::memcpy(view, vm.m, sizeof(view));

    if (!rt.BuildScene(tris, b.AtlasRgba(), b.AtlasWidth(), b.AtlasHeight(), b.TileInfos(), W, H)) {
        std::printf("FRAMEPROBE BuildScene FAILED\n");
        return 1;
    }
    rt.SetLights(lights);
    std::vector<uint8_t> px;
    if (!rt.TraceToCPU(proj, view, W, H, px)) {
        std::printf("FRAMEPROBE TraceToCPU FAILED\n");
        return 1;
    }
    FILE* f = fopen(outPpm, "wb");
    if (!f) return 1;
    fprintf(f, "P6\n%u %u\n255\n", W, H);
    for (size_t i = 0; i + 3 < px.size(); i += 4)
        fputc(px[i], f), fputc(px[i+1], f), fputc(px[i+2], f);
    fclose(f);
    std::printf("FRAMEPROBE wrote %s (%u triangles)\n", outPpm, (unsigned)tris.size());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    zq::log::SetLogger(&zq::log::ConsoleLogger::Instance());
    zq::log::SetMinLevel(zq::log::LogLevel::Info);

    const char* pakPath = (argc > 1) ? argv[1] : "id1/pak0.pak";

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    SDL_Window* window = SDL_CreateWindow("zquake-frame-probe",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          320, 240, SDL_WINDOW_VULKAN);
    if (!window) { SDL_Quit(); return 1; }

    zq::vk::VulkanAPI vk;
    if (!vk.Initialize(window)) {
        zq::log::Error("Vulkan init failed");
        SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }

    zq::fs::PAKArchive pak;
    if (!pak.Open(pakPath)) {
        std::printf("cannot open %s\n", pakPath);
        vk.Shutdown(); SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }

    std::vector<uint8_t> pal;
    LoadPalette(pak, pal);
    zq::engine::MdlModel vshot, spike;
    if (!LoadMdl(pak, "progs/v_shot.mdl", vshot)) { std::printf("no v_shot\n"); return 1; }
    if (!LoadMdl(pak, "progs/spike.mdl", spike)) { std::printf("no spike\n"); return 1; }

    zq::render::RayTracer rt;
    if (!rt.Initialize(vk.GetPhysicalDevice(), vk.GetDevice(),
                       vk.GetGraphicsQueueFamily(), vk.GetGraphicsQueue(),
                       vk.GetSwapchainFormat())) {
        zq::log::Error("RayTracer init failed");
        vk.Shutdown(); SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }

    uint32_t W = vk.GetSwapchainWidth();
    uint32_t H = vk.GetSwapchainHeight();

    int rc = RenderFrame(rt, W, H, vshot, spike, pal, "/tmp/rt_muzzle.ppm", false, false);
    rc |= RenderFrame(rt, W, H, vshot, spike, pal, "/tmp/rt_muzzle_old.ppm", true, false);
    rc |= RenderFrame(rt, W, H, vshot, spike, pal, "/tmp/rt_spike.ppm", false, true);
    rc |= RenderFrame(rt, W, H, vshot, spike, pal, "/tmp/rt_spike_old.ppm", true, true);

    rt.Shutdown();
    vk.Shutdown();
    SDL_DestroyWindow(window); SDL_Quit();
    return rc;
}