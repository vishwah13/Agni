// Mesh shader for PBR rendering
import input_structures;

// Scene data (set 0, binding 0)
[[vk::binding(0, 0)]]
ConstantBuffer<SceneData> sceneData;

// Light data SSBO (set 0, binding 1)
[[vk::binding(1, 0)]]
StructuredBuffer<LightData> lightData;

// Material data (set 1, binding 0)
[[vk::binding(0, 1)]]
ConstantBuffer<GLTFMaterialData> materialData;

// Material textures (set 1)
[[vk::binding(1, 1)]]
Sampler2D colorTex;

[[vk::binding(2, 1)]]
Sampler2D metalRoughTex;

[[vk::binding(3, 1)]]
Sampler2D normalTex;

[[vk::binding(4, 1)]]
Sampler2D aoTex;

// Push constants matching C++ GPUDrawPushConstants
struct PushConstants
{
    float4x4 worldMatrix;
    uint64_t vertexBufferPtr; // Buffer device address as raw uint64
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

    VertexOutput output;

    float4 position = float4(v.position, 1.0f);
    float4 worldPos = mul(pushConstants.worldMatrix, position);

    output.position = mul(sceneData.viewproj, worldPos);
    output.worldPos = worldPos.xyz;
    output.normal = normalize(mul((float3x3)pushConstants.worldMatrix, v.normal));
    output.color = v.color.xyz * materialData.colorFactors.xyz;
    output.uv = float2(v.uv_x, v.uv_y);

    // Transform tangent to world space
    float3 T = normalize(mul((float3x3)pushConstants.worldMatrix, v.tangent.xyz));
    // Re-orthogonalize T with respect to N using Gram-Schmidt process
    T = normalize(T - dot(T, output.normal) * output.normal);
    // Calculate bitangent using handedness (w component)
    float3 B = cross(output.normal, T) * v.tangent.w;

    output.tangent = T;
    output.bitangent = B;

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

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    // Sample textures
    float3 albedo = pow(colorTex.Sample(input.uv).rgb, float3(2.2)); // sRGB to linear
    albedo *= input.color;

    float2 metallicRoughness = metalRoughTex.Sample(input.uv).bg; // B=metallic, G=roughness in glTF
    float metallic = metallicRoughness.x * materialData.metal_rough_factors.x;
    float roughness = metallicRoughness.y * materialData.metal_rough_factors.y;

    float ao = aoTex.Sample(input.uv).r;

    // Normal mapping
    // Construct TBN matrix to transform from tangent space to world space
    float3 T = normalize(input.tangent);
    float3 B = normalize(input.bitangent);
    float3 N = normalize(input.normal);
    float3x3 TBN = float3x3(T, B, N);

    // Sample normal from normal map (stored in tangent space)
    float3 normalMap = normalTex.Sample(input.uv).xyz;
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
    Lo += (kD * albedo / PI + specular) * radiance * NdotL;

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
        Lo += (pKd * albedo / PI + pSpecular) * pRadiance * pNdotL;
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
        Lo += (sKd * albedo / PI + sSpecular) * sRadiance * sNdotL;
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
