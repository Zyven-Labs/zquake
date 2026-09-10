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

layout(std430, binding = 0) readonly buffer ETriBuf { Tri etris[]; };
layout(std430, binding = 1) readonly buffer MortBuf { uvec2 morton[]; };
layout(std430, binding = 2) buffer OrderBuf { uint order[]; };
layout(std430, binding = 3) buffer SortedBuf { Tri sortedTris[]; };
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
    uint i = gl_GlobalInvocationID.x;

    // Reorder: order[i] = sorted triangle index (morton[i].y); sortedTris[i] =
    // etris[order[i]] (pad beyond N with triangle 0).
    if (i < n) {
        uint si = (i < ubo.triCount) ? morton[i].y : 0u;
        order[i] = si;
        sortedTris[i] = etris[si];
    }

    // Build the full binary heap: internal nodes 0..n-2, leaves n-1..2n-2.
    if (i < 2u * n - 1u) {
        BvhNode nd;
        nd.aabbMin = vec4(0.0);
        nd.aabbMax = vec4(0.0);
        nd.leftFirst = 0;
        nd.rightFirst = 0;
        nd.triCount = 0;
        nd.pad = 0;
        if (i < n - 1u) {
            nd.leftFirst = int(2u * i + 1u);
            nd.rightFirst = int(2u * i + 2u);
        } else {
            uint s = i - (n - 1u);       // leaf slot in [0, n)
            nd.leftFirst = -(int(s) + 1); // covers sortedTris[s]
            nd.triCount = 1;
        }
        nodes[i] = nd;
    }
}