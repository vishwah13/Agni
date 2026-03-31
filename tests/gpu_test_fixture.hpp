#pragma once

#include <gtest/gtest.h>

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <vk_mem_alloc.h>
#include <VkBootstrap.h>

#include <ResourceManager.hpp>
#include <Initializers.hpp>
#include <Debug.hpp>

// Headless Vulkan test fixture — no window, no surface, no swapchain.
// Mirrors AgniEngine::initVulkan() but without SDL.
// GPU tests skip gracefully if no Vulkan driver is available.
class GpuTestFixture : public ::testing::Test
{
protected:
	VkInstance       m_instance       = VK_NULL_HANDLE;
	VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
	VkDevice         m_device         = VK_NULL_HANDLE;
	VkQueue          m_graphicsQueue  = VK_NULL_HANDLE;
	uint32_t         m_graphicsQueueFamily = 0;
	ResourceManager  m_resourceManager;

	void SetUp() override
	{
		// Initialize Volk
		if (volkInitialize() != VK_SUCCESS)
		{
			GTEST_SKIP() << "No Vulkan driver available";
			return;
		}

		// Create instance (validation layers on for tests)
		vkb::InstanceBuilder instanceBuilder;
		auto instanceResult = instanceBuilder
		    .set_app_name("AgniGpuTests")
		    .request_validation_layers(true)
		    .require_api_version(1, 3, 0)
		    .build();

		if (!instanceResult)
		{
			GTEST_SKIP() << "Failed to create Vulkan instance: "
			             << instanceResult.error().message();
			return;
		}

		vkb::Instance vkbInstance = instanceResult.value();
		m_instance = vkbInstance.instance;
		volkLoadInstance(m_instance);

		// Required device features (same as engine)
		VkPhysicalDeviceFeatures deviceFeatures {};
		deviceFeatures.multiDrawIndirect         = VK_TRUE;
		deviceFeatures.drawIndirectFirstInstance  = VK_TRUE;
		deviceFeatures.shaderInt64               = VK_TRUE;

		VkPhysicalDeviceVulkan13Features features13 {
		    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
		features13.dynamicRendering = true;
		features13.synchronization2 = true;

		VkPhysicalDeviceVulkan12Features features12 {
		    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
		features12.bufferDeviceAddress = true;
		features12.descriptorIndexing  = true;
		features12.hostQueryReset      = true;

		VkPhysicalDeviceVulkan11Features features11 {
		    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
		features11.shaderDrawParameters = true;

		// Select physical device (no surface required)
		vkb::PhysicalDeviceSelector selector {vkbInstance};
		auto physResult = selector
		    .set_minimum_version(1, 3)
		    .set_required_features(deviceFeatures)
		    .set_required_features_13(features13)
		    .set_required_features_12(features12)
		    .set_required_features_11(features11)
		    .add_required_extension(VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME)
		    .require_present(false)
		    .select();

		if (!physResult)
		{
			vkDestroyInstance(m_instance, nullptr);
			m_instance = VK_NULL_HANDLE;
			GTEST_SKIP() << "No suitable Vulkan GPU found: "
			             << physResult.error().message();
			return;
		}

		vkb::PhysicalDevice vkbPhysDevice = physResult.value();

		// Enable descriptor buffer feature
		VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBufferFeatures {
		    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT};
		descriptorBufferFeatures.descriptorBuffer = VK_TRUE;

		// Create logical device
		vkb::DeviceBuilder deviceBuilder {vkbPhysDevice};
		deviceBuilder.add_pNext(&descriptorBufferFeatures);
		auto deviceResult = deviceBuilder.build();

		if (!deviceResult)
		{
			vkDestroyInstance(m_instance, nullptr);
			m_instance = VK_NULL_HANDLE;
			GTEST_SKIP() << "Failed to create Vulkan device: "
			             << deviceResult.error().message();
			return;
		}

		vkb::Device vkbDevice = deviceResult.value();
		m_device         = vkbDevice.device;
		m_physicalDevice = vkbPhysDevice.physical_device;
		volkLoadDevice(m_device);

		m_graphicsQueue =
		    vkbDevice.get_queue(vkb::QueueType::graphics).value();
		m_graphicsQueueFamily =
		    vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

		// Initialize ResourceManager
		m_resourceManager.init(
		    m_instance, m_physicalDevice, m_device,
		    m_graphicsQueue, m_graphicsQueueFamily);
		m_resourceManager.initGlobalIndexBuffer();
	}

	void TearDown() override
	{
		if (m_device != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_device);
			m_resourceManager.cleanup();
			m_resourceManager.destroyAllocator();
			vkDestroyDevice(m_device, nullptr);
		}
		if (m_instance != VK_NULL_HANDLE)
			vkDestroyInstance(m_instance, nullptr);
	}
};
