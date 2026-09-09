#version 450 core

layout(location = 0) in vec3 aPosition;

layout(location = 0) out vec2 vUV;

layout(std140, set = 0, binding = 0) uniform UBO {
    mat4 projection;
    mat4 view;
    mat4 model;
    vec4 uPointLight[8];
} ubo;

void main() {
    gl_Position = ubo.projection * ubo.view * ubo.model * vec4(aPosition, 1.0);
    vUV = aPosition.xy * 0.5 + 0.5;
}
