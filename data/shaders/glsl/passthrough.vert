#version 450

layout(location = 0) out vec2 fragUV;

void main() {
    // Generates a clip-space triangle covering the screen (-1 to 1)
    // Vertex 0: (-1, -1), Vertex 1: (3, -1), Vertex 2: (-1, 3)
    float x = -1.0 + float((gl_VertexIndex & 1) << 2);
    float y = -1.0 + float((gl_VertexIndex & 2) << 1);
    
    // Convert clip space (-1 to 1) to UV space (0 to 1)
    fragUV.x = (x + 1.0) * 0.5;
    fragUV.y = (1.0 - y) * 0.5; // Flips Y so image isn't upside down
    
    gl_Position = vec4(x, y, 0.0, 1.0);
}