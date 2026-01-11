// Object ID shader for GPU-based picking
import input_structures;

// Scene data (set 0, binding 0)
[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> sceneData;

// Push constants with entity ID for picking
struct ObjectIDPushConstants
{
    float4x4 worldMatrix;
    uint64_t vertexBufferPtr;
    uint     entityID;
    uint     padding;
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
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    // Encode entity ID as RGBA color (supports up to 4 billion unique IDs)
    uint id = pushConstants.entityID;
    float4 color;
    color.r = float((id >> 24) & 0xFF) / 255.0;
    color.g = float((id >> 16) & 0xFF) / 255.0;
    color.b = float((id >> 8) & 0xFF) / 255.0;
    color.a = float(id & 0xFF) / 255.0;
    return color;
}
