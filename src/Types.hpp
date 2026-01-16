#pragma once
#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#define VK_NO_PROTOTYPES
#include <vk_mem_alloc.h>
#include <volk.h>

#include <fmt/core.h>

#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>


struct AllocatedImage
{
	VkImage       m_image;
	VkImageView   m_imageView;
	VmaAllocation m_allocation;
	VkExtent3D    m_imageExtent;
	VkFormat      m_imageFormat;
};

struct AllocatedBuffer
{
	VkBuffer          m_buffer     = VK_NULL_HANDLE;
	VmaAllocation     m_allocation = VK_NULL_HANDLE;
	VmaAllocationInfo m_info       = {};
};

struct Vertex
{

	glm::vec3 m_position;
	float     m_uv_x;
	glm::vec3 m_normal;
	float     m_uv_y;
	glm::vec4 m_color;
	glm::vec4 m_tangent;
};

// holds the resources needed for a mesh
struct GPUMeshBuffers
{

	AllocatedBuffer m_indexBuffer;
	AllocatedBuffer m_vertexBuffer;
	VkDeviceAddress m_vertexBufferAddress;
};

// bounding volume for frustum culling
struct Bounds
{
	glm::vec3 m_origin;
	float     m_sphereRadius;
	glm::vec3 m_extents;
};

// push constants for our mesh object draws
struct GPUDrawPushConstants
{
	glm::mat4       m_worldMatrix;
	VkDeviceAddress m_vertexBuffer;
	uint32_t        m_materialIndex;  // Index into bindless material array
	uint32_t        m_padding;
};

// push constants for object ID picking pass
struct ObjectIDPushConstants
{
	glm::mat4       m_worldMatrix;
	VkDeviceAddress m_vertexBuffer;
	uint32_t        m_entityID;
	uint32_t        m_padding;
};

struct GPUSceneData
{
	glm::mat4 m_view;
	glm::mat4 m_proj;
	glm::mat4 m_viewproj;
	glm::mat4 m_lightSpaceMatrix;     // Directional light view-projection for shadow mapping
	glm::mat4 m_spotLightSpaceMatrix; // Spot light view-projection for shadow mapping
	glm::vec4 m_ambientColor;
	glm::vec4 m_sunlightDirection;    // w for sun power
	glm::vec4 m_sunlightColor;
	glm::vec4 m_shadowParams;         // x=bias, y=normalBias, z=1/resolution, w=dirEnabled
	glm::vec4 m_spotShadowParams;     // x=bias, y=normalBias, z=spotLightIndex, w=spotEnabled
	glm::vec4 m_pointShadowParams;    // x=bias, y=PCFRadius, z=farPlane, w=pointLightIndex+1 (0=disabled)
	glm::vec3 m_pointLightShadowPos;  // Position of the shadow-casting point light
	float     m_pointShadowPadding;   // Alignment padding
	glm::vec3 m_cameraPosition;
	float     m_padding;              // Alignment padding
};

// Shadow mapping constants
constexpr uint32_t SHADOW_MAP_RESOLUTION = 2048;
constexpr uint32_t POINT_SHADOW_MAP_RESOLUTION = 2048;

// Push constants for shadow pass (minimal - depth only)
struct ShadowPushConstants
{
	glm::mat4       m_worldMatrix;
	VkDeviceAddress m_vertexBuffer;
};

// Push constants for point light shadow pass (includes light position for distance calculation)
// Must match shader layout (std430 alignment: mat4 requires 16-byte alignment)
struct PointShadowPushConstants
{
	glm::mat4       m_worldMatrix;    // 64 bytes, offset 0
	VkDeviceAddress m_vertexBuffer;   // 8 bytes, offset 64
	uint64_t        m_padding;        // 8 bytes padding to align m_lightViewProj to 16 bytes
	glm::mat4       m_lightViewProj;  // 64 bytes, offset 80 (View-proj matrix for current cube face)
	glm::vec3       m_lightPos;       // 12 bytes, offset 144 (Light position for distance calculation)
	float           m_farPlane;       // 4 bytes, offset 156 (Far plane for depth normalization)
};                                    // Total: 160 bytes

// Maximum number of lights supported
constexpr uint32_t MAX_POINT_LIGHTS = 256;
constexpr uint32_t MAX_SPOT_LIGHTS  = 64;

// GPU-side point light structure (std430 layout compatible)
struct GPUPointLight
{
	glm::vec3 m_position;
	float     m_radius;      // Attenuation radius
	glm::vec3 m_color;
	float     m_intensity;
};

// GPU-side spot light structure (std430 layout compatible)
struct GPUSpotLight
{
	glm::vec3 m_position;
	float     m_radius;         // Attenuation radius
	glm::vec3 m_direction;
	float     m_innerCutoff;    // cos(innerConeAngle)
	glm::vec3 m_color;
	float     m_outerCutoff;    // cos(outerConeAngle)
	float     m_intensity;
	float     m_padding[3];     // Align to 16 bytes
};

// Light buffer structure for SSBO
struct GPULightData
{
	uint32_t      m_numPointLights;
	uint32_t      m_numSpotLights;
	uint32_t      m_padding[2];  // Align to 16 bytes for std430
	GPUPointLight m_pointLights[MAX_POINT_LIGHTS];
	GPUSpotLight  m_spotLights[MAX_SPOT_LIGHTS];
};