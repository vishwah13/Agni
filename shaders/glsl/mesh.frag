#version 450

#extension GL_GOOGLE_include_directive : require
#include "input_structures.glsl"

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inWorldPos;
layout (location = 4) in vec3 inTangent;
layout (location = 5) in vec3 inBitangent;

layout (location = 0) out vec4 outFragColor;

const float PI = 3.14159265359;

// Normal Distribution Function - GGX/Trowbridge-Reitz
float DistributionGGX(vec3 N, vec3 H, float roughness)
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
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float ggx2 = GeometrySchlickGGX(NdotV, roughness);
	float ggx1 = GeometrySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Point light attenuation (smooth falloff within radius)
float calculateAttenuation(float distance, float radius)
{
	// Smooth quadratic falloff with radius cutoff
	float ratio = distance / radius;
	float attenuation = clamp(1.0 - ratio * ratio, 0.0, 1.0);
	return attenuation * attenuation;
}

void main()
{
	// Sample textures
	vec3 albedo = pow(texture(colorTex, inUV).rgb, vec3(2.2)); // sRGB to linear
	albedo *= inColor;

	vec2 metallicRoughness = texture(metalRoughTex, inUV).bg; // B=metallic, G=roughness in glTF
	float metallic = metallicRoughness.x * materialData.metal_rough_factors.x;
	float roughness = metallicRoughness.y * materialData.metal_rough_factors.y;

	float ao = texture(aoTex, inUV).r;

	// Normal mapping
	// Construct TBN matrix to transform from tangent space to world space
	vec3 T = normalize(inTangent);
	vec3 B = normalize(inBitangent);
	vec3 N = normalize(inNormal);
	mat3 TBN = mat3(T, B, N);

	// Sample normal from normal map (stored in tangent space)
	vec3 normalMap = texture(normalTex, inUV).xyz;
	// Transform from [0,1] range to [-1,1] range
	normalMap = normalMap * 2.0 - 1.0;
	// Transform normal from tangent space to world space
	N = normalize(TBN * normalMap);

	vec3 V = normalize(sceneData.camPos.xyz - inWorldPos);

	// Calculate F0 (base reflectivity)
	vec3 F0 = vec3(0.04);
	F0 = mix(F0, albedo, metallic);

	// Reflectance equation
	vec3 Lo = vec3(0.0);

	// Directional light calculation
	vec3 L = normalize(sceneData.sunlightDirection.xyz);
	vec3 H = normalize(V + L);
	vec3 radiance = sceneData.sunlightColor.rgb * sceneData.sunlightColor.w;

	// Cook-Torrance BRDF
	float NDF = DistributionGGX(N, H, roughness);
	float G = GeometrySmith(N, V, L, roughness);
	vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

	vec3 numerator = NDF * G * F;
	float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001; // prevent divide by zero
	vec3 specular = numerator / denominator;

	// Energy conservation
	vec3 kS = F; // specular contribution
	vec3 kD = vec3(1.0) - kS; // diffuse contribution
	kD *= 1.0 - metallic; // metals have no diffuse

	float NdotL = max(dot(N, L), 0.0);
	Lo += (kD * albedo / PI + specular) * radiance * NdotL;

	// Point lights
	uint numPointLights = lightData.numPointLights;
	for (uint i = 0; i < numPointLights; ++i)
	{
		PointLight light = lightData.pointLights[i];

		// Calculate light direction and distance
		vec3 lightVec = light.position - inWorldPos;
		float distance = length(lightVec);
		vec3 pL = normalize(lightVec);
		vec3 pH = normalize(V + pL);

		// Attenuation based on distance and radius
		float attenuation = calculateAttenuation(distance, light.radius);
		vec3 pRadiance = light.color * light.intensity * attenuation;

		// Cook-Torrance BRDF (same as directional)
		float pNDF = DistributionGGX(N, pH, roughness);
		float pG = GeometrySmith(N, V, pL, roughness);
		vec3 pF = fresnelSchlick(max(dot(pH, V), 0.0), F0);

		vec3 pNumerator = pNDF * pG * pF;
		float pDenominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, pL), 0.0) + 0.0001;
		vec3 pSpecular = pNumerator / pDenominator;

		vec3 pKs = pF;
		vec3 pKd = vec3(1.0) - pKs;
		pKd *= 1.0 - metallic;

		float pNdotL = max(dot(N, pL), 0.0);
		Lo += (pKd * albedo / PI + pSpecular) * pRadiance * pNdotL;
	}

	// Spot lights
	uint numSpotLights = lightData.numSpotLights;
	for (uint i = 0; i < numSpotLights; ++i)
	{
		SpotLight light = lightData.spotLights[i];

		// Calculate light direction and distance
		vec3 lightVec = light.position - inWorldPos;
		float distance = length(lightVec);
		vec3 sL = normalize(lightVec);
		vec3 sH = normalize(V + sL);

		// Spotlight cone attenuation
		float theta = dot(sL, normalize(-light.direction));
		float epsilon = light.innerCutoff - light.outerCutoff;
		float spotIntensity = clamp((theta - light.outerCutoff) / epsilon, 0.0, 1.0);

		// Distance attenuation based on radius
		float attenuation = calculateAttenuation(distance, light.radius);
		vec3 sRadiance = light.color * light.intensity * attenuation * spotIntensity;

		// Cook-Torrance BRDF
		float sNDF = DistributionGGX(N, sH, roughness);
		float sG = GeometrySmith(N, V, sL, roughness);
		vec3 sF = fresnelSchlick(max(dot(sH, V), 0.0), F0);

		vec3 sNumerator = sNDF * sG * sF;
		float sDenominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, sL), 0.0) + 0.0001;
		vec3 sSpecular = sNumerator / sDenominator;

		vec3 sKs = sF;
		vec3 sKd = vec3(1.0) - sKs;
		sKd *= 1.0 - metallic;

		float sNdotL = max(dot(N, sL), 0.0);
		Lo += (sKd * albedo / PI + sSpecular) * sRadiance * sNdotL;
	}

	// Ambient lighting (IBL approximation)
	vec3 ambient = sceneData.ambientColor.rgb * albedo * ao;

	vec3 color = ambient + Lo;

	// HDR tonemapping (Reinhard)
	color = color / (color + vec3(1.0));

	// Gamma correction
	color = pow(color, vec3(1.0/2.2));

	outFragColor = vec4(color, 1.0);
}