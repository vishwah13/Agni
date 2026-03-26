// Skybox shader
import InputStructures;

// Scene data (set 0, binding 0)
[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> sceneData;

// Cubemap sampler for skybox (set 1, binding 0)
[[vk::binding(0, 1)]]
SamplerCube skyboxTexture;

// Push constants for skybox (only vertex buffer address needed)
struct SkyboxPushConstants
{
    uint64_t vertexBufferPtr; // Buffer device address as raw uint64
};

[[vk::push_constant]]
ConstantBuffer<SkyboxPushConstants> pushConstants;

// Vertex shader output / Fragment shader input
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
    // Sample the cubemap using the direction vector
    return skyboxTexture.Sample(input.texCoord);
}
