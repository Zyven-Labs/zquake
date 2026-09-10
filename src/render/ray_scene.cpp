#include "render/ray_scene.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

namespace zq::render {

namespace {

constexpr float kEpsilon = 1e-5f;
constexpr int kMaxStack = 128;

inline void CopyV3(float* dst, const float* src) {
    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
}

// Used internally for building bounds from arrays.
struct Bounds {
    float min[3] = { std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max() };
    float max[3] = { std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest() };
    void Include(const float* p) {
        for (int i = 0; i < 3; i++) {
            min[i] = std::min(min[i], p[i]);
            max[i] = std::max(max[i], p[i]);
        }
    }
};

} // namespace

bool IntersectRayTriangle(const float* o, const float* d,
                          const RtTriangle& tri, float tMin, float tMax,
                          float& t, float& u, float& v) {
    const float* e1 = tri.p1;
    const float* e2 = tri.p2;
    const float p0[3] = {tri.p0[0], tri.p0[1], tri.p0[2]};

    float tvec[3], pvec[3], qvec[3];
    pvec[0] = d[1] * (e2[2] - p0[2]) - d[2] * (e2[1] - p0[1]);
    pvec[1] = d[2] * (e2[0] - p0[0]) - d[0] * (e2[2] - p0[2]);
    pvec[2] = d[0] * (e2[1] - p0[1]) - d[1] * (e2[0] - p0[0]);

    float det = (e1[0] - p0[0]) * pvec[0] + (e1[1] - p0[1]) * pvec[1] + (e1[2] - p0[2]) * pvec[2];
    if (det > -kEpsilon && det < kEpsilon) return false;
    float invDet = 1.0f / det;

    tvec[0] = o[0] - p0[0];
    tvec[1] = o[1] - p0[1];
    tvec[2] = o[2] - p0[2];

    u = (tvec[0] * pvec[0] + tvec[1] * pvec[1] + tvec[2] * pvec[2]) * invDet;
    if (u < 0.0f || u > 1.0f) return false;

    qvec[0] = tvec[1] * (e1[2] - p0[2]) - tvec[2] * (e1[1] - p0[1]);
    qvec[1] = tvec[2] * (e1[0] - p0[0]) - tvec[0] * (e1[2] - p0[2]);
    qvec[2] = tvec[0] * (e1[1] - p0[1]) - tvec[1] * (e1[0] - p0[0]);

    v = (d[0] * qvec[0] + d[1] * qvec[1] + d[2] * qvec[2]) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;

    float tt = (e2[0] - p0[0]) * qvec[0] + (e2[1] - p0[1]) * qvec[1] + (e2[2] - p0[2]) * qvec[2];
    t = tt * invDet;
    return t > tMin && t < tMax;
}

void ComputeBounds(const std::vector<RtTriangle>& tris, float* minOut, float* maxOut) {
    Bounds b;
    for (const auto& tri : tris) {
        b.Include(tri.p0);
        b.Include(tri.p1);
        b.Include(tri.p2);
    }
    CopyV3(minOut, b.min);
    CopyV3(maxOut, b.max);
}

bool IntersectRayAabb(const float* o, const float* d,
                      const float* aabbMin, const float* aabbMax,
                      float tMin, float tMax) {
    for (int i = 0; i < 3; i++) {
        if (d[i] == 0.0f) {
            // Parallel to this axis: only a hit if the origin is inside the slab.
            if (o[i] < aabbMin[i] || o[i] > aabbMax[i]) return false;
            continue;
        }
        float invD = 1.0f / d[i];
        float t0 = (aabbMin[i] - o[i]) * invD;
        float t1 = (aabbMax[i] - o[i]) * invD;
        if (invD < 0.0f) std::swap(t0, t1);
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMax < tMin) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// BVH construction: median split over the largest bounding axis.
// ---------------------------------------------------------------------------

namespace {

// Node index assigned by BuildRec; -1 means invalid/empty.
struct BuildData {
    const std::vector<RtTriangle>* tris;      // original (pre-reorder)
    std::vector<BvhNode>* nodes;              // output
};

// Returns bounds of tris[first..first+count) using the CURRENT (possibly
// reordered) triangle array. We build on a reordered copy, so we store an
// index permutation instead.
struct BuildCtx {
    std::vector<RtTriangle> tris;   // working copy (reordered in place)
    std::vector<BvhNode> nodes;
};

int BuildRec(BuildCtx& ctx, int first, int count) {
    Bounds b;
    for (int i = first; i < first + count; i++) {
        b.Include(ctx.tris[i].p0);
        b.Include(ctx.tris[i].p1);
        b.Include(ctx.tris[i].p2);
    }

    BvhNode node;
    CopyV3(node.aabbMin, b.min);
    CopyV3(node.aabbMax, b.max);
    node.leftFirst = 0;
    node.rightFirst = 0;
    node.triCount = 0;

    int nodeIdx = (int)ctx.nodes.size();
    ctx.nodes.push_back(node);

    if (count <= 1) {
        // leaf: store negative tri index in leftFirst, count in triCount
        ctx.nodes[nodeIdx].leftFirst = -(first + 1);
        ctx.nodes[nodeIdx].triCount = count;
        return nodeIdx;
    }

    // Split over largest axis.
    float ext[3] = { b.max[0] - b.min[0], b.max[1] - b.min[1], b.max[2] - b.min[2] };
    int axis = (ext[0] >= ext[1]) ? 0 : 1;
    if (ext[2] > ext[axis]) axis = 2;

    // Median split using nth_element (O(n) per level -> O(n log n) total) instead
    // of a full sort (O(n log n) per level). Much faster on large scenes.
    auto begin = ctx.tris.begin() + first;
    std::nth_element(begin, begin + count / 2, begin + count,
                     [axis](const RtTriangle& a, const RtTriangle& b) {
                         float ca = (a.p0[axis] + a.p1[axis] + a.p2[axis]);
                         float cb = (b.p0[axis] + b.p1[axis] + b.p2[axis]);
                         return ca < cb;
                     });

    int mid = first + count / 2;
    int left = BuildRec(ctx, first, mid - first);
    int right = BuildRec(ctx, mid, first + count - mid);
    ctx.nodes[nodeIdx].leftFirst = left;
    ctx.nodes[nodeIdx].rightFirst = right;
    ctx.nodes[nodeIdx].triCount = 0;
    return nodeIdx;
}

// Brute-force reference: linear scan over all triangles.
bool TraceBrute(const std::vector<RtTriangle>& tris, const float* o, const float* d,
                float tMax, std::uint32_t& index, float& tOut, float& uOut, float& vOut) {
    bool hit = false;
    float bestT = tMax;
    std::uint32_t bestIndex = 0;
    float bu = 0, bv = 0;
    for (std::size_t i = 0; i < tris.size(); i++) {
        float t, u, v;
        if (IntersectRayTriangle(o, d, tris[i], 0.0f, bestT, t, u, v)) {
            bestT = t; bestIndex = (std::uint32_t)i; bu = u; bv = v;
            hit = true;
        }
    }
    if (hit) {
        index = bestIndex; tOut = bestT; uOut = bu; vOut = bv;
    }
    return hit;
}

} // namespace

std::vector<BvhNode> BuildBvh(std::vector<RtTriangle>& tris) {
    BuildCtx ctx;
    ctx.tris = tris;
    if (!tris.empty()) BuildRec(ctx, 0, (int)tris.size());
    tris = std::move(ctx.tris); // reordered array must match leaf indices
    return std::move(ctx.nodes);
}

// ---------------------------------------------------------------------------
// RtScene
// ---------------------------------------------------------------------------

void RtScene::Build(std::vector<RtTriangle> triangles) {
    triangles_ = std::move(triangles);
    nodes_ = BuildBvh(triangles_);
}

bool RtScene::TraceClosest(const float* o, const float* d, float tMax,
                           std::uint32_t& triangleIndex, float& t, float& u, float& v) const {
    if (triangles_.empty() || nodes_.empty()) return false;

    int stack[kMaxStack];
    int sp = 0;
    stack[sp++] = 0;
    bool hit = false;
    float bestT = tMax;
    std::uint32_t bestIndex = 0;
    float bu = 0, bv = 0;

    while (sp > 0) {
        int nodeIdx = stack[--sp];
        const BvhNode& node = nodes_[nodeIdx];
        if (!IntersectRayAabb(o, d, node.aabbMin, node.aabbMax, 0.0f, bestT))
            continue;
        if (node.triCount > 0) {
            int first = -(node.leftFirst) - 1;
            for (int i = first; i < first + node.triCount; i++) {
                float t, uu, vv;
                if (IntersectRayTriangle(o, d, triangles_[i], 0.0f, bestT, t, uu, vv)) {
                    bestT = t; bestIndex = (std::uint32_t)i; bu = uu; bv = vv;
                    hit = true;
                }
            }
        } else {
            // internal node: push both children
            stack[sp++] = node.rightFirst;
            stack[sp++] = node.leftFirst;
        }
    }
    if (hit) {
        triangleIndex = bestIndex; t = bestT; u = bu; v = bv;
    }
    return hit;
}

bool RtScene::TraceShadow(const float* o, const float* d, float tMax, int skipTriangle) const {
    if (triangles_.empty() || nodes_.empty()) return false;
    int stack[kMaxStack];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        int nodeIdx = stack[--sp];
        const BvhNode& node = nodes_[nodeIdx];
        if (!IntersectRayAabb(o, d, node.aabbMin, node.aabbMax, 0.0f, tMax))
            continue;
        if (node.triCount > 0) {
            int first = -(node.leftFirst) - 1;
            for (int i = first; i < first + node.triCount; i++) {
                if (i == skipTriangle) continue;
                float t, u, v;
                if (IntersectRayTriangle(o, d, triangles_[i], 0.0f, tMax, t, u, v))
                    return true;
            }
        } else {
            stack[sp++] = node.rightFirst;
            stack[sp++] = node.leftFirst;
        }
    }
    return false;
}

} // namespace zq::render