// Gradient compute shader with push constants

[[vk::binding(0, 0)]]
RWTexture2D<float4> image;

// Push constants block
struct ComputePushConstants
{
    float4 data1;
    float4 data2;
    float4 data3;
    float4 data4;
};

[[vk::push_constant]]
ComputePushConstants pushConstants;

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 globalID : SV_DispatchThreadID)
{
    int2 texelCoord = int2(globalID.xy);
    int2 size;
    image.GetDimensions(size.x, size.y);

    float4 topColor = pushConstants.data1;
    float4 bottomColor = pushConstants.data2;

    if (texelCoord.x < size.x && texelCoord.y < size.y)
    {
        float blend = float(texelCoord.y) / float(size.y);

        image[texelCoord] = lerp(topColor, bottomColor, blend);
    }
}
