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
	glm::vec4 tangent;
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

struct GPUSceneData
{
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewproj;
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection; // w for sun power
	glm::vec4 sunlightColor;
	glm::vec3 cameraPosition;
};

enum class MaterialPass : uint8_t
{
	MainColor,
	Transparent,
	Other
};
struct MaterialPipeline
{
	VkPipeline       pipeline;
	VkPipelineLayout layout;
};

struct MaterialInstance
{
	MaterialPipeline* pipeline;
	VkDescriptorSet   materialSet;
	MaterialPass      passType;
};

struct DrawContext;

// base class for a renderable dynamic object
class IRenderable
{

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
};

// implementation of a drawable scene node.
// the scene node can hold children and will also keep a transform to propagate
// to them
class Node : public IRenderable
{
public:
	// The Node class will hold the object matrix for the transforms. Both local
	// and world transform. The world transform needs to be updated, so whenever
	// the local Transform gets changed, refreshTransform must be called. This
	// will recursively go down the node tree and make sure the matrices are on
	// their correct places.
	void refreshTransform(const glm::mat4& parentMatrix)
	{
		worldTransform = parentMatrix * localTransform;
		for (const auto& c : children)
		{
			c->refreshTransform(worldTransform);
		}
	}

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx)
	{
		// draw children
		for (auto& c : children)
		{
			c->Draw(topMatrix, ctx);
		}
	}

	// Accessors
	glm::mat4& getLocalTransform() { return localTransform; }
	const glm::mat4& getLocalTransform() const { return localTransform; }

	glm::mat4& getWorldTransform() { return worldTransform; }
	const glm::mat4& getWorldTransform() const { return worldTransform; }

	std::vector<std::shared_ptr<Node>>& getChildren() { return children; }
	const std::vector<std::shared_ptr<Node>>& getChildren() const { return children; }

	std::weak_ptr<Node>& getParent() { return parent; }
	const std::weak_ptr<Node>& getParent() const { return parent; }

protected:
	// parent pointer must be a weak pointer to avoid circular dependencies
	std::weak_ptr<Node>                parent;
	std::vector<std::shared_ptr<Node>> children;

	glm::mat4 localTransform;
	glm::mat4 worldTransform;
};