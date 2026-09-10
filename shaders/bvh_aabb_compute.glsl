#version 450
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

struct Tri {
    vec4 p0;
    vec4 p1;
    vec4 p2;
    vec2 uv0;
    vec2 uv1;
    vec2 uv2;
    uint tex;
    uint light;
};
struct BvhNode {
    vec4 aabbMin;
    vec4 aabbMax;
    int leftFirst;
    int rightFirst;
    int triCount;
    int pad;
};

layout(std430, binding = 3) readonly buffer SortedBuf { Tri sortedTris[]; };
layout(std430, binding = 4) buffer NodeBuf { BvhNode nodes[]; };

layout(std140, binding = 5) uniform BvhUBO {
    uint triCount;
    uint padCount;   // N'
    uint _pad0;
    uint _pad1;
    vec4 bmin;
    vec4 bmax;
} ubo;
layout(push_constant) uniform PC {
    uint depth;   // aabb level (0 = root, D = leaves)
    uint sortK;   // bitonic merge size
    uint sortJ;   // bitonic merge distance
} pc;

void main() {
    uint n = ubo.padCount;
    uint depth = pc.depth;
    if (depth >= 31u) return;
    uint lo = (1u << depth) - 1u;
    uint hi = (1u << (depth + 1u)) - 2u;
    uint j = lo + gl_GlobalInvocationID.x;
    if (j > hi) return;

    BvhNode nd = nodes[j];
    if (j >= n - 1u) {
        // leaf: triangle slot s = j - (n-1)
        uint s = j - (n - 1u);
        Tri t = sortedTris[s];
        nd.aabbMin = min(min(t.p0, t.p1), t.p2);
        nd.aabbMax = max(max(t.p0, t.p1), t.p2);
    } else {
        BvhNode l = nodes[2u*j + 1u];
        BvhNode r = nodes[2u*j + 2u];
        nd.aabbMin = min(l.aabbMin, r.aabbMin);
        nd.aabbMax = max(l.aabbMax, r.aabbMax);
    }
    nodes[j] = nd;
}