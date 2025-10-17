// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.
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

// it's not very good now it just gives an error code, need to improve it
// later
// need something like this:
// https://github.com/SaschaWillems/Vulkan/blob/master/base/VulkanTools.h
#define VK_CHECK(x)                                                           \
	do                                                                        \
	{                                                                         \
		VkResult err = x;                                                     \
		if (err)                                                              \
		{                                                                     \
			fmt::println("Detected Vulkan error: {}", static_cast<int>(err)); \
			abort();                                                          \
		}                                                                     \
	} while (0)

struct AllocatedImage
{
	VkImage       image;
	VkImageView   imageView;
	VmaAllocation allocation;
	VkExtent3D    imageExtent;
	VkFormat      imageFormat;
};

struct AllocatedBuffer
{
	VkBuffer          buffer;
	VmaAllocation     allocation;
	VmaAllocationInfo info;
};

struct Vertex
{

	glm::vec3 position;
	float     uv_x;
	glm::vec3 normal;
	float     uv_y;
	glm::vec4 color;
};

// holds the resources needed for a mesh
struct GPUMeshBuffers
{

	AllocatedBuffer indexBuffer;
	AllocatedBuffer vertexBuffer;
	VkDeviceAddress vertexBufferAddress;
};

// push constants for our mesh object draws
struct GPUDrawPushConstants
{
	glm::mat4       worldMatrix;
	VkDeviceAddress vertexBuffer;
};