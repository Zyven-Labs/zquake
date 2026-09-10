#version 450
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

// In-place bitonic sort step. One dispatch per (k, j) step so global memory is
// consistent between dispatches (queue-ordered). Threads with i < (i^j) handle
// the compare-exchange for the pair, writing both slots deterministically.
layout(std430, binding = 1) buffer MortBuf { uvec2 morton[]; };
layout(std430, binding = 2) buffer OrderBuf { uint order[]; };

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
    if (i >= n) return;
    uint k = pc.sortK, j = pc.sortJ;
    uint ixj = i ^ j;
    if (ixj >= n) return;
    if (i < ixj) {
        // All np2 slots are pre-initialized in the Morton pass (padding = high
        // key), so every pair is read/written uniformly; indices are preserved.
        uvec2 a = morton[i];
        uvec2 b = morton[ixj];
        bool asc = ((i & k) == 0u);   // ascending half of this pair
        bool swap = asc ? (a.x > b.x) : (a.x < b.x);
        if (swap) { uvec2 t = a; a = b; b = t; }
        morton[i] = a;
        morton[ixj] = b;
    }
    if (i == 0u) order[0] = morton[0].y;
}