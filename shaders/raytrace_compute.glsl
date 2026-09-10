#version 450

layout(local_size_x = 8, local_size_y = 8) in;

// Triangle storage (std430), mirrors CPU RtTriangle (96 bytes).
struct Tri {
    vec4 p0;
    vec4 p1;
    vec4 p2;
    vec2 uv0;
    vec2 uv1;
    vec2 uv2;
    uint tex;
    uint light;
    uint tag;
    uint _pad;
};
layout(std430, binding = 0) readonly buffer TriBuf { Tri tris[]; };

// BVH node storage (std430), mirrors CPU BvhNode (48 bytes).
// internal: leftFirst/rightFirst >= 0 are child indices, triCount == 0
// leaf:     leftFirst == -(firstTri+1) < 0, triCount == triangle count
struct BvhNode {
    vec4 aabbMin;
    vec4 aabbMax;
    int leftFirst;
    int rightFirst;
    int triCount;
    int _pad;
};
layout(std430, binding = 1) readonly buffer NodeBuf { BvhNode nodes[]; };

layout(std140, binding = 2) uniform CamUBO {
    mat4 invViewProj;
    vec3 camPos;
    float painFlash;      // 0..1: full-screen red tint for damage feedback
    vec3 lightDir;    // unit direction TOWARD the light (fallback key light)
    float _pad1;
    vec3 lightColor;
    float _pad2;
    vec3 ambient;
    float _pad3;
    vec2 atlasSize;   // atlas dimensions in texels
    uint triCount;    // number of scene triangles
    uint numLights;   // number of point lights
    uint etriCount;   // number of MDL entity triangles
    uint numShadowLights; // how many of the lights cast shadow rays
    uint gunTriCount; // number of first-person viewmodel triangles
} cam;

// Point lights (from map `light` entities). pos.xyz = origin, pos.w = intensity;
// color.rgb = tint, color.w = radius (max distance, 0 disables the light).
struct PLight {
    vec4 pos;
    vec4 color;
};
layout(std430, binding = 5) readonly buffer LightBuf { PLight lights[]; };

// Per-tile atlas info: vec4 (originU, originV, sizeU, sizeV) in texels.
layout(std430, binding = 6) readonly buffer TileBuf { vec4 tileInfo[]; };

// Dynamic MDL entity triangles + BVH (separate from the static world).
layout(std430, binding = 7) readonly buffer ETriBuf { Tri etris[]; };
layout(std430, binding = 8) readonly buffer ENodeBuf { BvhNode enodes[]; };

// First-person viewmodel (current weapon): small dedicated triangle set + BVH
// rebuilt only when the viewmodel changes, so the on-top overlay stays cheap.
layout(std430, binding = 9) readonly buffer GunTriBuf { Tri gtris[]; };
layout(std430, binding = 10) readonly buffer GunNodeBuf { BvhNode gunodes[]; };

layout(binding = 3) uniform sampler2D uAtlas;

layout(binding = 4, rgba8) uniform image2D outImage;

const int kStackSize = 256;
const float kEps = 1e-5f;

bool rayAabb(vec3 o, vec3 d, vec4 amin, vec4 amax, float tMin, float tMax, out float tNear, out float tFar) {
    for (int i = 0; i < 3; i++) {
        if (d[i] == 0.0) {
            // Parallel to this axis: hit only if the origin is inside the slab.
            if (o[i] < amin[i] || o[i] > amax[i]) return false;
            continue;
        }
        float invD = 1.0 / d[i];
        float t0 = (amin[i] - o[i]) * invD;
        float t1 = (amax[i] - o[i]) * invD;
        if (invD < 0.0) { float tmp = t0; t0 = t1; t1 = tmp; }
        tMin = max(tMin, t0);
        tMax = min(tMax, t1);
        if (tMax < tMin) return false;
    }
    tNear = tMin;
    tFar = tMax;
    return true;
}

float hash(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

vec3 cosineHemisphere(vec3 N, vec3 seed) {
    vec3 up = abs(N.y) < 0.99 ? vec3(0,1,0) : vec3(1,0,0);
    vec3 T = normalize(cross(up, N));
    vec3 B = cross(N, T);

    // Two independent random numbers
    float r1 = hash(seed);
    float r2 = hash(seed + vec3(17.0, 59.0, 113.0));

    // Fixed cosine sample (can be jittered later)
    // float r1 = 1;
    // float r2 = 1;

    float phi = 2.0 * 3.14159265 * r1;
    float cosTheta = sqrt(1.0 - r2);
    float sinTheta = sqrt(r2);

    return normalize(
        T * cos(phi) * sinTheta +
        B * sin(phi) * sinTheta +
        N * cosTheta
    );
}

// Moller-Trumbore, mirrors CPU IntersectRayTriangle. Double-sided.
bool rayTri(vec3 o, vec3 d, Tri tr, float tMin, float tMax, out float t, out float u, out float v) {
    vec3 e1 = tr.p1.xyz - tr.p0.xyz;
    vec3 e2 = tr.p2.xyz - tr.p0.xyz;
    vec3 pvec = cross(d, e2);
    float det = dot(e1, pvec);
    if (det > -kEps && det < kEps) return false;
    float invDet = 1.0 / det;
    vec3 tvec = o - tr.p0.xyz;
    u = dot(tvec, pvec) * invDet;
    if (u < 0.0 || u > 1.0) return false;
    vec3 qvec = cross(tvec, e1);
    v = dot(d, qvec) * invDet;
    if (v < 0.0 || u + v > 1.0) return false;
    t = dot(e2, qvec) * invDet;
    return (t > tMin && t < tMax);
}

vec3 triNormal(Tri t) {
    return normalize(cross(t.p1.xyz - t.p0.xyz, t.p2.xyz - t.p0.xyz));
}

bool traceClosest(vec3 o, vec3 d, float tMax, out int hitTri, out float hitU, out float hitV, out float hitT) {
    int stack[kStackSize];
    int sp = 0;
    stack[sp++] = 0;
    bool hit = false;
    float bestT = tMax;
    hitTri = -1;
    while (sp > 0) {
        int idx = stack[--sp];
        BvhNode n = nodes[idx];
        float tNear, tFar;
        if (!rayAabb(o, d, n.aabbMin, n.aabbMax, 0.0, bestT, tNear, tFar))
            continue;
        if (n.triCount > 0) {
            int first = -(n.leftFirst) - 1;
            for (int i = first; i < first + n.triCount; i++) {
                float t, u, v;
                if (rayTri(o, d, tris[i], 0.0, bestT, t, u, v)) {
                    bestT = t; hitTri = i; hitU = u; hitV = v; hitT = t;
                    hit = true;
                }
            }
        } else {
            if (sp + 1 < kStackSize) {
                stack[sp++] = n.rightFirst;
                stack[sp++] = n.leftFirst;
            }
        }
    }
    return hit;
}

// Entity BVH closest-hit (mirrors traceClosest, but over enodes/etris).
bool traceClosestEnt(vec3 o, vec3 d, float tMax, out int hitTri, out float hitU, out float hitV, out float hitT) {
    if (cam.etriCount == 0u) return false;
    int stack[kStackSize];
    int sp = 0;
    stack[sp++] = 0;
    bool hit = false;
    float bestT = tMax;
    hitTri = -1;
    while (sp > 0) {
        int idx = stack[--sp];
        BvhNode n = enodes[idx];
        float tNear, tFar;
        if (!rayAabb(o, d, n.aabbMin, n.aabbMax, 0.0, bestT, tNear, tFar))
            continue;
        if (n.triCount > 0) {
            int first = -(n.leftFirst) - 1;
            for (int i = first; i < first + n.triCount; i++) {
                float t, u, v;
                if (rayTri(o, d, etris[i], 0.0, bestT, t, u, v)) {
                    bestT = t; hitTri = i; hitU = u; hitV = v; hitT = t;
                    hit = true;
                }
            }
        } else {
            if (sp + 1 < kStackSize) { stack[sp++] = n.rightFirst; stack[sp++] = n.leftFirst; }
        }
    }
    return hit;
}

// Combined closest-hit over the static world BVH and the dynamic entity BVH.
bool traceAny(vec3 o, vec3 d, float tMax, out Tri hit, out float u, out float v,
              out float t, out int isEnt, out int srcIdx) {
    int wTri; float wu, wv, wt;
    bool wh = traceClosest(o, d, tMax, wTri, wu, wv, wt);
    int eTri; float eu, ev, et;
    bool eh = traceClosestEnt(o, d, tMax, eTri, eu, ev, et);
    if (wh && (!eh || wt <= et)) { hit = tris[wTri]; u = wu; v = wv; t = wt; isEnt = 0; srcIdx = wTri; return true; }
    if (eh) { hit = etris[eTri]; u = eu; v = ev; t = et; isEnt = 1; srcIdx = eTri; return true; }
    return false;
}

// First-person viewmodel overlay: choose-hit over the dedicated gun BVH (its
// own small buffer, tag-free). Rendered on top of the scene regardless of
// depth, matching Quake's "viewmodel drawn last" look.
bool traceGun(vec3 o, vec3 d, float tMax, out Tri hit, out float u, out float v, out float t) {
    if (cam.gunTriCount == 0u) return false;
    int stack[kStackSize];
    int sp = 0;
    stack[sp++] = 0;
    bool found = false;
    float bestT = tMax;
    while (sp > 0) {
        int idx = stack[--sp];
        BvhNode n = gunodes[idx];
        float tNear, tFar;
        if (!rayAabb(o, d, n.aabbMin, n.aabbMax, 0.0, bestT, tNear, tFar))
            continue;
        if (n.triCount > 0) {
            int first = -(n.leftFirst) - 1;
            for (int i = first; i < first + n.triCount; i++) {
                float tt, tu, tv;
                if (rayTri(o, d, gtris[i], 0.0, bestT, tt, tu, tv)) {
                    bestT = tt; u = tu; v = tv; t = tt; hit = gtris[i]; found = true;
                }
            }
        } else {
            if (sp + 1 < kStackSize) { stack[sp++] = n.rightFirst; stack[sp++] = n.leftFirst; }
        }
    }
    return found;
}
bool traceGunRed(vec3 o, vec3 d, out float t) {
    if (cam.gunTriCount == 0u) return false;
    float bestT = 1e30;
    bool found = false;
    for (uint i = 0u; i < cam.gunTriCount; i++) {
        float tt, tu, tv;
        if (rayTri(o, d, gtris[i], 0.0, bestT, tt, tu, tv)) { bestT = tt; found = true; }
    }
    return found;
}

// Shadow test over BOTH world and entity geometry. skipW/skipE are the source
// triangle indices to ignore (the surface the ray leaves), -1 if not applicable.
bool traceShadow(vec3 o, vec3 d, float tMax, int skipW, int skipE) {
    int stack[kStackSize];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        int idx = stack[--sp];
        BvhNode n = nodes[idx];
        float tNear, tFar;
        if (!rayAabb(o, d, n.aabbMin, n.aabbMax, 0.0, tMax, tNear, tFar))
            continue;
        if (n.triCount > 0) {
            int first = -(n.leftFirst) - 1;
            for (int i = first; i < first + n.triCount; i++) {
                if (i == skipW) continue;
                float t, u, v;
                if (rayTri(o, d, tris[i], 0.0, tMax, t, u, v)) return true;
            }
        } else {
            if (sp + 1 < kStackSize) { stack[sp++] = n.rightFirst; stack[sp++] = n.leftFirst; }
        }
    }
    // entities
    if (cam.etriCount == 0u) return false;
    sp = 0; stack[sp++] = 0;
    while (sp > 0) {
        int idx = stack[--sp];
        BvhNode n = enodes[idx];
        float tNear, tFar;
        if (!rayAabb(o, d, n.aabbMin, n.aabbMax, 0.0, tMax, tNear, tFar))
            continue;
        if (n.triCount > 0) {
            int first = -(n.leftFirst) - 1;
            for (int i = first; i < first + n.triCount; i++) {
                if (i == skipE) continue;
                float t, u, v;
                if (rayTri(o, d, etris[i], 0.0, tMax, t, u, v)) return true;
            }
        } else {
            if (sp + 1 < kStackSize) { stack[sp++] = n.rightFirst; stack[sp++] = n.leftFirst; }
        }
    }
    return false;
}

// Interpolated texture coordinate on the triangle.
vec2 interpolateUV(Tri t, float u, float v) {
    return t.uv0 * (1.0 - u - v) + t.uv1 * u + t.uv2 * v;
}

// Direct lighting at a surface point: the diffuse term from every point light.
vec3 directLight(vec3 P, vec3 N, int isEnt, int srcIdx) {
    vec3 light = cam.ambient;
    for (int i = 0; i < int(cam.numLights); i++) {
        PLight pl = lights[i];
        float radius = pl.color.w;
        if (radius <= 0.0) continue;
        vec3 toL = pl.pos.xyz - P;
        float dl = length(toL);
        if (dl > radius) continue;
        vec3 L = toL / dl;
        // Orient the surface normal toward the light: world faces are not
        // consistently wound, so half the walls' normals point away from the
        // light. Flipping the normal here (no abs) makes every interior wall
        // face the light, then the dot is clamped.
        float ndl = abs(dot(N, L));
        // Inverse-square falloff: at dl=0 the light is at full intensity and it
        // falls off ~1/dl^2 away from the light (reaching half at dl=radius).
        float r2 = radius * radius;
        float atten = pl.pos.w * r2 / (r2 + dl * dl);
        // Only the nearest few lights cast shadow rays (they dominate visibility);
        // the rest illuminate without occlusion. This keeps lighting rich while
        // bounding shadow-ray cost.
        float sh = 1.0;
        if (i < int(cam.numShadowLights)) {
            vec3 spo = P + L * max(dl * 1e-3, 0.1);
            int skipW = (isEnt != 0) ? -1 : srcIdx;
            int skipE = (isEnt != 0) ? srcIdx : -1;
            sh = traceShadow(spo, L, dl - 1e-2, skipW, skipE) ? 0.15 : 1.0;
        }
        light += pl.color.rgb * (atten * ndl * sh + 0.1);
    }
    return light;
}

// Multi-bounce shading: traces the ray, and up to `BOUNCES` specular
// reflections, accumulating indirect light so lit areas bleed into shadow.
vec3 rayShade(vec3 o, vec3 d) {
    const int BOUNCES = 2;
    vec3 color = vec3(0.0);
    vec3 throughput = vec3(1.0);
    vec3 ro = o, rd = d;
    float mirror = 0.1;

    for (int bounce = 0; bounce < BOUNCES; bounce++) {
        Tri hit;
        float u, v, t;
        int isEnt, srcIdx;
        if (!traceAny(ro, rd, 1e30, hit, u, v, t, isEnt, srcIdx))
          break;
        vec3 P = ro + rd * t;
        vec3 N = triNormal(hit);
        vec2 uv = interpolateUV(hit, u, v);

        // Sample the texture atlas tile at its native size. World-face UVs are
        // not clamped to [0,1]; wrap with fract so the tile repeats.
        uint ti2 = hit.tex;
        vec4 ti = tileInfo[ti2];
        vec2 atlasUV = (ti.xy + fract(uv) * ti.zw) / cam.atlasSize;
        vec4 tint = texture(uAtlas, atlasUV);
        vec3 albedo = tint.rgb;

        // Emissive surfaces (glow quads for particles / muzzle flash / health
        // bar): fullbright emission, no bounce — the texture tile itself is the
        // emission profile (e.g. radial gradient for soft falloff), with the
        // alpha channel as the glow's opacity mask.
        if (hit.light != 0u) {
            color += tint.rgb * tint.a * throughput * 1.7;
            break;
        }

        color += albedo * throughput * (1.0 - mirror) * directLight(P, N, isEnt, srcIdx);


        if (bounce + 1 >= BOUNCES) break;

        // Reflect the incoming ray about the surface normal (oriented outward
        // against the ray) and recurse. Attenuate by the surface reflectance so
        // each bounce contributes less.
        vec3 Nb = (dot(N, rd) < 0.0) ? N : -N;
        vec3 R = reflect(rd, Nb);
        ro = P + Nb * 1e-2;
        rd = normalize(R);
        throughput *= mirror;
        if (dot(throughput, vec3(1.0)) < 1e-4) break;
    }
    return color;
}

void main() {
    ivec2 size = imageSize(outImage);
    ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
    if (pix.x >= size.x || pix.y >= size.y) return;

    vec2 uv = (vec2(pix) + 0.5) / vec2(size);
    // Vulkan NDC: x left->right in [-1,1]; y is TOP->BOTTOM so -1 is top.
    // Storage row 0 is the top of the displayed image.
    vec4 ndc = vec4(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, 0.5, 1.0);
    vec4 wp = cam.invViewProj * ndc;
    vec3 P = wp.xyz / wp.w;
    vec3 d = normalize(P - cam.camPos);

    vec3 color;
    {
        float gmint;
        Tri ghit; float gu, gv, gt;
        if (traceGun(cam.camPos, d, 1e30, ghit, gu, gv, gt)) {
            vec4 ti = tileInfo[ghit.tex];
            vec2 uv = ghit.uv0*(1.0 - gu - gv) + ghit.uv1*gu + ghit.uv2*gv;
            // Viewmodel UVs are normalized to the skin; map tile->atlas 1:1,
            // inset half a texel so bilinear sampling never bleeds into a
            // neighbouring tile.
            vec2 px = clamp(uv * ti.zw, vec2(0.5), ti.zw - vec2(0.5));
            vec2 atlasUV = (ti.xy + px) / cam.atlasSize;
            vec3 albedo = texture(uAtlas, atlasUV).rgb;
            vec3 n = triNormal(ghit);
            float lam = max(dot(n, normalize(cam.lightDir)), 0.0);
            color = albedo * (0.75 + 0.35 * lam) + albedo * 0.05;
        } else if (cam.triCount == 0u) {
            // No scene: draw sky.
            float h = max(d.z, 0.0);
            color = mix(vec3(0.10, 0.13, 0.22), vec3(0.55, 0.62, 0.72), h);
        } else {
            color = rayShade(cam.camPos, d);
        }
    }
    // Desaturate: mix the colour toward its luminance (grey) by `SAT`.
    const float SAT = 0.5;
    float lum = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(lum), color, SAT);

    // Pain flash: brief red tint when taking damage (pulsed via cam.painFlash 0..1)
    float pf = clamp(cam.painFlash, 0.0, 0.8);
    color = mix(color, vec3(1.0, 0.15, 0.1), pf);

    imageStore(outImage, pix, vec4(color, 1.0));
}