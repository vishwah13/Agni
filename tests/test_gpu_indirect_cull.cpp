#include "gpu_test_fixture.hpp"

#include <Descriptors.hpp>
#include <DescriptorBuffer.hpp>
#include <Pipelines.hpp>
#include <Initializers.hpp>
#include <Types.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

// ============================================================================
// GPU_IndirectCull — end-to-end compute shader tests
// ============================================================================

class GPU_IndirectCull : public GpuTestFixture
{
protected:
	VkDescriptorSetLayout     m_sceneDataLayout = VK_NULL_HANDLE;
	DescriptorLayoutInfo      m_sceneDataLayoutInfo;
	DescriptorBufferProperties m_descriptorBufferProps;
	VkPipeline                m_cullPipeline = VK_NULL_HANDLE;
	VkPipelineLayout          m_cullPipelineLayout = VK_NULL_HANDLE;

	void SetUp() override
	{
		GpuTestFixture::SetUp();
		if (::testing::Test::IsSkipped()) return;

		// Query descriptor buffer properties
		DescriptorBufferAllocator::queryProperties(m_physicalDevice, m_descriptorBufferProps);

		// Create scene data descriptor layout (6 bindings, matching engine)
		DescriptorLayoutBuilder layoutBuilder;
		layoutBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		layoutBuilder.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
		layoutBuilder.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		layoutBuilder.addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		layoutBuilder.addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		layoutBuilder.addBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		m_sceneDataLayoutInfo = layoutBuilder.buildForDescriptorBuffer(
		    m_device,
		    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
		m_sceneDataLayout = m_sceneDataLayoutInfo.layout;

		// Create cull compute pipeline
		auto result = ComputePipelineBuilder(m_device)
		    .setShader(resPath("shaders/slang/IndirectCull.comp.spv").c_str())
		    .addDescriptorSetLayout(m_sceneDataLayout)
		    .setPushConstantSize(sizeof(CullPushConstants))
		    .build();

		if (result.m_pipeline == VK_NULL_HANDLE)
		{
			GTEST_SKIP() << "Failed to create cull compute pipeline (shader missing?)";
			return;
		}
		m_cullPipeline       = result.m_pipeline;
		m_cullPipelineLayout = result.m_layout;
	}

	void TearDown() override
	{
		if (m_device != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_device);
			if (m_cullPipeline != VK_NULL_HANDLE)
				vkDestroyPipeline(m_device, m_cullPipeline, nullptr);
			if (m_cullPipelineLayout != VK_NULL_HANDLE)
				vkDestroyPipelineLayout(m_device, m_cullPipelineLayout, nullptr);
			if (m_sceneDataLayout != VK_NULL_HANDLE)
				vkDestroyDescriptorSetLayout(m_device, m_sceneDataLayout, nullptr);
		}
		GpuTestFixture::TearDown();
	}

	// Helper: create a view-projection matrix looking down -Z
	// Uses standard GLM perspective (GLM_FORCE_DEPTH_ZERO_TO_ONE maps Z to [0,1])
	// No reverse-Z — the shader's frustum test handles standard Vulkan clip space:
	//   clip.z < 0 (behind near) and clip.z > clip.w (beyond far)
	glm::mat4 makeViewProj(float fovDeg = 60.0f, float aspect = 16.0f / 9.0f,
	                        float nearPlane = 0.1f, float farPlane = 1000.0f,
	                        glm::vec3 eye = {0, 0, 5}, glm::vec3 target = {0, 0, 0})
	{
		glm::mat4 view = glm::lookAt(eye, target, glm::vec3(0, 1, 0));
		glm::mat4 proj = glm::perspective(glm::radians(fovDeg), aspect, nearPlane, farPlane);
		// Vulkan Y-flip
		proj[1][1] *= -1.0f;
		return proj * view;
	}

	struct CullTestResult
	{
		uint32_t visibleCount;
		std::vector<VkDrawIndexedIndirectCommand> commands;
		std::vector<GPUDrawData> drawData;
	};

	// Helper: run the cull shader with given bounds and view-projection
	CullTestResult runCull(const std::vector<GPUBoundsData>& bounds,
	                       const glm::mat4& viewproj)
	{
		const uint32_t drawCount = static_cast<uint32_t>(bounds.size());

		// Create scene data UBO with viewproj
		GPUSceneData sceneData {};
		sceneData.m_viewproj = viewproj;

		AllocatedBuffer sceneUBO = m_resourceManager.createBuffer(
		    sizeof(GPUSceneData),
		    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
		    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
		    VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT,
		    VMA_MEMORY_USAGE_CPU_TO_GPU);
		memcpy(sceneUBO.m_info.pMappedData, &sceneData, sizeof(sceneData));

		// Create descriptor buffer for scene data
		DescriptorBufferAllocator descAlloc;
		descAlloc.init(m_device, &m_resourceManager,
		               m_descriptorBufferProps, 4096);

		VkDeviceSize descOffset = descAlloc.allocate(m_sceneDataLayoutInfo);
		void* descPtr = descAlloc.getPtrAtOffset(descOffset);

		// Get BDA for scene UBO
		VkBufferDeviceAddressInfo uboAddrInfo {
		    .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
		    .buffer = sceneUBO.m_buffer};
		VkDeviceAddress sceneUBOAddress = vkGetBufferDeviceAddress(m_device, &uboAddrInfo);

		DescriptorBufferWriter writer;
		writer.init(m_device, m_descriptorBufferProps);
		writer.writeUniformBuffer(descPtr, m_sceneDataLayoutInfo.bindingOffsets[0],
		                          sceneUBOAddress, sizeof(GPUSceneData));

		// Create input bounds buffer
		AllocatedBuffer boundsBuffer = m_resourceManager.createBuffer(
		    drawCount * sizeof(GPUBoundsData),
		    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		    VMA_MEMORY_USAGE_CPU_TO_GPU);
		memcpy(boundsBuffer.m_info.pMappedData, bounds.data(), drawCount * sizeof(GPUBoundsData));

		// Create input indirect commands (all with instanceCount=1)
		std::vector<VkDrawIndexedIndirectCommand> inputCmds(drawCount);
		for (uint32_t i = 0; i < drawCount; i++)
		{
			inputCmds[i].indexCount    = 36;
			inputCmds[i].instanceCount = 1;
			inputCmds[i].firstIndex    = i * 36;
			inputCmds[i].vertexOffset  = 0;
			inputCmds[i].firstInstance = i;
		}

		AllocatedBuffer indirectIn = m_resourceManager.createBuffer(
		    drawCount * sizeof(VkDrawIndexedIndirectCommand),
		    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		    VMA_MEMORY_USAGE_CPU_TO_GPU);
		memcpy(indirectIn.m_info.pMappedData, inputCmds.data(),
		       drawCount * sizeof(VkDrawIndexedIndirectCommand));

		// Create input draw data
		std::vector<GPUDrawData> inputDrawData(drawCount);
		for (uint32_t i = 0; i < drawCount; i++)
		{
			inputDrawData[i].m_worldMatrix = bounds[i].m_worldMatrix;
			inputDrawData[i].m_materialIndex = i;
		}

		AllocatedBuffer drawDataIn = m_resourceManager.createBuffer(
		    drawCount * sizeof(GPUDrawData),
		    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		    VMA_MEMORY_USAGE_CPU_TO_GPU);
		memcpy(drawDataIn.m_info.pMappedData, inputDrawData.data(),
		       drawCount * sizeof(GPUDrawData));

		// Create output buffers (GPU-only, with TRANSFER_SRC for readback)
		AllocatedBuffer indirectOut = m_resourceManager.createBuffer(
		    drawCount * sizeof(VkDrawIndexedIndirectCommand),
		    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
		    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		    VMA_MEMORY_USAGE_GPU_ONLY);

		AllocatedBuffer drawDataOut = m_resourceManager.createBuffer(
		    drawCount * sizeof(GPUDrawData),
		    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
		    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		    VMA_MEMORY_USAGE_GPU_ONLY);

		AllocatedBuffer drawCountBuf = m_resourceManager.createBuffer(
		    sizeof(uint32_t),
		    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
		    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		    VMA_MEMORY_USAGE_GPU_ONLY);

		// Get BDAs
		auto getBDA = [&](VkBuffer buf) -> VkDeviceAddress
		{
			VkBufferDeviceAddressInfo info {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = buf};
			return vkGetBufferDeviceAddress(m_device, &info);
		};

		// Dispatch
		m_resourceManager.immediateSubmit(
		[&](VkCommandBuffer cmd)
		{
			// Reset draw count to 0
			vkCmdFillBuffer(cmd, drawCountBuf.m_buffer, 0, sizeof(uint32_t), 0);
			vkinit::memoryBarrier(cmd,
			    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cullPipeline);

			// Bind descriptor buffer
			VkDescriptorBufferBindingInfoEXT bufBinding {};
			bufBinding.sType   = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
			bufBinding.address = descAlloc.getDeviceAddress();
			bufBinding.usage   = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
			                     VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
			vkCmdBindDescriptorBuffersEXT(cmd, 1, &bufBinding);

			uint32_t bufferIndex = 0;
			vkCmdSetDescriptorBufferOffsetsEXT(cmd,
			    VK_PIPELINE_BIND_POINT_COMPUTE, m_cullPipelineLayout,
			    0, 1, &bufferIndex, &descOffset);

			CullPushConstants pc {};
			pc.m_boundsBufferPtr      = getBDA(boundsBuffer.m_buffer);
			pc.m_indirectBufferInPtr  = getBDA(indirectIn.m_buffer);
			pc.m_indirectBufferOutPtr = getBDA(indirectOut.m_buffer);
			pc.m_drawDataInPtr        = getBDA(drawDataIn.m_buffer);
			pc.m_drawDataOutPtr       = getBDA(drawDataOut.m_buffer);
			pc.m_drawCountPtr         = getBDA(drawCountBuf.m_buffer);
			pc.m_drawCount            = drawCount;
			pc.m_hizEnabled           = 0; // frustum only
			pc.m_hizWidth             = 0;
			pc.m_hizHeight            = 0;

			vkCmdPushConstants(cmd, m_cullPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
			                   0, sizeof(CullPushConstants), &pc);

			vkCmdDispatch(cmd, (drawCount + 255) / 256, 1, 1);

			// Barrier: compute -> host read
			vkinit::memoryBarrier(cmd,
			    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
			    VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
		});

		// Readback draw count
		AllocatedBuffer countStaging = m_resourceManager.createBuffer(
		    sizeof(uint32_t), VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
		AllocatedBuffer cmdStaging = m_resourceManager.createBuffer(
		    drawCount * sizeof(VkDrawIndexedIndirectCommand),
		    VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
		AllocatedBuffer dataStaging = m_resourceManager.createBuffer(
		    drawCount * sizeof(GPUDrawData),
		    VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);

		m_resourceManager.immediateSubmit(
		[&](VkCommandBuffer cmd)
		{
			VkBufferCopy copy {};
			copy.size = sizeof(uint32_t);
			vkCmdCopyBuffer(cmd, drawCountBuf.m_buffer, countStaging.m_buffer, 1, &copy);

			copy.size = drawCount * sizeof(VkDrawIndexedIndirectCommand);
			vkCmdCopyBuffer(cmd, indirectOut.m_buffer, cmdStaging.m_buffer, 1, &copy);

			copy.size = drawCount * sizeof(GPUDrawData);
			vkCmdCopyBuffer(cmd, drawDataOut.m_buffer, dataStaging.m_buffer, 1, &copy);
		});

		CullTestResult result;
		result.visibleCount = *static_cast<uint32_t*>(countStaging.m_info.pMappedData);

		auto* cmdsOut = static_cast<VkDrawIndexedIndirectCommand*>(cmdStaging.m_info.pMappedData);
		result.commands.assign(cmdsOut, cmdsOut + result.visibleCount);

		auto* dataOut = static_cast<GPUDrawData*>(dataStaging.m_info.pMappedData);
		result.drawData.assign(dataOut, dataOut + result.visibleCount);

		// Cleanup
		m_resourceManager.destroyBuffer(sceneUBO);
		m_resourceManager.destroyBuffer(boundsBuffer);
		m_resourceManager.destroyBuffer(indirectIn);
		m_resourceManager.destroyBuffer(drawDataIn);
		m_resourceManager.destroyBuffer(indirectOut);
		m_resourceManager.destroyBuffer(drawDataOut);
		m_resourceManager.destroyBuffer(drawCountBuf);
		m_resourceManager.destroyBuffer(countStaging);
		m_resourceManager.destroyBuffer(cmdStaging);
		m_resourceManager.destroyBuffer(dataStaging);
		descAlloc.destroy();

		return result;
	}
};

// --- Tests ---

TEST_F(GPU_IndirectCull, AllVisible)
{
	// Place 3 objects directly in front of camera — all should pass frustum test
	glm::mat4 vp = makeViewProj();

	std::vector<GPUBoundsData> bounds(3);
	for (int i = 0; i < 3; i++)
	{
		bounds[i].m_aabbMin     = glm::vec3(-0.5f);
		bounds[i].m_aabbMax     = glm::vec3(0.5f);
		bounds[i].m_worldMatrix = glm::translate(glm::mat4(1.0f),
		                          glm::vec3(static_cast<float>(i) - 1.0f, 0.0f, 0.0f));
	}

	auto result = runCull(bounds, vp);
	EXPECT_EQ(result.visibleCount, 3u);
}

TEST_F(GPU_IndirectCull, AllCulled)
{
	// Place 3 objects far behind the camera — all should be frustum culled
	glm::mat4 vp = makeViewProj();

	std::vector<GPUBoundsData> bounds(3);
	for (int i = 0; i < 3; i++)
	{
		bounds[i].m_aabbMin     = glm::vec3(-0.5f);
		bounds[i].m_aabbMax     = glm::vec3(0.5f);
		bounds[i].m_worldMatrix = glm::translate(glm::mat4(1.0f),
		                          glm::vec3(0.0f, 0.0f, 100.0f)); // behind camera at z=5 looking at z=0
	}

	auto result = runCull(bounds, vp);
	EXPECT_EQ(result.visibleCount, 0u);
}

TEST_F(GPU_IndirectCull, MixedVisibility)
{
	// Camera at z=5 looking at origin
	glm::mat4 vp = makeViewProj();

	std::vector<GPUBoundsData> bounds(4);

	// Object 0: in front of camera (visible)
	bounds[0].m_aabbMin     = glm::vec3(-0.5f);
	bounds[0].m_aabbMax     = glm::vec3(0.5f);
	bounds[0].m_worldMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0));

	// Object 1: far to the right (outside frustum)
	bounds[1].m_aabbMin     = glm::vec3(-0.5f);
	bounds[1].m_aabbMax     = glm::vec3(0.5f);
	bounds[1].m_worldMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(500, 0, 0));

	// Object 2: in front of camera (visible)
	bounds[2].m_aabbMin     = glm::vec3(-0.5f);
	bounds[2].m_aabbMax     = glm::vec3(0.5f);
	bounds[2].m_worldMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, -1));

	// Object 3: behind camera (culled)
	bounds[3].m_aabbMin     = glm::vec3(-0.5f);
	bounds[3].m_aabbMax     = glm::vec3(0.5f);
	bounds[3].m_worldMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 50));

	auto result = runCull(bounds, vp);
	EXPECT_EQ(result.visibleCount, 2u);
}

TEST_F(GPU_IndirectCull, CompactionPreservesDrawData)
{
	// Verify that compacted draw data matches the input for surviving draws
	glm::mat4 vp = makeViewProj();

	std::vector<GPUBoundsData> bounds(3);

	// Object 0: visible
	bounds[0].m_aabbMin     = glm::vec3(-1);
	bounds[0].m_aabbMax     = glm::vec3(1);
	bounds[0].m_worldMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 0));

	// Object 1: culled (behind camera)
	bounds[1].m_aabbMin     = glm::vec3(-1);
	bounds[1].m_aabbMax     = glm::vec3(1);
	bounds[1].m_worldMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, 100));

	// Object 2: visible
	bounds[2].m_aabbMin     = glm::vec3(-1);
	bounds[2].m_aabbMax     = glm::vec3(1);
	bounds[2].m_worldMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(1, 0, -1));

	auto result = runCull(bounds, vp);
	EXPECT_EQ(result.visibleCount, 2u);

	// Verify compacted commands have correct firstInstance (slot index)
	for (uint32_t i = 0; i < result.visibleCount; i++)
		EXPECT_EQ(result.commands[i].firstInstance, i);

	// Verify draw data material indices match original visible objects
	// Object 0 has materialIndex=0, Object 2 has materialIndex=2
	// Order in compacted output depends on atomic ordering (nondeterministic)
	// but both should be present
	std::vector<uint32_t> matIndices;
	for (uint32_t i = 0; i < result.visibleCount; i++)
		matIndices.push_back(result.drawData[i].m_materialIndex);

	std::sort(matIndices.begin(), matIndices.end());
	EXPECT_EQ(matIndices[0], 0u); // Object 0
	EXPECT_EQ(matIndices[1], 2u); // Object 2
}

TEST_F(GPU_IndirectCull, LargeObjectInFrustum)
{
	// A very large object centered at origin should be visible
	glm::mat4 vp = makeViewProj();

	std::vector<GPUBoundsData> bounds(1);
	bounds[0].m_aabbMin     = glm::vec3(-100);
	bounds[0].m_aabbMax     = glm::vec3(100);
	bounds[0].m_worldMatrix = glm::mat4(1.0f);

	auto result = runCull(bounds, vp);
	EXPECT_EQ(result.visibleCount, 1u);
}

TEST_F(GPU_IndirectCull, ObjectAtFrustumEdge)
{
	// Object at the far edge of the frustum — should still be visible
	glm::mat4 vp = makeViewProj(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);

	std::vector<GPUBoundsData> bounds(1);
	bounds[0].m_aabbMin     = glm::vec3(-1);
	bounds[0].m_aabbMax     = glm::vec3(1);
	bounds[0].m_worldMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0, -990));

	auto result = runCull(bounds, vp);
	EXPECT_EQ(result.visibleCount, 1u);
}
