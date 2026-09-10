// GPU ray-tracer regression test. Initializes Vulkan + the RayTracer, builds a
// small scene (a red square facing the camera over a dark sky), ray-traces it
// via the compute shader, reads the result back to the CPU and verifies:
//   * the center is the lit square color (red-ish),
//   * the corners are the sky background (dark blue-ish),
//   * an empty scene produces all-sky pixels.
// Exits 0 only if all checks pass. This exercises the real compute->storage
// ->readback path (no window present needed).
#include "vulkan/vulkan_api.hpp"
#include "render/raytracer.hpp"
#include "render/ray_scene.hpp"
#include "app/rt_scene.hpp"
#include "app/world_builder.hpp"
#include "engine/bsp.hpp"
#include "filesystem/pak_archive.hpp"
#include "core/math/mathf.hpp"
#include "core/logging/logger.hpp"
#include <SDL.h>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <random>
#include <map>
#include <array>
#include <set>

using zq::mathf::Vec3;

namespace {

// A square in the YZ plane at x=-depth (normal +X, facing the camera at the
// origin looking down -X), split into two triangles. Uses atlas tile `tex`.
void AddSquare(std::vector<zq::render::RtTriangle>& tris, float half, float depth,
               std::uint32_t tex) {
    auto mk = [&](float x0,float y0,float z0, float x1,float y1,float z1,
                  float x2,float y2,float z2,
                  float u0,float v0,float u1,float v1,float u2,float v2) {
        zq::render::RtTriangle t;
        t.p0[0]=x0; t.p0[1]=y0; t.p0[2]=z0;
        t.p1[0]=x1; t.p1[1]=y1; t.p1[2]=z1;
        t.p2[0]=x2; t.p2[1]=y2; t.p2[2]=z2;
        t.uv0[0]=u0; t.uv0[1]=v0;
        t.uv1[0]=u1; t.uv1[1]=v1;
        t.uv2[0]=u2; t.uv2[1]=v2;
        t.tex = tex;
        return t;
    };
    float x = -depth;
    // normal +X: e1=(0,2h,0), e2=(0,0,2h), cross = +X
    tris.push_back(mk(x,-half,-half, x,half,-half, x,-half,half, 0,0, 1,0, 0,1));
    tris.push_back(mk(x,half,-half, x,half,half, x,-half,half, 1,0, 1,1, 0,1));
}

// Build a 2x1 atlas of 16x16 tiles: tile0 = red, tile1 = blue.
void BuildAtlas(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h,
                uint32_t& tilesPerRow, uint32_t& tileSize) {
    tileSize = 16; tilesPerRow = 2;
    w = tileSize * tilesPerRow; h = tileSize;
    rgba.assign((size_t)w * h * 4, 255);
    auto fillTile = [&](int col, uint8_t r, uint8_t g, uint8_t b) {
        for (int y = 0; y < (int)tileSize; y++)
            for (int x = 0; x < (int)tileSize; x++) {
                size_t p = ((size_t)(y * w) + col * tileSize + x) * 4;
                rgba[p+0]=r; rgba[p+1]=g; rgba[p+2]=b; rgba[p+3]=255;
            }
    };
    fillTile(0, 255, 0, 0);   // red
    fillTile(1, 0, 0, 255);   // blue
}

void CenterAverage(const std::vector<uint8_t>& px, uint32_t W, uint32_t H,
                   int block, float* rOut, float* gOut, float* bOut) {
    float r=0,g=0,b=0; int n=0;
    for (int y=H/2-block; y<=(int)H/2+block; y++)
        for (int x=W/2-block; x<=(int)W/2+block; x++) {
            size_t i=((size_t)y*W+x)*4;
            r+=px[i]; g+=px[i+1]; b+=px[i+2]; n++;
        }
    *rOut=r/n; *gOut=g/n; *bOut=b/n;
}

void CornerAverage(const std::vector<uint8_t>& px, uint32_t W, int block,
                   float* rOut, float* gOut, float* bOut) {
    float r=0,g=0,b=0; int n=0;
    for (int y=0; y<block; y++)
        for (int x=0; x<block; x++) {
            size_t i=((size_t)y*W+x)*4;
            r+=px[i]; g+=px[i+1]; b+=px[i+2]; n++;
        }
    *rOut=r/n; *gOut=g/n; *bOut=b/n;
}

int RunScene(zq::render::RayTracer& rt, const std::vector<zq::render::RtTriangle>& tris,
             uint32_t W, uint32_t H, const char* name, bool expectSquare) {
    // Camera: eye at origin looking down -X, up +Z.
    float proj[16], view[16];
    auto pm = zq::mathf::Mat4::Perspective(75.0f, (float)W/H, 0.1f, 100.0f);
    auto vm = zq::mathf::Mat4::LookAt(Vec3{0,0,0}, Vec3{-10,0,0}, Vec3{0,0,1});
    std::memcpy(proj, pm.m, sizeof(proj));
    std::memcpy(view, vm.m, sizeof(view));

    std::vector<uint8_t> atlas; uint32_t aw, ah, tpr, ts;
    BuildAtlas(atlas, aw, ah, tpr, ts);
    // tile infos for the 2-tile atlas (tile size ts, tilesPerRow tpr)
    std::vector<zq::render::RtTileInfo> tis;
    for (uint32_t i = 0; i < tpr; i++)
        tis.push_back({ (float)(i*ts), 0.0f, (float)ts, (float)ts });
    if (!rt.BuildScene(tris, atlas, aw, ah, tis, W, H)) {
        std::printf("RAYTRACE[%s] BuildScene FAILED\n", name);
        return 1;
    }
    // A point light at the camera lights the square (which sits at x=-5).
    std::vector<zq::render::RtLight> lights(1);
    lights[0].intensity = 3.0f; lights[0].radius = 50.0f;
    lights[0].color[0]=lights[0].color[1]=lights[0].color[2]=1.0f;
    rt.SetLights(lights);

    std::vector<uint8_t> px;
    if (!rt.TraceToCPU(proj, view, W, H, px)) {
        std::printf("RAYTRACE[%s] TraceToCPU FAILED\n", name);
        return 1;
    }

    FILE* f = fopen(("/tmp/rt_" + std::string(name) + ".ppm").c_str(), "wb");
    if (f) {
        fprintf(f, "P6\n%u %u\n255\n", W, H);
        for (size_t i = 0; i + 3 < px.size(); i += 4)
            fputc(px[i], f), fputc(px[i+1], f), fputc(px[i+2], f);
        fclose(f);
    }

    float cr,cg,cb, br,bg,bb;
    CenterAverage(px, W, H, 4, &cr,&cg,&cb);
    CornerAverage(px, W, 6, &br,&bg,&bb);
    std::printf("RAYTRACE[%s] center=(%.0f %.0f %.0f) corner=(%.0f %.0f %.0f)\n",
                name, cr,cg,cb, br,bg,bb);

    bool ok;
    if (expectSquare) {
        bool lit = (cr > 90.0f) && (cr > cg + 40.0f) && (cr > cb + 40.0f); // red dominates
        bool sky = (bb > br + 10.0f); // corner is blue-ish sky
        ok = lit && sky;
    } else {
        // Empty scene: everything should be sky (blue-ish, low red).
        ok = (bb > br) && (cb > cr);
    }
    if (ok) std::printf("RAYTRACE[%s] PASSED\n", name);
    else    std::printf("RAYTRACE[%s] FAILED\n", name);
    return ok ? 0 : 1;
}

} // namespace

int main() {
    zq::log::SetLogger(&zq::log::ConsoleLogger::Instance());
    zq::log::SetMinLevel(zq::log::LogLevel::Info);

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
    SDL_Window* window = SDL_CreateWindow("zquake-raytrace",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          320, 240, SDL_WINDOW_VULKAN);
    if (!window) { SDL_Quit(); return 1; }

    zq::vk::VulkanAPI vk;
    if (!vk.Initialize(window)) {
        zq::log::Error("Vulkan init failed");
        SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }

    zq::render::RayTracer rt;
    if (!rt.Initialize(vk.GetPhysicalDevice(), vk.GetDevice(),
                       vk.GetGraphicsQueueFamily(), vk.GetGraphicsQueue(),
                       vk.GetSwapchainFormat())) {
        zq::log::Error("RayTracer init failed");
        vk.Shutdown(); SDL_DestroyWindow(window); SDL_Quit(); return 1;
    }

    uint32_t W = vk.GetSwapchainWidth();
    uint32_t H = vk.GetSwapchainHeight();

    int fail = 0;
    {
        std::vector<zq::render::RtTriangle> tris;
        AddSquare(tris, 2.0f, 5.0f, 0); // square at x=-5 (facing +X, toward camera)
        fail |= RunScene(rt, tris, W, H, "square", true);
    }
    {
        std::vector<zq::render::RtTriangle> tris;
        fail |= RunScene(rt, tris, W, H, "empty", false);
    }
    // Scatter: many single-colour triangles at varying positions/depths, each a
    // distinct atlas tile. If the GPU BVH traversal is broken at scale it
    // returns a constant triangle and the output is one flat colour.
    {
        zq::app::RtSceneBuilder b;
        for (int i = 0; i < 400; i++) {
            float dx = -5.0f - (float)(i % 20) * 0.6f;
            float dy = -2.0f + (float)(i % 10) * 1.1f;
            float dz = -2.0f + (float)(i % 10) * 1.1f;
            struct V { float pos[3]; float uv[2]; };
            V verts[3] = {
                { {dx, dy, dz},     {0,0} },
                { {dx, dy+0.9f, dz},{1,0} },
                { {dx, dy, dz+0.9f},{0,1} },
            };
            std::uint32_t idx[3] = {0,1,2};
            std::vector<uint8_t> tex(16*16*4, 255);
            for (size_t k = 0; k < tex.size(); k += 4) {
                tex[k] = (i*37)%256; tex[k+1] = (i*71)%256; tex[k+2] = (i*13)%256;
            }
            b.AddMesh(verts, 3, sizeof(V), idx, 3, tex.data(), 16, 16, nullptr);
        }
        auto tris = b.Triangles();
        float proj[16], view[16];
        auto pm = zq::mathf::Mat4::Perspective(75.0f, (float)W/H, 0.1f, 100.0f);
        auto vm = zq::mathf::Mat4::LookAt(Vec3{0,0,0}, Vec3{-10,0,0}, Vec3{0,0,1});
        std::memcpy(proj, pm.m, sizeof(proj));
        std::memcpy(view, vm.m, sizeof(view));
        std::vector<uint8_t> px;
        bool ok = rt.BuildScene(tris, b.AtlasRgba(), b.AtlasWidth(), b.AtlasHeight(),
                                b.TileInfos(), W, H);
        {
            std::vector<zq::render::RtLight> L(1);
            L[0].intensity=3.0f; L[0].radius=200.0f;
            L[0].color[0]=L[0].color[1]=L[0].color[2]=1.0f;
            rt.SetLights(L);
        }
        ok = ok && rt.TraceToCPU(proj, view, W, H, px);
        if (!ok) { std::printf("RAYTRACE[scatter] FAILED (build/trace)\n"); fail = 1; }
        else {
            // Count distinct RGB values across a sampled grid.
            std::vector<int> seen;
            auto has = [&](int v){ for (auto s : seen) if (s==v) return true; return false; };
            for (size_t i = 0; i + 3 < px.size(); i += 4*13) {
                int v = (px[i]<<16)|(px[i+1]<<8)|px[i+2];
                if (!has(v)) seen.push_back(v);
            }
            FILE* f = fopen("/tmp/rt_scatter.ppm", "wb");
            if (f) { fprintf(f, "P6\n%u %u\n255\n", W, H);
                for (size_t i = 0; i + 3 < px.size(); i += 4)
                    fputc(px[i],f), fputc(px[i+1],f), fputc(px[i+2],f);
                fclose(f); }
            std::printf("RAYTRACE[scatter] distinct_colors=%zu\n", seen.size());
            bool pass = seen.size() >= 30;
            std::printf("RAYTRACE[scatter] %s\n", pass ? "PASSED" : "FAILED");
            fail |= pass ? 0 : 1;
        }
    }

    // Regression: scatter scene SHIFTED to world-like large coordinates.
    // If the GPU BVH traversal has a precision bug at large coords, this
    // renders as a single colour.
    {
        zq::app::RtSceneBuilder b;
        for (int i = 0; i < 400; i++) {
            float dx = -5.0f - (float)(i % 20) * 0.6f + 480.0f;
            float dy = -2.0f + (float)(i % 10) * 1.1f - 352.0f;
            float dz = -2.0f + (float)(i % 10) * 1.1f + 116.0f;
            struct V { float pos[3]; float uv[2]; };
            V verts[3] = {
                { {dx, dy, dz},     {0,0} },
                { {dx, dy+0.9f, dz},{1,0} },
                { {dx, dy, dz+0.9f},{0,1} },
            };
            std::uint32_t idx[3] = {0,1,2};
            std::vector<uint8_t> tex(16*16*4, 255);
            for (size_t k = 0; k < tex.size(); k += 4) {
                tex[k] = (i*37)%256; tex[k+1] = (i*71)%256; tex[k+2] = (i*13)%256;
            }
            b.AddMesh(verts, 3, sizeof(V), idx, 3, tex.data(), 16, 16, nullptr);
        }
        auto tris = b.Triangles();
        float proj[16], view[16];
        auto pm = zq::mathf::Mat4::Perspective(75.0f, (float)W/H, 0.1f, 100.0f);
        auto vm = zq::mathf::Mat4::LookAt(Vec3{0,0,0}, Vec3{-10,0,0}, Vec3{0,0,1});
        std::memcpy(proj, pm.m, sizeof(proj));
        std::memcpy(view, vm.m, sizeof(view));
        std::vector<uint8_t> px;
        bool ok = rt.BuildScene(tris, b.AtlasRgba(), b.AtlasWidth(), b.AtlasHeight(),
                                b.TileInfos(), W, H);
        {
            std::vector<zq::render::RtLight> L(1);
            L[0].intensity=3.0f; L[0].radius=200.0f;
            L[0].color[0]=L[0].color[1]=L[0].color[2]=1.0f;
            rt.SetLights(L);
        }
        ok = ok && rt.TraceToCPU(proj, view, W, H, px);
        if (!ok) { std::printf("RAYTRACE[shift] FAILED\n"); fail=1; }
        else {
            std::vector<int> seen;
            auto has=[&](int v){for(auto s:seen)if(s==v)return true;return false;};
            for (size_t i=0;i+3<px.size();i+=4*13){ int v=(px[i]<<16)|(px[i+1]<<8)|px[i+2]; if(!has(v))seen.push_back(v);}
            std::printf("RAYTRACE[shift] distinct_colors=%zu\n", seen.size());
            fail |= (seen.size() >= 5) ? 0 : 1;
        }
    }

    // Render the e1m1 world from the spawn camera through the GPU ray tracer.
    // If this shows varied geometry, the GPU+world data are fine and the
    // in-game camera path was the problem.
    {
        zq::app::RtSceneBuilder sb;
        std::vector<uint8_t> md;
        zq::fs::PAKArchive pak2;
        if (pak2.Open("/home/rechenplan/Code/zyven/zquake/id1/pak0.pak"))
            for (auto& e : pak2.GetEntries())
                if (std::string(e.name)=="maps/e1m1.bsp"){ md.resize(e.file_size); pak2.ReadFile(e.name,md.data(),e.file_size); break; }
        if (!md.empty()) {
            zq::engine::BSPMap map2; if (map2.Load(md.data(), md.size())) {
                float org[3], ang[3]; map2.FindSpawnPoint(org, ang);
                auto g2 = zq::app::BuildGroups(map2);
                int gi = 0;
                for (auto& grp : g2) {
                    if (grp.indices.empty()) continue;
                    std::vector<uint8_t> tex(8*8*4, 255);
                    for (size_t k = 0; k < tex.size(); k += 4) {
                        tex[k] = (gi*47)%256; tex[k+1] = (gi*91)%256; tex[k+2] = (gi*19)%256;
                    }
                    sb.AddMesh(grp.vertices.data(), grp.vertices.size(), sizeof(zq::app::WorldVertex),
                               grp.indices.data(), grp.indices.size(), tex.data(), 8, 8, nullptr);
                    gi++;
                }
                auto wtri = sb.Triangles();
                float proj[16], view[16];
                auto pm = zq::mathf::Mat4::Perspective(75.0f, (float)W/H, 0.1f, 4000.0f);
                float yaw = ang[1] * 3.14159265f / 180.0f;
                auto vm = zq::mathf::Mat4::LookAt(Vec3{org[0],org[1],org[2]+28},
                        Vec3{org[0]+std::cos(yaw), org[1]+std::sin(yaw), org[2]+28}, Vec3{0,0,1});
                std::memcpy(proj, pm.m, sizeof(proj));
                std::memcpy(view, vm.m, sizeof(view));
                std::vector<uint8_t> px;
                bool ok = rt.BuildScene(wtri, sb.AtlasRgba(), sb.AtlasWidth(), sb.AtlasHeight(),
                                        sb.TileInfos(), W, H);
        {
            std::vector<zq::render::RtLight> L(1);
            L[0].intensity=3.0f; L[0].radius=200.0f;
            L[0].color[0]=L[0].color[1]=L[0].color[2]=1.0f;
            rt.SetLights(L);
        }
        ok = ok && rt.TraceToCPU(proj, view, W, H, px);
                if (!ok) { std::printf("RAYTRACE[world] FAILED (build/trace)\n"); fail=1; }
                else {
                    std::vector<int> seen;
                    auto has=[&](int v){for(auto s:seen)if(s==v)return true;return false;};
                    for (size_t i=0;i+3<px.size();i+=4*13){ int v=(px[i]<<16)|(px[i+1]<<8)|px[i+2]; if(!has(v))seen.push_back(v);}
                    FILE* f=fopen("/tmp/rt_world.ppm","wb");
                    if(f){fprintf(f,"P6\n%u %u\n255\n",W,H); for(size_t i=0;i+3<px.size();i+=4)fputc(px[i],f),fputc(px[i+1],f),fputc(px[i+2],f); fclose(f);}
                    std::printf("RAYTRACE[world] distinct_colors=%zu\n", seen.size());
                    fail |= (seen.size() >= 5) ? 0 : 1;
                }
            }
        }
    }

    // Large-triangle test: a big floor (z=0) and a big wall (x=0) forming a
    // corner, camera looking at the corner from (5,5,5). The floor and wall are
    // different solid colours, so a correct trace shows a clear edge between
    // them. If large triangles break the GPU BVH, this collapses to one colour.
    {
        zq::app::RtSceneBuilder b;
        // Floor: big square in z=0 plane, x in [-8,8], y in [-8,8], colour A.
        {
            struct V { float pos[3]; float uv[2]; };
            V v[4] = { {{-8,-8,0},{0,0}}, {{8,-8,0},{1,0}}, {{8,8,0},{1,1}}, {{-8,8,0},{0,1}} };
            std::uint32_t idx[6] = {0,1,2, 0,2,3};
            std::vector<uint8_t> tex(8*8*4,255); for(size_t k=0;k<tex.size();k+=4){tex[k]=200;tex[k+1]=40;tex[k+2]=40;}
            b.AddMesh(v,4,sizeof(V),idx,6,tex.data(),8,8,nullptr);
        }
        // Wall: big square in x=0 plane, z in [0,8], y in [-8,8], colour B.
        {
            struct V { float pos[3]; float uv[2]; };
            V v[4] = { {{0,-8,0},{0,0}}, {{0,8,0},{1,0}}, {{0,8,8},{1,1}}, {{0,-8,8},{0,1}} };
            std::uint32_t idx[6] = {0,1,2, 0,2,3};
            std::vector<uint8_t> tex(8*8*4,255); for(size_t k=0;k<tex.size();k+=4){tex[k]=40;tex[k+1]=40;tex[k+2]=200;}
            b.AddMesh(v,4,sizeof(V),idx,6,tex.data(),8,8,nullptr);
        }
        auto tris = b.Triangles();
        float proj[16], view[16];
        auto pm = zq::mathf::Mat4::Perspective(75.0f,(float)W/H,0.1f,100.0f);
        auto vm = zq::mathf::Mat4::LookAt(Vec3{5,5,5}, Vec3{0,0,2}, Vec3{0,0,1});
        std::memcpy(proj,pm.m,sizeof(proj)); std::memcpy(view,vm.m,sizeof(view));
        std::vector<uint8_t> px;
        bool ok = rt.BuildScene(tris, b.AtlasRgba(), b.AtlasWidth(), b.AtlasHeight(),
                                b.TileInfos(), W, H);
        {
            std::vector<zq::render::RtLight> L(1);
            L[0].intensity=3.0f; L[0].radius=200.0f;
            L[0].color[0]=L[0].color[1]=L[0].color[2]=1.0f;
            rt.SetLights(L);
        }
        ok = ok && rt.TraceToCPU(proj, view, W, H, px);
        if(!ok){ std::printf("RAYTRACE[corner] FAILED\n"); fail=1; }
        else {
            auto L=[&](int x,int y)->int{size_t o=((size_t)y*W+x)*3; return px[o]+px[o+1]+px[o+2];};
            int hedges=0; for(int y=1;y<(int)H;y++){for(int x=1;x<(int)W;x++){if(abs(L(x,y)-L(x-1,y))>60){hedges++;break;}}}
            int vedges=0; for(int x=1;x<(int)W;x++){for(int y=1;y<(int)H;y++){if(abs(L(x,y)-L(x,y-1))>60){vedges++;break;}}}
            FILE* f=fopen("/tmp/rt_corner.ppm","wb");
            if(f){fprintf(f,"P6\n%u %u\n255\n",W,H); for(size_t i=0;i+3<px.size();i+=4)fputc(px[i],f),fputc(px[i+1],f),fputc(px[i+2],f); fclose(f);}
            std::printf("RAYTRACE[corner] hedges=%d vedges=%d\n", hedges, vedges);
            bool pass = hedges>0 || vedges>0;
            std::printf("RAYTRACE[corner] %s\n", pass?"PASSED":"FAILED");
            fail |= pass?0:1;
        }
    }

    // Shadow test: a ground plane (z=0) and a box at the origin, lit by a single
    // point light up-left at (-6,0,3). Camera looks straight down (-Z) so screen
    // X maps to world X; the box casts a shadow on the ground to its +X side,
    // which must render darker than the open ground to its -X side.
    {
        zq::app::RtSceneBuilder b;
        auto addQuad = [&](float ax,float ay,float az, float cx,float cy,float cz,
                           float dx,float dy,float dz, float ex,float ey,float ez,
                           int r,int g,int bl){
            struct V { float pos[3]; float uv[2]; };
            V v[4] = { {{ax,ay,az},{0,0}}, {{cx,cy,cz},{1,0}},
                       {{dx,dy,dz},{1,1}}, {{ex,ey,ez},{0,1}} };
            std::uint32_t idx[6] = {0,1,2, 0,2,3};
            std::vector<uint8_t> tex(8*8*4,255); for(size_t k=0;k<tex.size();k+=4){tex[k]=r;tex[k+1]=g;tex[k+2]=bl;}
            b.AddMesh(v,4,sizeof(V),idx,6,tex.data(),8,8,nullptr);
        };
        // ground plane z=0 (grey)
        addQuad(-8,-8,0, 8,-8,0, 8,8,0, -8,8,0, 150,150,150);
        // box sides at x in [0,2], y in [-1,1], z in [0,2]
        addQuad(0,-1,0, 0,1,0, 0,1,2, 0,-1,2, 200,80,80 );   // -x face
        addQuad(2,-1,0, 2,1,0, 2,1,2, 2,-1,2, 80,80,200 );   // +x face
        addQuad(0,1,0, 2,1,0, 2,1,2, 0,1,2, 80,200,80 );     // +y face
        addQuad(0,1,2, 2,1,2, 2,-1,2, 0,-1,2, 200,200,80 );  // top face
        auto tris = b.Triangles();
        float proj[16], view[16];
        auto pm = zq::mathf::Mat4::Perspective(75.0f,(float)W/H,0.1f,100.0f);
        // top-down camera at (0,0,12) looking straight down (-Z), up = +Y
        auto vm = zq::mathf::Mat4::LookAt(Vec3{0,0,12}, Vec3{0,0,0}, Vec3{0,1,0});
        std::memcpy(proj,pm.m,sizeof(proj)); std::memcpy(view,vm.m,sizeof(view));
        std::vector<zq::render::RtLight> lights(1);
        lights[0].pos[0]=-6; lights[0].pos[1]=0; lights[0].pos[2]=3;
        lights[0].intensity=3.0f; lights[0].radius=40.0f;
        lights[0].color[0]=lights[0].color[1]=lights[0].color[2]=1.0f;
        std::vector<uint8_t> px;
        bool ok = rt.BuildScene(tris, b.AtlasRgba(), b.AtlasWidth(), b.AtlasHeight(),
                                b.TileInfos(), W, H);
        rt.SetLights(lights);
        ok = ok && rt.TraceToCPU(proj, view, W, H, px);
        if(!ok){ std::printf("RAYTRACE[shadow] FAILED (build/trace)\n"); fail=1; }
        else {
            auto L=[&](int x,int y)->int{size_t o=((size_t)y*W+x)*3; return px[o]+px[o+1]+px[o+2];};
            // A shadow exists if a substantial region of the frame is far darker
            // than the median (the box casts a dark shadow on the ground).
            std::vector<int> all;
            for(int y=1;y<(int)H;y++) for(int x=1;x<(int)W;x++) all.push_back(L(x,y));
            std::sort(all.begin(), all.end());
            int median = all[all.size()/2];
            int darkCount=0;
            for(auto v : all) if (v < median - 35) darkCount++;
            float frac = (float)darkCount/all.size();
            std::printf("RAYTRACE[shadow] median=%d dark_px=%.1f%% (shadow)\n", median, frac*100);
            FILE* f=fopen("/tmp/rt_shadow.ppm","wb");
            if(f){fprintf(f,"P6\n%u %u\n255\n",W,H); for(size_t i=0;i+3<px.size();i+=4)fputc(px[i],f),fputc(px[i+1],f),fputc(px[i+2],f); fclose(f);}
            bool pass = frac > 0.01f; // at least 1% of pixels are clearly darker (a shadow)
            std::printf("RAYTRACE[shadow] %s\n", pass?"PASSED":"FAILED");
            fail |= pass?0:1;
        }
    }

// GPU entity-BVH parity test: build a large random entity scene on the GPU via
    // UpdateEntities, read the resulting nodes + reordered triangles back, and
    // verify the closest-hit DISTANCE matches the CPU BuildBvh over many rays.
    {
        // 16k random triangles as entities.
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> d(-50, 50);
        std::vector<zq::render::RtTriangle> ent;
        for (int i = 0; i < 16000; i++) {
            float bx=d(rng), by=d(rng), bz=d(rng);
            zq::render::RtTriangle t;
            t.p0[0]=bx;t.p0[1]=by;t.p0[2]=bz;
            t.p1[0]=bx+1.0f;t.p1[1]=by;t.p1[2]=bz;
            t.p2[0]=bx;t.p2[1]=by+1.0f;t.p2[2]=bz;
            t.tex=1; ent.push_back(t);
        }
        // CPU reference scene over the ORIGINAL triangles.
        zq::render::RtScene cpuScene;
        cpuScene.Build(ent);
        // Build the GPU BVH (reorders into the entity buffers).
        std::vector<zq::render::RtTriangle> worldTris; // empty world
        std::vector<uint8_t> atlas(8*8*4, 200);
        std::vector<zq::render::RtTileInfo> tis; tis.push_back({0,0,8,8});
        bool ok = rt.BuildScene(worldTris, atlas, 8, 8, tis, W, H);
        rt.UpdateEntities(ent);
        std::vector<zq::render::RtTriangle> gTris;
        std::vector<zq::render::BvhNode> gNodes;
        ok = ok && rt.ReadbackEntity(gTris, gNodes);
        if (!ok) { std::printf("RAYTRACE[parity] FAILED (build/readback)\n"); fail=1; }
        else {
            // Invariant check: leaves bound their triangle; internal nodes bound
            // their children; every slot is a leaf covering exactly one triangle.
            int badLeaf=0, badInternal=0, badMinMax=0;
            for (size_t ni = 0; ni < gNodes.size(); ni++) {
                auto& n = gNodes[ni];
                if (n.aabbMin[0]>n.aabbMax[0]||n.aabbMin[1]>n.aabbMax[1]||n.aabbMin[2]>n.aabbMax[2]) badMinMax++;
                if (n.triCount > 0) {
                    int first = -n.leftFirst - 1;
                    if (first < 0 || first >= (int)gTris.size()) { badLeaf++; continue; }
                    auto& t = gTris[first];
                    for (int k=0;k<3;k++)
                        if (n.aabbMin[k] > std::min(t.p0[k],std::min(t.p1[k],t.p2[k])) + 1e-4f ||
                            n.aabbMax[k] < std::max(t.p0[k],std::max(t.p1[k],t.p2[k])) - 1e-4f) badLeaf++;
                } else {
                    if (ni>=2)
                        for (int k=0;k<3;k++)
                            if (n.aabbMin[k] > gNodes[2*ni+1].aabbMin[k]+1e-4f || n.aabbMax[k] < gNodes[2*ni+1].aabbMax[k]-1e-4f ||
                                n.aabbMin[k] > gNodes[2*ni+2].aabbMin[k]+1e-4f || n.aabbMax[k] < gNodes[2*ni+2].aabbMax[k]-1e-4f) badInternal++;
                }
            }
                                                std::printf("RAYTRACE[parity] nodes=%zu badMinMax=%d badLeaf=%d badInternal=%d gTris=%zu\n",
                        gNodes.size(), badMinMax, badLeaf, badInternal, gTris.size());
            // CPU re-implementation of the GPU traversal over the readback data.
            auto traceCpu = [&](const float* o, const float* d, float tMax,
                                float& tOut, unsigned& idxOut) -> bool {
                int stack[128]; int sp=0; stack[sp++]=0;
                bool hit=false; float best=tMax; unsigned bi=0;
                auto rayAabb=[&](const float* o,const float* d,const float* mn,const float* mx,float tmin,float tmax){
                    for(int ax=0;ax<3;ax++){ if(d[ax]==0.0f){ if(o[ax]<mn[ax]||o[ax]>mx[ax]) return false; continue; }
                        float inv=1.0f/d[ax]; float t0=(mn[ax]-o[ax])*inv,t1=(mx[ax]-o[ax])*inv;
                        if(inv<0.0f){float tt=t0;t0=t1;t1=tt;} tmin=std::max(tmin,t0); tmax=std::min(tmax,t1);
                        if(tmax<tmin) return false; } return true; };
                while(sp>0){ int ni=stack[--sp]; auto&n=gNodes[ni];
                    if(!rayAabb(o,d,n.aabbMin,n.aabbMax,0.0f,best)) continue;
                    if(n.triCount>0){ int first=-(n.leftFirst)-1;
                        for(int i=first;i<first+n.triCount;i++){ float t,u,v;
                            if(zq::render::IntersectRayTriangle(o,d,gTris[i],0.0f,best,t,u,v)){ best=t; bi=(unsigned)i; hit=true; } } }
                    else { stack[sp++]=n.rightFirst; stack[sp++]=n.leftFirst; } }
                if(hit){ tOut=best; idxOut=bi; } return hit;
            };
            // Brute-force ground truth over the ORIGINAL triangles.
            auto bruteForce = [&](const float* o, const float* d, float tMax, float& tOut) {
                float best = tMax; bool hit=false; float t,u,v;
                for (auto& tr : ent)
                    if (zq::render::IntersectRayTriangle(o, d, tr, 0.0f, best, t, u, v)) { best=t; hit=true; }
                if (hit) tOut=best; return hit;
            };
            int mismatches=0, hits=0, hitMiss=0, distMismatch=0;
            int cpuVsBrute=0, gpuVsBrute=0;
            std::uniform_real_distribution<float> r3(-1,1);
            for (int r = 0; r < 4000; r++) {
                float o[3]={d(rng),d(rng),d(rng)};
                float dd[3]={r3(rng),r3(rng),r3(rng)};
                float l=std::sqrt(dd[0]*dd[0]+dd[1]*dd[1]+dd[2]*dd[2]);
                if(l<1e-5f) continue; dd[0]/=l; dd[1]/=l; dd[2]/=l;
                float bt=0; bool bh = bruteForce(o, dd, 1e6f, bt);
                unsigned cIdx; float cT=0;
                bool ch = cpuScene.TraceClosest(o, dd, 1e6f, cIdx, cT, *(new float), *(new float));
                float gT=0; unsigned gIdx=0;
                bool gh = traceCpu(o, dd, 1e6f, gT, gIdx);
                if (ch != bh) cpuVsBrute++;
                if (gh != bh) gpuVsBrute++;
                if (ch != gh) { mismatches++; hitMiss++; continue; }
                if (ch) { hits++;
                    if (std::fabs(cT-gT) > 0.05f) { mismatches++; distMismatch++; } }
            }
            std::printf("RAYTRACE[parity] rays_hit=%d hitMiss=%d distMismatch=%d cpuVsBrute=%d gpuVsBrute=%d\n",
                        hits, hitMiss, distMismatch, cpuVsBrute, gpuVsBrute);
            bool pass = mismatches==0 && hits>100;
            std::printf("RAYTRACE[parity] %s\n", pass?"PASSED":"FAILED");
            fail |= pass?0:1;
        }
    }

    rt.Shutdown();
    vk.Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return fail;
}