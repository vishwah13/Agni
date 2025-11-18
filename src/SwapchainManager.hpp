#pragma once

#include <Types.hpp>

#include <vector>

class SwapchainManager
{
public:
	SwapchainManager()                                        = default;
	~SwapchainManager()                                       = default;
	SwapchainManager(const SwapchainManager& other)           = delete;
	SwapchainManager(SwapchainManager&& other)                = delete;
	SwapchainManager& operator=(const SwapchainManager& other) = delete;
	SwapchainManager& operator=(SwapchainManager&& other)      = delete;

	// Initialize swapchain
	void init(VkPhysicalDevice physicalDevice,
	          VkDevice         device,
	          VkSurfaceKHR     surface,
	          VkExtent2D       windowExtent);

	// Cleanup all swapchain resources
	void cleanup(VkDevice device);

	// Recreate swapchain after resize
	void resize(VkPhysicalDevice physicalDevice,
	            VkDevice         device,
	            VkSurfaceKHR     surface,
	            VkExtent2D       newExtent);

	// Accessors
	VkSwapchainKHR getSwapchain() const { return m_swapchain; }
	VkFormat       getSwapchainImageFormat() const { return m_swapchainImageFormat; }
	VkExtent2D     getSwapchainExtent() const { return m_swapchainExtent; }

	const std::vector<VkImage>&     getSwapchainImages() const { return m_swapchainImages; }
	const std::vector<VkImageView>& getSwapchainImageViews() const { return m_swapchainImageViews; }

	bool isResizeRequested() const { return m_resizeRequested; }
	void requestResize() { m_resizeRequested = true; }
	void clearResizeRequest() { m_resizeRequested = false; }

private:
	// Swapchain resources
	VkSwapchainKHR m_swapchain {VK_NULL_HANDLE};
	VkFormat       m_swapchainImageFormat {VK_FORMAT_UNDEFINED};
	VkExtent2D     m_swapchainExtent {0, 0};

	std::vector<VkImage>     m_swapchainImages;
	std::vector<VkImageView> m_swapchainImageViews;

	bool m_resizeRequested {false};

	// Internal helper methods
	void createSwapchain(VkPhysicalDevice physicalDevice,
	                     VkDevice         device,
	                     VkSurfaceKHR     surface,
	                     uint32_t         width,
	                     uint32_t         height);

	void destroySwapchain(VkDevice device);
};
