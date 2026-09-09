#version 450 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;
layout(location = 3) in vec3 aLightmapUV;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vLightmapUV;
layout(location = 2) out vec3 vNormal;

layout(std140, set = 0, binding = 0) uniform UBO {
    mat4 projection;
    mat4 view;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

void main() {
    gl_Position = ubo.projection * ubo.view * pc.model * vec4(aPosition, 1.0);
    vUV = aUV;
    vLightmapUV = aLightmapUV;
    vNormal = mat3(pc.model) * aNormal;
}
