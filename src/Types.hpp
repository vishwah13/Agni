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
	VkBuffer          m_buffer;
	VmaAllocation     m_allocation;
	VmaAllocationInfo m_info;
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

// push constants for our mesh object draws
struct GPUDrawPushConstants
{
	glm::mat4       m_worldMatrix;
	VkDeviceAddress m_vertexBuffer;
};

struct GPUSceneData
{
	glm::mat4 m_view;
	glm::mat4 m_proj;
	glm::mat4 m_viewproj;
	glm::vec4 m_ambientColor;
	glm::vec4 m_sunlightDirection; // w for sun power
	glm::vec4 m_sunlightColor;
	glm::vec3 m_cameraPosition;
};

enum class MaterialPass : uint8_t
{
	MainColor,
	Transparent,
	Other
};
struct MaterialPipeline
{
	VkPipeline       m_pipeline;
	VkPipelineLayout m_layout;
};

struct MaterialInstance
{
	MaterialPipeline* m_pipeline;
	VkDescriptorSet   m_materialSet;
	MaterialPass      m_passType;
};