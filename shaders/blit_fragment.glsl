#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uImage;

void main() {
    outColor = texture(uImage, vUV);
}