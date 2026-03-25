// GPU frustum culling compute shader
// Reads per-draw bounds via BDA, tests sphere-vs-6-planes,
// writes instanceCount=0 for culled draws via BDA.
import input_structures;

// Scene data (set 0, binding 0) — only frustumPlanes used
[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> sceneData;

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
    uint     padding;
};

[[vk::push_constant]]
ConstantBuffer<CullPushConstants> pc;

// Sphere-vs-frustum test: returns true if the sphere is (at least partially) inside
bool sphereInsideFrustum(float3 center, float radius)
{
    for (int i = 0; i < 6; i++)
    {
        float4 plane = sceneData.frustumPlanes[i];
        // signed distance from center to plane
        float dist = dot(plane.xyz, center) + plane.w;
        if (dist < -radius)
            return false; // entirely outside this plane
    }
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

    // Frustum test
    bool visible = sphereInsideFrustum(worldCenter.xyz, worldRadius);

    // Write instanceCount via BDA (only this field, leave everything else untouched)
    DrawIndexedIndirectCommand* cmds = (DrawIndexedIndirectCommand*)pc.indirectBufferPtr;
    cmds[idx].instanceCount = visible ? 1u : 0u;
}
