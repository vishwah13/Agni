// Mesh shader for PBR rendering with bindless textures
import input_structures;

// Scene data (set 0, binding 0)
[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> sceneData;

// Light data SSBO (set 0, binding 1)
[[vk::binding(1, 0)]]
StructuredBuffer<LightData> lightData;

// Directional light shadow map (set 0, binding 2)
[[vk::binding(2, 0)]]
Texture2D<float> shadowMap;

[[vk::binding(2, 0)]]
SamplerComparisonState shadowSampler;

// Spot light shadow map (set 0, binding 3)
[[vk::binding(3, 0)]]
Texture2D<float> spotShadowMap;

[[vk::binding(3, 0)]]
SamplerComparisonState spotShadowSampler;

// Point light shadow cube map (set 0, binding 4)
[[vk::binding(4, 0)]]
TextureCube<float> pointShadowCubeMap;

[[vk::binding(4, 0)]]
SamplerComparisonState pointShadowSampler;

// Bindless texture array (set 1, binding 0)
[[vk::binding(0, 1)]]
Texture2D textures[];

// Sampler array (set 2, binding 0)
[[vk::binding(0, 2)]]
SamplerState samplers[4];

// Material data SSBO (set 3, binding 0)
[[vk::binding(0, 3)]]
StructuredBuffer<GPUMaterialData> materials;

// Push constants matching C++ GPUDrawPushConstants
struct PushConstants
{
    float4x4 worldMatrix;
    uint64_t vertexBufferPtr; // Buffer device address as raw uint64
    uint     materialIndex;   // Index into materials array
    uint     padding;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

// Vertex shader output / Fragment shader input
struct VertexOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float3 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float3 tangent : TEXCOORD2;
    float3 bitangent : TEXCOORD3;
    float4 lightSpacePos : TEXCOORD4;      // Directional light space position
    float4 spotLightSpacePos : TEXCOORD5;  // Spot light space position
};

// Helper function to load vertex from buffer device address
Vertex loadVertex(uint64_t bufferPtr, uint index)
{
    Vertex* vertices = (Vertex*)bufferPtr;
    return vertices[index];
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    Vertex v = loadVertex(pushConstants.vertexBufferPtr, vertexIndex);

    // Get material data for color factors
    GPUMaterialData mat = materials[pushConstants.materialIndex];

    VertexOutput output;

    float4 position = float4(v.position, 1.0f);
    float4 worldPos = mul(pushConstants.worldMatrix, position);

    output.position = mul(sceneData.viewproj, worldPos);
    output.worldPos = worldPos.xyz;
    output.normal = normalize(mul((float3x3)pushConstants.worldMatrix, v.normal));
    output.color = v.color.xyz * mat.colorFactors.xyz;
    output.uv = float2(v.uv_x, v.uv_y);

    // Transform tangent to world space
    float3 T = normalize(mul((float3x3)pushConstants.worldMatrix, v.tangent.xyz));
    // Re-orthogonalize T with respect to N using Gram-Schmidt process
    T = normalize(T - dot(T, output.normal) * output.normal);
    // Calculate bitangent using handedness (w component)
    float3 B = cross(output.normal, T) * v.tangent.w;

    output.tangent = T;
    output.bitangent = B;

    // Calculate light space positions for shadow mapping
    output.lightSpacePos = mul(sceneData.lightSpaceMatrix, worldPos);         // Directional
    output.spotLightSpacePos = mul(sceneData.spotLightSpaceMatrix, worldPos); // Spot

    return output;
}

// PBR Constants
static const float PI = 3.14159265359;

// Normal Distribution Function - GGX/Trowbridge-Reitz
float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

// Geometry Function - Schlick-GGX
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

// Smith's method - combines view and light directions
float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Fresnel-Schlick approximation
float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Point light attenuation (smooth falloff within radius)
float calculateAttenuation(float distance, float radius)
{
    // Smooth quadratic falloff with radius cutoff
    float ratio = distance / radius;
    float attenuation = saturate(1.0 - ratio * ratio);
    return attenuation * attenuation;
}

// Shadow calculation using PCF (Percentage Closer Filtering) for soft shadows - Directional light
float calculateShadow(float4 lightSpacePos, float3 N, float3 L)
{
    // Check if shadows are enabled
    if (sceneData.shadowParams.w < 0.5)
        return 1.0;

    // Perspective divide (for orthographic this is effectively a no-op)
    float3 projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Transform to [0,1] UV coordinates
    float2 shadowUV = projCoords.xy * 0.5 + 0.5;

    // Check if outside shadow map bounds
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
        shadowUV.y < 0.0 || shadowUV.y > 1.0)
        return 1.0;

    float currentDepth = projCoords.z;

    // Apply bias based on surface angle to reduce shadow acne
    float bias = sceneData.shadowParams.x;
    float normalBias = sceneData.shadowParams.y;
    float NdotL = max(dot(N, L), 0.0);
    float slopeBias = bias * (1.0 - NdotL);

    // Reverse-Z: add bias (toward 1.0 = nearer)
    float biasedDepth = currentDepth + slopeBias + normalBias;

    // PCF filtering - sample 3x3 grid for soft shadows
    float shadow = 0.0;
    float texelSize = sceneData.shadowParams.z;  // 1.0 / SHADOW_MAP_RESOLUTION

    // 3x3 PCF kernel
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x, y) * texelSize;
            shadow += shadowMap.SampleCmpLevelZero(shadowSampler, shadowUV + offset, biasedDepth);
        }
    }

    return shadow / 9.0;  // Average of 9 samples
}

// Spot light shadow calculation using PCF
float calculateSpotShadow(float4 spotLightSpacePos, float3 N, float3 L)
{
    // Check if spot shadows are enabled
    if (sceneData.spotShadowParams.w < 0.5)
        return 1.0;

    // Perspective divide (important for perspective projection)
    float3 projCoords = spotLightSpacePos.xyz / spotLightSpacePos.w;

    // Transform to [0,1] UV coordinates
    float2 shadowUV = projCoords.xy * 0.5 + 0.5;

    // Check if outside shadow map bounds or behind light
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
        shadowUV.y < 0.0 || shadowUV.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0)
        return 1.0;

    float currentDepth = projCoords.z;

    // Apply bias
    float bias = sceneData.spotShadowParams.x;
    float normalBias = sceneData.spotShadowParams.y;
    float NdotL = max(dot(N, L), 0.0);
    float slopeBias = bias * (1.0 - NdotL);
    float biasedDepth = currentDepth + slopeBias + normalBias;

    // PCF filtering - sample 3x3 grid
    float shadow = 0.0;
    float texelSize = 1.0 / 2048.0;  // Shadow map resolution

    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x, y) * texelSize;
            shadow += spotShadowMap.SampleCmpLevelZero(spotShadowSampler, shadowUV + offset, biasedDepth);
        }
    }

    return shadow / 9.0;
}

// Point light shadow calculation (omnidirectional cube map with PCF)
float calculatePointShadow(float3 worldPos, float3 lightPos, float3 N)
{
    // Check if point shadows are enabled (w > 0 means enabled, w-1 is the light index)
    if (sceneData.pointShadowParams.w < 0.5)
        return 1.0;

    // Direction from light to fragment (used for cube map sampling)
    float3 lightToFrag = worldPos - lightPos;
    float currentDistance = length(lightToFrag);

    // Normalize distance for comparison
    // Reverse-Z: depth 0 = far, depth 1 = near
    // closer objects have higher depth values
    float farPlane = sceneData.pointShadowParams.z;
    float normalizedDepth = 1.0 - saturate(currentDistance / farPlane);

    // Apply bias - in reverse-Z, we ADD bias to move toward near (1.0)
    // This makes the comparison more lenient for the fragment
    float bias = sceneData.pointShadowParams.x;
    float biasedDepth = normalizedDepth + bias;

    // Sample direction - direct world-space vector from light to fragment
    // Rotation-based view matrices work correctly with direct sampling
    float3 sampleDir = normalize(lightToFrag);

    // Get PCF radius from scene data (y component)
    float pcfRadius = sceneData.pointShadowParams.y;

    // Hard shadows (single sample) if PCF disabled (radius <= 0)
    if (pcfRadius <= 0.0)
    {
        return pointShadowCubeMap.SampleCmpLevelZero(pointShadowSampler, sampleDir, biasedDepth);
    }

    // PCF soft shadows - sample with disk-based pattern
    // Disk radius scales with distance for perspective-correct softness
    float diskRadius = pcfRadius * (1.0 + currentDistance * 0.01);

    // 20-sample Poisson disk pattern for soft shadows
    float3 sampleOffsets[20] = {
        float3(1, 1, 1), float3(1, -1, 1), float3(-1, -1, 1), float3(-1, 1, 1),
        float3(1, 1, -1), float3(1, -1, -1), float3(-1, -1, -1), float3(-1, 1, -1),
        float3(1, 1, 0), float3(1, -1, 0), float3(-1, -1, 0), float3(-1, 1, 0),
        float3(1, 0, 1), float3(-1, 0, 1), float3(1, 0, -1), float3(-1, 0, -1),
        float3(0, 1, 1), float3(0, -1, 1), float3(0, -1, -1), float3(0, 1, -1)
    };

    float shadow = 0.0;
    for (int i = 0; i < 20; ++i)
    {
        float3 offset = sampleOffsets[i] * diskRadius;
        float3 sampleDirection = normalize(sampleDir + offset);
        shadow += pointShadowCubeMap.SampleCmpLevelZero(pointShadowSampler, sampleDirection, biasedDepth);
    }

    return shadow / 20.0;  // Average of 20 samples
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    // Get material data using materialIndex
    GPUMaterialData mat = materials[pushConstants.materialIndex];

    // Get sampler for this material
    SamplerState matSampler = samplers[mat.samplerIndex];

    // Sample textures using bindless arrays with NonUniformResourceIndex for dynamic indexing
    float3 albedo = pow(textures[NonUniformResourceIndex(mat.colorTexIndex)].Sample(matSampler, input.uv).rgb, float3(2.2)); // sRGB to linear
    albedo *= input.color;

    float2 metallicRoughness = textures[NonUniformResourceIndex(mat.metalRoughTexIndex)].Sample(matSampler, input.uv).bg; // B=metallic, G=roughness in glTF
    float metallic = metallicRoughness.x * mat.metalRoughFactors.x;
    float roughness = metallicRoughness.y * mat.metalRoughFactors.y;

    float ao = textures[NonUniformResourceIndex(mat.aoTexIndex)].Sample(matSampler, input.uv).r;

    // Normal mapping
    // Construct TBN matrix to transform from tangent space to world space
    float3 T = normalize(input.tangent);
    float3 B = normalize(input.bitangent);
    float3 N = normalize(input.normal);
    float3x3 TBN = float3x3(T, B, N);

    // Sample normal from normal map (stored in tangent space)
    float3 normalMap = textures[NonUniformResourceIndex(mat.normalTexIndex)].Sample(matSampler, input.uv).xyz;
    // Transform from [0,1] range to [-1,1] range
    normalMap = normalMap * 2.0 - 1.0;
    // Transform normal from tangent space to world space
    N = normalize(mul(normalMap, TBN));

    float3 V = normalize(sceneData.camPos - input.worldPos);

    // Calculate F0 (base reflectivity)
    float3 F0 = float3(0.04);
    F0 = lerp(F0, albedo, metallic);

    // Reflectance equation
    float3 Lo = float3(0.0);

    // Directional light calculation (negate: emission direction -> to-light direction)
    float3 L = normalize(-sceneData.sunlightDirection.xyz);
    float3 H = normalize(V + L);
    float3 radiance = sceneData.sunlightColor.rgb * sceneData.sunlightColor.w;

    // Calculate shadow factor
    float shadow = calculateShadow(input.lightSpacePos, N, L);

    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001; // prevent divide by zero
    float3 specular = numerator / denominator;

    // Energy conservation
    float3 kS = F; // specular contribution
    float3 kD = float3(1.0) - kS; // diffuse contribution
    kD *= 1.0 - metallic; // metals have no diffuse

    float NdotL = max(dot(N, L), 0.0);
    Lo += (kD * albedo / PI + specular) * radiance * NdotL * shadow;  // Apply shadow

    // Point lights
    uint numPointLights = lightData[0].numPointLights;
    for (uint i = 0; i < numPointLights; ++i)
    {
        PointLight light = lightData[0].pointLights[i];

        // Calculate light direction and distance
        float3 lightVec = light.position - input.worldPos;
        float distance = length(lightVec);
        float3 pL = normalize(lightVec);
        float3 pH = normalize(V + pL);

        // Attenuation based on distance and radius
        float attenuation = calculateAttenuation(distance, light.radius);
        float3 pRadiance = light.color * light.intensity * attenuation;

        // Cook-Torrance BRDF (same as directional)
        float pNDF = DistributionGGX(N, pH, roughness);
        float pG = GeometrySmith(N, V, pL, roughness);
        float3 pF = fresnelSchlick(max(dot(pH, V), 0.0), F0);

        float3 pNumerator = pNDF * pG * pF;
        float pDenominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, pL), 0.0) + 0.0001;
        float3 pSpecular = pNumerator / pDenominator;

        float3 pKs = pF;
        float3 pKd = float3(1.0) - pKs;
        pKd *= 1.0 - metallic;

        float pNdotL = max(dot(N, pL), 0.0);

        // Apply point shadow if this is the shadow-casting light
        // pointShadowParams.w stores lightIndex+1 (0 means disabled)
        float pointShadow = 1.0;
        if (i == uint(sceneData.pointShadowParams.w - 1) && sceneData.pointShadowParams.w > 0.5)
        {
            pointShadow = calculatePointShadow(input.worldPos, sceneData.pointLightShadowPos, N);
        }

        Lo += (pKd * albedo / PI + pSpecular) * pRadiance * pNdotL * pointShadow;
    }

    // Spot lights
    uint numSpotLights = lightData[0].numSpotLights;
    for (uint i = 0; i < numSpotLights; ++i)
    {
        SpotLight light = lightData[0].spotLights[i];

        // Calculate light direction and distance
        float3 lightVec = light.position - input.worldPos;
        float distance = length(lightVec);
        float3 sL = normalize(lightVec);
        float3 sH = normalize(V + sL);

        // Spotlight cone attenuation
        float theta = dot(sL, normalize(-light.direction));
        float epsilon = light.innerCutoff - light.outerCutoff;
        float spotIntensity = saturate((theta - light.outerCutoff) / epsilon);

        // Distance attenuation based on radius
        float attenuation = calculateAttenuation(distance, light.radius);
        float3 sRadiance = light.color * light.intensity * attenuation * spotIntensity;

        // Cook-Torrance BRDF
        float sNDF = DistributionGGX(N, sH, roughness);
        float sG = GeometrySmith(N, V, sL, roughness);
        float3 sF = fresnelSchlick(max(dot(sH, V), 0.0), F0);

        float3 sNumerator = sNDF * sG * sF;
        float sDenominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, sL), 0.0) + 0.0001;
        float3 sSpecular = sNumerator / sDenominator;

        float3 sKs = sF;
        float3 sKd = float3(1.0) - sKs;
        sKd *= 1.0 - metallic;

        float sNdotL = max(dot(N, sL), 0.0);

        // Apply shadow to first spot light only (index matches spotShadowParams.z)
        float spotShadow = 1.0;
        if (i == uint(sceneData.spotShadowParams.z) && sceneData.spotShadowParams.w > 0.5)
        {
            spotShadow = calculateSpotShadow(input.spotLightSpacePos, N, sL);
        }

        Lo += (sKd * albedo / PI + sSpecular) * sRadiance * sNdotL * spotShadow;
    }

    // Ambient lighting (IBL approximation)
    float3 ambient = sceneData.ambientColor.rgb * albedo * ao;

    float3 color = ambient + Lo;

    // HDR tonemapping (Reinhard)
    color = color / (color + float3(1.0));

    // Gamma correction
    color = pow(color, float3(1.0 / 2.2));

    return float4(color, 1.0);
}
