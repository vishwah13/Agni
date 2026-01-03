// Fallback skybox shader - outputs blue gradient to indicate missing shader
import input_structures;

// Scene data (set 0, binding 0)
[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> sceneData;

// Cubemap sampler for skybox (set 1, binding 0) - needed for compatibility
[[vk::binding(0, 1)]]
SamplerCube skyboxTexture;

// Push constants for skybox
struct SkyboxPushConstants
{
    uint64_t vertexBufferPtr;
};

[[vk::push_constant]]
ConstantBuffer<SkyboxPushConstants> pushConstants;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 texCoord : TEXCOORD0;
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

    // Remove translation from view matrix (keep only rotation)
    float4x4 viewNoTranslation = float4x4(
        float4(sceneData.view[0].xyz, 0.0),
        float4(sceneData.view[1].xyz, 0.0),
        float4(sceneData.view[2].xyz, 0.0),
        float4(0.0, 0.0, 0.0, 1.0)
    );

    // Transform position with projection and rotation-only view
    float4 clipPos = mul(sceneData.proj, mul(viewNoTranslation, float4(v.position, 1.0)));

    // Ensure skybox is always at far plane (reversed-Z: far=0.0, near=1.0)
    output.position = float4(clipPos.xy, 0.0, clipPos.w);

    // Use position as texture coordinate direction
    output.texCoord = v.position;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    // Blue gradient based on view direction to indicate fallback shader
    // Creates a nice sky-like gradient from dark blue at bottom to light blue at top
    float3 dir = normalize(input.texCoord);

    // Vertical gradient based on Y component
    float t = dir.y * 0.5 + 0.5; // Map from [-1,1] to [0,1]

    // Dark blue at bottom, bright cyan-blue at top
    float3 bottomColor = float3(0.1, 0.2, 0.4);  // Dark blue
    float3 topColor = float3(0.3, 0.6, 1.0);     // Bright sky blue

    float3 color = lerp(bottomColor, topColor, t);

    return float4(color, 1.0);
}
