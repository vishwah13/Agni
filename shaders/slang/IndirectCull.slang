//Reference: https://github.com/PanosK92/SpartanEngine/blob/master/data/shaders/indirect_cull.hlsl

// GPU frustum + Hi-Z occlusion culling with draw compaction
// Projects 8 AABB corners to clip space, tests frustum via clip planes,
// performs Hi-Z occlusion with mip refinement and 5-tap sampling,
// compacts surviving draws into contiguous output buffers via atomic append.
import InputStructures;

// Scene data (set 0, binding 0)
[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> sceneData;

// Hi-Z depth pyramid (set 0, binding 5)
[[vk::binding(5, 0)]]
Texture2D<float> hizTexture;

[[vk::binding(5, 0)]]
SamplerState hizSampler;

// VkDrawIndexedIndirectCommand layout (matches Vulkan spec)
struct DrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

// Push constants for cull + compaction
struct CullPushConstants {
    uint64_t boundsBufferPtr;       // BDA to BoundsData[] (input)
    uint64_t indirectBufferInPtr;   // BDA to DrawIndexedIndirectCommand[] (input)
    uint64_t indirectBufferOutPtr;  // BDA to DrawIndexedIndirectCommand[] (output)
    uint64_t drawDataInPtr;         // BDA to GPUDrawData[] (input)
    uint64_t drawDataOutPtr;        // BDA to GPUDrawData[] (output)
    uint64_t drawCountPtr;          // BDA to uint32_t atomic counter
    uint     drawCount;
    uint     hizEnabled;            // 0 = frustum only, 1 = frustum + Hi-Z
    uint     hizWidth;
    uint     hizHeight;
};

[[vk::push_constant]]
ConstantBuffer<CullPushConstants> pc;

[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 globalID : SV_DispatchThreadID)
{
    uint idx = globalID.x;
    if (idx >= pc.drawCount)
        return;

    // Load local-space AABB and world matrix
    BoundsData* bounds = (BoundsData*)pc.boundsBufferPtr;
    BoundsData b = bounds[idx];

    float3 aabbMin = b.aabbMin;
    float3 aabbMax = b.aabbMax;
    float4x4 mvp = mul(sceneData.viewproj, b.worldMatrix);

    // Project 8 AABB corners to clip space, accumulating:
    // - Frustum plane counters (clip-space half-space tests)
    // - NDC min/max for screen-space AABB
    // - Closest depth (max ndc.z in reverse-Z)
    float2 ndcMin      = float2(1e30f);
    float2 ndcMax      = float2(-1e30f);
    float  closestZ    = 0.0;  // farthest in reverse-Z (0 = far plane)
    bool   anyBehind   = false;
    bool   anyFront    = false;
    uint   outsideLeft = 0, outsideRight = 0;
    uint   outsideBot  = 0, outsideTop   = 0;
    uint   outsideNear = 0, outsideFar   = 0;

    [unroll]
    for (int i = 0; i < 8; i++)
    {
        float3 corner = float3(
            (i & 1) ? aabbMax.x : aabbMin.x,
            (i & 2) ? aabbMax.y : aabbMin.y,
            (i & 4) ? aabbMax.z : aabbMin.z
        );

        float4 clip = mul(mvp, float4(corner, 1.0));

        // Frustum plane tests (clip-space)
        if (clip.x < -clip.w) outsideLeft++;
        if (clip.x >  clip.w) outsideRight++;
        if (clip.y < -clip.w) outsideBot++;
        if (clip.y >  clip.w) outsideTop++;
        if (clip.z <  0.0)    outsideNear++;  // reverse-Z: near plane at z=1, but clip.z < 0 means behind
        if (clip.z >  clip.w) outsideFar++;

        if (clip.w <= 0.0)
        {
            anyBehind = true;
        }
        else
        {
            float3 ndc = clip.xyz / clip.w;
            ndcMin   = min(ndcMin, ndc.xy);
            ndcMax   = max(ndcMax, ndc.xy);
            closestZ = max(closestZ, ndc.z);  // reverse-Z: max = closest
            anyFront = true;
        }
    }

    // Entirely behind camera
    if (!anyFront)
        return;

    // Frustum cull: if ALL 8 corners are outside any single clip plane
    if (outsideLeft == 8 || outsideRight == 8 ||
        outsideBot  == 8 || outsideTop   == 8 ||
        outsideNear == 8 || outsideFar   == 8)
        return;

    // If any corner is behind camera, skip Hi-Z (straddles near plane)
    bool visible = true;
    if (!anyBehind && pc.hizEnabled != 0)
    {
        // Clamp NDC to screen bounds
        ndcMin = clamp(ndcMin, float2(-1.0), float2(1.0));
        ndcMax = clamp(ndcMax, float2(-1.0), float2(1.0));

        // NDC to UV [0, 1]
        float2 uvMin = ndcMin * 0.5 + 0.5;
        float2 uvMax = ndcMax * 0.5 + 0.5;

        // Pixel footprint
        float2 hizSize = float2(pc.hizWidth, pc.hizHeight);
        float2 sizePixels = (uvMax - uvMin) * hizSize;
        float  maxDim = max(sizePixels.x, sizePixels.y);

        // Mip selection
        float mip = maxDim > 0.0 ? ceil(log2(max(maxDim, 1.0))) : 0.0;
        uint totalMips = uint(floor(log2(float(max(pc.hizWidth, pc.hizHeight))))) + 1;
        mip = clamp(mip, 0.0, float(totalMips - 1));

        // Mip refinement: use finer mip if footprint is small enough (<=2x2 texels)
        if (mip > 0.0)
        {
            float finerMip = mip - 1.0;
            float2 finerScale = exp2(-finerMip);
            float2 a = floor(uvMin * finerScale * hizSize);
            float2 bv = ceil(uvMax * finerScale * hizSize);
            float2 dims = bv - a;
            if (dims.x <= 2.0 && dims.y <= 2.0)
                mip = finerMip;
        }

        // Texel inflation: expand rect by 1 texel at selected mip
        float2 mipTexel = exp2(mip) / hizSize;
        uvMin = saturate(uvMin - mipTexel);
        uvMax = saturate(uvMax + mipTexel);

        // 5-tap Hi-Z sampling (4 corners + center)
        float2 uvCenter = (uvMin + uvMax) * 0.5;
        float d0 = hizTexture.SampleLevel(hizSampler, uvMin, mip);
        float d1 = hizTexture.SampleLevel(hizSampler, float2(uvMax.x, uvMin.y), mip);
        float d2 = hizTexture.SampleLevel(hizSampler, float2(uvMin.x, uvMax.y), mip);
        float d3 = hizTexture.SampleLevel(hizSampler, uvMax, mip);
        float d4 = hizTexture.SampleLevel(hizSampler, uvCenter, mip);

        // Farthest depth in reverse-Z (min = farthest surface)
        float furthestHiZ = min(min(min(d0, d1), min(d2, d3)), d4);

        // Conservative occlusion: object occluded if its closest point
        // is farther than the farthest occluder surface (with bias)
        // Reverse-Z: smaller z = farther from camera
        const float bias = 0.005;
        visible = closestZ > furthestHiZ - bias;
    }

    // Compact surviving draw into output buffers
    if (visible)
    {
        uint* drawCount = (uint*)pc.drawCountPtr;
        uint slot;
        InterlockedAdd(drawCount[0], 1, slot);

        // Copy indirect command, remap firstInstance for draw data indexing
        DrawIndexedIndirectCommand* cmdsIn  = (DrawIndexedIndirectCommand*)pc.indirectBufferInPtr;
        DrawIndexedIndirectCommand* cmdsOut = (DrawIndexedIndirectCommand*)pc.indirectBufferOutPtr;
        cmdsOut[slot] = cmdsIn[idx];
        cmdsOut[slot].firstInstance = slot;

        // Copy draw data
        GPUDrawData* dataIn  = (GPUDrawData*)pc.drawDataInPtr;
        GPUDrawData* dataOut = (GPUDrawData*)pc.drawDataOutPtr;
        dataOut[slot] = dataIn[idx];
    }
}
