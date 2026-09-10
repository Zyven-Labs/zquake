#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uImage;

layout(push_constant) uniform PC { float uHealth; float uW; float uH; } p;

void main() {
    outColor = texture(uImage, vUV);
    if (p.uHealth < 0.0) return;

    // Screen-space HUD health bar: thin, long strip at the top center. Fixed
    // in framebuffer space (gl_FragCoord), independent of the camera/world.
    float barW = p.uW * 0.44f;
    float barH = p.uH * 0.013f;
    float x0 = (p.uW - barW) * 0.5f;
    float y0 = p.uH * 0.030f;
    float b = 3.0f;
    vec2 px = gl_FragCoord.xy;

    if (px.x < x0 - b || px.x > x0 + barW + b) return;
    if (px.y < y0 - b || px.y > y0 + barH + b) return;

    vec3 borderCol = vec3(0.05f);
    vec3 trackCol = vec3(0.10f, 0.13f, 0.09f);
    vec3 fillCol = vec3(0.20f, 0.55f, 0.16f);

    if (px.x < x0 || px.x > x0 + barW || px.y < y0 || px.y > y0 + barH) {
        outColor = vec4(borderCol, 1.0f);
        return;
    }
    float fx = (px.x - x0) / barW;
    outColor = vec4((fx <= p.uHealth) ? fillCol : trackCol, 1.0f);
}