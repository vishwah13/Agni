layout(set = 0, binding = 0) uniform  SceneData{

	mat4 view;
	mat4 proj;
	mat4 viewproj;
	vec4 ambientColor;
	vec4 sunlightDirection; //w for sun power
	vec4 sunlightColor;
	vec3 camPos; // camera position for view vector
} sceneData;

// Point light structure (matches C++ GPUPointLight)
struct PointLight {
	vec3  position;
	float radius;
	vec3  color;
	float intensity;
};

// Spot light structure (matches C++ GPUSpotLight)
struct SpotLight {
	vec3  position;
	float radius;
	vec3  direction;
	float innerCutoff;    // cos(innerConeAngle)
	vec3  color;
	float outerCutoff;    // cos(outerConeAngle)
	float intensity;
	float padding0;
	float padding1;
	float padding2;
};

// Light data SSBO (set 0, binding 1)
layout(std430, set = 0, binding = 1) readonly buffer LightData {
	uint       numPointLights;
	uint       numSpotLights;
	uint       padding0;
	uint       padding1;
	PointLight pointLights[256]; // MAX_POINT_LIGHTS
	SpotLight  spotLights[64];   // MAX_SPOT_LIGHTS
} lightData;

layout(set = 1, binding = 0) uniform GLTFMaterialData{   

	vec4 colorFactors;
	vec4 metal_rough_factors;
	
} materialData;

layout(set = 1, binding = 1) uniform sampler2D colorTex;
layout(set = 1, binding = 2) uniform sampler2D metalRoughTex;
layout(set = 1, binding = 3) uniform sampler2D normalTex;
layout(set = 1, binding = 4) uniform sampler2D aoTex;