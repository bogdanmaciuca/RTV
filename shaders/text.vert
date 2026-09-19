#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 outUV;

layout(std140, set = 1, binding = 0) uniform PushConstants {
    mat4 projection;
} pc;

void main() {
    outUV = inUV;
    gl_Position = pc.projection * vec4(inPos, 0.0, 1.0);
}
