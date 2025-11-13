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

struct DrawContext;

// base class for a renderable dynamic object
class IRenderable
{

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
};

// implementation of a drawable scene node.
// the scene node can hold m_children and will also keep a transform to
// propagate to them
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
		m_worldTransform = parentMatrix * m_localTransform;
		for (const auto& c : m_children)
		{
			c->refreshTransform(m_worldTransform);
		}
	}

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx)
	{
		// draw m_children
		for (auto& c : m_children)
		{
			c->Draw(topMatrix, ctx);
		}
	}

	// Accessors
	glm::mat4& getLocalTransform()
	{
		return m_localTransform;
	}
	const glm::mat4& getLocalTransform() const
	{
		return m_localTransform;
	}

	glm::mat4& getWorldTransform()
	{
		return m_worldTransform;
	}
	const glm::mat4& getWorldTransform() const
	{
		return m_worldTransform;
	}

	std::vector<std::shared_ptr<Node>>& getChildren()
	{
		return m_children;
	}
	const std::vector<std::shared_ptr<Node>>& getChildren() const
	{
		return m_children;
	}

	std::weak_ptr<Node>& getParent()
	{
		return m_parent;
	}
	const std::weak_ptr<Node>& getParent() const
	{
		return m_parent;
	}

protected:
	// m_parent pointer must be a weak pointer to avoid circular dependencies
	std::weak_ptr<Node>                m_parent;
	std::vector<std::shared_ptr<Node>> m_children;

	glm::mat4 m_localTransform;
	glm::mat4 m_worldTransform;
};