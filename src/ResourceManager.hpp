#pragma once

#include <Types.hpp>
#include <deque>
#include <functional>

struct DeletionQueue
{
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function)
	{
		deletors.push_back(function);
	}

	void flush()
	{
		// reverse iterate the deletion queue to execute all the functions
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
		{
			(*it)(); // call functors
		}

		deletors.clear();
	}
};

class ResourceManager
{
public:
	ResourceManager()                                       = default;
	~ResourceManager()                                      = default;
	ResourceManager(const ResourceManager& other)           = delete;
	ResourceManager(ResourceManager&& other)                = delete;
	ResourceManager& operator=(const ResourceManager& other) = delete;
	ResourceManager& operator=(ResourceManager&& other)      = delete;

	// Initialize the resource manager with Vulkan objects
	void init(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue, uint32_t graphicsQueueFamily);

	// Cleanup all resources
	void cleanup();

	// Immediate submit for one-time GPU commands (uploads, transitions, etc.)
	void immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

	// Buffer management
	AllocatedBuffer createBuffer(size_t             allocSize,
	                              VkBufferUsageFlags usage,
	                              VmaMemoryUsage     memoryUsage);
	void            destroyBuffer(const AllocatedBuffer& buffer);

	// Image management (without initial data)
	AllocatedImage createImage(VkExtent3D            size,
	                            VkFormat              format,
	                            VkImageUsageFlags     usage,
	                            bool                  mipmapped  = false,
	                            VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT);

	// Image management (with initial data)
	AllocatedImage createImage(void*                 data,
	                            VkExtent3D            size,
	                            VkFormat              format,
	                            VkImageUsageFlags     usage,
	                            bool                  mipmapped  = false,
	                            VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT);

	void destroyImage(const AllocatedImage& img);

	// Mesh upload (creates vertex + index buffers and uploads data)
	GPUMeshBuffers uploadMesh(std::span<uint32_t> indices,
	                          std::span<Vertex>   vertices);

	// Accessors
	VmaAllocator getAllocator() const
	{
		return m_allocator;
	}

	DeletionQueue& getMainDeletionQueue()
	{
		return m_mainDeletionQueue;
	}

private:
	VmaAllocator     m_allocator {VK_NULL_HANDLE};
	VkDevice         m_device {VK_NULL_HANDLE};
	VkInstance       m_instance {VK_NULL_HANDLE};
	VkPhysicalDevice m_physicalDevice {VK_NULL_HANDLE};
	VkQueue          m_graphicsQueue {VK_NULL_HANDLE};
	uint32_t         m_graphicsQueueFamily {0};

	// Immediate submit resources for one-time GPU commands
	VkFence         m_immFence {VK_NULL_HANDLE};
	VkCommandBuffer m_immCommandBuffer {VK_NULL_HANDLE};
	VkCommandPool   m_immCommandPool {VK_NULL_HANDLE};

	DeletionQueue m_mainDeletionQueue;
};
