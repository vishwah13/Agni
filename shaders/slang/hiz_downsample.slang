// Hi-Z pyramid downsample compute shader
// Reads source mip level, writes MIN of 2x2 texels to destination mip level.
// MIN reduction: in reverse-Z (near=1, far=0), min = farthest depth = conservative for occlusion.

struct HiZPushConstants {
    uint srcWidth;
    uint srcHeight;
};

[[vk::push_constant]]
ConstantBuffer<HiZPushConstants> pc;

[[vk::binding(0, 0)]]
RWTexture2D<float> srcMip;

[[vk::binding(1, 0)]]
RWTexture2D<float> dstMip;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 globalID : SV_DispatchThreadID)
{
    uint dstWidth  = max(pc.srcWidth / 2, 1u);
    uint dstHeight = max(pc.srcHeight / 2, 1u);

    if (globalID.x >= dstWidth || globalID.y >= dstHeight)
        return;

    uint2 srcCoord = globalID.xy * 2;
    uint2 srcMax = uint2(pc.srcWidth - 1, pc.srcHeight - 1);

    float d00 = srcMip[srcCoord];
    float d10 = srcMip[min(srcCoord + uint2(1, 0), srcMax)];
    float d01 = srcMip[min(srcCoord + uint2(0, 1), srcMax)];
    float d11 = srcMip[min(srcCoord + uint2(1, 1), srcMax)];

    dstMip[globalID.xy] = min(min(d00, d10), min(d01, d11));
}
