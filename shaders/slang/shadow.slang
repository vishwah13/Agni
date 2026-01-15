// Shadow pass shader - depth-only rendering from light's perspective
import input_structures;

// Scene data (set 0, binding 0) - contains lightSpaceMatrix
[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> sceneData;

// Push constants for shadow pass
struct ShadowPushConstants
{
    float4x4 worldMatrix;
    uint64_t vertexBufferPtr;
};

[[vk::push_constant]]
ConstantBuffer<ShadowPushConstants> pushConstants;

// Helper function to load vertex from buffer device address
Vertex loadVertex(uint64_t bufferPtr, uint index)
{
    Vertex* vertices = (Vertex*)bufferPtr;
    return vertices[index];
}

[shader("vertex")]
float4 vertexMain(uint vertexIndex : SV_VertexID) : SV_Position
{
    Vertex v = loadVertex(pushConstants.vertexBufferPtr, vertexIndex);

    float4 worldPos = mul(pushConstants.worldMatrix, float4(v.position, 1.0f));
    return mul(sceneData.lightSpaceMatrix, worldPos);
}

// No fragment shader - depth-only pass
// Vulkan will automatically write depth from vertex shader output
