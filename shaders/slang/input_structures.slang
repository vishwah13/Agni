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

// Legacy material data uniform buffer (unused with bindless)
struct GLTFMaterialData
{
    float4 colorFactors;
    float4 metal_rough_factors;
};

// Bindless material data (stored in SSBO, indexed by materialIndex)
// Must match C++ GPUMaterialData struct (std430 layout)
struct GPUMaterialData
{
    float4 colorFactors;
    float4 metalRoughFactors;
    uint   colorTexIndex;
    uint   metalRoughTexIndex;
    uint   normalTexIndex;
    uint   aoTexIndex;
    uint   samplerIndex;
    uint   padding0;
    uint   padding1;
    uint   padding2;
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

// Spot light structure (matches C++ GPUSpotLight)
struct SpotLight
{
    float3 position;
    float  radius;
    float3 direction;
    float  innerCutoff;    // cos(innerConeAngle)
    float3 color;
    float  outerCutoff;    // cos(outerConeAngle)
    float  intensity;
    float  padding0;
    float  padding1;
    float  padding2;
};

// Light data buffer structure (matches C++ GPULightData)
struct LightData
{
    uint numPointLights;
    uint numSpotLights;
    uint padding0;
    uint padding1;
    PointLight pointLights[256];  // MAX_POINT_LIGHTS
    SpotLight  spotLights[64];    // MAX_SPOT_LIGHTS
};
