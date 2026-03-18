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

// Resolve resource paths relative to the project root (dev) or working directory (production).
inline std::string resPath(const char* relativePath) {
	return std::string(RESOURCES_PATH) + relativePath;
}

#include <glm/gtc/type_ptr.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>


struct AllocatedImage
{
	VkImage       m_image       = VK_NULL_HANDLE;
	VkImageView   m_imageView   = VK_NULL_HANDLE;
	VmaAllocation m_allocation  = VK_NULL_HANDLE;
	VkExtent3D    m_imageExtent = {0, 0, 0};
	VkFormat      m_imageFormat = VK_FORMAT_UNDEFINED;
};

struct AllocatedBuffer
{
	VkBuffer          m_buffer     = VK_NULL_HANDLE;
	VmaAllocation     m_allocation = VK_NULL_HANDLE;
	VmaAllocationInfo m_info       = {};
};

struct Vertex
{

	glm::vec3 m_position {0.0f};
	float     m_uv_x = 0.0f;
	glm::vec3 m_normal {0.0f};
	float     m_uv_y = 0.0f;
	glm::vec4 m_color {0.0f};
	glm::vec4 m_tangent {0.0f};
};

// holds the resources needed for a mesh
struct GPUMeshBuffers
{

	AllocatedBuffer m_indexBuffer {};
	AllocatedBuffer m_vertexBuffer {};
	VkDeviceAddress m_vertexBufferAddress = 0;
};

// bounding volume for frustum culling
struct Bounds
{
	glm::vec3 m_origin {0.0f};
	float     m_sphereRadius {0.0f};
	glm::vec3 m_extents {0.0f};
};

// push constants for our mesh object draws
struct GPUDrawPushConstants
{
	glm::mat4       m_worldMatrix {0.0f};
	VkDeviceAddress m_vertexBuffer  = 0;
	uint32_t        m_materialIndex = 0; // Index into bindless material array
	uint32_t        m_padding       = 0;
};

// Per-draw data for indirect drawing (stored in SSBO, indexed by firstInstance)
struct GPUDrawData
{
	glm::mat4       m_worldMatrix {1.0f};   // 64 bytes
	VkDeviceAddress m_vertexBuffer = 0;     // 8 bytes
	uint32_t        m_materialIndex = 0;    // 4 bytes
	uint32_t        m_padding = 0;          // 4 bytes
};  // 80 bytes total, std430 compatible

// Push constants for indirect draw path (just BDA to draw data SSBO)
struct IndirectDrawPushConstants
{
	VkDeviceAddress m_drawDataBufferPtr = 0;  // 8 bytes
};

// push constants for object ID picking pass (64-bit entity ID support)
struct ObjectIDPushConstants
{
	glm::mat4       m_worldMatrix {0.0f};
	VkDeviceAddress m_vertexBuffer = 0;
	uint64_t        m_entityID     = 0; // Full 64-bit Flecs entity ID
};

struct GPUSceneData
{
	glm::mat4 m_view {1.0f};
	glm::mat4 m_proj {1.0f};
	glm::mat4 m_viewproj {1.0f};
	glm::mat4 m_lightSpaceMatrix {1.0f};     // Directional light view-projection for shadow mapping
	glm::mat4 m_spotLightSpaceMatrix {1.0f}; // Spot light view-projection for shadow mapping
	glm::vec4 m_ambientColor {0.0f};
	glm::vec4 m_sunlightDirection {0.0f};    // w for sun power
	glm::vec4 m_sunlightColor {0.0f};
	glm::vec4 m_shadowParams {0.0f};         // x=bias, y=normalBias, z=1/resolution, w=dirEnabled
	glm::vec4 m_spotShadowParams {0.0f};     // x=bias, y=normalBias, z=spotLightIndex, w=spotEnabled
	glm::vec4 m_pointShadowParams {0.0f};    // x=bias, y=PCFRadius, z=farPlane, w=pointLightIndex+1 (0=disabled)
	glm::vec3 m_pointLightShadowPos {0.0f};  // Position of the shadow-casting point light
	float     m_pointShadowPadding = 0.0f;   // Alignment padding
	glm::vec3 m_cameraPosition {0.0f};
	float     m_padding = 0.0f;              // Alignment padding
};

// Shadow mapping constants
constexpr uint32_t SHADOW_MAP_RESOLUTION       = 2048;
constexpr uint32_t POINT_SHADOW_MAP_RESOLUTION = 2048;

// Headroom reserved for non-bindless sampled image descriptors
// (e.g. shadow maps) sharing the same pipeline stage as the bindless set.
constexpr uint32_t RESERVED_SAMPLED_IMAGES = 8;

// Push constants for shadow pass (minimal - depth only)
struct ShadowPushConstants
{
	glm::mat4       m_worldMatrix {0.0f};
	VkDeviceAddress m_vertexBuffer = 0;
};

// Push constants for point light shadow pass (includes light position for
// distance calculation) Must match shader layout (std430 alignment: mat4
// requires 16-byte alignment)
struct PointShadowPushConstants
{
	glm::mat4       m_worldMatrix {0.0f};   // 64 bytes, offset 0
	VkDeviceAddress m_vertexBuffer  = 0;    // 8 bytes, offset 64
	uint64_t        m_padding       = 0;    // 8 bytes padding to align m_lightViewProj to 16 bytes
	glm::mat4       m_lightViewProj {0.0f}; // 64 bytes, offset 80 (View-proj matrix for current cube face)
	glm::vec3       m_lightPos {0.0f};      // 12 bytes, offset 144 (Light position for distance calculation)
	float           m_farPlane = 0.0f;      // 4 bytes, offset 156 (Far plane for depth normalization)
}; // Total: 160 bytes

// Maximum number of lights supported
constexpr uint32_t MAX_POINT_LIGHTS = 256;
constexpr uint32_t MAX_SPOT_LIGHTS  = 64;

// GPU-side point light structure (std430 layout compatible)
struct GPUPointLight
{
	glm::vec3 m_position {0.0f};
	float     m_radius = 0.0f;    // Attenuation radius
	glm::vec3 m_color {0.0f};
	float     m_intensity = 0.0f;
};

// GPU-side spot light structure (std430 layout compatible)
struct GPUSpotLight
{
	glm::vec3 m_position {0.0f};
	float     m_radius      = 0.0f; // Attenuation radius
	glm::vec3 m_direction {0.0f};
	float     m_innerCutoff = 0.0f; // cos(innerConeAngle)
	glm::vec3 m_color {0.0f};
	float     m_outerCutoff = 0.0f; // cos(outerConeAngle)
	float     m_intensity   = 0.0f;
	float     m_padding[3]  = {0.0f, 0.0f, 0.0f}; // Align to 16 bytes
};

// Light buffer structure for SSBO
struct GPULightData
{
	uint32_t      m_numPointLights = 0;
	uint32_t      m_numSpotLights  = 0;
	uint32_t      m_padding[2]     = {0, 0}; // Align to 16 bytes for std430
	GPUPointLight m_pointLights[MAX_POINT_LIGHTS] = {};
	GPUSpotLight  m_spotLights[MAX_SPOT_LIGHTS]   = {};
};