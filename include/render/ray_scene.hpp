#pragma once
#include <cstdint>
#include <vector>

namespace zq::render {

// Vec2 used for texture coordinates.
struct Vec2 {
    float x = 0, y = 0;
};

// One triangle of the ray-traced scene. The layout matches the std430
// declaration used by the GPU compute shader (80 bytes) so a CPU-side
// triangle array can be uploaded verbatim into a storage buffer.
struct RtTriangle {
    float p0[3] = {0, 0, 0};
    float pad0 = 0;
    float p1[3] = {0, 0, 0};
    float pad1 = 0;
    float p2[3] = {0, 0, 0};
    float pad2 = 0;
    float uv0[2] = {0, 0};
    float uv1[2] = {0, 0};
    float uv2[2] = {0, 0};
    std::uint32_t tex = 0;
    std::uint32_t light = 0;
    std::uint32_t tag = 0;   // overlay flags: 1 = first-person viewmodel
    // Explicit padding: GLSL std430 aligns the trailing vec4 vectors to 16, so
    // the GPU element is 96 bytes. Keep the CPU struct at exactly 96 bytes too
    // (C++ would otherwise pack it to 88) or every array element after the
    // first is misread.
    std::uint32_t _pad = 0;
    std::uint32_t _pad2 = 0;
    std::uint32_t _pad3 = 0;
};

// A point light placed at a map `light` entity's origin.
struct RtLight {
    float pos[3] = {0, 0, 0};
    float intensity = 1.0f;
    float color[3] = {1, 1, 1};
    float radius = 500.0f;
};

// Per-tile atlas info in texel coordinates: origin (u,v) and native size (w,h).
struct RtTileInfo {
    float u = 0, v = 0, w = 0, h = 0;
};

// BVH node. Layout matches the std430 GPU declaration (48 bytes).
//   internal node: leftFirst >= 0 and rightFirst >= 0 are child indices; triCount == 0
//   leaf node:     leftFirst == -(firstTriangle+1) (< 0); triCount == triangle count
struct BvhNode {
    float aabbMin[4] = {0, 0, 0, 0};
    float aabbMax[4] = {0, 0, 0, 0};
    std::int32_t leftFirst = 0;
    std::int32_t rightFirst = 0;
    std::int32_t triCount = 0;
    std::int32_t pad = 0;   // keep the struct at 48 bytes to match the GPU std430 node
};

// Ray-triangle intersection (Moller-Trumbore). Returns true if the ray hits
// within [tMin, tMax]. On hit, t and the interpolated barycentric uv are set.
bool IntersectRayTriangle(const float* o, const float* d,
                          const RtTriangle& tri, float tMin, float tMax,
                          float& t, float& u, float& v);

// Axis-aligned bounding box helper.
void ComputeBounds(const std::vector<RtTriangle>& tris, float* minOut, float* maxOut);

// Builds a median-split BVH over the given triangles. Returns the node array.
// The root is nodes[0]. Triangles are REORDERED IN PLACE so leaf triangles are
// contiguous in the range implied by each leaf node; the reordered array must
// be uploaded to the GPU alongside the nodes so leaf indices line up.
std::vector<BvhNode> BuildBvh(std::vector<RtTriangle>& tris);

// Ray vs axis-aligned bounding box (slab test). Returns true if ray [tMin,tMax]
// intersects the box.
bool IntersectRayAabb(const float* o, const float* d,
                      const float* aabbMin, const float* aabbMax,
                      float tMin, float tMax);

// A complete ray-traced scene: triangles + BVH. Provides closest-hit and
// shadow (occlusion) queries. This is the CPU reference for the GPU compute
// shader and is unit-tested against brute-force traversal.
class RtScene {
public:
    // Rebuilds triangles and BVH from the given triangle list.
    void Build(std::vector<RtTriangle> triangles);

    // Closest-hit trace. Returns true on any hit within [0, tMax]; on hit,
    // triangleIndex, t and interpolated uv are set.
    bool TraceClosest(const float* o, const float* d, float tMax,
                      std::uint32_t& triangleIndex, float& t, float& u, float& v) const;

    // Shadow / occlusion test. Returns true if anything blocks [0, tMax].
    // If skipTriangle >= 0, that triangle index is ignored (used to prevent
    // a shadow ray from self-intersecting the surface it leaves).
    bool TraceShadow(const float* o, const float* d, float tMax, int skipTriangle = -1) const;

    const std::vector<RtTriangle>& Triangles() const { return triangles_; }
    const std::vector<BvhNode>& Nodes() const { return nodes_; }

    std::size_t TriangleCount() const { return triangles_.size(); }
    bool Empty() const { return triangles_.empty(); }

private:
    std::vector<RtTriangle> triangles_;
    std::vector<BvhNode> nodes_;
};

} // namespace zq::render