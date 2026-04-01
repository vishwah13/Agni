#include "gpu_test_fixture.hpp"
#include <Types.hpp>
#include <cstring>

// ============================================================================
// GPU_ResourceManager — tests with real Vulkan device
// ============================================================================

class GPU_ResourceManager : public GpuTestFixture {};

TEST_F(GPU_ResourceManager, CreateBuffer)
{
	AllocatedBuffer buf = m_resourceManager.createBuffer(
	    1024,
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
	    VMA_MEMORY_USAGE_GPU_ONLY);

	EXPECT_NE(buf.m_buffer, VK_NULL_HANDLE);
	EXPECT_NE(buf.m_allocation, VK_NULL_HANDLE);

	m_resourceManager.destroyBuffer(buf);
}

TEST_F(GPU_ResourceManager, BufferWriteReadback)
{
	// Create CPU-visible buffer
	AllocatedBuffer buf = m_resourceManager.createBuffer(
	    256,
	    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	    VMA_MEMORY_USAGE_CPU_TO_GPU);

	ASSERT_NE(buf.m_info.pMappedData, nullptr);

	// Write known pattern
	uint32_t pattern[4] = {0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0xAABBCCDD};
	memcpy(buf.m_info.pMappedData, pattern, sizeof(pattern));

	// Read back and verify
	auto* readback = static_cast<uint32_t*>(buf.m_info.pMappedData);
	EXPECT_EQ(readback[0], 0xDEADBEEF);
	EXPECT_EQ(readback[1], 0xCAFEBABE);
	EXPECT_EQ(readback[2], 0x12345678);
	EXPECT_EQ(readback[3], 0xAABBCCDD);

	m_resourceManager.destroyBuffer(buf);
}

TEST_F(GPU_ResourceManager, UploadMesh)
{
	std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};
	std::vector<Vertex> vertices(4);
	vertices[0].m_position = {-1, -1, 0};
	vertices[1].m_position = { 1, -1, 0};
	vertices[2].m_position = { 1,  1, 0};
	vertices[3].m_position = {-1,  1, 0};

	GPUMeshBuffers mesh = m_resourceManager.uploadMesh(indices, vertices);

	EXPECT_NE(mesh.m_vertexBuffer.m_buffer, VK_NULL_HANDLE);
	EXPECT_NE(mesh.m_vertexBufferAddress, 0u);
	EXPECT_EQ(mesh.m_indexCount, 6u);
	// Offset should be valid (page-aligned or allocator-determined)
	EXPECT_NE(mesh.m_indexAllocation.m_firstPageIndex, UINT32_MAX);

	m_resourceManager.destroyBuffer(mesh.m_vertexBuffer);
	m_resourceManager.freeIndexAllocation(mesh.m_indexAllocation);
}

TEST_F(GPU_ResourceManager, UploadMeshIndexReadback)
{
	// Upload known indices
	std::vector<uint32_t> indices = {10, 20, 30, 40, 50, 60};
	std::vector<Vertex> vertices(61); // enough for max index

	GPUMeshBuffers mesh = m_resourceManager.uploadMesh(indices, vertices);

	// Create staging buffer for readback
	const size_t readbackSize = indices.size() * sizeof(uint32_t);
	AllocatedBuffer staging = m_resourceManager.createBuffer(
	    readbackSize,
	    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	    VMA_MEMORY_USAGE_GPU_TO_CPU);

	// Copy from global index buffer to staging
	const VkDeviceSize srcOffset = mesh.m_globalIndexOffset * sizeof(uint32_t);
	m_resourceManager.immediateSubmit(
	[&](VkCommandBuffer cmd)
	{
		VkBufferCopy copy {};
		copy.srcOffset = srcOffset;
		copy.dstOffset = 0;
		copy.size      = readbackSize;
		vkCmdCopyBuffer(cmd, m_resourceManager.getGlobalIndexBuffer(),
		                staging.m_buffer, 1, &copy);
	});

	// Verify index data
	auto* readback = static_cast<uint32_t*>(staging.m_info.pMappedData);
	ASSERT_NE(readback, nullptr);
	for (size_t i = 0; i < indices.size(); i++)
		EXPECT_EQ(readback[i], indices[i]) << "Index mismatch at " << i;

	m_resourceManager.destroyBuffer(staging);
	m_resourceManager.destroyBuffer(mesh.m_vertexBuffer);
	m_resourceManager.freeIndexAllocation(mesh.m_indexAllocation);
}

TEST_F(GPU_ResourceManager, MultiMeshUpload)
{
	std::vector<Vertex> verts(10);

	std::vector<uint32_t> indicesA = {0, 1, 2};
	std::vector<uint32_t> indicesB = {3, 4, 5};
	std::vector<uint32_t> indicesC = {6, 7, 8};

	auto meshA = m_resourceManager.uploadMesh(indicesA, verts);
	auto meshB = m_resourceManager.uploadMesh(indicesB, verts);
	auto meshC = m_resourceManager.uploadMesh(indicesC, verts);

	// Each should get a different offset
	EXPECT_NE(meshA.m_globalIndexOffset, meshB.m_globalIndexOffset);
	EXPECT_NE(meshB.m_globalIndexOffset, meshC.m_globalIndexOffset);
	EXPECT_NE(meshA.m_globalIndexOffset, meshC.m_globalIndexOffset);

	// No overlap: each allocation is at least indexCount apart
	// (page allocator may add padding, so just check non-overlap)
	auto rangeOverlaps = [](uint32_t startA, uint32_t countA,
	                        uint32_t startB, uint32_t countB) -> bool
	{
		return startA < startB + countB && startB < startA + countA;
	};

	EXPECT_FALSE(rangeOverlaps(meshA.m_globalIndexOffset, meshA.m_indexCount,
	                           meshB.m_globalIndexOffset, meshB.m_indexCount));
	EXPECT_FALSE(rangeOverlaps(meshB.m_globalIndexOffset, meshB.m_indexCount,
	                           meshC.m_globalIndexOffset, meshC.m_indexCount));

	m_resourceManager.destroyBuffer(meshA.m_vertexBuffer);
	m_resourceManager.destroyBuffer(meshB.m_vertexBuffer);
	m_resourceManager.destroyBuffer(meshC.m_vertexBuffer);
	m_resourceManager.freeIndexAllocation(meshA.m_indexAllocation);
	m_resourceManager.freeIndexAllocation(meshB.m_indexAllocation);
	m_resourceManager.freeIndexAllocation(meshC.m_indexAllocation);
}

TEST_F(GPU_ResourceManager, FreeMeshAndReuse)
{
	std::vector<Vertex> verts(4);
	std::vector<uint32_t> indices = {0, 1, 2};

	// Upload and free mesh A
	auto meshA = m_resourceManager.uploadMesh(indices, verts);
	uint32_t offsetA = meshA.m_globalIndexOffset;
	m_resourceManager.destroyBuffer(meshA.m_vertexBuffer);
	m_resourceManager.freeIndexAllocation(meshA.m_indexAllocation);

	// Upload mesh B of same size — should reuse A's page
	auto meshB = m_resourceManager.uploadMesh(indices, verts);
	EXPECT_EQ(meshB.m_globalIndexOffset, offsetA);

	m_resourceManager.destroyBuffer(meshB.m_vertexBuffer);
	m_resourceManager.freeIndexAllocation(meshB.m_indexAllocation);
}

TEST_F(GPU_ResourceManager, ImmediateSubmitFillBuffer)
{
	// Create GPU buffer with transfer dst usage
	AllocatedBuffer gpuBuf = m_resourceManager.createBuffer(
	    256,
	    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	    VMA_MEMORY_USAGE_GPU_ONLY);

	// Fill with known pattern via GPU command
	const uint32_t fillValue = 0x42424242;
	m_resourceManager.immediateSubmit(
	[&](VkCommandBuffer cmd)
	{
		vkCmdFillBuffer(cmd, gpuBuf.m_buffer, 0, 256, fillValue);
	});

	// Readback via staging buffer
	AllocatedBuffer staging = m_resourceManager.createBuffer(
	    256,
	    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	    VMA_MEMORY_USAGE_GPU_TO_CPU);

	m_resourceManager.immediateSubmit(
	[&](VkCommandBuffer cmd)
	{
		VkBufferCopy copy {};
		copy.size = 256;
		vkCmdCopyBuffer(cmd, gpuBuf.m_buffer, staging.m_buffer, 1, &copy);
	});

	auto* data = static_cast<uint32_t*>(staging.m_info.pMappedData);
	ASSERT_NE(data, nullptr);
	for (int i = 0; i < 64; i++) // 256 bytes / 4 = 64 uint32s
		EXPECT_EQ(data[i], fillValue) << "Mismatch at index " << i;

	m_resourceManager.destroyBuffer(gpuBuf);
	m_resourceManager.destroyBuffer(staging);
}
