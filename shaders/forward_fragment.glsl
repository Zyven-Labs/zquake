#version 450 core

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vLightmapUV;
layout(location = 2) in vec3 vNormal;

layout(location = 0) out vec4 fragColor;

layout(binding = 1) uniform sampler2D uTexture;
layout(binding = 2) uniform sampler2D uLightmap;

void main() {
    vec4 texColor = texture(uTexture, vUV);
    vec4 lightmap = texture(uLightmap, vLightmapUV.xy);
    fragColor = texColor * lightmap;
}
