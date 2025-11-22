#version 450

#extension GL_EXT_buffer_reference : require

layout(set = 0, binding = 0) uniform  SceneData{
	mat4 view;
	mat4 proj;
	mat4 viewproj;
	vec4 ambientColor;
	vec4 sunlightDirection;
	vec4 sunlightColor;
} sceneData;

layout (location = 0) out vec3 outTexCoord;

struct Vertex {
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
    vec4 tangent;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer{
    Vertex vertices[];
};

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

    // Ensure skybox is always at far plane (reversed-Z: far=0.0, near=1.0)
    gl_Position = vec4(clipPos.xy, 0.0, clipPos.w);

    // Use position as texture coordinate direction
    outTexCoord = v.position;
}
