// Render e1m1 offscreen from the exact spawn camera and dump the result.
// This is the ground-truth check for "is the camera at the wrong position".
#include "filesystem/pak_archive.hpp"
#include "engine/bsp.hpp"
#include "engine/mdl_model.hpp"
#include "app/world_builder.hpp"
#include "vulkan/vulkan_api.hpp"
#include "core/math/mathf.hpp"
#include "core/logging/logger.hpp"
#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>

using zq::mathf::Vec3;
using zq::mathf::Mat4;
static float DegToRad(float d) { return d * 0.0174532925f; }

struct Drawable {
    zq::vk::VulkanBuffer* vbuf = nullptr;
    zq::vk::VulkanBuffer* ibuf = nullptr;
    uint32_t index_count = 0;
    zq::vk::VulkanImage* tex = nullptr;
};

int main() {
    zq::log::SetMinLevel(zq::log::LogLevel::Error);
    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    SDL_Window* win = SDL_CreateWindow("spawnview", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       640, 480, SDL_WINDOW_VULKAN);
    if (!win) { SDL_Quit(); return 1; }
    zq::vk::VulkanAPI vk;
    if (!vk.Initialize(win)) { SDL_DestroyWindow(win); SDL_Quit(); return 1; }
    int W = vk.GetSwapchainWidth(), H = vk.GetSwapchainHeight();

    zq::fs::PAKArchive pak;
    if (!pak.Open("id1/pak0.pak")) { vk.Shutdown(); SDL_DestroyWindow(win); SDL_Quit(); return 1; }
    std::vector<uint8_t> md;
    for (auto& e : pak.GetEntries())
        if (strcmp(e.name, "maps/e1m1.bsp") == 0) { md.resize(e.file_size); pak.ReadFile(e.name, md.data(), md.size()); break; }
    zq::engine::BSPMap map;
    if (!map.Load(md.data(), md.size())) { printf("bsp fail\n"); return 1; }

    uint8_t palette[768];
    for (int i = 0; i < 256; i++) palette[i*3]=palette[i*3+1]=palette[i*3+2]=(uint8_t)i;

    // World mesh
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

    // Spawn camera
    float o[3], a[3];
    if (!map.FindPlayerStart(o, a)) { o[0]=0;o[1]=0;o[2]=60; a[1]=0; }
    Vec3 eye = { o[0], o[1], o[2] + 28.0f };
    float yaw = a[1], pitch = 0;
    float pr = DegToRad(pitch), yr = DegToRad(yaw);
    Vec3 dir = { std::cos(pr)*std::cos(yr), std::cos(pr)*std::sin(yr), std::sin(pr) };
    Vec3 center = eye + dir;
    float aspect = (float)W / (float)H;
    Mat4 proj = Mat4::Perspective(75.0f, aspect, 0.1f, 4096.0f);
    Mat4 view = Mat4::LookAt(eye, center, Vec3{0,0,1});
    vk.SetCamera(proj.m, view.m);

    std::vector<zq::vk::VulkanAPI::Mesh> meshes;
    for (auto& d : drawables) {
        zq::vk::VulkanAPI::Mesh m;
        m.vertex_buffer = d.vbuf; m.index_buffer = d.ibuf;
        m.index_count = d.index_count; m.texture = d.tex; m.lightmap = lightmap;
        m.has_model = false;
        meshes.push_back(m);
    }
    std::printf("world meshes: %zu\n", meshes.size());

    std::vector<uint8_t> px;
    if (!vk.RenderMeshesToImage(W, H, meshes, px)) { printf("render fail\n"); return 1; }

    // Dump as PPM for inspection + ASCII luminance
    FILE* f = fopen("/tmp/spawnview.ppm", "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        // px is BGRA (swapchain format) -> convert to RGB
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                size_t i = ((size_t)y*W + x)*4;
                fputc(px[i+2], f); // R
                fputc(px[i+1], f); // G
                fputc(px[i+0], f); // B
            }
        fclose(f);
    }
    // ASCII luminance
    int gx=80, gy=45;
    const char* ramp=" .:-=+*#%@";
    for (int gy_=0; gy_<gy; gy_++) {
        std::string row;
        for (int gx_=0; gx_<gx; gx_++) {
            long sum=0; int n=0;
            for (int y=gy_*H/gy; y<(gy_+1)*H/gy; y+=2)
                for (int x=gx_*W/gx; x<(gx_+1)*W/gx; x+=2) {
                    size_t i=((size_t)y*W+x)*4;
                    sum += (px[i+0]+px[i+1]+px[i+2])/3; n++;
                }
            int avg = n? (int)(sum/n) : 0;
            row += ramp[std::min(9, avg*10/256)];
        }
        printf("%s\n", row.c_str());
    }

    vk.Shutdown(); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}