#version 450

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "input_structures.glsl"

layout (location = 0) out vec3 outTexCoord;

struct Vertex {
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer{
    Vertex vertices[];
};

// Push constants block
layout(push_constant) uniform constants
{
    VertexBuffer vertexBuffer;
} PushConstants;

void main()
{
    Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];

    // Remove translation from view matrix (keep only rotation)
    mat4 viewNoTranslation = mat4(mat3(sceneData.view));

    // Transform position with projection and rotation-only view
    vec4 clipPos = sceneData.proj * viewNoTranslation * vec4(v.position, 1.0);

    // Ensure skybox is always at far plane
    gl_Position = clipPos.xyww;

    // Use position as texture coordinate direction
    outTexCoord = v.position;
}
