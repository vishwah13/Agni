// Debug line rendering — position + packed RGBA color per vertex
import InputStructures;

struct DebugLineVertex
{
    float3 position;
    uint   packedColor;
};

struct DebugLinePushConstants
{
    uint64_t vertexBufferPtr;
};

[[vk::push_constant]]
ConstantBuffer<DebugLinePushConstants> pc;

// Scene data (set 0, binding 0) — reuses existing layout
[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> sceneData;

struct VSOut
{
    float4 position : SV_Position;
    float4 color    : COLOR0;
};

[shader("vertex")]
VSOut vertexMain(uint vertexIndex : SV_VertexID)
{
    DebugLineVertex* verts = (DebugLineVertex*)pc.vertexBufferPtr;
    DebugLineVertex v = verts[vertexIndex];

    VSOut output;
    output.position = mul(sceneData.viewproj, float4(v.position, 1.0));

    // Unpack RGBA from uint32 (little-endian: R in low byte)
    output.color = float4(
        float((v.packedColor >>  0) & 0xFF) / 255.0,
        float((v.packedColor >>  8) & 0xFF) / 255.0,
        float((v.packedColor >> 16) & 0xFF) / 255.0,
        float((v.packedColor >> 24) & 0xFF) / 255.0);

    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOut input) : SV_Target0
{
    return input.color;
}
