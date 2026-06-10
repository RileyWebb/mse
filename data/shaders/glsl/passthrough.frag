#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// SDL3 GPU maps uniform samplers like this for SPIR-V
layout(set = 0, binding = 0) uniform sampler2D surfaceTexture;

void main() {
    outColor = texture(surfaceTexture, fragUV);
}