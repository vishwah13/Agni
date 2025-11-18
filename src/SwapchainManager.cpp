#include <SwapchainManager.hpp>

#include <VkBootstrap.h>

void SwapchainManager::init(VkPhysicalDevice physicalDevice,
                            VkDevice         device,
                            VkSurfaceKHR     surface,
                            VkExtent2D       windowExtent)
{
	createSwapchain(physicalDevice, device, surface, windowExtent.width, windowExtent.height);
}

void SwapchainManager::cleanup(VkDevice device)
{
	destroySwapchain(device);
}

void SwapchainManager::resize(VkPhysicalDevice physicalDevice,
                              VkDevice         device,
                              VkSurfaceKHR     surface,
                              VkExtent2D       newExtent)
{
	vkDeviceWaitIdle(device);

	destroySwapchain(device);
	createSwapchain(physicalDevice, device, surface, newExtent.width, newExtent.height);

	m_resizeRequested = false;
}

void SwapchainManager::createSwapchain(VkPhysicalDevice physicalDevice,
                                       VkDevice         device,
                                       VkSurfaceKHR     surface,
                                       uint32_t         width,
                                       uint32_t         height)
{
	vkb::SwapchainBuilder swapchainBuilder {physicalDevice, device, surface};

	m_swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

	vkb::Swapchain vkbSwapchain =
	    swapchainBuilder
	        //.use_default_format_selection()
	        .set_desired_format(
	            VkSurfaceFormatKHR {.format     = m_swapchainImageFormat,
	                                .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
	        // use vsync present mode
	        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
	        .set_desired_extent(width, height)
	        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
	        .build()
	        .value();

	m_swapchainExtent = vkbSwapchain.extent;
	// store swapchain and its related images
	m_swapchain           = vkbSwapchain.swapchain;
	m_swapchainImages     = vkbSwapchain.get_images().value();
	m_swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void SwapchainManager::destroySwapchain(VkDevice device)
{
	if (device != VK_NULL_HANDLE && m_swapchain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(device, m_swapchain, nullptr);
	}

	// destroy swapchain resources
	for (int i = 0; i < m_swapchainImageViews.size(); i++)
	{
		vkDestroyImageView(device, m_swapchainImageViews[i], nullptr);
	}

	// Clear the vectors
	m_swapchainImages.clear();
	m_swapchainImageViews.clear();
	m_swapchain = VK_NULL_HANDLE;
}
