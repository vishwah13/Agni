// Hi-Z pyramid downsample compute shader
// Pass 0 (isFirstMip=1): Copy depth resolve texture -> Hi-Z mip0 (1:1 copy)
// Pass 1..N (isFirstMip=0): Downsample Hi-Z mip N-1 -> mip N using MIN of 2x2 texels
// MIN reduction: in reverse-Z (near=1, far=0), min = farthest depth = conservative for occlusion.

struct HiZPushConstants {
    uint srcWidth;
    uint srcHeight;
    uint isFirstMip;  // 1 = read from depth texture (binding 2), 0 = read from storage (binding 0)
};

[[vk::push_constant]]
ConstantBuffer<HiZPushConstants> pc;

[[vk::binding(0, 0)]]
RWTexture2D<float> srcMip;    // source mip (used when isFirstMip=0)

[[vk::binding(1, 0)]]
RWTexture2D<float> dstMip;    // destination mip

[[vk::binding(2, 0)]]
Texture2D<float> depthSource; // resolved depth texture (used when isFirstMip=1)

[[vk::binding(2, 0)]]
SamplerState depthSampler;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 globalID : SV_DispatchThreadID)
{
    if (pc.isFirstMip != 0)
    {
        // First pass: 1:1 copy from depth resolve texture to Hi-Z mip0
        if (globalID.x >= pc.srcWidth || globalID.y >= pc.srcHeight)
            return;

        float depth = depthSource.Load(int3(globalID.xy, 0));
        dstMip[globalID.xy] = depth;
    }
    else
    {
        // Subsequent passes: MIN downsample 2x2 -> 1
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
}
