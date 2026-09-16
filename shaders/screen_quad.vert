#version 450

layout (location = 0) out vec2 outUV;

void main() {
    // Generates UV coordinates:
    // Index 0: (0.0, 0.0)
    // Index 1: (2.0, 0.0)
    // Index 2: (0.0, 2.0)
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);

    // Maps UV [0, 2] to Normalized Device Coordinates (NDC) [-1, 3]
    // Vulkan NDC: top-left is (-1, -1), bottom-right is (1, 1), Z is [0, 1]
    gl_Position = vec4(outUV * 2.0f - 1.0f, 0.0f, 1.0f);
}
