// Fallback mesh shader - outputs bright pink to indicate missing shader
import input_structures;

// Scene data (set 0, binding 0)
[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> sceneData;

// Light data SSBO (set 0, binding 1) - needed for compatibility
[[vk::binding(1, 0)]]
StructuredBuffer<LightData> lightData;

// Material data (set 1, binding 0)
[[vk::binding(0, 1)]]
ConstantBuffer<GLTFMaterialData> materialData;

// Material textures (set 1) - needed for compatibility
[[vk::binding(1, 1)]]
Sampler2D colorTex;

[[vk::binding(2, 1)]]
Sampler2D metalRoughTex;

[[vk::binding(3, 1)]]
Sampler2D normalTex;

[[vk::binding(4, 1)]]
Sampler2D aoTex;

// Push constants matching C++ GPUDrawPushConstants
struct PushConstants
{
    float4x4 worldMatrix;
    uint64_t vertexBufferPtr;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPos : TEXCOORD0;
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
    output.worldPos = worldPos.xyz;

    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    // Bright pink/magenta color to indicate fallback shader is being used
    // Add slight variation based on world position for visual interest
    float checker = frac(input.worldPos.x * 0.5 + input.worldPos.y * 0.5 + input.worldPos.z * 0.5);
    float intensity = lerp(0.8, 1.0, step(0.5, checker));

    return float4(1.0 * intensity, 0.0, 1.0 * intensity, 1.0);
}
