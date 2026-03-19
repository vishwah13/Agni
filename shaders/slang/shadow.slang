// Shadow pass shader - depth-only rendering from light's perspective
// Uses indirect draw with BDA-based GPUDrawData lookup
import input_structures;

// Scene data (set 0, binding 0) - contains lightSpaceMatrix
[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> sceneData;

// Push constants for indirect shadow pass (just BDA to draw data SSBO)
struct ShadowPushConstants
{
    uint64_t drawDataBufferPtr;
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
float4 vertexMain(uint vertexIndex : SV_VertexID,
                  uint instanceID : SV_InstanceID,
                  uint baseInstance : SV_StartInstanceLocation) : SV_Position
{
    uint drawIndex = instanceID + baseInstance;

    GPUDrawData* drawDataArray = (GPUDrawData*)pushConstants.drawDataBufferPtr;
    GPUDrawData drawData = drawDataArray[drawIndex];

    Vertex v = loadVertex(drawData.vertexBufferPtr, vertexIndex);

    float4 worldPos = mul(drawData.worldMatrix, float4(v.position, 1.0f));
    return mul(sceneData.lightSpaceMatrix, worldPos);
}

// No fragment shader - depth-only pass
// Vulkan will automatically write depth from vertex shader output
