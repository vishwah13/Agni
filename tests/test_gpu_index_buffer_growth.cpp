#include "gpu_test_fixture.hpp"

#include <Types.hpp>
#include <cstring>
#include <set>

// ============================================================================
// GPU_IndexBufferGrowth — stress tests for global index buffer
// ============================================================================

class GPU_IndexBufferGrowth : public GpuTestFixture {};

TEST_F(GPU_IndexBufferGrowth, ManySmallMeshes)
{
	// Upload 100 small meshes, verify each gets unique non-overlapping offsets
	constexpr int NUM_MESHES = 100;
	constexpr int INDICES_PER_MESH = 36; // a cube

	std::vector<Vertex> verts(8);
	std::vector<uint32_t> indices(INDICES_PER_MESH);
	for (uint32_t i = 0; i < INDICES_PER_MESH; i++)
		indices[i] = i % 8;

	std::vector<GPUMeshBuffers> meshes(NUM_MESHES);
	for (int i = 0; i < NUM_MESHES; i++)
		meshes[i] = m_resourceManager.uploadMesh(indices, verts);

	// Verify all offsets are unique
	std::set<uint32_t> offsets;
	for (int i = 0; i < NUM_MESHES; i++)
	{
		EXPECT_NE(meshes[i].m_globalIndexOffset, UINT32_MAX)
		    << "Mesh " << i << " allocation failed";
		auto [_, inserted] = offsets.insert(meshes[i].m_globalIndexOffset);
		EXPECT_TRUE(inserted) << "Duplicate offset at mesh " << i;
	}

	// Cleanup
	for (auto& mesh : meshes)
	{
		m_resourceManager.destroyBuffer(mesh.m_vertexBuffer);
		m_resourceManager.freeIndexAllocation(mesh.m_indexAllocation);
	}
}

TEST_F(GPU_IndexBufferGrowth, LargeMeshMultiPage)
{
	// Upload a mesh large enough to span multiple pages (16K+ indices)
	constexpr uint32_t LARGE_INDEX_COUNT = 50000; // ~3 pages

	std::vector<Vertex> verts(100);
	std::vector<uint32_t> indices(LARGE_INDEX_COUNT);
	for (uint32_t i = 0; i < LARGE_INDEX_COUNT; i++)
		indices[i] = i % 100;

	auto mesh = m_resourceManager.uploadMesh(indices, verts);

	EXPECT_NE(mesh.m_indexAllocation.m_firstPageIndex, UINT32_MAX);
	EXPECT_GE(mesh.m_indexAllocation.m_pageCount, 3u); // 50K / 16K = 4 pages (ceil)
	EXPECT_EQ(mesh.m_indexCount, LARGE_INDEX_COUNT);

	// Readback and verify
	const size_t readbackSize = LARGE_INDEX_COUNT * sizeof(uint32_t);
	AllocatedBuffer staging = m_resourceManager.createBuffer(
	    readbackSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);

	m_resourceManager.immediateSubmit(
	[&](VkCommandBuffer cmd)
	{
		VkBufferCopy copy {};
		copy.srcOffset = mesh.m_globalIndexOffset * sizeof(uint32_t);
		copy.size      = readbackSize;
		vkCmdCopyBuffer(cmd, m_resourceManager.getGlobalIndexBuffer(),
		                staging.m_buffer, 1, &copy);
	});

	auto* readback = static_cast<uint32_t*>(staging.m_info.pMappedData);
	for (uint32_t i = 0; i < LARGE_INDEX_COUNT; i++)
		EXPECT_EQ(readback[i], i % 100) << "Mismatch at index " << i;

	m_resourceManager.destroyBuffer(staging);
	m_resourceManager.destroyBuffer(mesh.m_vertexBuffer);
	m_resourceManager.freeIndexAllocation(mesh.m_indexAllocation);
}

TEST_F(GPU_IndexBufferGrowth, AllocFreeReallocCycle)
{
	// Simulate editor workflow: load, unload, reload many times
	constexpr int CYCLES = 20;
	constexpr uint32_t INDEX_COUNT = 1000;

	std::vector<Vertex> verts(100);
	std::vector<uint32_t> indices(INDEX_COUNT);
	for (uint32_t i = 0; i < INDEX_COUNT; i++)
		indices[i] = i % 100;

	uint32_t firstOffset = UINT32_MAX;

	for (int cycle = 0; cycle < CYCLES; cycle++)
	{
		auto mesh = m_resourceManager.uploadMesh(indices, verts);
		EXPECT_NE(mesh.m_indexAllocation.m_firstPageIndex, UINT32_MAX)
		    << "Allocation failed on cycle " << cycle;

		if (cycle == 0)
			firstOffset = mesh.m_globalIndexOffset;
		else
			EXPECT_EQ(mesh.m_globalIndexOffset, firstOffset)
			    << "Page not reused on cycle " << cycle;

		m_resourceManager.destroyBuffer(mesh.m_vertexBuffer);
		m_resourceManager.freeIndexAllocation(mesh.m_indexAllocation);
	}
}

TEST_F(GPU_IndexBufferGrowth, InterleavedAllocFree)
{
	// Allocate A, B, C. Free B. Allocate D (should reuse B's pages).
	// Free A, C. Allocate E large enough to span A+B+C's freed space.
	std::vector<Vertex> verts(10);
	std::vector<uint32_t> indices(100);
	for (uint32_t i = 0; i < 100; i++)
		indices[i] = i % 10;

	auto meshA = m_resourceManager.uploadMesh(indices, verts);
	auto meshB = m_resourceManager.uploadMesh(indices, verts);
	auto meshC = m_resourceManager.uploadMesh(indices, verts);

	uint32_t offsetB = meshB.m_globalIndexOffset;

	// Free B
	m_resourceManager.destroyBuffer(meshB.m_vertexBuffer);
	m_resourceManager.freeIndexAllocation(meshB.m_indexAllocation);

	// D should reuse B's page
	auto meshD = m_resourceManager.uploadMesh(indices, verts);
	EXPECT_EQ(meshD.m_globalIndexOffset, offsetB);

	// Cleanup
	m_resourceManager.destroyBuffer(meshA.m_vertexBuffer);
	m_resourceManager.freeIndexAllocation(meshA.m_indexAllocation);
	m_resourceManager.destroyBuffer(meshC.m_vertexBuffer);
	m_resourceManager.freeIndexAllocation(meshC.m_indexAllocation);
	m_resourceManager.destroyBuffer(meshD.m_vertexBuffer);
	m_resourceManager.freeIndexAllocation(meshD.m_indexAllocation);
}
