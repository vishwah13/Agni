// Point shadow pass shader - outputs linear depth (distance from light)
// Used for omnidirectional shadow mapping with cube maps
// Uses indirect draw with BDA-based GPUDrawData lookup
import input_structures;

// Push constants for indirect point shadow pass
// Per-draw data (worldMatrix, vertexBuffer) moved to GPUDrawData SSBO
// Per-face data (lightViewProj, lightPos, farPlane) stays in push constants
struct PointShadowPushConstants
{
    uint64_t drawDataBufferPtr;  // 8 bytes, offset 0
    uint64_t padding;            // 8 bytes, align lightViewProj to 16
    float4x4 lightViewProj;     // 64 bytes, offset 16
    float3   lightPos;          // 12 bytes, offset 80
    float    farPlane;          // 4 bytes, offset 92
};  // 96 bytes total

[[vk::push_constant]]
ConstantBuffer<PointShadowPushConstants> pushConstants;

// Helper function to load vertex from buffer device address
Vertex loadVertex(uint64_t bufferPtr, uint index)
{
    Vertex* vertices = (Vertex*)bufferPtr;
    return vertices[index];
}

struct VSOutput
{
    float4 position : SV_Position;
    float3 worldPos : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID,
                    uint instanceID : SV_InstanceID,
                    uint baseInstance : SV_StartInstanceLocation)
{
    uint drawIndex = instanceID + baseInstance;

    GPUDrawData* drawDataArray = (GPUDrawData*)pushConstants.drawDataBufferPtr;
    GPUDrawData drawData = drawDataArray[drawIndex];

    Vertex v = loadVertex(drawData.vertexBufferPtr, vertexIndex);

    float4 worldPos = mul(drawData.worldMatrix, float4(v.position, 1.0f));

    VSOutput output;
    output.position = mul(pushConstants.lightViewProj, worldPos);
    output.worldPos = worldPos.xyz;
    return output;
}

// Fragment shader outputs linear depth (distance from light)
// Reverse-Z: depth 0 = far plane, depth 1 = near plane
[shader("fragment")]
float fragmentMain(VSOutput input) : SV_Depth
{
    // Calculate distance from light to fragment
    float dist = length(input.worldPos - pushConstants.lightPos);

    // Normalize to [0,1] range based on far plane
    // Reverse-Z: closer objects have higher depth values (closer to 1)
    // dist=0 -> depth=1 (near), dist=farPlane -> depth=0 (far)
    return 1.0 - saturate(dist / pushConstants.farPlane);
}
