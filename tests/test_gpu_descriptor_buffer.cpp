#include "gpu_test_fixture.hpp"

#include <DescriptorBuffer.hpp>
#include <Descriptors.hpp>

#include <cstring>

// ============================================================================
// GPU_DescriptorBuffer — descriptor buffer allocator and writer tests
// ============================================================================

class GPU_DescriptorBuffer : public GpuTestFixture
{
protected:
	DescriptorBufferProperties m_descriptorBufferProps;

	void SetUp() override
	{
		GpuTestFixture::SetUp();
		if (::testing::Test::IsSkipped()) return;

		DescriptorBufferAllocator::queryProperties(m_physicalDevice, m_descriptorBufferProps);
	}
};

TEST_F(GPU_DescriptorBuffer, QueryProperties)
{
	// Properties should have been queried successfully
	EXPECT_GT(m_descriptorBufferProps.uniformBufferDescriptorSize, 0u);
	EXPECT_GT(m_descriptorBufferProps.storageBufferDescriptorSize, 0u);
	EXPECT_GT(m_descriptorBufferProps.sampledImageDescriptorSize, 0u);
	EXPECT_GT(m_descriptorBufferProps.samplerDescriptorSize, 0u);
	EXPECT_GT(m_descriptorBufferProps.descriptorBufferOffsetAlignment, 0u);
}

TEST_F(GPU_DescriptorBuffer, AllocatorInit)
{
	DescriptorBufferAllocator alloc;
	alloc.init(m_device, &m_resourceManager, m_descriptorBufferProps, 4096);

	EXPECT_NE(alloc.getBuffer(), VK_NULL_HANDLE);
	EXPECT_NE(alloc.getDeviceAddress(), 0u);
	EXPECT_NE(alloc.getMappedPtr(), nullptr);
	EXPECT_EQ(alloc.getCurrentOffset(), 0u);
	EXPECT_GE(alloc.getCapacity(), 4096u);

	alloc.destroy();
}

TEST_F(GPU_DescriptorBuffer, AllocateAndReset)
{
	DescriptorBufferAllocator alloc;
	alloc.init(m_device, &m_resourceManager, m_descriptorBufferProps, 8192);

	VkDeviceSize offset1 = alloc.allocate(256);
	EXPECT_EQ(offset1, 0u); // first allocation at beginning

	VkDeviceSize offset2 = alloc.allocate(256);
	EXPECT_GT(offset2, 0u); // second allocation after first
	EXPECT_NE(offset1, offset2);

	EXPECT_GT(alloc.getCurrentOffset(), 0u);

	alloc.reset();
	EXPECT_EQ(alloc.getCurrentOffset(), 0u); // reset returns to start

	alloc.destroy();
}

TEST_F(GPU_DescriptorBuffer, AllocateWithLayoutInfo)
{
	// Create a descriptor set layout
	DescriptorLayoutBuilder layoutBuilder;
	layoutBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	layoutBuilder.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	DescriptorLayoutInfo layoutInfo = layoutBuilder.buildForDescriptorBuffer(
	    m_device,
	    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

	EXPECT_NE(layoutInfo.layout, VK_NULL_HANDLE);
	EXPECT_GT(layoutInfo.size, 0u);

	DescriptorBufferAllocator alloc;
	alloc.init(m_device, &m_resourceManager, m_descriptorBufferProps, 8192);

	VkDeviceSize offset = alloc.allocate(layoutInfo);
	void* ptr = alloc.getPtrAtOffset(offset);
	EXPECT_NE(ptr, nullptr);

	alloc.destroy();
	vkDestroyDescriptorSetLayout(m_device, layoutInfo.layout, nullptr);
}

TEST_F(GPU_DescriptorBuffer, WriteUBODescriptor)
{
	// Create a UBO
	AllocatedBuffer ubo = m_resourceManager.createBuffer(
	    256,
	    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
	    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
	    VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT,
	    VMA_MEMORY_USAGE_CPU_TO_GPU);

	VkBufferDeviceAddressInfo addrInfo {
	    .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
	    .buffer = ubo.m_buffer};
	VkDeviceAddress uboAddress = vkGetBufferDeviceAddress(m_device, &addrInfo);
	EXPECT_NE(uboAddress, 0u);

	// Create layout and allocator
	DescriptorLayoutBuilder layoutBuilder;
	layoutBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	DescriptorLayoutInfo layoutInfo = layoutBuilder.buildForDescriptorBuffer(
	    m_device, VK_SHADER_STAGE_VERTEX_BIT);

	DescriptorBufferAllocator descAlloc;
	descAlloc.init(m_device, &m_resourceManager, m_descriptorBufferProps, 4096);

	VkDeviceSize offset = descAlloc.allocate(layoutInfo);
	void* ptr = descAlloc.getPtrAtOffset(offset);

	// Write UBO descriptor — should not crash
	DescriptorBufferWriter writer;
	writer.init(m_device, m_descriptorBufferProps);
	writer.writeUniformBuffer(ptr, layoutInfo.bindingOffsets[0], uboAddress, 256);

	descAlloc.destroy();
	vkDestroyDescriptorSetLayout(m_device, layoutInfo.layout, nullptr);
	m_resourceManager.destroyBuffer(ubo);
}

TEST_F(GPU_DescriptorBuffer, WriteSSBODescriptor)
{
	AllocatedBuffer ssbo = m_resourceManager.createBuffer(
	    1024,
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
	    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
	    VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT,
	    VMA_MEMORY_USAGE_CPU_TO_GPU);

	VkBufferDeviceAddressInfo addrInfo {
	    .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
	    .buffer = ssbo.m_buffer};
	VkDeviceAddress ssboAddress = vkGetBufferDeviceAddress(m_device, &addrInfo);

	DescriptorLayoutBuilder layoutBuilder;
	layoutBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
	DescriptorLayoutInfo layoutInfo = layoutBuilder.buildForDescriptorBuffer(
	    m_device, VK_SHADER_STAGE_COMPUTE_BIT);

	DescriptorBufferAllocator descAlloc;
	descAlloc.init(m_device, &m_resourceManager, m_descriptorBufferProps, 4096);

	VkDeviceSize offset = descAlloc.allocate(layoutInfo);
	void* ptr = descAlloc.getPtrAtOffset(offset);

	DescriptorBufferWriter writer;
	writer.init(m_device, m_descriptorBufferProps);
	writer.writeStorageBuffer(ptr, layoutInfo.bindingOffsets[0], ssboAddress, 1024);

	descAlloc.destroy();
	vkDestroyDescriptorSetLayout(m_device, layoutInfo.layout, nullptr);
	m_resourceManager.destroyBuffer(ssbo);
}
