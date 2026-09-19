#version 450

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D fontAtlas;

const vec4 TEXT_COLOR = vec4(1.0, 1.0, 1.0, 1.0);

void main() {
    float alpha = texture(fontAtlas, inUV).r;
    outColor = vec4(TEXT_COLOR.rgb, alpha);
}
