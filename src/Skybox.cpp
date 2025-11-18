#include <AgniEngine.hpp>
#include <Images.hpp>
#include <Initializers.hpp>
#include <Pipelines.hpp>
#include <ResourceManager.hpp>
#include <Skybox.hpp>
#include <VulkanTools.hpp>

#include <fmt/core.h>
#include <stb_image.h>
#include <cmath>

void Skybox::init(AgniEngine*                       engine,
                  const std::array<std::string, 6>& cubemapFaces)
{
	// Create cubemap image
	m_cubemapImage = createCubemap(
	    engine->m_resourceManager,
	    engine->m_device,
	    cubemapFaces,
	    VK_FORMAT_R8G8B8A8_UNORM,
	    VK_IMAGE_USAGE_SAMPLED_BIT,
	    false);

	// Create sampler for cubemap
	VkSamplerCreateInfo cubemapSamplerInfo = {
	.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
	cubemapSamplerInfo.magFilter    = VK_FILTER_LINEAR;
	cubemapSamplerInfo.minFilter    = VK_FILTER_LINEAR;
	cubemapSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	cubemapSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	cubemapSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

	vkCreateSampler(
	engine->m_device, &cubemapSamplerInfo, nullptr, &m_cubemapSampler);

	// Create cube mesh
	createCubeMesh(engine);

	// Create material
	createMaterial(engine);
}

void Skybox::buildPipelines(AgniEngine* engine)
{
	VkShaderModule skyFragShader;
	if (!vkutil::loadShaderModule(
	    "../../shaders/glsl/skybox.frag.spv", engine->m_device, &skyFragShader))
	{
		fmt::println("Error when building the skybox fragment shader module");
	}

	VkShaderModule skyVertexShader;
	if (!vkutil::loadShaderModule("../../shaders/glsl/skybox.vert.spv",
	                              engine->m_device,
	                              &skyVertexShader))
	{
		fmt::println("Error when building the skybox vertex shader module");
	}

	VkPushConstantRange matrixRange {};
	matrixRange.offset     = 0;
	matrixRange.size       = sizeof(SkyBoxPushConstants);
	matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	DescriptorLayoutBuilder layoutBuilder;
	layoutBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	m_skyboxMaterialLayout = layoutBuilder.build(engine->m_device,
	                                             VK_SHADER_STAGE_VERTEX_BIT |
	                                             VK_SHADER_STAGE_FRAGMENT_BIT);

	VkDescriptorSetLayout layouts[] = {engine->m_gpuSceneDataDescriptorLayout,
	                                   m_skyboxMaterialLayout};

	VkPipelineLayoutCreateInfo mesh_layout_info =
	vkinit::pipelineLayoutCreateInfo();
	mesh_layout_info.setLayoutCount         = 2;
	mesh_layout_info.pSetLayouts            = layouts;
	mesh_layout_info.pPushConstantRanges    = &matrixRange;
	mesh_layout_info.pushConstantRangeCount = 1;

	VkPipelineLayout newLayout;
	VK_CHECK(vkCreatePipelineLayout(
	engine->m_device, &mesh_layout_info, nullptr, &newLayout));

	m_skyboxPipeline.m_layout = newLayout;

	// build the stage-create-info for both vertex and fragment stages. This
	// lets the pipeline know the shader modules per stage
	PipelineBuilder pipelineBuilder;
	pipelineBuilder.setShaders(skyVertexShader, skyFragShader);
	pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
	pipelineBuilder.setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
	pipelineBuilder.enableMultisampling(engine->m_msaaSamples);
	pipelineBuilder.disableBlending();
	// turning off depth buffer writes for skybox, but enable depth test with
	// reversed-Z
	pipelineBuilder.enableDepthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

	// render format
	pipelineBuilder.setColorAttachmentFormat(
	engine->m_msaaColorImage.m_imageFormat);
	pipelineBuilder.setDepthFormat(engine->m_depthImage.m_imageFormat);

	// use the triangle layout we created
	pipelineBuilder.m_pipelineLayout = newLayout;

	// finally build the pipeline
	m_skyboxPipeline.m_pipeline =
	pipelineBuilder.buildPipeline(engine->m_device);

	vkDestroyShaderModule(engine->m_device, skyFragShader, nullptr);
	vkDestroyShaderModule(engine->m_device, skyVertexShader, nullptr);
}

void Skybox::cleanup(AgniEngine* engine)
{
	// Cleanup pipeline resources
	clearPipelineResources(engine->m_device);

	// Cleanup mesh buffers
	engine->m_resourceManager.destroyBuffer(m_meshBuffers.m_indexBuffer);
	engine->m_resourceManager.destroyBuffer(m_meshBuffers.m_vertexBuffer);

	// Cleanup material
	if (m_skyboxMaterial)
	{
		delete m_skyboxMaterial;
		m_skyboxMaterial = nullptr;
	}

	// Cleanup cubemap resources
	vkDestroySampler(engine->m_device, m_cubemapSampler, nullptr);
	engine->m_resourceManager.destroyImage(m_cubemapImage);
}

void Skybox::draw(VkCommandBuffer cmd,
                  VkDescriptorSet sceneDescriptor,
                  VkExtent2D      drawExtent)
{
	// Bind skybox pipeline
	vkCmdBindPipeline(
	cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyboxPipeline.m_pipeline);

	// Set dynamic viewport and scissor
	VkViewport viewport = {};
	viewport.x          = 0;
	viewport.y          = 0;
	viewport.width      = static_cast<float>(drawExtent.width);
	viewport.height     = static_cast<float>(drawExtent.height);
	viewport.minDepth   = 0.f;
	viewport.maxDepth   = 1.f;

	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor      = {};
	scissor.offset.x      = 0;
	scissor.offset.y      = 0;
	scissor.extent.width  = drawExtent.width;
	scissor.extent.height = drawExtent.height;

	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// Bind scene descriptor (set 0)
	vkCmdBindDescriptorSets(cmd,
	                        VK_PIPELINE_BIND_POINT_GRAPHICS,
	                        m_skyboxPipeline.m_layout,
	                        0,
	                        1,
	                        &sceneDescriptor,
	                        0,
	                        nullptr);

	// Bind skybox material descriptor (set 1)
	vkCmdBindDescriptorSets(cmd,
	                        VK_PIPELINE_BIND_POINT_GRAPHICS,
	                        m_skyboxPipeline.m_layout,
	                        1,
	                        1,
	                        &m_skyboxMaterial->m_materialSet,
	                        0,
	                        nullptr);

	// Bind index buffer
	vkCmdBindIndexBuffer(
	cmd, m_meshBuffers.m_indexBuffer.m_buffer, 0, VK_INDEX_TYPE_UINT32);

	// Push constants for vertex buffer address
	SkyBoxPushConstants skyboxPush;
	skyboxPush.m_vertexBufferAddress = m_meshBuffers.m_vertexBufferAddress;

	vkCmdPushConstants(cmd,
	                   m_skyboxPipeline.m_layout,
	                   VK_SHADER_STAGE_VERTEX_BIT,
	                   0,
	                   sizeof(SkyBoxPushConstants),
	                   &skyboxPush);

	// Draw skybox
	vkCmdDrawIndexed(cmd, m_indexCount, 1, m_firstIndex, 0, 0);
}

void Skybox::createCubeMesh(AgniEngine* engine)
{
	// Create cube mesh for skybox (large cube around camera)
	std::vector<Vertex> cubeVertices = {
	// Positions are used as texture coordinates in the shader
	// Front face (+Z)
	{{-1.0f, -1.0f, 1.0f}, 0, {0, 0, 1}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{1.0f, -1.0f, 1.0f}, 0, {0, 0, 1}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{1.0f, 1.0f, 1.0f}, 0, {0, 0, 1}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{-1.0f, 1.0f, 1.0f}, 0, {0, 0, 1}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	// Back face (-Z)
	{{1.0f, -1.0f, -1.0f}, 0, {0, 0, -1}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{-1.0f, -1.0f, -1.0f}, 0, {0, 0, -1}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{-1.0f, 1.0f, -1.0f}, 0, {0, 0, -1}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{1.0f, 1.0f, -1.0f}, 0, {0, 0, -1}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	// Top face (+Y)
	{{-1.0f, 1.0f, 1.0f}, 0, {0, 1, 0}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{1.0f, 1.0f, 1.0f}, 0, {0, 1, 0}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{1.0f, 1.0f, -1.0f}, 0, {0, 1, 0}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{-1.0f, 1.0f, -1.0f}, 0, {0, 1, 0}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	// Bottom face (-Y)
	{{-1.0f, -1.0f, -1.0f}, 0, {0, -1, 0}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{1.0f, -1.0f, -1.0f}, 0, {0, -1, 0}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{1.0f, -1.0f, 1.0f}, 0, {0, -1, 0}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{-1.0f, -1.0f, 1.0f}, 0, {0, -1, 0}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	// Right face (+X)
	{{1.0f, -1.0f, 1.0f}, 0, {1, 0, 0}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{1.0f, -1.0f, -1.0f}, 0, {1, 0, 0}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{1.0f, 1.0f, -1.0f}, 0, {1, 0, 0}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{1.0f, 1.0f, 1.0f}, 0, {1, 0, 0}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	// Left face (-X)
	{{-1.0f, -1.0f, -1.0f}, 0, {-1, 0, 0}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{-1.0f, -1.0f, 1.0f}, 0, {-1, 0, 0}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{-1.0f, 1.0f, 1.0f}, 0, {-1, 0, 0}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	{{-1.0f, 1.0f, -1.0f}, 0, {-1, 0, 0}, 0, {1, 1, 1, 1}, {1, 1, 1, 1}},
	};

	std::vector<uint32_t> cubeIndices = {
	0,  1,  2,  2,  3,  0,  // Front
	4,  5,  6,  6,  7,  4,  // Back
	8,  9,  10, 10, 11, 8,  // Top
	12, 13, 14, 14, 15, 12, // Bottom
	16, 17, 18, 18, 19, 16, // Right
	20, 21, 22, 22, 23, 20  // Left
	};

	m_meshBuffers = engine->uploadMesh(cubeIndices, cubeVertices);
	m_indexCount  = static_cast<uint32_t>(cubeIndices.size());
	m_firstIndex  = 0;
}

void Skybox::createMaterial(AgniEngine* engine)
{
	// Create skybox material resources
	MaterialResources skyboxResources;
	skyboxResources.m_cubemapImage   = m_cubemapImage;
	skyboxResources.m_cubemapSampler = m_cubemapSampler;

	// Write skybox material
	m_skyboxMaterial = new MaterialInstance(writeMaterial(
	engine->m_device, skyboxResources, engine->m_globalDescriptorAllocator));
}

void Skybox::clearPipelineResources(VkDevice device)
{
	vkDestroyDescriptorSetLayout(device, m_skyboxMaterialLayout, nullptr);
	vkDestroyPipelineLayout(device, m_skyboxPipeline.m_layout, nullptr);
	vkDestroyPipeline(device, m_skyboxPipeline.m_pipeline, nullptr);
}

MaterialInstance
Skybox::writeMaterial(VkDevice                     device,
                      const MaterialResources&     resources,
                      DescriptorAllocatorGrowable& descriptorAllocator)
{
	MaterialInstance matData;
	matData.m_passType = MaterialPass::Other;

	matData.m_materialSet =
	descriptorAllocator.allocate(device, m_skyboxMaterialLayout);


	m_writer.clear();
	m_writer.writeImage(/*binding*/ 0,
	                    resources.m_cubemapImage.m_imageView,
	                    resources.m_cubemapSampler,
	                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	// use the materialSet and update it here.
	m_writer.updateSet(device, matData.m_materialSet);

	return matData;
}

AllocatedImage Skybox::createCubemap(
    ResourceManager&                  resourceManager,
    VkDevice                          device,
    const std::array<std::string, 6>& faceFiles,
    VkFormat                          format,
    VkImageUsageFlags                 usage,
    bool                              mipmapped)
{
	// Load all 6 faces
	std::array<stbi_uc*, 6> faceData;
	int                     width, height, channels;

	// Load first face to get dimensions
	faceData[0] =
	    stbi_load(faceFiles[0].c_str(), &width, &height, &channels, 4);
	if (!faceData[0])
	{
		fmt::println("Failed to load cubemap face: {}", faceFiles[0]);
		throw std::runtime_error("Failed to load cubemap face");
	}

	// Load remaining faces (ensure they match dimensions)
	for (int i = 1; i < 6; i++)
	{
		int w, h, c;
		faceData[i] = stbi_load(faceFiles[i].c_str(), &w, &h, &c, 4);
		if (!faceData[i] || w != width || h != height)
		{
			fmt::println(
			    "Failed to load or dimension mismatch for cubemap face: {}",
			    faceFiles[i]);
			// Clean up loaded faces
			for (int j = 0; j <= i; j++)
			{
				if (faceData[j])
					stbi_image_free(faceData[j]);
			}
			throw std::runtime_error("Failed to load cubemap face");
		}
	}

	// Calculate total size for all 6 faces
	size_t faceSize  = width * height * 4; // 4 bytes per pixel (RGBA)
	size_t totalSize = faceSize * 6;

	// Create staging buffer
	AllocatedBuffer uploadBuffer = resourceManager.createBuffer(
	    totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	// Copy all faces into staging buffer
	uint8_t* bufferData = (uint8_t*) uploadBuffer.m_info.pMappedData;
	for (int i = 0; i < 6; i++)
	{
		memcpy(bufferData + (i * faceSize), faceData[i], faceSize);
		stbi_image_free(faceData[i]);
	}

	// Create cubemap image
	AllocatedImage cubemap;
	cubemap.m_imageFormat = format;
	cubemap.m_imageExtent = {(uint32_t) width, (uint32_t) height, 1};

	VkImageCreateInfo img_info = vkinit::imageCreateInfo(
	    format,
	    usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
	    cubemap.m_imageExtent);

	// Set cubemap-specific flags
	img_info.arrayLayers = 6;
	img_info.flags       = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

	if (mipmapped)
	{
		img_info.mipLevels =
		    static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) +
		    1;
	}

	// Allocate image on GPU
	VmaAllocationCreateInfo allocInfo = {};
	allocInfo.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;
	allocInfo.requiredFlags =
	    VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VK_CHECK(vmaCreateImage(resourceManager.getAllocator(),
	                        &img_info,
	                        &allocInfo,
	                        &cubemap.m_image,
	                        &cubemap.m_allocation,
	                        nullptr));

	// Create image view for cubemap
	VkImageViewCreateInfo view_info = vkinit::imageViewCreateInfo(
	    format, cubemap.m_image, VK_IMAGE_ASPECT_COLOR_BIT);
	view_info.viewType                    = VK_IMAGE_VIEW_TYPE_CUBE;
	view_info.subresourceRange.layerCount = 6;
	view_info.subresourceRange.levelCount = img_info.mipLevels;

	VK_CHECK(
	    vkCreateImageView(device, &view_info, nullptr, &cubemap.m_imageView));

	// Upload data to GPU
	resourceManager.immediateSubmit(
	    [&](VkCommandBuffer cmd)
	    {
		    // Transition to transfer dst
		    VkImageMemoryBarrier2 barrier {
		        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
		    barrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		    barrier.srcAccessMask = 0;
		    barrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		    barrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
		    barrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		    barrier.image         = cubemap.m_image;
		    barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		    barrier.subresourceRange.baseMipLevel   = 0;
		    barrier.subresourceRange.levelCount     = img_info.mipLevels;
		    barrier.subresourceRange.baseArrayLayer = 0;
		    barrier.subresourceRange.layerCount     = 6;

		    VkDependencyInfo depInfo {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
		    depInfo.imageMemoryBarrierCount = 1;
		    depInfo.pImageMemoryBarriers    = &barrier;
		    vkCmdPipelineBarrier2(cmd, &depInfo);

		    // Copy each face from buffer to image
		    for (uint32_t face = 0; face < 6; face++)
		    {
			    VkBufferImageCopy copyRegion           = {};
			    copyRegion.bufferOffset                = face * faceSize;
			    copyRegion.bufferRowLength             = 0;
			    copyRegion.bufferImageHeight           = 0;
			    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			    copyRegion.imageSubresource.mipLevel   = 0;
			    copyRegion.imageSubresource.baseArrayLayer = face;
			    copyRegion.imageSubresource.layerCount     = 1;
			    copyRegion.imageExtent = {(uint32_t) width, (uint32_t) height, 1};

			    vkCmdCopyBufferToImage(cmd,
			                           uploadBuffer.m_buffer,
			                           cubemap.m_image,
			                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			                           1,
			                           &copyRegion);
		    }

		    // Transition to shader read
		    barrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		    barrier.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
		    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
		    barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		    barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		    vkCmdPipelineBarrier2(cmd, &depInfo);
	    });

	resourceManager.destroyBuffer(uploadBuffer);

	return cubemap;
}
