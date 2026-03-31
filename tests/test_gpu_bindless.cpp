#include "gpu_test_fixture.hpp"

#include <BindlessResources.hpp>
#include <DescriptorBuffer.hpp>
#include <Descriptors.hpp>
#include <Initializers.hpp>
#include <Images.hpp>
#include <VulkanTools.hpp>

#include <cstring>

// ============================================================================
// GPU_BindlessResources — texture/material registry tests
// ============================================================================

class GPU_Bindless : public GpuTestFixture
{
protected:
	DescriptorBufferProperties m_descriptorBufferProps;

	// Helper textures for testing
	AllocatedImage m_testImageA {};
	AllocatedImage m_testImageB {};

	void SetUp() override
	{
		GpuTestFixture::SetUp();
		if (::testing::Test::IsSkipped()) return;

		DescriptorBufferAllocator::queryProperties(m_physicalDevice, m_descriptorBufferProps);

		// Create two small 1x1 test images for texture registry testing
		VkExtent3D extent {1, 1, 1};
		m_testImageA = m_resourceManager.createImage(
		    extent, VK_FORMAT_R8G8B8A8_UNORM,
		    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);

		m_testImageB = m_resourceManager.createImage(
		    extent, VK_FORMAT_R8G8B8A8_UNORM,
		    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	}

	void TearDown() override
	{
		if (m_device != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_device);
			m_resourceManager.destroyImage(m_testImageA);
			m_resourceManager.destroyImage(m_testImageB);
		}
		GpuTestFixture::TearDown();
	}
};

TEST_F(GPU_Bindless, TextureRegistryInit)
{
	TextureRegistry registry;
	registry.init(m_device, &m_resourceManager, m_descriptorBufferProps, 128);

	EXPECT_EQ(registry.getTextureCount(), 0u);
	EXPECT_EQ(registry.getMaxTextures(), 128u);
	EXPECT_NE(registry.getLayout(), VK_NULL_HANDLE);
	EXPECT_NE(registry.getBufferAddress(), 0u);

	registry.destroy();
}

TEST_F(GPU_Bindless, TextureRegistryRegister)
{
	TextureRegistry registry;
	registry.init(m_device, &m_resourceManager, m_descriptorBufferProps, 128);

	uint32_t idxA = registry.registerTexture(m_testImageA.m_imageView);
	EXPECT_NE(idxA, INVALID_BINDLESS_INDEX);
	EXPECT_EQ(registry.getTextureCount(), 1u);

	uint32_t idxB = registry.registerTexture(m_testImageB.m_imageView);
	EXPECT_NE(idxB, INVALID_BINDLESS_INDEX);
	EXPECT_NE(idxA, idxB);
	EXPECT_EQ(registry.getTextureCount(), 2u);

	registry.destroy();
}

TEST_F(GPU_Bindless, TextureRegistryDeduplication)
{
	TextureRegistry registry;
	registry.init(m_device, &m_resourceManager, m_descriptorBufferProps, 128);

	uint32_t idx1 = registry.registerTexture(m_testImageA.m_imageView);
	uint32_t idx2 = registry.registerTexture(m_testImageA.m_imageView); // same image

	EXPECT_EQ(idx1, idx2); // should return same index
	EXPECT_EQ(registry.getTextureCount(), 1u); // only 1 registered

	registry.destroy();
}

TEST_F(GPU_Bindless, MaterialRegistryInit)
{
	MaterialRegistry registry;
	registry.init(m_device, &m_resourceManager, m_descriptorBufferProps, 256);

	EXPECT_EQ(registry.getMaterialCount(), 0u);
	EXPECT_EQ(registry.getMaxMaterials(), 256u);
	EXPECT_NE(registry.getLayout(), VK_NULL_HANDLE);

	registry.destroy();
}

TEST_F(GPU_Bindless, MaterialRegistryRegisterAndUpdate)
{
	MaterialRegistry registry;
	registry.init(m_device, &m_resourceManager, m_descriptorBufferProps, 256);

	GPUMaterialData mat {};
	mat.colorFactors       = glm::vec4(1, 0, 0, 1);
	mat.metalRoughFactors  = glm::vec4(0.5f, 0.8f, 0, 0);
	mat.colorTexIndex      = 0;
	mat.metalRoughTexIndex = 0;

	uint32_t idx = registry.registerMaterial(mat);
	EXPECT_EQ(idx, 0u);
	EXPECT_EQ(registry.getMaterialCount(), 1u);

	// Register a second
	GPUMaterialData mat2 {};
	mat2.colorFactors = glm::vec4(0, 1, 0, 1);
	uint32_t idx2 = registry.registerMaterial(mat2);
	EXPECT_EQ(idx2, 1u);
	EXPECT_EQ(registry.getMaterialCount(), 2u);

	// Update the first material
	mat.colorFactors = glm::vec4(0, 0, 1, 1);
	registry.updateMaterial(idx, mat); // should not crash

	registry.destroy();
}

TEST_F(GPU_Bindless, SamplerRegistryInit)
{
	// Create test samplers
	VkSamplerCreateInfo samplerInfo {};
	samplerInfo.sType     = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;

	VkSampler linearSampler, nearestSampler, linearMip, nearestMip;

	VK_CHECK(vkCreateSampler(m_device, &samplerInfo, nullptr, &linearSampler));
	samplerInfo.magFilter = VK_FILTER_NEAREST;
	samplerInfo.minFilter = VK_FILTER_NEAREST;
	VK_CHECK(vkCreateSampler(m_device, &samplerInfo, nullptr, &nearestSampler));
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.magFilter  = VK_FILTER_LINEAR;
	samplerInfo.minFilter  = VK_FILTER_LINEAR;
	VK_CHECK(vkCreateSampler(m_device, &samplerInfo, nullptr, &linearMip));
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.magFilter  = VK_FILTER_NEAREST;
	samplerInfo.minFilter  = VK_FILTER_NEAREST;
	VK_CHECK(vkCreateSampler(m_device, &samplerInfo, nullptr, &nearestMip));

	SamplerRegistry registry;
	registry.init(m_device, &m_resourceManager, m_descriptorBufferProps,
	              linearSampler, nearestSampler, linearMip, nearestMip);

	EXPECT_NE(registry.getLayout(), VK_NULL_HANDLE);
	EXPECT_NE(registry.getBufferAddress(), 0u);

	registry.destroy();

	vkDestroySampler(m_device, linearSampler, nullptr);
	vkDestroySampler(m_device, nearestSampler, nullptr);
	vkDestroySampler(m_device, linearMip, nullptr);
	vkDestroySampler(m_device, nearestMip, nullptr);
}
