// Debug line rendering — fully self-contained via push constants only.
// No descriptor sets — avoids descriptor buffer EXT tracking issues on shutdown.

struct DebugLineVertex
{
    float3 position;
    uint   packedColor;
};

struct DebugLinePushConstants
{
    float4x4 viewproj;
    uint64_t vertexBufferPtr;
};

[[vk::push_constant]]
ConstantBuffer<DebugLinePushConstants> pc;

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
    output.position = mul(pc.viewproj, float4(v.position, 1.0));

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
