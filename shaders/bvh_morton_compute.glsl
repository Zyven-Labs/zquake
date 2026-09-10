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
layout(std430, binding = 0) readonly buffer ETriBuf { Tri etris[]; };
layout(std430, binding = 1) buffer MortBuf { uvec2 morton[]; };

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

uint morton3(uint x) { // expand 10 bits to 30 interleaved
    x = (x | (x << 16)) & 0x030000FFu;
    x = (x | (x << 8))  & 0x0300F00Fu;
    x = (x | (x << 4))  & 0x030C30C3u;
    x = (x | (x << 2))  & 0x09249249u;
    return x;
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= ubo.padCount) return;
    if (i >= ubo.triCount) {
        // Padding slot: high key so it sorts to the end, with its own index so
        // the (code, index) pairs are always preserved by the bitonic sort.
        morton[i] = uvec2(0xFFFFFFFFu, i);
        return;
    }
    Tri t = etris[i];
    vec3 c = (t.p0.xyz + t.p1.xyz + t.p2.xyz) * (1.0 / 3.0);
    vec3 n = (c - ubo.bmin.xyz) / max(ubo.bmax.xyz - ubo.bmin.xyz, vec3(1e-6));
    uvec3 q = uvec3(clamp(n * 1024.0, vec3(0.0), vec3(1023.0)));
    uint code = morton3(q.x) | (morton3(q.y) << 1) | (morton3(q.z) << 2);
    morton[i] = uvec2(code, i);
}