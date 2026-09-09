// Vulkan render verification: creates a window, initializes the renderer,
// draws a fullscreen checkerboard quad through the offscreen capture path,
// and verifies the resulting pixels on the CPU (no display/screenshot needed).
// Exits 0 only if the checkerboard pattern is actually present in the output.
#include "vulkan/vulkan_api.hpp"
#include "core/logging/logger.hpp"
#include <SDL.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>

int main() {
    zq::log::SetLogger(&zq::log::ConsoleLogger::Instance());
    zq::log::SetMinLevel(zq::log::LogLevel::Info);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        zq::log::Error("SDL init failed");
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("zquake-smoke",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          640, 480, SDL_WINDOW_VULKAN);
    if (!window) {
        zq::log::Error("window creation failed");
        SDL_Quit();
        return 1;
    }

    zq::vk::VulkanAPI vk;
    if (!vk.Initialize(window)) {
        zq::log::Error("Vulkan init failed");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    uint32_t W = vk.GetSwapchainWidth();
    uint32_t H = vk.GetSwapchainHeight();

    // Texture: 16x16 checker with clearly visible colors (green/magenta)
    std::vector<uint8_t> tex(16 * 16 * 4);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            int idx = (x / 4 + y / 4) % 2;
            size_t p = ((y * 16) + x) * 4;
            tex[p+0] = idx ? 200 : 40;
            tex[p+1] = idx ? 40 : 200;
            tex[p+2] = 60;
            tex[p+3] = 255;
        }
    zq::vk::VulkanImage* texture = vk.CreateTexture(16, 16, tex.data());
    if (!texture) {
        zq::log::Error("texture upload failed");
        vk.Shutdown(); SDL_DestroyWindow(window); SDL_Quit();
        return 1;
    }
    float white[4] = {1,1,1,1};
    zq::vk::VulkanImage* lightmap = vk.CreateLightmap(white);

    // Fullscreen quad in clip space. Identity camera => gl_Position = vertex.
    struct Vert { float pos[3], uv[2], normal[3], lmuv[3]; };
    Vert verts[4] = {
        { {-1,-1,0}, {0,0}, {0,0,1}, {0.5f,0.5f,0} },
        { { 1,-1,0}, {1,0}, {0,0,1}, {0.5f,0.5f,0} },
        { { 1, 1,0}, {1,1}, {0,0,1}, {0.5f,0.5f,0} },
        { {-1, 1,0}, {0,1}, {0,0,1}, {0.5f,0.5f,0} },
    };
    uint32_t indices[6] = { 0,1,2, 0,2,3 };

    zq::vk::VulkanBuffer* vbuf = vk.CreateVertexBuffer(sizeof(verts));
    vk.UpdateBuffer(vbuf, verts, sizeof(verts));
    zq::vk::VulkanBuffer* ibuf = vk.CreateIndexBuffer(sizeof(indices));
    vk.UpdateBuffer(ibuf, indices, sizeof(indices));

    // Identity "projection * view" — drawn geometry is clip-space NDC.
    float identity[16];
    std::memset(identity, 0, sizeof(identity));
    identity[0] = identity[5] = identity[10] = identity[15] = 1.0f;
    vk.SetCamera(identity, identity);

    zq::vk::VulkanAPI::Mesh mesh;
    mesh.vertex_buffer = vbuf;
    mesh.index_buffer = ibuf;
    mesh.index_count = 6;
    mesh.texture = texture;
    mesh.lightmap = lightmap;
    mesh.has_model = false;

    // Capture rendered pixels
    std::vector<uint8_t> pixels;
    if (!vk.RenderMeshToImage(W, H, mesh, pixels)) {
        zq::log::Error("RenderMeshToImage failed");
        vk.Shutdown(); SDL_DestroyWindow(window); SDL_Quit();
        return 1;
    }

    // Also present a few frames to the real window (exercises normal path).
    for (int i = 0; i < 30; i++) {
        if (!vk.BeginFrame()) { i--; continue; }
        vk.SetCamera(identity, identity);
        vk.DrawMesh(mesh);
        vk.EndFrame();
    }

    // ---- Verify pixels ----
    // Dump raw bytes for offline inspection
    {
        FILE* f = fopen("/tmp/cap.rgba", "wb");
        if (f) { fwrite(pixels.data(), 1, pixels.size(), f); fclose(f); }
    }
    // The framebuffer is the swapchain format (usually B8G8R8A8_UNORM).
    // Texture color A=(200,40,60) shows as BGRA bytes (60,40,200).
    // Texture color B=(40,200,60) shows as BGRA bytes (60,200,40).
    // Detect both groups by the G and R byte values.
    size_t n = pixels.size() / 4;
    long groupA = 0, groupB = 0, other = 0;
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        uint8_t B = pixels[i+0];
        uint8_t G = pixels[i+1];
        uint8_t R = pixels[i+2];
        // group A: greenish G channel low, R channel high (== texture color A)
        // group B: R channel low, G channel high (== texture color B)
        bool a = (R > 150) && (G > 20 && G < 90) && (B < 90);
        bool b = (G > 150) && (R > 20 && R < 90) && (B < 90);
        if (a) groupA++;
        else if (b) groupB++;
        else other++;
    }

    std::printf("SMOKE capture: %zu px | colorA=%ld (%.1f%%) colorB=%ld (%.1f%%) other=%ld (%.1f%%)\n",
                n, groupA, 100.0*groupA/n, groupB, 100.0*groupB/n, other, 100.0*other/n);

    bool ok = (groupA > n/8) && (groupB > n/8);
    if (ok) {
        zq::log::Info("SMOKE TEST PASSED: checkerboard quad rendered correctly");
    } else {
        zq::log::Error("SMOKE TEST FAILED: quad did not render as expected");
    }

    vk.DestroyBuffer(vbuf);
    vk.DestroyBuffer(ibuf);
    vk.DestroyTexture(texture);
    vk.DestroyTexture(lightmap);
    vk.Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return ok ? 0 : 1;
}