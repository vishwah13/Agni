// Shared input structures for Agni shaders

// Scene data uniform buffer (set 0, binding 0)
struct SceneData
{
    float4x4 view;
    float4x4 proj;
    float4x4 viewproj;
    float4 ambientColor;
    float4 sunlightDirection; // w for sun power
    float4 sunlightColor;
    float3 camPos; // camera position for view vector
};

// Material data uniform buffer (set 1, binding 0)
struct GLTFMaterialData
{
    float4 colorFactors;
    float4 metal_rough_factors;
};

// Vertex structure matching CPU-side layout (64 bytes total)
struct Vertex
{
    float3 position;    // 12 bytes
    float uv_x;         // 4 bytes
    float3 normal;      // 12 bytes
    float uv_y;         // 4 bytes
    float4 color;       // 16 bytes
    float4 tangent;     // 16 bytes
};

// Point light structure (matches C++ GPUPointLight)
struct PointLight
{
    float3 position;
    float  radius;
    float3 color;
    float  intensity;
};

// Light data buffer structure (matches C++ GPULightData)
struct LightData
{
    uint numPointLights;
    uint padding0;
    uint padding1;
    uint padding2;
    PointLight pointLights[256];  // MAX_POINT_LIGHTS
};
