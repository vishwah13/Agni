// Point shadow pass shader - outputs linear depth (distance from light)
// Used for omnidirectional shadow mapping with cube maps
import input_structures;

// Push constants for point shadow pass
// Must match C++ struct layout (160 bytes total)
struct PointShadowPushConstants
{
    float4x4 worldMatrix;      // 64 bytes, offset 0
    uint64_t vertexBufferPtr;  // 8 bytes, offset 64
    uint64_t padding;          // 8 bytes padding for alignment
    float4x4 lightViewProj;    // 64 bytes, offset 80
    float3   lightPos;         // 12 bytes, offset 144
    float    farPlane;         // 4 bytes, offset 156
};                             // Total: 160 bytes

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
VSOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    Vertex v = loadVertex(pushConstants.vertexBufferPtr, vertexIndex);

    float4 worldPos = mul(pushConstants.worldMatrix, float4(v.position, 1.0f));

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
