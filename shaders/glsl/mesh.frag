#version 450

#extension GL_GOOGLE_include_directive : require
#include "input_structures.glsl"

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inWorldPos;

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

void main()
{
	// Sample textures
	vec3 albedo = pow(texture(colorTex, inUV).rgb, vec3(2.2)); // sRGB to linear
	albedo *= inColor;

	vec2 metallicRoughness = texture(metalRoughTex, inUV).bg; // B=metallic, G=roughness in glTF
	float metallic = metallicRoughness.x * materialData.metal_rough_factors.x;
	float roughness = metallicRoughness.y * materialData.metal_rough_factors.y;

	float ao = texture(aoTex, inUV).r;

	// Calculate normal (could add normal mapping here)
	vec3 N = normalize(inNormal);
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

	// Ambient lighting (IBL approximation)
	vec3 ambient = sceneData.ambientColor.rgb * albedo * ao;

	vec3 color = ambient + Lo;

	// HDR tonemapping (Reinhard)
	color = color / (color + vec3(1.0));

	// Gamma correction
	color = pow(color, vec3(1.0/2.2));

	outFragColor = vec4(color, 1.0);
}