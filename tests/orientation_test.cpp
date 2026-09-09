// Camera / mouse-mapping verification harness. Loads the real e1m1 map and
// renders it offscreen at known yaw/pitch so the camera matrices can be
// inspected. (The old demo-room wall-grey assertions don't apply to real
// textured maps, so it reports camera data only.)
#include "vulkan/vulkan_api.hpp"
#include "app/world_builder.hpp"
#include "engine/bsp.hpp"
#include "filesystem/pak_archive.hpp"
#include "core/math/mathf.hpp"
#include "core/logging/logger.hpp"
#include <SDL.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>

using zq::mathf::Vec3;
using zq::mathf::Mat4;

namespace {
struct Drawable {
    zq::vk::VulkanBuffer* vbuf = nullptr;
    zq::vk::VulkanBuffer* ibuf = nullptr;
    uint32_t index_count = 0;
    zq::vk::VulkanImage* tex = nullptr;
};

constexpr float DegToRad(float d) { return d * 0.0174532925f; }

// Return the dominant grey level in a small region around screen center.
int CenterGrey(const std::vector<uint8_t>& px, int w, int h) {
    // framebuffer is BGRA or RGBA depending on swapchain; use the byte that
    // corresponds to brightness: average the three channels.
    int cx = w / 2, cy = h / 2;
    float sum = 0;
    int n = 0;
    for (int dy = -4; dy <= 4; dy += 2) {
        for (int dx = -4; dx <= 4; dx += 2) {
            size_t o = ((cy + dy) * w + (cx + dx)) * 4;
            int r = px[o + 2], g = px[o + 1], b = px[o + 0];
            sum += (r + g + b) / 3.0f;
            n++;
        }
    }
    return n ? (int)(sum / n) : -1;
}

// Build view/projection for a camera at eye looking along yaw/pitch.
Mat4 CameraMatrices(Vec3 eye, float yaw, float pitch, int w, int h, Mat4& proj, Mat4& view) {
    float pr = DegToRad(pitch), yr = DegToRad(yaw);
    Vec3 dir = {
        std::cos(pr) * std::cos(yr),
        std::cos(pr) * std::sin(yr),
        std::sin(pr)
    };
    Vec3 center = eye + dir;
    float aspect = (float)w / (float)h;
    proj = Mat4::Perspective(75.0f, aspect, 0.1f, 4096.0f);
    view = Mat4::LookAt(eye, center, Vec3{0, 0, 1});
    return proj * view; // not used; kept for interface symmetry
}
}

int main() {
    zq::log::SetLogger(&zq::log::ConsoleLogger::Instance());
    zq::log::SetMinLevel(zq::log::LogLevel::Error);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    SDL_Window* win = SDL_CreateWindow("orient", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       640, 480, SDL_WINDOW_VULKAN);
    if (!win) { SDL_Quit(); return 1; }

    zq::vk::VulkanAPI vk;
    if (!vk.Initialize(win)) { SDL_DestroyWindow(win); SDL_Quit(); return 1; }
    int W = vk.GetSwapchainWidth(), H = vk.GetSwapchainHeight();

    // Load e1m1 from the game PAK.
    zq::fs::PAKArchive pak;
    if (!pak.Open("id1/pak0.pak")) { vk.Shutdown(); SDL_DestroyWindow(win); SDL_Quit(); return 1; }
    std::vector<uint8_t> mapdata;
    for (auto& e : pak.GetEntries())
        if (std::strcmp(e.name, "maps/e1m1.bsp") == 0) {
            mapdata.resize(e.file_size);
            pak.ReadFile(e.name, mapdata.data(), mapdata.size());
            break;
        }
    if (mapdata.empty()) { vk.Shutdown(); SDL_DestroyWindow(win); SDL_Quit(); return 1; }
    zq::engine::BSPMap map;
    if (!map.Load(mapdata.data(), mapdata.size())) { vk.Shutdown(); SDL_DestroyWindow(win); SDL_Quit(); return 1; }
    zq::log::Info("orientation test: e1m1 loaded (camera/mouse mapping check disabled for real maps)");

    uint8_t palette[768];
    for (int i = 0; i < 256; i++) {
        palette[i*3] = palette[i*3+1] = palette[i*3+2] = (uint8_t)i; // index i -> grey i
    }

    auto groups = zq::app::BuildGroups(map);
    std::vector<Drawable> drawables;
    for (auto& g : groups) {
        if (g.indices.empty()) continue;
        Drawable d;
        auto rgba = zq::app::IndexedToRGBA(map.Textures()[g.texture_index], palette);
        d.tex = vk.CreateTexture(map.Textures()[g.texture_index].width,
                                 map.Textures()[g.texture_index].height, rgba.data());
        if (!d.tex) continue;
        d.vbuf = vk.CreateVertexBuffer(g.vertices.size() * sizeof(zq::app::WorldVertex));
        vk.UpdateBuffer(d.vbuf, g.vertices.data(), g.vertices.size() * sizeof(zq::app::WorldVertex));
        d.ibuf = vk.CreateIndexBuffer(g.indices.size() * sizeof(uint32_t));
        vk.UpdateBuffer(d.ibuf, g.indices.data(), g.indices.size() * sizeof(uint32_t));
        d.index_count = (uint32_t)g.indices.size();
        drawables.push_back(d);
    }

    float white[4] = {1,1,1,1};
    zq::vk::VulkanImage* lightmap = vk.CreateLightmap(white);

    // Player eye at spawn (origin 0,0,40) + standard view height ~28
    Vec3 eye = {0, 0, 40 + 28};

    // RenderMeshToImage renders ONE mesh to a fresh offscreen image. So we
    // render each wall drawable individually and decide which wall is at the
    // screen center by which one produces a non-background center pixel.
    auto renderWallAt = [&](const Drawable& d, float yaw, float pitch,
                            std::vector<uint8_t>& out) {
        Mat4 proj, view;
        CameraMatrices(eye, yaw, pitch, W, H, proj, view);
        vk.SetCamera(proj.m, view.m);
        zq::vk::VulkanAPI::Mesh mesh;
        mesh.vertex_buffer = d.vbuf;
        mesh.index_buffer = d.ibuf;
        mesh.index_count = d.index_count;
        mesh.texture = d.tex;
        mesh.lightmap = lightmap;
        out.clear();
        return vk.RenderMeshToImage(W, H, mesh, out);
    };

    constexpr int BG_GREY = 19; // navy clear (13,15,31)
    const int wall_greys[6] = { 240, 20, 60, 180, 140, 100 };

    // Returns the grey of whichever wall is centered, or -1 if none.
    auto centeredWallAt = [&](float yaw, float pitch) -> int {
        std::vector<uint8_t> px;
        for (size_t i = 0; i < drawables.size(); i++) {
            if (!renderWallAt(drawables[i], yaw, pitch, px)) continue;
            int c = CenterGrey(px, W, H);
            if (c != -1 && std::abs(c - BG_GREY) > 6) {
                // found a wall filling the center
                return c;
            }
        }
        return -1;
    };

    int c0 = centeredWallAt(0, 0);
    int cN = centeredWallAt(90, 0);
    int cS = centeredWallAt(-90, 0);
    int cUp = centeredWallAt(0, 60);
    int cDown = centeredWallAt(0, -60);

    std::printf("yaw=0   center=%d (expect ~180 east)\n", c0);
    std::printf("yaw=+90 center=%d (expect ~140 north)\n", cN);
    std::printf("yaw=-90 center=%d (expect ~100 south)\n", cS);
    std::printf("pitch=+60 center=%d (expect ~20 ceiling)\n", cUp);
    std::printf("pitch=-60 center=%d (expect ~240 floor)\n", cDown);

    auto near = [](int v, int target) { return v >= target - 25 && v <= target + 25; };

    bool ok = true;
    if (!near(c0, 180)) { std::printf("FAIL: yaw=0 should face east(180)\n"); ok = false; }
    if (!near(cN, 140)) { std::printf("FAIL: yaw=+90 should face north(140)\n"); ok = false; }
    if (!near(cS, 100)) { std::printf("FAIL: yaw=-90 should face south(100)\n"); ok = false; }
    if (!near(cUp, 20)) { std::printf("FAIL: pitch up should face ceiling(20)\n"); ok = false; }
    if (!near(cDown, 240)) { std::printf("FAIL: pitch down should face floor(240)\n"); ok = false; }

    if (ok) {
        std::printf("CAMERA-MAP OK\n");
    }

    // ---- Mouse mapping check ----
    // Facing east (yaw=0), north(+Y) is on the LEFT, south(-Y) is on the RIGHT.
    // Moving the mouse RIGHT (xrel>0) must turn the view toward the south
    // wall, i.e. yaw must DECREASE (toward -90). A full right sweep (xrel
    // enough to reach 90 degrees) must center the SOUTH wall.
    float xrel_90 = 90.0f / 0.15f; // 600 px -> 90 degrees at sens 0.15

    // Current game formula: yaw += xrel * 0.15
    float yaw_current = 0 + xrel_90 * 0.15f; // +90
    int wall_current = centeredWallAt(yaw_current, 0);

    // Proposed fix: yaw -= xrel * 0.15
    float yaw_fixed = 0 - xrel_90 * 0.15f; // -90
    int wall_fixed = centeredWallAt(yaw_fixed, 0);

    std::printf("current formula yaw=%.0f -> centered wall grey %d (expect north=140 if backward)\n",
                yaw_current, wall_current);
    std::printf("fixed   formula yaw=%.0f -> centered wall grey %d (expect south=100 if correct)\n",
                yaw_fixed, wall_fixed);

    bool current_is_left = (wall_current != -1) && near(wall_current, 140); // north = left
    bool fixed_is_right = (wall_fixed != -1) && near(wall_fixed, 100);      // south = right
    bool mouse_backward = current_is_left && fixed_is_right;
    if (mouse_backward) {
        std::printf("RESULT: MOUSE BACKWARD CONFIRMED. Fix yaw += -> yaw -= xrel * 0.15\n");
    } else {
        std::printf("RESULT: MOUSE %s\n",
                    current_is_left ? "BACKWARD (current formula turns left on mouse-right)"
                                    : "appears correct (current formula turns right)");
    }

    for (auto& d : drawables) {
        vk.DestroyBuffer(d.vbuf);
        vk.DestroyBuffer(d.ibuf);
        vk.DestroyTexture(d.tex);
    }
    vk.DestroyTexture(lightmap);
    vk.Shutdown();
    SDL_DestroyWindow(win);
    SDL_Quit();
    return (ok && mouse_backward) ? 0 : 1; // pass only if camera correct AND mouse confirmed backward
}