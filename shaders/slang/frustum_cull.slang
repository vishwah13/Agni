// GPU frustum + Hi-Z occlusion culling compute shader
// Reads per-draw bounds via BDA, tests sphere-vs-6-planes,
// optionally tests against Hi-Z depth pyramid,
// writes instanceCount=0 for culled draws via BDA.
import input_structures;

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

// Push constants
struct CullPushConstants {
    uint64_t boundsBufferPtr;    // BDA to BoundsData[]
    uint64_t indirectBufferPtr;  // BDA to DrawIndexedIndirectCommand[]
    uint     drawCount;
    uint     hizEnabled;         // 0 = frustum only, 1 = frustum + Hi-Z
    uint     hizWidth;
    uint     hizHeight;
};

[[vk::push_constant]]
ConstantBuffer<CullPushConstants> pc;

// Sphere-vs-frustum test: returns true if the sphere is (at least partially) inside
bool sphereInsideFrustum(float3 center, float radius)
{
    for (int i = 0; i < 6; i++)
    {
        float4 plane = sceneData.frustumPlanes[i];
        float dist = dot(plane.xyz, center) + plane.w;
        if (dist < -radius)
            return false;
    }
    return true;
}

// Hi-Z occlusion test: returns true if the object might be visible
bool hizOcclusionTest(float3 worldCenter, float worldRadius)
{
    // Project bounding sphere center to clip space
    float4 clipCenter = mul(sceneData.viewproj, float4(worldCenter, 1.0));

    // Behind camera — let frustum cull handle it
    if (clipCenter.w <= 0.0)
        return true;

    // NDC (perspective divide)
    float3 ndc = clipCenter.xyz / clipCenter.w;

    // Project sphere radius to screen-space NDC
    float projScaleX = abs(sceneData.proj[0][0]);
    float projScaleY = abs(sceneData.proj[1][1]);
    float screenRadiusNDC = worldRadius * max(projScaleX, projScaleY) / clipCenter.w;

    // Screen-space AABB in NDC [-1, 1]
    float2 aabbMin = clamp(ndc.xy - float2(screenRadiusNDC), float2(-1.0), float2(1.0));
    float2 aabbMax = clamp(ndc.xy + float2(screenRadiusNDC), float2(-1.0), float2(1.0));

    // Convert NDC to UV [0, 1]
    float2 uvMin = aabbMin * 0.5 + 0.5;
    float2 uvMax = aabbMax * 0.5 + 0.5;

    // Screen-space AABB size in pixels
    float2 hizSize = float2(pc.hizWidth, pc.hizHeight);
    float2 aabbSizePixels = (uvMax - uvMin) * hizSize;

    // Pick mip level where one texel covers roughly the AABB
    float maxDim = max(aabbSizePixels.x, aabbSizePixels.y);
    float mipLevel = maxDim > 0.0 ? ceil(log2(maxDim)) : 0.0;
    uint totalMips = uint(floor(log2(float(max(pc.hizWidth, pc.hizHeight))))) + 1;
    mipLevel = clamp(mipLevel, 0.0, float(totalMips - 1));

    // Sample Hi-Z at 4 corners of the AABB
    float d0 = hizTexture.SampleLevel(hizSampler, uvMin, mipLevel);
    float d1 = hizTexture.SampleLevel(hizSampler, float2(uvMax.x, uvMin.y), mipLevel);
    float d2 = hizTexture.SampleLevel(hizSampler, float2(uvMin.x, uvMax.y), mipLevel);
    float d3 = hizTexture.SampleLevel(hizSampler, uvMax, mipLevel);

    // Conservative Hi-Z depth = min of 4 samples (farthest surface in reverse-Z)
    float hizDepth = min(min(d0, d1), min(d2, d3));

    // Object center depth in NDC
    // Reverse-Z: object occluded if objDepth < hizDepth (farther than farthest surface)
    if (ndc.z < hizDepth)
        return false; // Occluded

    return true;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 globalID : SV_DispatchThreadID)
{
    uint idx = globalID.x;
    if (idx >= pc.drawCount)
        return;

    // Load bounds via BDA
    BoundsData* bounds = (BoundsData*)pc.boundsBufferPtr;
    BoundsData b = bounds[idx];

    // Transform bounding sphere center to world space
    float4 worldCenter = mul(b.worldMatrix, float4(b.origin, 1.0));

    // Scale radius by max axis scale of world matrix
    float3 scale = float3(
        length(b.worldMatrix[0].xyz),
        length(b.worldMatrix[1].xyz),
        length(b.worldMatrix[2].xyz)
    );
    float worldRadius = b.sphereRadius * max(scale.x, max(scale.y, scale.z));

    // Frustum test (always performed)
    bool visible = sphereInsideFrustum(worldCenter.xyz, worldRadius);

    // Hi-Z occlusion test (only if passed frustum and Hi-Z is enabled)
    if (visible && pc.hizEnabled != 0)
        visible = hizOcclusionTest(worldCenter.xyz, worldRadius);

    // Write instanceCount via BDA
    DrawIndexedIndirectCommand* cmds = (DrawIndexedIndirectCommand*)pc.indirectBufferPtr;
    cmds[idx].instanceCount = visible ? 1u : 0u;
}
