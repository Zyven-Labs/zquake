#version 450 core

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC {
    float uW;
    float uH;
} p;

// Screen-space crosshair, drawn procedurally at the framebuffer center. The
// host draws a full-screen triangle through the HUD pipeline (alpha-blended,
// depth disabled) so this runs on top of the world/weapon image.
void main() {
    vec2 px = gl_FragCoord.xy;
    vec2 c  = vec2(p.uW * 0.5, p.uH * 0.5);
    vec2 d  = abs(px - c);

    float len  = min(p.uW, p.uH) * 0.028;
    float gap  = max(2.0, min(p.uW, p.uH) * 0.005);
    float core = max(1.0, min(p.uW, p.uH) * 0.0012);
    float outl = core + max(1.0, min(p.uW, p.uH) * 0.0010);

    bool inH = (d.x + outl) > gap && d.x <= len + outl && d.y <= outl;
    bool inV = (d.y + outl) > gap && d.y <= len + outl && d.x <= outl;
    if (!inH && !inV) {
        outColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    bool coreH = inH && d.y <= core && d.x >= gap && d.x <= len;
    bool coreV = inV && d.x <= core && d.y >= gap && d.y <= len;
    vec3 col = (coreH || coreV) ? vec3(0.04, 0.90, 0.22)
                                : vec3(0.02, 0.05, 0.03);
    outColor = vec4(col, 1.0);
}