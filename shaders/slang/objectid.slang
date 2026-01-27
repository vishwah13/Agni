// Object ID shader for GPU-based picking
import input_structures;

// Scene data (set 0, binding 0)
[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> sceneData;

// Push constants with 64-bit entity ID for picking
struct ObjectIDPushConstants
{
    float4x4 worldMatrix;
    uint64_t vertexBufferPtr;
    uint64_t entityID;      // Full 64-bit Flecs entity ID
};

[[vk::push_constant]]
ConstantBuffer<ObjectIDPushConstants> pushConstants;

// Vertex shader output
struct VertexOutput
{
    float4 position : SV_Position;
};

// Helper function to load vertex from buffer device address
Vertex loadVertex(uint64_t bufferPtr, uint index)
{
    Vertex* vertices = (Vertex*)bufferPtr;
    return vertices[index];
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    Vertex v = loadVertex(pushConstants.vertexBufferPtr, vertexIndex);

    VertexOutput output;
    float4 position = float4(v.position, 1.0f);
    float4 worldPos = mul(pushConstants.worldMatrix, position);
    output.position = mul(sceneData.viewproj, worldPos);

    return output;
}

[shader("fragment")]
uint2 fragmentMain(VertexOutput input) : SV_Target0
{
    // Split 64-bit entity ID into two 32-bit values for R32G32_UINT format
    uint low = uint(pushConstants.entityID & 0xFFFFFFFF);
    uint high = uint(pushConstants.entityID >> 32);
    return uint2(low, high);
}
