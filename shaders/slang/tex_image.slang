// Simple texture display shader

// Texture to display (set 0, binding 0)
[[vk::binding(0, 0)]]
Sampler2D displayTexture;

// Fragment shader input
struct FragmentInput
{
    float4 position : SV_Position;
    float3 color : COLOR;
    float2 uv : TEXCOORD0;
};

[shader("fragment")]
float4 fragmentMain(FragmentInput input) : SV_Target
{
    return displayTexture.Sample(input.uv);
}
