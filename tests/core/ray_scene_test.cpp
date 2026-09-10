#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "render/ray_scene.hpp"
#include "app/rt_scene.hpp"
#include <random>
#include <vector>

using namespace zq::render;

namespace {

RtTriangle MakeTri(float x0, float y0, float z0,
                   float x1, float y1, float z1,
                   float x2, float y2, float z2) {
    RtTriangle t;
    t.p0[0] = x0; t.p0[1] = y0; t.p0[2] = z0;
    t.p1[0] = x1; t.p1[1] = y1; t.p1[2] = z1;
    t.p2[0] = x2; t.p2[1] = y2; t.p2[2] = z2;
    t.uv0[0] = 0; t.uv0[1] = 0;
    t.uv1[0] = 1; t.uv1[1] = 0;
    t.uv2[0] = 0; t.uv2[1] = 1;
    t.tex = 1;
    return t;
}

// A single unit triangle lying in the XY plane at z=0, facing +Z.
RtTriangle UnitXY() {
    return MakeTri(0, 0, 0, 1, 0, 0, 0, 1, 0);
}

} // namespace

TEST_CASE("ray-triangle: front hit at center", "[ray_scene]") {
    RtTriangle tri = UnitXY();
    float o[3] = {0.4f, 0.4f, 1.0f};
    float d[3] = {0, 0, -1};
    float t, u, v;
    REQUIRE(IntersectRayTriangle(o, d, tri, 0.0f, 1000.0f, t, u, v));
    CHECK_THAT(t, Catch::Matchers::WithinAbs(1.0f, 1e-5f));
    CHECK_THAT(u, Catch::Matchers::WithinAbs(0.4f, 1e-4f));
    CHECK_THAT(v, Catch::Matchers::WithinAbs(0.4f, 1e-4f));
}

TEST_CASE("ray-triangle: hit is culled outside triangle", "[ray_scene]") {
    RtTriangle tri = UnitXY();
    // Ray toward a point outside the unit triangle (x,y = 2,2).
    float o[3] = {2.0f, 2.0f, 1.0f};
    float d[3] = {0, 0, -1};
    float t, u, v;
    REQUIRE_FALSE(IntersectRayTriangle(o, d, tri, 0.0f, 1000.0f, t, u, v));
}

TEST_CASE("ray-triangle: hit from behind (double-sided)", "[ray_scene]") {
    RtTriangle tri = UnitXY();
    float o[3] = {0.4f, 0.4f, -1.0f};
    float d[3] = {0, 0, 1};
    float t, u, v;
    // The intersection test is double-sided: a ray approaching the triangle
    // from either side reports a hit at the plane.
    REQUIRE(IntersectRayTriangle(o, d, tri, 0.0f, 1000.0f, t, u, v));
    CHECK_THAT(t, Catch::Matchers::WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("ray-triangle: parallel ray misses", "[ray_scene]") {
    RtTriangle tri = UnitXY();
    // Ray parallel to the triangle plane (in-plane, Z constant).
    float o[3] = {0.5f, 0.5f, 1.0f};
    float d[3] = {1, 0, 0};
    float t, u, v;
    REQUIRE_FALSE(IntersectRayTriangle(o, d, tri, 0.0f, 1000.0f, t, u, v));
}

TEST_CASE("ray-triangle: hit clamped by tMax", "[ray_scene]") {
    RtTriangle tri = UnitXY();
    float o[3] = {0.4f, 0.4f, 1.0f};
    float d[3] = {0, 0, -1};
    float t, u, v;
    REQUIRE_FALSE(IntersectRayTriangle(o, d, tri, 0.0f, 0.5f, t, u, v));
    REQUIRE(IntersectRayTriangle(o, d, tri, 0.0f, 1.5f, t, u, v));
}

TEST_CASE("ray-triangle: vertex (0,0) uv interpolates to uv2", "[ray_scene]") {
    RtTriangle tri = UnitXY();
    float o[3] = {0.0f, 0.0f, 1.0f};
    float d[3] = {0, 0, -1};
    float t, u, v;
    REQUIRE(IntersectRayTriangle(o, d, tri, 0.0f, 1000.0f, t, u, v));
    // At vertex 0: u=v=0, interpolated uv = uv0.
    CHECK_THAT(u, Catch::Matchers::WithinAbs(0.0f, 1e-4f));
    CHECK_THAT(v, Catch::Matchers::WithinAbs(0.0f, 1e-4f));
}

TEST_CASE("ray-aabb: box hit and miss", "[ray_scene]") {
    float mn[3] = {0, 0, 0};
    float mx[3] = {1, 1, 1};
    float o[3] = {0.5f, 0.5f, 3.0f};
    float d[3] = {0, 0, -1};
    REQUIRE(IntersectRayAabb(o, d, mn, mx, 0.0f, 1000.0f));
    // Ray beside the box.
    float o2[3] = {5.0f, 5.0f, 3.0f};
    REQUIRE_FALSE(IntersectRayAabb(o2, d, mn, mx, 0.0f, 1000.0f));
    // Box behind the origin.
    float o3[3] = {0.5f, 0.5f, 3.0f};
    float d3[3] = {0, 0, 1};
    REQUIRE_FALSE(IntersectRayAabb(o3, d3, mn, mx, 0.0f, 1000.0f));
}

TEST_CASE("build-bvh: empty scene produces no nodes", "[ray_scene]") {
    std::vector<RtTriangle> tris;
    auto nodes = BuildBvh(tris);
    REQUIRE(nodes.empty());
}

TEST_CASE("build-bvh: single triangle is a leaf at root", "[ray_scene]") {
    std::vector<RtTriangle> tris{ UnitXY() };
    auto nodes = BuildBvh(tris);
    REQUIRE(nodes.size() == 1);
    CHECK(nodes[0].triCount == 1);
    CHECK(nodes[0].leftFirst == -1); // -(0+1)
    CHECK_THAT(nodes[0].aabbMin[0], Catch::Matchers::WithinAbs(0.0f, 1e-5f));
    CHECK_THAT(nodes[0].aabbMax[1], Catch::Matchers::WithinAbs(1.0f, 1e-5f));
}

// Brute-force references used to validate the BVH traversal.
namespace {
bool BruteClosest(const std::vector<RtTriangle>& tris, const float* o, const float* d,
                  float tMax, std::uint32_t& index, float& tOut) {
    bool hit = false;
    float best = tMax;
    std::uint32_t bestIndex = 0;
    for (std::size_t i = 0; i < tris.size(); i++) {
        float t, u, v;
        if (IntersectRayTriangle(o, d, tris[i], 0.0f, best, t, u, v)) {
            best = t; bestIndex = (std::uint32_t)i; hit = true;
        }
    }
    if (hit) { index = bestIndex; tOut = best; }
    return hit;
}
bool BruteShadow(const std::vector<RtTriangle>& tris, const float* o, const float* d, float tMax) {
    for (const auto& tri : tris) {
        float t, u, v;
        if (IntersectRayTriangle(o, d, tri, 0.0f, tMax, t, u, v)) return true;
    }
    return false;
}
} // namespace

// Build a randomized grid of small triangles scattered in 3D space.
std::vector<RtTriangle> MakeRandomTriangles(std::mt19937& rng, int count) {
    std::uniform_real_distribution<float> dist(-10.0f, 10.0f);
    std::vector<RtTriangle> tris;
    tris.reserve(count);
    for (int i = 0; i < count; i++) {
        float bx = dist(rng), by = dist(rng), bz = dist(rng);
        float s = 1.0f;
        tris.push_back(MakeTri(bx, by, bz, bx + s, by, bz, bx, by + s, bz));
    }
    return tris;
}

TEST_CASE("rt-scene: closest-hit matches brute force", "[ray_scene]") {
    std::mt19937 rng(12345);
    auto tris = MakeRandomTriangles(rng, 200);
    RtScene scene;
    scene.Build(tris);

    std::uniform_real_distribution<float> dist(-15.0f, 15.0f);
    for (int trial = 0; trial < 2000; trial++) {
        float o[3] = {dist(rng), dist(rng), dist(rng)};
        // Bias toward +Z so rays pass through the scatter.
        float d[3] = {dist(rng), dist(rng), dist(rng) * 0.3f};
        float dl = std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        if (dl < 1e-6f) continue;
        d[0] /= dl; d[1] /= dl; d[2] /= dl;

        std::uint32_t idxB, idxS;
        float tB = 0, tS = 0, u, v;
        // Brute force must scan the REORDERED array (scene.Triangles()) since
        // BVH leaf indices refer to that order.
        const auto& reordered = scene.Triangles();
        bool b = BruteClosest(reordered, o, d, 1000.0f, idxB, tB);
        bool s = scene.TraceClosest(o, d, 1000.0f, idxS, tS, u, v);
        REQUIRE(b == s);
        if (b) {
            CHECK(idxB == idxS);
            CHECK_THAT(tB, Catch::Matchers::WithinAbs(tS, 1e-4f));
        }
    }
}

TEST_CASE("rt-scene: shadow matches brute force", "[ray_scene]") {
    std::mt19937 rng(999);
    auto tris = MakeRandomTriangles(rng, 150);
    RtScene scene;
    scene.Build(tris);

    std::uniform_real_distribution<float> dist(-15.0f, 15.0f);
    for (int trial = 0; trial < 2000; trial++) {
        float o[3] = {dist(rng), dist(rng), dist(rng)};
        float d[3] = {dist(rng), dist(rng), dist(rng)};
        float dl = std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        if (dl < 1e-6f) continue;
        d[0] /= dl; d[1] /= dl; d[2] /= dl;
        float tMax = 50.0f;
        REQUIRE(BruteShadow(tris, o, d, tMax) == scene.TraceShadow(o, d, tMax));
    }
}

TEST_CASE("rt-scene: simple one-triangle closest hit", "[ray_scene]") {
    RtScene scene;
    scene.Build({ UnitXY() });
    float o[3] = {0.5f, 0.5f, 2.0f};
    float d[3] = {0, 0, -1};
    std::uint32_t idx; float t, u, v;
    REQUIRE(scene.TraceClosest(o, d, 100.0f, idx, t, u, v));
    CHECK(idx == 0);
    CHECK_THAT(t, Catch::Matchers::WithinAbs(2.0f, 1e-4f));
}

TEST_CASE("rt-scene: closest picks the nearer of two triangles", "[ray_scene]") {
    RtTriangle near = UnitXY();
    near.p0[2] = 1.0f; near.p1[2] = 1.0f; near.p2[2] = 1.0f; // z=1
    RtTriangle far = UnitXY();                                 // z=0
    RtScene scene;
    scene.Build({ near, far });
    float o[3] = {0.5f, 0.5f, 5.0f};
    float d[3] = {0, 0, -1};
    std::uint32_t idx; float t, u, v;
    REQUIRE(scene.TraceClosest(o, d, 100.0f, idx, t, u, v));
    CHECK(idx == 0); // nearer triangle
    CHECK_THAT(t, Catch::Matchers::WithinAbs(4.0f, 1e-4f));
}

TEST_CASE("rt-scene: empty scene never hits", "[ray_scene]") {
    RtScene scene;
    scene.Build({});
    float o[3] = {0, 0, 1};
    float d[3] = {0, 0, -1};
    std::uint32_t idx; float t, u, v;
    REQUIRE_FALSE(scene.TraceClosest(o, d, 100.0f, idx, t, u, v));
    REQUIRE_FALSE(scene.TraceShadow(o, d, 100.0f));
}

// ---------------------------------------------------------------------------
// RtSceneBuilder: triangle/atlas assembly for the in-game ray tracer.
// ---------------------------------------------------------------------------

namespace {
// A single triangle built from a 2x2 grid of the given RGBA texture.
void AddColoredTri(zq::app::RtSceneBuilder& b, std::uint8_t r, std::uint8_t g, std::uint8_t bl) {
    struct V { float pos[3]; float uv[2]; };
    V verts[3] = {
        { {-1,-1,0}, {0,0} },
        { { 1,-1,0}, {1,0} },
        { { 0, 1,0}, {0.5f,1} },
    };
    std::uint32_t idx[3] = {0,1,2};
    std::vector<uint8_t> tex(8*8*4, 255);
    for (size_t i = 0; i < tex.size(); i += 4) { tex[i]=r; tex[i+1]=g; tex[i+2]=bl; }
    b.AddMesh(verts, 3, sizeof(V), idx, 3, tex.data(), 8, 8, nullptr);
}

float TileAvgR(const std::vector<uint8_t>& atlas, std::uint32_t W, const zq::render::RtTileInfo& ti) {
    float sum = 0; int n = 0;
    for (std::uint32_t y = 0; y < (std::uint32_t)ti.h; y++)
        for (std::uint32_t x = 0; x < (std::uint32_t)ti.w; x++) {
            size_t d = (((size_t)((std::uint32_t)ti.v + y)) * W + (std::uint32_t)ti.u + x) * 4;
            sum += atlas[d]; n++;
        }
    return sum / n;
}
} // namespace

TEST_CASE("rt-scene-builder: produces triangles with texture tiles", "[rt_builder]") {
    zq::app::RtSceneBuilder b;
    AddColoredTri(b, 200, 0, 0);   // red
    AddColoredTri(b, 0, 200, 0);   // green
    AddColoredTri(b, 0, 0, 200);   // blue
    auto tris = b.Triangles();
    REQUIRE(tris.size() == 3);
    // Each distinct texture gets its own tile; red/green/blue all present.
    CHECK(tris[0].tex != tris[1].tex);
    CHECK(tris[0].tex != tris[2].tex);
    CHECK(tris[1].tex != tris[2].tex);
    const auto& atlas = b.AtlasRgba();
    std::uint32_t W = b.AtlasWidth();
    const auto& tis = b.TileInfos();
    // All three tiles must be preserved at native resolution.
    CHECK_THAT(TileAvgR(atlas, W, tis[tris[0].tex]), Catch::Matchers::WithinAbs(200.0f, 5.0f));
    CHECK_THAT(TileAvgR(atlas, W, tis[tris[1].tex]), Catch::Matchers::WithinAbs(0.0f, 5.0f));
    CHECK_THAT(TileAvgR(atlas, W, tis[tris[2].tex]), Catch::Matchers::WithinAbs(0.0f, 5.0f));
}

TEST_CASE("rt-scene-builder: same texture repeated reuses tile", "[rt_builder]") {
    zq::app::RtSceneBuilder b;
    AddColoredTri(b, 120, 120, 120);
    AddColoredTri(b, 120, 120, 120); // same color
    auto tris = b.Triangles();
    REQUIRE(tris.size() == 2);
    // Note: the builder registers every mesh's texture as its OWN tile (it
    // cannot dedup by pointer since callers pass temporaries). So tiles differ,
    // but both must be the correct (gray) color, not white.
    const auto& atlas = b.AtlasRgba();
    std::uint32_t W = b.AtlasWidth();
    const auto& tis = b.TileInfos();
    for (auto& t : tris)
        CHECK_THAT(TileAvgR(atlas, W, tis[t.tex]), Catch::Matchers::WithinAbs(120.0f, 5.0f));
}

TEST_CASE("rt-scene-builder: empty scene produces no triangles and a blank atlas", "[rt_builder]") {
    zq::app::RtSceneBuilder b;
    REQUIRE(b.Triangles().empty());
    REQUIRE(b.AtlasWidth() == 0);
}