#include <Renderer.hpp>

#include <AgniEngine.hpp>
#include <Components.hpp>
#include <Debug.hpp>
#include <ECS/World.hpp>
#include <Images.hpp>
#include <Initializers.hpp>
#include <Pipelines.hpp>
#include <VulkanTools.hpp>

#include <algorithm>
#include <chrono>


#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

void Renderer::init(VkDevice                          device,
                    ResourceManager*                  resourceManager,
                    SwapchainManager*                 swapchainManager,
                    Camera*                           camera,
                    Skybox*                           skybox,
                    DescriptorAllocatorGrowable*      globalDescriptorAllocator,
                    const DescriptorBufferProperties& descriptorBufferProps,
                    VkPhysicalDevice                  physicalDevice,
                    VkExtent2D                        windowExtent)
{
	m_device                    = device;
	m_resourceManager           = resourceManager;
	m_swapchainManager          = swapchainManager;
	m_camera                    = camera;
	m_skybox                    = skybox;
	m_globalDescriptorAllocator = globalDescriptorAllocator;
	m_descriptorBufferProps     = descriptorBufferProps;

	// Initialize descriptor buffer writer
	m_descriptorBufferWriter.init(device, descriptorBufferProps);

	// Query GPU bindless limits
	BindlessLimits bindlessLimits = queryBindlessLimits(physicalDevice);

	// Initialize bindless texture registry with GPU-queried limits
	m_textureRegistry.init(
	device, resourceManager, descriptorBufferProps, bindlessLimits.maxTextures);

	// Initialize bindless material registry with GPU-queried limits
	m_materialRegistry.init(device,
	                        resourceManager,
	                        descriptorBufferProps,
	                        bindlessLimits.maxMaterials);

	// Note: SamplerRegistry is initialized later via initBindlessSamplers()
	// after AssetLoader creates the samplers

	initRenderTargets(windowExtent);
	initHiZResources();
	initShadowResources();
	initPointShadowResources();
	initDescriptors();
	initBackgroundPipelines();
	initShadowPipeline();
	initPointShadowPipeline();
	initCullPipeline();
	initHiZPipeline();

	// Create pipeline statistics query pools (one per frame-in-flight)
	for (uint32_t i = 0; i < STATS_FRAME_OVERLAP; i++)
	{
		VkQueryPoolCreateInfo queryInfo {};
		queryInfo.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		queryInfo.queryType  = VK_QUERY_TYPE_PIPELINE_STATISTICS;
		queryInfo.queryCount = 1;
		queryInfo.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT;
		VK_CHECK(vkCreateQueryPool(m_device, &queryInfo, nullptr, &m_statsQueryPool[i]));
		vkResetQueryPool(m_device, m_statsQueryPool[i], 0, 1);
	}

	initPickingResources(windowExtent);
	initObjectIDPipeline();
	initDebugLinePipeline();
}

void Renderer::initBindlessSamplers(VkSampler linearSampler,
                                    VkSampler nearestSampler,
                                    VkSampler linearMipmapSampler,
                                    VkSampler nearestMipmapSampler)
{
	m_samplerRegistry.init(m_device,
	                       m_resourceManager,
	                       m_descriptorBufferProps,
	                       linearSampler,
	                       nearestSampler,
	                       linearMipmapSampler,
	                       nearestMipmapSampler);
}

void Renderer::cleanup()
{
	// Cleanup render targets
	m_resourceManager->destroyImage(m_drawImage);
	m_resourceManager->destroyImage(m_msaaColorImage);
	m_resourceManager->destroyImage(m_depthImage);

	// Cleanup picking resources
	m_resourceManager->destroyImage(m_objectIDImage);
	m_resourceManager->destroyImage(m_pickingDepthImage);
	m_resourceManager->destroyBuffer(m_pickingStagingBuffer);
	if (m_objectIDPipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(m_device, m_objectIDPipeline, nullptr);
	if (m_objectIDPipelineLayout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(m_device, m_objectIDPipelineLayout, nullptr);
	if (m_debugLinePipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(m_device, m_debugLinePipeline, nullptr);
	if (m_debugLinePipelineLayout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(m_device, m_debugLinePipelineLayout, nullptr);

	// Cleanup shadow resources
	m_resourceManager->destroyImage(m_shadowMap);
	m_resourceManager->destroyImage(m_spotShadowMap);
	if (m_shadowSampler != VK_NULL_HANDLE)
		vkDestroySampler(m_device, m_shadowSampler, nullptr);
	if (m_shadowPipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(m_device, m_shadowPipeline, nullptr);
	if (m_shadowPipelineLayout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(m_device, m_shadowPipelineLayout, nullptr);

	// Cleanup point shadow resources
	// Destroy face views first (they're separate from the cube map image view)
	for (int i = 0; i < 6; i++)
	{
		if (m_pointShadowFaceViews[i] != VK_NULL_HANDLE)
			vkDestroyImageView(m_device, m_pointShadowFaceViews[i], nullptr);
	}
	// destroyImage will destroy the cube map image view and the image itself
	m_resourceManager->destroyImage(m_pointShadowCubeMap);
	if (m_pointShadowSampler != VK_NULL_HANDLE)
		vkDestroySampler(m_device, m_pointShadowSampler, nullptr);
	if (m_pointShadowPipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(m_device, m_pointShadowPipeline, nullptr);
	if (m_pointShadowPipelineLayout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(m_device, m_pointShadowPipelineLayout, nullptr);

	// Cleanup pipeline statistics query pools
	for (uint32_t i = 0; i < STATS_FRAME_OVERLAP; i++)
	{
		if (m_statsQueryPool[i] != VK_NULL_HANDLE)
			vkDestroyQueryPool(m_device, m_statsQueryPool[i], nullptr);
	}

	// Cleanup Hi-Z resources
	for (auto& view : m_hizMipViews)
		if (view != VK_NULL_HANDLE)
			vkDestroyImageView(m_device, view, nullptr);
	m_hizMipViews.clear();
	m_resourceManager->destroyImage(m_hizImage);
	m_resourceManager->destroyImage(m_depthResolveImage);
	if (m_hizSampler != VK_NULL_HANDLE)
		vkDestroySampler(m_device, m_hizSampler, nullptr);
	if (m_hizDownsamplePipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(m_device, m_hizDownsamplePipeline, nullptr);
	if (m_hizDownsamplePipelineLayout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(m_device, m_hizDownsamplePipelineLayout, nullptr);
	if (m_hizDownsampleDescriptorLayout != VK_NULL_HANDLE)
		vkDestroyDescriptorSetLayout(m_device, m_hizDownsampleDescriptorLayout, nullptr);

	// Cleanup GPU culling pipeline
	if (m_cullPipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(m_device, m_cullPipeline, nullptr);
	if (m_cullPipelineLayout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(m_device, m_cullPipelineLayout, nullptr);

	// Cleanup pipelines
	vkDestroyPipelineLayout(m_device, m_gradientPipelineLayout, nullptr);
	for (auto& effect : m_backgroundEffects)
	{
		vkDestroyPipeline(m_device, effect.m_pipeline, nullptr);
	}

	// Cleanup descriptor layouts
	vkDestroyDescriptorSetLayout(
	m_device, m_drawImageDescriptorLayout, nullptr);
	vkDestroyDescriptorSetLayout(
	m_device, m_gpuSceneDataDescriptorLayout, nullptr);

	// Cleanup bindless resources
	m_textureRegistry.destroy();
	m_samplerRegistry.destroy();
	m_materialRegistry.destroy();

	// Clear loaded scenes
	m_loadedScenes.clear();
}

void Renderer::resize(VkExtent2D newExtent, VkSampleCountFlagBits msaaSamples)
{
	m_msaaSamples = msaaSamples;

	// Destroy old render targets
	m_resourceManager->destroyImage(m_drawImage);
	m_resourceManager->destroyImage(m_msaaColorImage);
	m_resourceManager->destroyImage(m_depthImage);
	m_resourceManager->destroyImage(m_objectIDImage);
	m_resourceManager->destroyImage(m_pickingDepthImage);

	// Recreate render targets with new extent and MSAA settings
	VkExtent3D drawImageExtent = {newExtent.width, newExtent.height, 1};

	VkImageUsageFlags drawImageUsages {};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageUsageFlags msaaImageUsages {};
	msaaImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	msaaImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	msaaImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageUsageFlags depthImageUsages {};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	depthImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // For Hi-Z depth resolve

	m_drawImage = m_resourceManager->createImage(
	drawImageExtent, VK_FORMAT_R16G16B16A16_SFLOAT, drawImageUsages);

	m_msaaColorImage =
	m_resourceManager->createImage(drawImageExtent,
	                               VK_FORMAT_R16G16B16A16_SFLOAT,
	                               msaaImageUsages,
	                               false,
	                               m_msaaSamples);

	m_depthImage = m_resourceManager->createImage(drawImageExtent,
	                                              VK_FORMAT_D32_SFLOAT,
	                                              depthImageUsages,
	                                              false,
	                                              m_msaaSamples);

	// Recreate Hi-Z resources with new extent
	for (auto& view : m_hizMipViews)
		if (view != VK_NULL_HANDLE)
			vkDestroyImageView(m_device, view, nullptr);
	m_hizMipViews.clear();
	m_resourceManager->destroyImage(m_hizImage);
	m_resourceManager->destroyImage(m_depthResolveImage);
	if (m_hizSampler != VK_NULL_HANDLE)
	{
		vkDestroySampler(m_device, m_hizSampler, nullptr);
		m_hizSampler = VK_NULL_HANDLE;
	}
	m_hizReady = false;
	initHiZResources();

	// Recreate object ID image for picking (64-bit entity ID)
	VkImageUsageFlags pickingColorUsages =
	VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	m_objectIDImage = m_resourceManager->createImage(
	drawImageExtent,
	VK_FORMAT_R32G32_UINT, // Two 32-bit channels for full 64-bit entity ID
	pickingColorUsages,
	false,
	VK_SAMPLE_COUNT_1_BIT);

	// Recreate picking depth image
	VkImageUsageFlags pickingDepthUsages =
	VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	m_pickingDepthImage = m_resourceManager->createImage(drawImageExtent,
	                                                     VK_FORMAT_D32_SFLOAT,
	                                                     pickingDepthUsages,
	                                                     false,
	                                                     VK_SAMPLE_COUNT_1_BIT);

	// Update the draw image descriptor to point to the new image
	DescriptorWriter writer;
	writer.writeImage(0,
	                  m_drawImage.m_imageView,
	                  VK_NULL_HANDLE,
	                  VK_IMAGE_LAYOUT_GENERAL,
	                  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	writer.updateSet(m_device, m_drawImageDescriptors);

	// Rebuild debug line pipeline with new MSAA settings
	if (m_debugLinePipeline != VK_NULL_HANDLE)
		vkDestroyPipeline(m_device, m_debugLinePipeline, nullptr);
	if (m_debugLinePipelineLayout != VK_NULL_HANDLE)
		vkDestroyPipelineLayout(m_device, m_debugLinePipelineLayout, nullptr);
	m_debugLinePipeline       = VK_NULL_HANDLE;
	m_debugLinePipelineLayout = VK_NULL_HANDLE;
	initDebugLinePipeline();
}

void Renderer::initRenderTargets(VkExtent2D windowExtent)
{
	VkExtent3D drawImageExtent = {windowExtent.width, windowExtent.height, 1};

	VkImageUsageFlags drawImageUsages {};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageUsageFlags msaaImageUsages {};
	msaaImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	msaaImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	msaaImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	VkImageUsageFlags depthImageUsages {};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	depthImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // For Hi-Z depth resolve

	m_drawImage = m_resourceManager->createImage(
	drawImageExtent, VK_FORMAT_R16G16B16A16_SFLOAT, drawImageUsages);

	m_msaaColorImage =
	m_resourceManager->createImage(drawImageExtent,
	                               VK_FORMAT_R16G16B16A16_SFLOAT,
	                               msaaImageUsages,
	                               false,
	                               m_msaaSamples);

	m_depthImage = m_resourceManager->createImage(drawImageExtent,
	                                              VK_FORMAT_D32_SFLOAT,
	                                              depthImageUsages,
	                                              false,
	                                              m_msaaSamples);
}

void Renderer::initShadowResources()
{
	// Create shadow map depth image
	VkExtent3D shadowExtent = {SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION, 1};

	VkImageUsageFlags shadowUsages =
	VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	// Directional light shadow map
	m_shadowMap = m_resourceManager->createImage(shadowExtent,
	                                             VK_FORMAT_D32_SFLOAT,
	                                             shadowUsages,
	                                             false,
	                                             VK_SAMPLE_COUNT_1_BIT);

	// Spot light shadow map
	m_spotShadowMap = m_resourceManager->createImage(shadowExtent,
	                                                 VK_FORMAT_D32_SFLOAT,
	                                                 shadowUsages,
	                                                 false,
	                                                 VK_SAMPLE_COUNT_1_BIT);

	// Create comparison sampler for shadow mapping (shared by both)
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType               = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter           = VK_FILTER_LINEAR;
	samplerInfo.minFilter           = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode          = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	samplerInfo.addressModeV        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	samplerInfo.addressModeW        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	samplerInfo.borderColor =
	VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK; // 0.0 = no shadow (reverse-Z far)
	samplerInfo.compareEnable = VK_TRUE;
	samplerInfo.compareOp =
	VK_COMPARE_OP_GREATER; // Reverse-Z: lit if fragment > shadow
	samplerInfo.maxLod           = 0.0f;
	samplerInfo.minLod           = 0.0f;
	samplerInfo.mipLodBias       = 0.0f;
	samplerInfo.anisotropyEnable = VK_FALSE;

	VK_CHECK(
	vkCreateSampler(m_device, &samplerInfo, nullptr, &m_shadowSampler));
}

void Renderer::initPointShadowResources()
{
	// Create depth cube map for point light shadow mapping
	VkExtent3D cubeExtent = {
	POINT_SHADOW_MAP_RESOLUTION, POINT_SHADOW_MAP_RESOLUTION, 1};

	// Create image with cube-compatible flag
	VkImageCreateInfo imgInfo = vkinit::imageCreateInfo(
	VK_FORMAT_D32_SFLOAT,
	VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	cubeExtent);
	imgInfo.arrayLayers = 6;
	imgInfo.flags       = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

	VmaAllocationCreateInfo allocInfo = {};
	allocInfo.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;
	allocInfo.requiredFlags           = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	m_pointShadowCubeMap.m_imageFormat = VK_FORMAT_D32_SFLOAT;
	m_pointShadowCubeMap.m_imageExtent = cubeExtent;

	VK_CHECK(vmaCreateImage(m_resourceManager->getAllocator(),
	                        &imgInfo,
	                        &allocInfo,
	                        &m_pointShadowCubeMap.m_image,
	                        &m_pointShadowCubeMap.m_allocation,
	                        nullptr));

	// Create cube map view for sampling (all 6 faces)
	VkImageViewCreateInfo cubeViewInfo =
	vkinit::imageViewCreateInfo(VK_FORMAT_D32_SFLOAT,
	                            m_pointShadowCubeMap.m_image,
	                            VK_IMAGE_ASPECT_DEPTH_BIT);
	cubeViewInfo.viewType                    = VK_IMAGE_VIEW_TYPE_CUBE;
	cubeViewInfo.subresourceRange.layerCount = 6;

	VK_CHECK(vkCreateImageView(
	m_device, &cubeViewInfo, nullptr, &m_pointShadowCubeMap.m_imageView));

	// Create individual face views for rendering (each face is a 2D attachment)
	for (uint32_t face = 0; face < 6; face++)
	{
		VkImageViewCreateInfo faceViewInfo =
		vkinit::imageViewCreateInfo(VK_FORMAT_D32_SFLOAT,
		                            m_pointShadowCubeMap.m_image,
		                            VK_IMAGE_ASPECT_DEPTH_BIT);
		faceViewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
		faceViewInfo.subresourceRange.baseArrayLayer = face;
		faceViewInfo.subresourceRange.layerCount     = 1;

		VK_CHECK(vkCreateImageView(
		m_device, &faceViewInfo, nullptr, &m_pointShadowFaceViews[face]));
	}

	// Create comparison sampler for point shadow cube map (same as other shadow
	// maps)
	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType               = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter           = VK_FILTER_LINEAR;
	samplerInfo.minFilter           = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode          = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW        = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.borderColor         = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
	samplerInfo.compareEnable       = VK_TRUE;
	samplerInfo.compareOp =
	VK_COMPARE_OP_GREATER; // Reverse-Z: lit if fragment > shadow
	samplerInfo.maxLod           = 0.0f;
	samplerInfo.minLod           = 0.0f;
	samplerInfo.mipLodBias       = 0.0f;
	samplerInfo.anisotropyEnable = VK_FALSE;

	VK_CHECK(
	vkCreateSampler(m_device, &samplerInfo, nullptr, &m_pointShadowSampler));

	AGNI_PRINT("Point shadow cube map created ({}x{} per face)\n",
	           POINT_SHADOW_MAP_RESOLUTION,
	           POINT_SHADOW_MAP_RESOLUTION);
}

void Renderer::initDescriptors()
{
	// Create descriptor set layout for draw image (compute shader)
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		m_drawImageDescriptorLayout =
		builder.build(m_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}

	m_drawImageDescriptors = m_globalDescriptorAllocator->allocate(
	m_device, m_drawImageDescriptorLayout);

	VkDebugName(m_device, VK_OBJECT_TYPE_DESCRIPTOR_SET,
	             (uint64_t)m_drawImageDescriptors, "DrawImageDescriptorSet");

	// Create descriptor set layout for GPU scene data + lights + shadow maps
	// (using descriptor buffer)
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER); // Scene data
		builder.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); // Light data
		builder.addBinding(
		2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); // Directional shadow map
		builder.addBinding(
		3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); // Spot shadow map
		builder.addBinding(
		4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); // Point shadow cube map
		builder.addBinding(
		5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); // Hi-Z depth pyramid
		m_gpuSceneDataLayoutInfo = builder.buildForDescriptorBuffer(
		m_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT);
		m_gpuSceneDataDescriptorLayout = m_gpuSceneDataLayoutInfo.layout;
	}

	// Write descriptor for draw image
	DescriptorWriter writer;
	writer.writeImage(0,
	                  m_drawImage.m_imageView,
	                  VK_NULL_HANDLE,
	                  VK_IMAGE_LAYOUT_GENERAL,
	                  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

	writer.updateSet(m_device, m_drawImageDescriptors);
}

void Renderer::initBackgroundPipelines()
{
	// Build gradient pipeline (creates the shared layout)
	auto gradientResult = ComputePipelineBuilder(m_device)
	.setShader(resPath("shaders/slang/GradientColor.comp.spv").c_str())
	.addDescriptorSetLayout(m_drawImageDescriptorLayout)
	.setPushConstantSize(sizeof(ComputePushConstants))
	.build();

	m_gradientPipelineLayout = gradientResult.m_layout;

	// Build sky pipeline (reuses the same layout)
	auto skyResult = ComputePipelineBuilder(m_device)
	.setShader(resPath("shaders/slang/Sky.comp.spv").c_str())
	.setLayout(m_gradientPipelineLayout)
	.build();

	ComputeEffect gradient;
	gradient.m_layout = m_gradientPipelineLayout;
	gradient.m_name   = "gradient";
	gradient.m_data   = {};
	gradient.m_data.m_data1 = glm::vec4(1, 0, 0, 1);
	gradient.m_data.m_data2 = glm::vec4(0, 0, 1, 1);
	gradient.m_pipeline     = gradientResult.m_pipeline;

	ComputeEffect sky;
	sky.m_layout = m_gradientPipelineLayout;
	sky.m_name   = "sky";
	sky.m_data   = {};
	sky.m_data.m_data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);
	sky.m_pipeline     = skyResult.m_pipeline;

	m_backgroundEffects.push_back(gradient);
	m_backgroundEffects.push_back(sky);
}

void Renderer::initShadowPipeline()
{
	// Load shadow pass shader (vertex only, no fragment for depth-only pass)
	VkShaderModule shadowVertShader;
	if (!vkutil::loadShaderModule(
	    resPath("shaders/slang/Shadow.vert.spv").c_str(), m_device, &shadowVertShader))
	{
		AGNI_PRINT("Failed to load shadow vertex shader\n");
		return;
	}

	// Create pipeline layout with push constants for shadow pass
	VkPushConstantRange pushConstantRange {};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pushConstantRange.offset     = 0;
	pushConstantRange.size       = sizeof(IndirectDrawPushConstants);

	VkPipelineLayoutCreateInfo layoutInfo {};
	layoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts    = &m_gpuSceneDataDescriptorLayout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges    = &pushConstantRange;

	VK_CHECK(vkCreatePipelineLayout(
	m_device, &layoutInfo, nullptr, &m_shadowPipelineLayout));

	// Build shadow pipeline
	PipelineBuilder builder;
	builder.m_shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(
	VK_SHADER_STAGE_VERTEX_BIT, shadowVertShader));

	builder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	builder.setPolygonMode(VK_POLYGON_MODE_FILL);

	// Cull front faces to reduce shadow acne
	builder.setCullMode(VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_CLOCKWISE);

	builder.setMultisamplingNone();

	// Reverse-Z depth test
	builder.enableDepthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

	// Depth bias to prevent shadow acne (negative for reverse-Z)
	builder.enableDepthBias(-1.25f, -1.75f, 0.0f);

	// No color attachment for depth-only pass
	builder.m_renderInfo.colorAttachmentCount    = 0;
	builder.m_renderInfo.pColorAttachmentFormats = nullptr;
	builder.setDepthFormat(VK_FORMAT_D32_SFLOAT);

	builder.m_pipelineLayout = m_shadowPipelineLayout;
	builder.enableDescriptorBuffer();

	m_shadowPipeline = builder.buildPipeline(m_device);
	VkDebugName(m_device, VK_OBJECT_TYPE_PIPELINE, (uint64_t)m_shadowPipeline, "ShadowPipeline");

	vkDestroyShaderModule(m_device, shadowVertShader, nullptr);

	AGNI_PRINT("Shadow pipeline created successfully\n");
}

void Renderer::initPointShadowPipeline()
{
	// Load point shadow pass shaders (vertex + fragment for linear depth
	// output)
	VkShaderModule pointShadowVertShader;
	if (!vkutil::loadShaderModule(resPath("shaders/slang/PointShadow.vert.spv").c_str(),
	                              m_device,
	                              &pointShadowVertShader))
	{
		AGNI_PRINT("Failed to load point shadow vertex shader\n");
		return;
	}

	VkShaderModule pointShadowFragShader;
	if (!vkutil::loadShaderModule(resPath("shaders/slang/PointShadow.frag.spv").c_str(),
	                              m_device,
	                              &pointShadowFragShader))
	{
		AGNI_PRINT("Failed to load point shadow fragment shader\n");
		vkDestroyShaderModule(m_device, pointShadowVertShader, nullptr);
		return;
	}

	// Create pipeline layout with push constants for point shadow pass
	VkPushConstantRange pushConstantRange {};
	pushConstantRange.stageFlags =
	VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size   = sizeof(PointShadowIndirectPushConstants);

	VkPipelineLayoutCreateInfo layoutInfo {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount =
	0; // No descriptor sets needed - all data via push constants
	layoutInfo.pSetLayouts            = nullptr;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges    = &pushConstantRange;

	VK_CHECK(vkCreatePipelineLayout(
	m_device, &layoutInfo, nullptr, &m_pointShadowPipelineLayout));

	// Build point shadow pipeline
	PipelineBuilder builder;
	builder.setShaders(pointShadowVertShader, pointShadowFragShader);

	builder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	builder.setPolygonMode(VK_POLYGON_MODE_FILL);

	// Cull front faces to reduce shadow acne
	builder.setCullMode(VK_CULL_MODE_FRONT_BIT, VK_FRONT_FACE_CLOCKWISE);

	builder.setMultisamplingNone();

	// Reverse-Z depth test
	builder.enableDepthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

	// Depth bias to prevent shadow acne
	builder.enableDepthBias(-1.25f, -1.75f, 0.0f);

	// No color attachment - fragment shader writes to depth
	builder.m_renderInfo.colorAttachmentCount    = 0;
	builder.m_renderInfo.pColorAttachmentFormats = nullptr;
	builder.setDepthFormat(VK_FORMAT_D32_SFLOAT);

	builder.m_pipelineLayout = m_pointShadowPipelineLayout;
	builder.enableDescriptorBuffer();

	m_pointShadowPipeline = builder.buildPipeline(m_device);
	VkDebugName(m_device, VK_OBJECT_TYPE_PIPELINE, (uint64_t)m_pointShadowPipeline, "PointShadowPipeline");

	vkDestroyShaderModule(m_device, pointShadowVertShader, nullptr);
	vkDestroyShaderModule(m_device, pointShadowFragShader, nullptr);

	AGNI_PRINT("Point shadow pipeline created successfully\n");
}

void Renderer::initCullPipeline()
{
	auto result = ComputePipelineBuilder(m_device)
	.setShader(resPath("shaders/slang/IndirectCull.comp.spv").c_str())
	.addDescriptorSetLayout(m_gpuSceneDataDescriptorLayout)
	.setPushConstantSize(sizeof(CullPushConstants))
	.build();

	if (result.m_pipeline == VK_NULL_HANDLE)
	{
		AGNI_PRINT("Failed to create indirect cull compute pipeline\n");
		return;
	}
	m_cullPipeline       = result.m_pipeline;
	m_cullPipelineLayout = result.m_layout;
	AGNI_PRINT("Indirect cull compute pipeline created successfully\n");
}

void Renderer::initHiZResources()
{
	uint32_t hizWidth  = m_drawImage.m_imageExtent.width;
	uint32_t hizHeight = m_drawImage.m_imageExtent.height;

	m_hizMipLevels = static_cast<uint32_t>(
	std::floor(std::log2(std::max(hizWidth, hizHeight)))) + 1;

	// Create Hi-Z image: R32_SFLOAT with full mip chain
	VkExtent3D hizExtent = {hizWidth, hizHeight, 1};
	VkImageUsageFlags hizUsage =
	VK_IMAGE_USAGE_STORAGE_BIT |
	VK_IMAGE_USAGE_SAMPLED_BIT |
	VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	m_hizImage = m_resourceManager->createImage(hizExtent,
	                                            VK_FORMAT_R32_SFLOAT,
	                                            hizUsage,
	                                            true, // mipmapped
	                                            VK_SAMPLE_COUNT_1_BIT);

	// Create per-mip image views for compute storage writes
	m_hizMipViews.resize(m_hizMipLevels);
	for (uint32_t mip = 0; mip < m_hizMipLevels; mip++)
	{
		VkImageViewCreateInfo viewInfo = vkinit::imageViewCreateInfo(
		VK_FORMAT_R32_SFLOAT, m_hizImage.m_image, VK_IMAGE_ASPECT_COLOR_BIT);
		viewInfo.subresourceRange.baseMipLevel = mip;
		viewInfo.subresourceRange.levelCount   = 1;
		VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &m_hizMipViews[mip]));
	}

	// Create depth resolve target (non-MSAA D32_SFLOAT, sampled for Hi-Z mip0 read)
	VkImageUsageFlags depthResolveUsage =
	VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
	VK_IMAGE_USAGE_SAMPLED_BIT;

	m_depthResolveImage = m_resourceManager->createImage(hizExtent,
	                                                     VK_FORMAT_D32_SFLOAT,
	                                                     depthResolveUsage,
	                                                     false,
	                                                     VK_SAMPLE_COUNT_1_BIT);

	// Create Hi-Z sampler: NEAREST filter, clamp to edge
	VkSamplerCreateInfo samplerInfo {};
	samplerInfo.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter     = VK_FILTER_NEAREST;
	samplerInfo.minFilter     = VK_FILTER_NEAREST;
	samplerInfo.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.minLod        = 0.0f;
	samplerInfo.maxLod        = static_cast<float>(m_hizMipLevels);
	samplerInfo.compareEnable = VK_FALSE;

	VK_CHECK(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_hizSampler));

	// Transition Hi-Z image to SHADER_READ_ONLY_OPTIMAL so the placeholder descriptor is valid
	m_resourceManager->immediateSubmit(
	[&](VkCommandBuffer cmd)
	{
		vkutil::transitionImage(cmd, m_hizImage.m_image,
		                        VK_IMAGE_LAYOUT_UNDEFINED,
		                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	});

	AGNI_PRINT("Hi-Z resources created: {}x{}, {} mip levels\n", hizWidth, hizHeight, m_hizMipLevels);
}

void Renderer::initHiZPipeline()
{
	// Descriptor layout: 2 storage images + 1 sampled depth texture (for first mip pass)
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);          // src mip (read)
		builder.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);          // dst mip (write)
		builder.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); // depth source (first pass only)
		m_hizDownsampleLayoutInfo = builder.buildForDescriptorBuffer(
		m_device, VK_SHADER_STAGE_COMPUTE_BIT);
		m_hizDownsampleDescriptorLayout = m_hizDownsampleLayoutInfo.layout;
	}

	auto result = ComputePipelineBuilder(m_device)
	.setShader(resPath("shaders/slang/HiZDownsample.comp.spv").c_str())
	.addDescriptorSetLayout(m_hizDownsampleDescriptorLayout)
	.setPushConstantSize(sizeof(HiZPushConstants))
	.build();

	if (result.m_pipeline == VK_NULL_HANDLE)
	{
		AGNI_PRINT("Failed to create Hi-Z downsample pipeline — Hi-Z occlusion disabled\n");
		m_hizOcclusionEnabled = false;
		return;
	}
	m_hizDownsamplePipeline       = result.m_pipeline;
	m_hizDownsamplePipelineLayout = result.m_layout;
	AGNI_PRINT("Hi-Z downsample pipeline created successfully\n");
}

void Renderer::buildHiZPyramid(VkCommandBuffer cmd, FrameData& currentFrame)
{
	// --- Step A: Transition images for compute ---

	// Transition resolved depth: DEPTH_ATTACHMENT -> DEPTH_READ_ONLY (for sampling)
	vkinit::imageBarrier(cmd, m_depthResolveImage.m_image,
	    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
	    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
	    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
	    VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1);

	// Transition all Hi-Z mips to GENERAL for compute read/write
	vkinit::imageBarrier(cmd, m_hizImage.m_image,
	    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
	    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
	    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	// --- Step B: Downsample mip chain via compute ---
	// First pass (mip=0): reads from depth resolve texture (binding 2) -> writes Hi-Z mip0 (binding 1)
	// Subsequent passes: reads Hi-Z mip N (binding 0) -> writes Hi-Z mip N+1 (binding 1)

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_hizDownsamplePipeline);

	VkDescriptorBufferBindingInfoEXT bufBinding {};
	bufBinding.sType   = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
	bufBinding.address = currentFrame.m_descriptorBuffer.getDeviceAddress();
	bufBinding.usage   = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
	                     VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
	vkCmdBindDescriptorBuffersEXT(cmd, 1, &bufBinding);

	uint32_t mipWidth  = m_drawExtent.width;
	uint32_t mipHeight = m_drawExtent.height;

	// The loop generates mipLevels total mip levels:
	// Pass 0: depth texture -> mip0 (isFirstMip=1)
	// Pass 1..N-1: mip[i-1] -> mip[i] (isFirstMip=0)
	for (uint32_t pass = 0; pass < m_hizMipLevels; pass++)
	{
		const bool isFirstPass = (pass == 0);

		// Allocate descriptor buffer space
		VkDeviceSize descOffset =
		currentFrame.m_descriptorBuffer.allocate(m_hizDownsampleLayoutInfo);
		void* descPtr =
		currentFrame.m_descriptorBuffer.getPtrAtOffset(descOffset);

		if (isFirstPass)
		{
			// Binding 0: unused but must be valid — use mip0 as placeholder
			m_descriptorBufferWriter.writeStorageImage(
			descPtr, m_hizDownsampleLayoutInfo.bindingOffsets[0], m_hizMipViews[0]);

			// Binding 1: dst = mip0 (storage image)
			m_descriptorBufferWriter.writeStorageImage(
			descPtr, m_hizDownsampleLayoutInfo.bindingOffsets[1], m_hizMipViews[0]);

			// Binding 2: depth resolve texture (combined image sampler)
			m_descriptorBufferWriter.writeImageSampler(
			descPtr, m_hizDownsampleLayoutInfo.bindingOffsets[2],
			m_depthResolveImage.m_imageView, m_hizSampler,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
		}
		else
		{
			// Binding 0: src = previous mip (storage image)
			m_descriptorBufferWriter.writeStorageImage(
			descPtr, m_hizDownsampleLayoutInfo.bindingOffsets[0], m_hizMipViews[pass - 1]);

			// Binding 1: dst = current mip (storage image)
			m_descriptorBufferWriter.writeStorageImage(
			descPtr, m_hizDownsampleLayoutInfo.bindingOffsets[1], m_hizMipViews[pass]);

			// Binding 2: placeholder (unused but must be valid)
			m_descriptorBufferWriter.writeImageSampler(
			descPtr, m_hizDownsampleLayoutInfo.bindingOffsets[2],
			m_depthResolveImage.m_imageView, m_hizSampler,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
		}

		// Set descriptor buffer offset
		uint32_t bufferIndex = 0;
		vkCmdSetDescriptorBufferOffsetsEXT(
		cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
		m_hizDownsamplePipelineLayout,
		0, 1, &bufferIndex, &descOffset);

		// Push constants
		HiZPushConstants hizPC {};
		hizPC.m_srcWidth    = mipWidth;
		hizPC.m_srcHeight   = mipHeight;
		hizPC.m_isFirstMip  = isFirstPass ? 1 : 0;
		vkCmdPushConstants(cmd, m_hizDownsamplePipelineLayout,
		                   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HiZPushConstants), &hizPC);

		// Dispatch — first pass writes full-res mip0, subsequent passes halve
		if (isFirstPass)
		{
			// First pass: copy depth to mip0 at full resolution
			vkCmdDispatch(cmd, (mipWidth + 7) / 8, (mipHeight + 7) / 8, 1);
		}
		else
		{
			uint32_t dstW = std::max(mipWidth / 2, 1u);
			uint32_t dstH = std::max(mipHeight / 2, 1u);
			vkCmdDispatch(cmd, (dstW + 7) / 8, (dstH + 7) / 8, 1);
			mipWidth  = dstW;
			mipHeight = dstH;
		}

		// Barrier between passes
		vkinit::memoryBarrier(cmd,
		    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
		    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
	}

	// --- Step C: Transition Hi-Z to SHADER_READ_ONLY for next frame's cull pass ---
	vkinit::imageBarrier(cmd, m_hizImage.m_image,
	    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
	    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
	    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	m_hizReady = true;
}

glm::mat4 Renderer::calculateLightSpaceMatrix(const glm::vec3& lightDir)
{
	// Calculate light space matrix for directional shadow mapping
	// Use camera position as the scene center to follow the camera
	glm::vec3 sceneCenter = m_camera->m_position;

	// Position the light "camera" backing away from scene along light direction
	glm::vec3 lightPos = sceneCenter - lightDir * 100.0f;

	// Create stable up vector (avoid parallel with light direction)
	glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
	if (glm::abs(glm::dot(lightDir, up)) > 0.99f)
	{
		up = glm::vec3(1.0f, 0.0f, 0.0f);
	}

	glm::mat4 lightView = glm::lookAt(lightPos, sceneCenter, up);

	// Orthographic projection for directional light (reverse-Z)
	// In reverse-Z, swap near/far: near maps to 1.0, far maps to 0.0
	glm::mat4 lightProj =
	glm::ortho(-m_shadowOrthoSize,
	           m_shadowOrthoSize,
	           -m_shadowOrthoSize,
	           m_shadowOrthoSize,
	           m_shadowFarPlane,   // Near = far value (reverse-Z)
	           m_shadowNearPlane); // Far = near value (reverse-Z)

	// Flip Y for Vulkan
	lightProj[1][1] *= -1.0f;

	return lightProj * lightView;
}

glm::mat4 Renderer::calculateSpotLightSpaceMatrix(const glm::vec3& position,
                                                  const glm::vec3& direction,
                                                  float outerConeAngle)
{
	// Calculate light space matrix for spot light shadow mapping
	// Spot lights use PERSPECTIVE projection (unlike directional which uses
	// ortho)

	glm::vec3 lightDir = glm::normalize(direction);
	glm::vec3 target   = position + lightDir;

	// Create stable up vector
	glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
	if (glm::abs(glm::dot(lightDir, up)) > 0.99f)
	{
		up = glm::vec3(1.0f, 0.0f, 0.0f);
	}

	glm::mat4 lightView = glm::lookAt(position, target, up);

	// Perspective projection for spot light (reverse-Z)
	// FOV should be at least 2x the outer cone angle to cover the entire
	// spotlight cone
	float fov =
	glm::radians(outerConeAngle) * 2.2f; // Slightly larger for margin
	float aspectRatio = 1.0f;            // Square shadow map

	// Reverse-Z: swap near/far
	glm::mat4 lightProj =
	glm::perspective(fov,
	                 aspectRatio,
	                 m_shadowFarPlane,   // Near = far value (reverse-Z)
	                 m_shadowNearPlane); // Far = near value (reverse-Z)

	// Flip Y for Vulkan
	lightProj[1][1] *= -1.0f;

	return lightProj * lightView;
}

std::array<glm::mat4, 6>
Renderer::calculatePointLightMatrices(const glm::vec3& lightPos,
                                      float            nearPlane,
                                      float            farPlane)
{
	// 90 degree FOV perspective projection for cube face
	// Reverse-Z: swap near/far (farPlane as near, nearPlane as far)
	// Note: NO Y-flip (proj[1][1] *= -1) for cube maps!
	// The rotation matrices already handle all orientation including Y-axis.
	// Adding Y-flip here would cause a double-flip and require negating Y when
	// sampling.
	glm::mat4 proj =
	glm::perspective(glm::radians(90.0f), 1.0f, farPlane, nearPlane);

	// Translate to light position
	glm::mat4 lightTranslate = glm::translate(glm::mat4(1.0f), -lightPos);

	// 6 view matrices for cube faces using rotation-based approach (Sascha
	// Willems style)
	std::array<glm::mat4, 6> matrices;

	// Face 0: POSITIVE_X
	glm::mat4 view = glm::mat4(1.0f);
	view = glm::rotate(view, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	view = glm::rotate(view, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	matrices[0] = proj * view * lightTranslate;

	// Face 1: NEGATIVE_X
	view = glm::mat4(1.0f);
	view = glm::rotate(view, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	view = glm::rotate(view, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	matrices[1] = proj * view * lightTranslate;

	// Face 2: POSITIVE_Y
	view = glm::mat4(1.0f);
	view = glm::rotate(view, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	matrices[2] = proj * view * lightTranslate;

	// Face 3: NEGATIVE_Y
	view = glm::mat4(1.0f);
	view = glm::rotate(view, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	matrices[3] = proj * view * lightTranslate;

	// Face 4: POSITIVE_Z
	view = glm::mat4(1.0f);
	view = glm::rotate(view, glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	matrices[4] = proj * view * lightTranslate;

	// Face 5: NEGATIVE_Z
	view = glm::mat4(1.0f);
	view = glm::rotate(view, glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f));
	matrices[5] = proj * view * lightTranslate;

	return matrices;
}

Renderer::ShadowIndirectResources Renderer::buildShadowIndirectBuffers(FrameData& currentFrame)
{
#ifdef TRACY_ENABLE
	ZoneScopedN("Build Shadow Indirect Buffers");
#endif

	ShadowIndirectResources res;
	const auto& surfaces = m_mainDrawContext.m_OpaqueSurfaces;
	res.totalDraws = static_cast<uint32_t>(surfaces.size());

	if (res.totalDraws == 0)
		return res;

	// Allocate indirect command buffer
	const VkDeviceSize indirectBufSize =
	res.totalDraws * sizeof(VkDrawIndexedIndirectCommand);
	res.indirectBuffer = m_resourceManager->createBuffer(
	indirectBufSize,
	VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
	VMA_MEMORY_USAGE_CPU_TO_GPU);

	// Allocate draw data SSBO
	const VkDeviceSize drawDataBufSize = res.totalDraws * sizeof(GPUDrawData);
	res.drawDataBuffer = m_resourceManager->createBuffer(
	drawDataBufSize,
	VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
	VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	VMA_MEMORY_USAGE_CPU_TO_GPU);

	// Add to frame deletion queue
	auto* rm = m_resourceManager;
	currentFrame.m_deletionQueue.push_function(
	[rm, indBuf = res.indirectBuffer, ddBuf = res.drawDataBuffer]()
	{
		rm->destroyBuffer(indBuf);
		rm->destroyBuffer(ddBuf);
	});

	// Fill indirect commands and draw data (no sorting — global index buffer)
	auto* indirectCmds =
	static_cast<VkDrawIndexedIndirectCommand*>(res.indirectBuffer.m_info.pMappedData);
	auto* drawDataPtr =
	static_cast<GPUDrawData*>(res.drawDataBuffer.m_info.pMappedData);

	for (uint32_t i = 0; i < res.totalDraws; i++)
	{
		const RenderObject& r = surfaces[i];

		indirectCmds[i].indexCount    = r.m_indexCount;
		indirectCmds[i].instanceCount = 1;
		indirectCmds[i].firstIndex    = r.m_firstIndex;
		indirectCmds[i].vertexOffset  = 0;
		indirectCmds[i].firstInstance = i;

		drawDataPtr[i].m_worldMatrix   = r.m_transform;
		drawDataPtr[i].m_vertexBuffer  = r.m_vertexBufferAddress;
		drawDataPtr[i].m_materialIndex = 0;
		drawDataPtr[i].m_padding       = 0;
	}

	// Get BDA for draw data buffer
	VkBufferDeviceAddressInfo drawDataAddrInfo {};
	drawDataAddrInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	drawDataAddrInfo.buffer = res.drawDataBuffer.m_buffer;
	res.drawDataBDA = vkGetBufferDeviceAddress(m_device, &drawDataAddrInfo);

	return res;
}

void Renderer::drawShadowPass(VkCommandBuffer cmd, FrameData& currentFrame, const ShadowIndirectResources& shadowRes)
{
#ifdef TRACY_ENABLE
	ZoneScopedN("Shadow Pass");
#endif

	// Transition shadow map for rendering
	vkutil::transitionImage(cmd,
	                        m_shadowMap.m_image,
	                        VK_IMAGE_LAYOUT_UNDEFINED,
	                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	// Setup depth attachment for shadow pass
	VkRenderingAttachmentInfo depthAttachment {};
	depthAttachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachment.imageView   = m_shadowMap.m_imageView;
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	depthAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.clearValue.depthStencil.depth = 0.0f; // Reverse-Z: far = 0

	VkRenderingInfo renderInfo {};
	renderInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderInfo.renderArea           = {{0, 0},
	                                   {SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION}};
	renderInfo.layerCount           = 1;
	renderInfo.colorAttachmentCount = 0;
	renderInfo.pColorAttachments    = nullptr;
	renderInfo.pDepthAttachment     = &depthAttachment;

	vkCmdBeginRendering(cmd, &renderInfo);

	// Bind shadow pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);

	// Set viewport and scissor for shadow map resolution
	VkViewport viewport {};
	viewport.x        = 0;
	viewport.y        = 0;
	viewport.width    = static_cast<float>(SHADOW_MAP_RESOLUTION);
	viewport.height   = static_cast<float>(SHADOW_MAP_RESOLUTION);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor {};
	scissor.offset = {0, 0};
	scissor.extent = {SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// Allocate scene data buffer for light matrix
	AllocatedBuffer gpuSceneDataBuffer =
	m_resourceManager->createBuffer(sizeof(GPUSceneData),
	                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
	                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	                                VMA_MEMORY_USAGE_CPU_TO_GPU);

	currentFrame.m_deletionQueue.push_function(
	[rm = m_resourceManager, gpuSceneDataBuffer]()
	{ rm->destroyBuffer(gpuSceneDataBuffer); });

	GPUSceneData* sceneUniformData =
	(GPUSceneData*) gpuSceneDataBuffer.m_info.pMappedData;
	*sceneUniformData = m_sceneData;

	// Allocate descriptor buffer space
	VkDeviceSize sceneDescriptorOffset =
	currentFrame.m_descriptorBuffer.allocate(m_gpuSceneDataLayoutInfo);

	VkBufferDeviceAddressInfo addressInfo {};
	addressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	addressInfo.buffer = gpuSceneDataBuffer.m_buffer;
	VkDeviceAddress sceneDataAddress =
	vkGetBufferDeviceAddress(m_device, &addressInfo);

	void* sceneDescriptorPtr =
	currentFrame.m_descriptorBuffer.getPtrAtOffset(sceneDescriptorOffset);
	m_descriptorBufferWriter.writeUniformBuffer(
	sceneDescriptorPtr,
	m_gpuSceneDataLayoutInfo.bindingOffsets[0],
	sceneDataAddress,
	sizeof(GPUSceneData));

	// Bind descriptor buffer
	VkDescriptorBufferBindingInfoEXT bufferBinding {};
	bufferBinding.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
	bufferBinding.address = currentFrame.m_descriptorBuffer.getDeviceAddress();
	bufferBinding.usage   = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
	                      VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
	vkCmdBindDescriptorBuffersEXT(cmd, 1, &bufferBinding);

	uint32_t bufferIndex = 0;
	vkCmdSetDescriptorBufferOffsetsEXT(cmd,
	                                   VK_PIPELINE_BIND_POINT_GRAPHICS,
	                                   m_shadowPipelineLayout,
	                                   0,
	                                   1,
	                                   &bufferIndex,
	                                   &sceneDescriptorOffset);

	// Push BDA to draw data (once for entire pass)
	IndirectDrawPushConstants pushConst;
	pushConst.m_drawDataBufferPtr = shadowRes.drawDataBDA;
	vkCmdPushConstants(cmd,
	                   m_shadowPipelineLayout,
	                   VK_SHADER_STAGE_VERTEX_BIT,
	                   0,
	                   sizeof(IndirectDrawPushConstants),
	                   &pushConst);

	// Bind global index buffer and issue single indirect draw
	vkCmdBindIndexBuffer(cmd, m_resourceManager->getGlobalIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
	constexpr uint32_t stride = sizeof(VkDrawIndexedIndirectCommand);

	if (m_multiDrawIndirectSupported && m_multiDrawIndirectEnabled)
		vkCmdDrawIndexedIndirect(cmd, shadowRes.indirectBuffer.m_buffer,
		                         0, shadowRes.totalDraws, stride);
	else
		for (uint32_t i = 0; i < shadowRes.totalDraws; i++)
			vkCmdDrawIndexedIndirect(cmd, shadowRes.indirectBuffer.m_buffer,
			                         i * stride, 1, stride);

	vkCmdEndRendering(cmd);

	// Transition shadow map for sampling in fragment shader
	vkutil::transitionImage(cmd,
	                        m_shadowMap.m_image,
	                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
	                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
}

void Renderer::drawSpotShadowPass(VkCommandBuffer cmd, FrameData& currentFrame, const ShadowIndirectResources& shadowRes)
{
#ifdef TRACY_ENABLE
	ZoneScopedN("Spot Shadow Pass");
#endif

	// Transition spot shadow map for rendering
	vkutil::transitionImage(cmd,
	                        m_spotShadowMap.m_image,
	                        VK_IMAGE_LAYOUT_UNDEFINED,
	                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	// Setup depth attachment for spot shadow pass
	VkRenderingAttachmentInfo depthAttachment {};
	depthAttachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachment.imageView   = m_spotShadowMap.m_imageView;
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	depthAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.clearValue.depthStencil.depth = 0.0f; // Reverse-Z: far = 0

	VkRenderingInfo renderInfo {};
	renderInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderInfo.renderArea           = {{0, 0},
	                                   {SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION}};
	renderInfo.layerCount           = 1;
	renderInfo.colorAttachmentCount = 0;
	renderInfo.pColorAttachments    = nullptr;
	renderInfo.pDepthAttachment     = &depthAttachment;

	vkCmdBeginRendering(cmd, &renderInfo);

	// Bind shadow pipeline (reuse same pipeline as directional)
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);

	// Set viewport and scissor
	VkViewport viewport {};
	viewport.x        = 0;
	viewport.y        = 0;
	viewport.width    = static_cast<float>(SHADOW_MAP_RESOLUTION);
	viewport.height   = static_cast<float>(SHADOW_MAP_RESOLUTION);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor {};
	scissor.offset = {0, 0};
	scissor.extent = {SHADOW_MAP_RESOLUTION, SHADOW_MAP_RESOLUTION};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// Allocate scene data buffer (contains spot light matrix)
	AllocatedBuffer gpuSceneDataBuffer =
	m_resourceManager->createBuffer(sizeof(GPUSceneData),
	                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
	                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	                                VMA_MEMORY_USAGE_CPU_TO_GPU);

	currentFrame.m_deletionQueue.push_function(
	[rm = m_resourceManager, gpuSceneDataBuffer]()
	{ rm->destroyBuffer(gpuSceneDataBuffer); });

	GPUSceneData* sceneUniformData =
	(GPUSceneData*) gpuSceneDataBuffer.m_info.pMappedData;
	*sceneUniformData = m_sceneData;
	// For spot shadow pass, copy spot matrix to lightSpaceMatrix slot (shader
	// expects it there)
	sceneUniformData->m_lightSpaceMatrix = m_sceneData.m_spotLightSpaceMatrix;

	// Allocate descriptor buffer space
	VkDeviceSize sceneDescriptorOffset =
	currentFrame.m_descriptorBuffer.allocate(m_gpuSceneDataLayoutInfo);

	VkBufferDeviceAddressInfo addressInfo {};
	addressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	addressInfo.buffer = gpuSceneDataBuffer.m_buffer;
	VkDeviceAddress sceneDataAddress =
	vkGetBufferDeviceAddress(m_device, &addressInfo);

	void* sceneDescriptorPtr =
	currentFrame.m_descriptorBuffer.getPtrAtOffset(sceneDescriptorOffset);
	m_descriptorBufferWriter.writeUniformBuffer(
	sceneDescriptorPtr,
	m_gpuSceneDataLayoutInfo.bindingOffsets[0],
	sceneDataAddress,
	sizeof(GPUSceneData));

	// Bind descriptor buffer
	VkDescriptorBufferBindingInfoEXT bufferBinding {};
	bufferBinding.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
	bufferBinding.address = currentFrame.m_descriptorBuffer.getDeviceAddress();
	bufferBinding.usage   = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
	                      VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;
	vkCmdBindDescriptorBuffersEXT(cmd, 1, &bufferBinding);

	uint32_t bufferIndex = 0;
	vkCmdSetDescriptorBufferOffsetsEXT(cmd,
	                                   VK_PIPELINE_BIND_POINT_GRAPHICS,
	                                   m_shadowPipelineLayout,
	                                   0,
	                                   1,
	                                   &bufferIndex,
	                                   &sceneDescriptorOffset);

	// Push BDA to draw data (once for entire pass)
	IndirectDrawPushConstants pushConst;
	pushConst.m_drawDataBufferPtr = shadowRes.drawDataBDA;
	vkCmdPushConstants(cmd,
	                   m_shadowPipelineLayout,
	                   VK_SHADER_STAGE_VERTEX_BIT,
	                   0,
	                   sizeof(IndirectDrawPushConstants),
	                   &pushConst);

	// Bind global index buffer and issue single indirect draw
	vkCmdBindIndexBuffer(cmd, m_resourceManager->getGlobalIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
	constexpr uint32_t stride = sizeof(VkDrawIndexedIndirectCommand);

	if (m_multiDrawIndirectSupported && m_multiDrawIndirectEnabled)
		vkCmdDrawIndexedIndirect(cmd, shadowRes.indirectBuffer.m_buffer,
		                         0, shadowRes.totalDraws, stride);
	else
		for (uint32_t i = 0; i < shadowRes.totalDraws; i++)
			vkCmdDrawIndexedIndirect(cmd, shadowRes.indirectBuffer.m_buffer,
			                         i * stride, 1, stride);

	vkCmdEndRendering(cmd);

	// Transition spot shadow map for sampling
	vkutil::transitionImage(cmd,
	                        m_spotShadowMap.m_image,
	                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
	                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
}

void Renderer::drawPointShadowPass(VkCommandBuffer cmd, const ShadowIndirectResources& shadowRes)
{
#ifdef TRACY_ENABLE
	ZoneScopedN("Point Shadow Pass");
#endif

	// Get the shadow-casting point light
	if (m_pointShadowLightIndex >=
	    static_cast<int>(m_mainDrawContext.m_PointLights.size()))
		return;

	const GPUPointLight& shadowLight =
	m_mainDrawContext.m_PointLights[m_pointShadowLightIndex];
	glm::vec3 lightPos = shadowLight.m_position;

	// Calculate 6 view-projection matrices for cube faces
	std::array<glm::mat4, 6> faceMatrices =
	calculatePointLightMatrices(lightPos, 0.1f, m_pointShadowFarPlane);

	// Setup viewport and scissor (same for all faces)
	VkViewport viewport {};
	viewport.x        = 0;
	viewport.y        = 0;
	viewport.width    = static_cast<float>(POINT_SHADOW_MAP_RESOLUTION);
	viewport.height   = static_cast<float>(POINT_SHADOW_MAP_RESOLUTION);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor {};
	scissor.offset = {0, 0};
	scissor.extent = {POINT_SHADOW_MAP_RESOLUTION, POINT_SHADOW_MAP_RESOLUTION};

	// Render each cube face
	for (uint32_t face = 0; face < 6; face++)
	{
		// Transition this cube face to depth attachment
		vkinit::imageBarrier(cmd, m_pointShadowCubeMap.m_image,
		    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0,
		    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		    VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, face, 1);

		// Setup depth attachment for this face
		VkRenderingAttachmentInfo depthAttachment {};
		depthAttachment.sType     = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depthAttachment.imageView = m_pointShadowFaceViews[face];
		depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depthAttachment.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
		depthAttachment.clearValue.depthStencil.depth =
		0.0f; // Reverse-Z: far = 0

		VkRenderingInfo renderInfo {};
		renderInfo.sType      = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderInfo.renderArea = {
		{0, 0}, {POINT_SHADOW_MAP_RESOLUTION, POINT_SHADOW_MAP_RESOLUTION}};
		renderInfo.layerCount           = 1;
		renderInfo.colorAttachmentCount = 0;
		renderInfo.pColorAttachments    = nullptr;
		renderInfo.pDepthAttachment     = &depthAttachment;

		vkCmdBeginRendering(cmd, &renderInfo);

		// Bind point shadow pipeline
		vkCmdBindPipeline(
		cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pointShadowPipeline);

		// Set viewport and scissor
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		// Push per-face constants (once per face, not per object)
		PointShadowIndirectPushConstants pushConst;
		pushConst.m_drawDataBufferPtr = shadowRes.drawDataBDA;
		pushConst.m_lightViewProj     = faceMatrices[face];
		pushConst.m_lightPos          = lightPos;
		pushConst.m_farPlane          = m_pointShadowFarPlane;

		vkCmdPushConstants(cmd,
		                   m_pointShadowPipelineLayout,
		                   VK_SHADER_STAGE_VERTEX_BIT |
		                   VK_SHADER_STAGE_FRAGMENT_BIT,
		                   0,
		                   sizeof(PointShadowIndirectPushConstants),
		                   &pushConst);

		// Bind global index buffer and issue single indirect draw
		vkCmdBindIndexBuffer(cmd, m_resourceManager->getGlobalIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
		constexpr uint32_t stride = sizeof(VkDrawIndexedIndirectCommand);

		if (m_multiDrawIndirectSupported && m_multiDrawIndirectEnabled)
			vkCmdDrawIndexedIndirect(cmd, shadowRes.indirectBuffer.m_buffer,
			                         0, shadowRes.totalDraws, stride);
		else
			for (uint32_t i = 0; i < shadowRes.totalDraws; i++)
				vkCmdDrawIndexedIndirect(cmd, shadowRes.indirectBuffer.m_buffer,
				                         i * stride, 1, stride);

		vkCmdEndRendering(cmd);
	}

	// Transition entire cube map to shader read optimal for sampling
	vkinit::imageBarrier(cmd, m_pointShadowCubeMap.m_image,
	    VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
	    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT,
	    VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
	    VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 6);
}

void Renderer::renderFrame(VkCommandBuffer cmd,
                           uint32_t        swapchainImageIndex,
                           FrameData&      currentFrame)
{
#ifdef TRACY_ENABLE
	ZoneScoped;
#endif

	m_drawExtent.width = static_cast<uint32_t>(
	std::min(m_swapchainManager->getSwapchainExtent().width,
	         m_drawImage.m_imageExtent.width) *
	m_renderScale);
	m_drawExtent.height = static_cast<uint32_t>(
	std::min(m_swapchainManager->getSwapchainExtent().height,
	         m_drawImage.m_imageExtent.height) *
	m_renderScale);

	// Transition MSAA images for rendering
	vkutil::transitionImage(cmd,
	                        m_msaaColorImage.m_image,
	                        VK_IMAGE_LAYOUT_UNDEFINED,
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	vkutil::transitionImage(cmd,
	                        m_depthImage.m_image,
	                        VK_IMAGE_LAYOUT_UNDEFINED,
	                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	// Transition depth resolve target for Hi-Z
	if (m_hizOcclusionEnabled && m_depthResolveImage.m_image != VK_NULL_HANDLE)
	{
		vkutil::transitionImage(cmd,
		                        m_depthResolveImage.m_image,
		                        VK_IMAGE_LAYOUT_UNDEFINED,
		                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
	}

	// Transition resolve target (draw image) for resolve operation
	vkutil::transitionImage(cmd,
	                        m_drawImage.m_image,
	                        VK_IMAGE_LAYOUT_UNDEFINED,
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	// Build shared shadow indirect buffers (once for all shadow passes)
	ShadowIndirectResources shadowRes;
	bool anyShadowPass =
	(m_shadowsEnabled && m_mainDrawContext.m_DirectionalLight.active) ||
	(m_spotShadowsEnabled && !m_mainDrawContext.m_SpotLights.empty()) ||
	(m_pointShadowsEnabled && !m_mainDrawContext.m_PointLights.empty() &&
	 m_pointShadowLightIndex < static_cast<int>(m_mainDrawContext.m_PointLights.size()));

	if (anyShadowPass && !m_mainDrawContext.m_OpaqueSurfaces.empty())
		shadowRes = buildShadowIndirectBuffers(currentFrame);

	// Shadow passes (before main geometry)
	if (m_shadowsEnabled && m_mainDrawContext.m_DirectionalLight.active)
	{
		drawShadowPass(cmd, currentFrame, shadowRes);
	}

	// Spot light shadow pass
	if (m_spotShadowsEnabled && !m_mainDrawContext.m_SpotLights.empty())
	{
		drawSpotShadowPass(cmd, currentFrame, shadowRes);
	}

	// Point light shadow pass
	if (m_pointShadowsEnabled && !m_mainDrawContext.m_PointLights.empty() &&
	    m_pointShadowLightIndex <
	    static_cast<int>(m_mainDrawContext.m_PointLights.size()))
	{
		drawPointShadowPass(cmd, shadowRes);
	}

	// Object ID pass for picking (only runs when picking is requested)
	drawObjectIDPass(cmd, currentFrame);

	drawGeometry(cmd, currentFrame);

	// Debug lines in separate pass (isolated from geometry descriptor buffer state)
	if (m_debugLineVertexCount > 0 && m_debugLineData && m_debugLinePipeline != VK_NULL_HANDLE)
	{
		VkRenderingAttachmentInfo colorAtt = vkinit::attachmentInfoMsaa(
		    m_msaaColorImage.m_imageView, m_drawImage.m_imageView,
		    nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // preserve geometry

		VkRenderingAttachmentInfo depthAtt = vkinit::depthAttachmentInfo(
		    m_depthImage.m_imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
		depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // preserve depth for testing

		VkRenderingInfo renderInfo = vkinit::renderingInfo(m_drawExtent, &colorAtt, &depthAtt);
		vkCmdBeginRendering(cmd, &renderInfo);
		drawDebugLines(cmd, currentFrame);
		vkCmdEndRendering(cmd);
	}

	// Build Hi-Z pyramid for next frame's occlusion culling
	if (m_hizOcclusionEnabled && m_hizDownsamplePipeline != VK_NULL_HANDLE)
	{
		buildHiZPyramid(cmd, currentFrame);
	}

	// transtion the draw image and the swapchain image into their correct
	// transfer layouts
	vkutil::transitionImage(cmd,
	                        m_drawImage.m_image,
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	vkutil::transitionImage(
	cmd,
	m_swapchainManager->getSwapchainImages()[swapchainImageIndex],
	VK_IMAGE_LAYOUT_UNDEFINED,
	VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// execute a copy from the draw image into the swapchain
	vkutil::copyImageToImage(
	cmd,
	m_drawImage.m_image,
	m_swapchainManager->getSwapchainImages()[swapchainImageIndex],
	m_drawExtent,
	m_swapchainManager->getSwapchainExtent());

	vkutil::transitionImage(
	cmd,
	m_swapchainManager->getSwapchainImages()[swapchainImageIndex],
	VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	// Draw UI overlay (editor sets this to ImGui, runtime leaves null)
	if (m_uiDrawCallback)
		m_uiDrawCallback(cmd, m_swapchainManager->getSwapchainImageViews()[swapchainImageIndex]);

	// make the swapchain image into presentable mode
	vkutil::transitionImage(
	cmd,
	m_swapchainManager->getSwapchainImages()[swapchainImageIndex],
	VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

void Renderer::drawBackground(VkCommandBuffer cmd)
{
#ifdef TRACY_ENABLE
	ZoneScoped;
#endif

	ComputeEffect& effect = m_backgroundEffects[m_currentBackgroundEffect];

	// bind the gradient drawing compute pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.m_pipeline);

	// bind the descriptor set containing the draw image for the compute
	// pipeline
	vkCmdBindDescriptorSets(cmd,
	                        VK_PIPELINE_BIND_POINT_COMPUTE,
	                        m_gradientPipelineLayout,
	                        0,
	                        1,
	                        &m_drawImageDescriptors,
	                        0,
	                        nullptr);

	vkCmdPushConstants(cmd,
	                   m_gradientPipelineLayout,
	                   VK_SHADER_STAGE_COMPUTE_BIT,
	                   0,
	                   sizeof(ComputePushConstants),
	                   &effect.m_data);

	// execute the compute pipeline dispatch. We are using 16x16 workgroup size
	// so we need to divide by it
	vkCmdDispatch(cmd,
	              static_cast<uint32_t>(std::ceil(m_drawExtent.width / 16.0)),
	              static_cast<uint32_t>(std::ceil(m_drawExtent.height / 16.0)),
	              1);
}


void Renderer::drawGeometry(VkCommandBuffer cmd, FrameData& currentFrame)
{
#ifdef TRACY_ENABLE
	ZoneScoped;
#endif

	// reset counters
	m_stats.m_drawcallCount  = 0;
	m_stats.m_triangleCount  = 0;

	// Read back pipeline statistics from the PREVIOUS frame's query
	{
		uint32_t readIdx = (m_statsFrameIndex + 1) % STATS_FRAME_OVERLAP;
		uint64_t statsResult = 0;
		VkResult qr = vkGetQueryPoolResults(
		m_device, m_statsQueryPool[readIdx], 0, 1,
		sizeof(uint64_t), &statsResult, sizeof(uint64_t),
		VK_QUERY_RESULT_64_BIT);
		if (qr == VK_SUCCESS)
			m_stats.m_renderedTriangles = static_cast<int>(statsResult);
		// VK_NOT_READY means previous frame not done yet — keep old value
	}

	// begin clock
	auto start = std::chrono::system_clock::now();

	std::vector<uint32_t> opaqueDraws;
	opaqueDraws.reserve(m_mainDrawContext.m_OpaqueSurfaces.size());
	std::vector<uint32_t> transparentDraws;
	transparentDraws.reserve(m_mainDrawContext.m_TransparentSurfaces.size());

	// Include all surfaces — GPU compute shader handles frustum + occlusion culling
	for (uint32_t i = 0; i < m_mainDrawContext.m_OpaqueSurfaces.size(); i++)
		opaqueDraws.push_back(i);
	for (uint32_t i = 0; i < m_mainDrawContext.m_TransparentSurfaces.size(); i++)
		transparentDraws.push_back(i);

	{
#ifdef TRACY_ENABLE
		ZoneScopedN("Sort Opaque");
#endif
		//  sort the opaque surfaces by material and mesh
		std::sort(opaqueDraws.begin(),
		          opaqueDraws.end(),
		          [&](const auto& iA, const auto& iB)
		          {
			          const RenderObject& A =
			          m_mainDrawContext.m_OpaqueSurfaces[iA];
			          const RenderObject& B =
			          m_mainDrawContext.m_OpaqueSurfaces[iB];
			          return A.m_material < B.m_material;
		          });
	}

	{
#ifdef TRACY_ENABLE
		ZoneScopedN("Sort Transparent");
#endif
		//  sort the transparent surfaces by distance from bounds to the camera
		std::sort(
		transparentDraws.begin(),
		transparentDraws.end(),
		[&](const auto& iA, const auto& iB)
		{
			const RenderObject& A = m_mainDrawContext.m_TransparentSurfaces[iA];
			const RenderObject& B = m_mainDrawContext.m_TransparentSurfaces[iB];
			// Calculate distance from camera to object center
			glm::vec3 centerA =
			glm::vec3(A.m_transform * glm::vec4(A.m_bounds.m_origin, 1.0f));
			glm::vec3 centerB =
			glm::vec3(B.m_transform * glm::vec4(B.m_bounds.m_origin, 1.0f));

			float distA = glm::length(m_camera->m_position - centerA);
			float distB = glm::length(m_camera->m_position - centerB);

			// Sort back to front (larger distance first)
			return distA > distB;
		});
	}

	// =========================================================
	// Scene UBO setup (before render pass for compute access)
	// =========================================================
	ResourceManager* rm = m_resourceManager;

	//  allocate a new uniform buffer for the scene data
	AllocatedBuffer gpuSceneDataBuffer =
	m_resourceManager->createBuffer(sizeof(GPUSceneData),
	                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
	                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	                                VMA_MEMORY_USAGE_CPU_TO_GPU);

	// allocate storage buffer for light data
	AllocatedBuffer gpuLightDataBuffer =
	m_resourceManager->createBuffer(sizeof(GPULightData),
	                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
	                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	                                VMA_MEMORY_USAGE_CPU_TO_GPU);

	VkDebugName(m_device, VK_OBJECT_TYPE_BUFFER,
	             (uint64_t)gpuSceneDataBuffer.m_buffer, "PerFrame_SceneDataUBO");
	VkDebugName(m_device, VK_OBJECT_TYPE_BUFFER,
	             (uint64_t)gpuLightDataBuffer.m_buffer, "PerFrame_LightDataSSBO");

	// add buffers to the deletion queue of this frame so they get deleted once
	// used
	currentFrame.m_deletionQueue.push_function(
	[rm, gpuSceneDataBuffer, gpuLightDataBuffer]()
	{
		rm->destroyBuffer(gpuSceneDataBuffer);
		rm->destroyBuffer(gpuLightDataBuffer);
	});

	// write the scene data buffer
	GPUSceneData* sceneUniformData =
	(GPUSceneData*) gpuSceneDataBuffer.m_info.pMappedData;
	*sceneUniformData = m_sceneData;

	// write the light data buffer
	GPULightData* lightData =
	(GPULightData*) gpuLightDataBuffer.m_info.pMappedData;
	lightData->m_numPointLights =
	static_cast<uint32_t>(std::min(m_mainDrawContext.m_PointLights.size(),
	                               static_cast<size_t>(MAX_POINT_LIGHTS)));
	lightData->m_numSpotLights =
	static_cast<uint32_t>(std::min(m_mainDrawContext.m_SpotLights.size(),
	                               static_cast<size_t>(MAX_SPOT_LIGHTS)));
	for (uint32_t i = 0; i < lightData->m_numPointLights; ++i)
	{
		lightData->m_pointLights[i] = m_mainDrawContext.m_PointLights[i];
	}
	for (uint32_t i = 0; i < lightData->m_numSpotLights; ++i)
	{
		lightData->m_spotLights[i] = m_mainDrawContext.m_SpotLights[i];
	}

	// Allocate descriptor buffer space for scene data
	VkDeviceSize sceneDescriptorOffset =
	currentFrame.m_descriptorBuffer.allocate(m_gpuSceneDataLayoutInfo);

	// Get buffer device addresses
	VkBufferDeviceAddressInfo addressInfo {};
	addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;

	addressInfo.buffer = gpuSceneDataBuffer.m_buffer;
	VkDeviceAddress sceneDataAddress =
	vkGetBufferDeviceAddress(m_device, &addressInfo);

	addressInfo.buffer = gpuLightDataBuffer.m_buffer;
	VkDeviceAddress lightDataAddress =
	vkGetBufferDeviceAddress(m_device, &addressInfo);

	// Write descriptors directly to descriptor buffer
	void* sceneDescriptorPtr =
	currentFrame.m_descriptorBuffer.getPtrAtOffset(sceneDescriptorOffset);

	// Binding 0: Scene data uniform buffer
	m_descriptorBufferWriter.writeUniformBuffer(
	sceneDescriptorPtr,
	m_gpuSceneDataLayoutInfo.bindingOffsets[0],
	sceneDataAddress,
	sizeof(GPUSceneData));

	// Binding 1: Light data storage buffer
	m_descriptorBufferWriter.writeStorageBuffer(
	sceneDescriptorPtr,
	m_gpuSceneDataLayoutInfo.bindingOffsets[1],
	lightDataAddress,
	sizeof(GPULightData));

	// Binding 2: Directional shadow map combined image sampler
	m_descriptorBufferWriter.writeImageSampler(
	sceneDescriptorPtr,
	m_gpuSceneDataLayoutInfo.bindingOffsets[2],
	m_shadowMap.m_imageView,
	m_shadowSampler,
	VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

	// Binding 3: Spot shadow map combined image sampler
	m_descriptorBufferWriter.writeImageSampler(
	sceneDescriptorPtr,
	m_gpuSceneDataLayoutInfo.bindingOffsets[3],
	m_spotShadowMap.m_imageView,
	m_shadowSampler,
	VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

	// Binding 4: Point shadow cube map combined image sampler
	m_descriptorBufferWriter.writeImageSampler(
	sceneDescriptorPtr,
	m_gpuSceneDataLayoutInfo.bindingOffsets[4],
	m_pointShadowCubeMap.m_imageView,
	m_pointShadowSampler,
	VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);

	// Binding 5: Hi-Z depth pyramid (for GPU occlusion culling)
	if (m_hizReady && m_hizSampler != VK_NULL_HANDLE)
	{
		m_descriptorBufferWriter.writeImageSampler(
		sceneDescriptorPtr,
		m_gpuSceneDataLayoutInfo.bindingOffsets[5],
		m_hizImage.m_imageView,
		m_hizSampler,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	else
	{
		// Placeholder when Hi-Z not yet built (first frame)
		m_descriptorBufferWriter.writeImageSampler(
		sceneDescriptorPtr,
		m_gpuSceneDataLayoutInfo.bindingOffsets[5],
		m_shadowMap.m_imageView,
		m_shadowSampler,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
	}

	// Bind descriptor buffers once before any draws (bindless architecture)
	VkDescriptorBufferBindingInfoEXT bufferBindings[4] = {};

	// Buffer 0: Frame descriptor buffer (scene data)
	bufferBindings[0].sType =
	VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
	bufferBindings[0].address =
	currentFrame.m_descriptorBuffer.getDeviceAddress();
	bufferBindings[0].usage =
	VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
	VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;

	// Buffer 1: Global texture array (bindless)
	bufferBindings[1].sType =
	VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
	bufferBindings[1].address = m_textureRegistry.getBufferAddress();
	bufferBindings[1].usage =
	VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;

	// Buffer 2: Sampler array (bindless)
	bufferBindings[2].sType =
	VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
	bufferBindings[2].address = m_samplerRegistry.getBufferAddress();
	bufferBindings[2].usage =
	VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT |
	VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;

	// Buffer 3: Material SSBO (bindless)
	bufferBindings[3].sType =
	VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
	bufferBindings[3].address = m_materialRegistry.getBufferAddress();
	bufferBindings[3].usage =
	VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;

	vkCmdBindDescriptorBuffersEXT(cmd, 4, bufferBindings);

	// Offsets for bindless descriptor sets (all at offset 0)
	VkDeviceSize textureOffset  = 0;
	VkDeviceSize samplerOffset  = 0;
	VkDeviceSize materialOffset = 0;

	// =========================================================
	// Build indirect command buffer + draw data SSBO
	// =========================================================
	const uint32_t totalDraws =
	static_cast<uint32_t>(opaqueDraws.size() + transparentDraws.size());

	// Helper lambda: fill indirect commands and draw data for a set of draws
	auto fillDrawData = [&](const std::vector<uint32_t>&    drawIndices,
	                        const std::vector<RenderObject>& surfaces,
	                        VkDrawIndexedIndirectCommand*    cmdBuf,
	                        GPUDrawData*                     dataBuf,
	                        uint32_t                         baseIndex)
	{
		for (uint32_t i = 0; i < drawIndices.size(); i++)
		{
			const RenderObject& r            = surfaces[drawIndices[i]];
			const uint32_t      drawIndex    = baseIndex + i;

			// Fill indirect command
			cmdBuf[drawIndex].indexCount    = r.m_indexCount;
			cmdBuf[drawIndex].instanceCount = 1;
			cmdBuf[drawIndex].firstIndex    = r.m_firstIndex;
			cmdBuf[drawIndex].vertexOffset  = 0;
			cmdBuf[drawIndex].firstInstance = drawIndex; // maps to SV_InstanceID

			// Fill per-draw data
			dataBuf[drawIndex].m_worldMatrix   = r.m_transform;
			dataBuf[drawIndex].m_vertexBuffer  = r.m_vertexBufferAddress;
			dataBuf[drawIndex].m_materialIndex = r.m_material->m_materialIndex;
			dataBuf[drawIndex].m_padding       = 0;

			// Accumulate stats
			m_stats.m_triangleCount += r.m_indexCount / 3;
		}
	};

	AllocatedBuffer indirectBuffer {};    // input: all draws (opaque + transparent)
	AllocatedBuffer drawDataBuffer {};
	AllocatedBuffer drawCountBuffer {};
	VkDeviceAddress drawDataBDA = 0;
	uint32_t transparentBase = 0;

	// Compacted opaque buffers (set by GPU cull, fallback to input if no cull)
	AllocatedBuffer opaqueIndirectBuffer {};
	VkDeviceAddress opaqueDrawDataBDA = 0;

	if (totalDraws > 0)
	{
		// Allocate indirect command buffer (extra flags for GPU culling compute access)
		const VkDeviceSize indirectBufSize =
		totalDraws * sizeof(VkDrawIndexedIndirectCommand);

		VkBufferUsageFlags indirectUsage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
		                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		                                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

		indirectBuffer = m_resourceManager->createBuffer(
		indirectBufSize,
		indirectUsage,
		VMA_MEMORY_USAGE_CPU_TO_GPU);

		// Allocate draw data SSBO
		const VkDeviceSize drawDataBufSize = totalDraws * sizeof(GPUDrawData);
		drawDataBuffer = m_resourceManager->createBuffer(
        drawDataBufSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);

		// Add to frame deletion queue
		currentFrame.m_deletionQueue.push_function(
		[rm, indirectBuffer, drawDataBuffer]()
		{
			rm->destroyBuffer(indirectBuffer);
			rm->destroyBuffer(drawDataBuffer);
		});

		auto* indirectCmds =
		static_cast<VkDrawIndexedIndirectCommand*>(indirectBuffer.m_info.pMappedData);
		auto* drawDataPtr =
		static_cast<GPUDrawData*>(drawDataBuffer.m_info.pMappedData);

		// Fill opaque draws (starting at index 0)
		fillDrawData(opaqueDraws,
		             m_mainDrawContext.m_OpaqueSurfaces,
		             indirectCmds,
		             drawDataPtr,
		             0);

		// Fill transparent draws (starting after opaques)
		transparentBase = static_cast<uint32_t>(opaqueDraws.size());
		fillDrawData(transparentDraws,
		             m_mainDrawContext.m_TransparentSurfaces,
		             indirectCmds,
		             drawDataPtr,
		             transparentBase);

		// Get BDA for draw data buffer
		VkBufferDeviceAddressInfo drawDataAddrInfo {};
		drawDataAddrInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
		drawDataAddrInfo.buffer = drawDataBuffer.m_buffer;
		drawDataBDA = vkGetBufferDeviceAddress(m_device, &drawDataAddrInfo);

		// Default: opaque uses same buffers as input (overridden by cull compaction)
		opaqueIndirectBuffer = indirectBuffer;
		opaqueDrawDataBDA    = drawDataBDA;

		// No batching needed — global index buffer bound once

		// =========================================================
		// GPU frustum + occlusion culling with draw compaction
		// =========================================================
		if (m_cullPipeline != VK_NULL_HANDLE && !opaqueDraws.empty())
		{
#ifdef TRACY_ENABLE
			ZoneScopedN("GPU Cull Dispatch");
#endif
			const uint32_t opaqueCount = static_cast<uint32_t>(opaqueDraws.size());

			// Allocate bounds buffer (per-opaque draw, CPU->GPU)
			AllocatedBuffer boundsBuffer = m_resourceManager->createBuffer(
			opaqueCount * sizeof(GPUBoundsData),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VMA_MEMORY_USAGE_CPU_TO_GPU);

			auto* boundsPtr = static_cast<GPUBoundsData*>(boundsBuffer.m_info.pMappedData);
			for (uint32_t i = 0; i < opaqueCount; i++)
			{
				const RenderObject& r = m_mainDrawContext.m_OpaqueSurfaces[opaqueDraws[i]];
				boundsPtr[i].m_aabbMin     = r.m_bounds.m_origin - r.m_bounds.m_extents;
				boundsPtr[i].m_aabbMax     = r.m_bounds.m_origin + r.m_bounds.m_extents;
				boundsPtr[i].m_worldMatrix = r.m_transform;
			}

			// Allocate compaction output buffers (GPU-only)
			AllocatedBuffer indirectBufferOut = m_resourceManager->createBuffer(
			opaqueCount * sizeof(VkDrawIndexedIndirectCommand),
			VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY);

			AllocatedBuffer drawDataOut = m_resourceManager->createBuffer(
			opaqueCount * sizeof(GPUDrawData),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY);

			// Atomic draw count buffer (GPU-only, reset via vkCmdFillBuffer)
			drawCountBuffer = m_resourceManager->createBuffer(
			sizeof(uint32_t),
			VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY);

			currentFrame.m_deletionQueue.push_function(
			[rm, boundsBuffer, indirectBufferOut, drawDataOut, drawCountBuffer]()
			{
				rm->destroyBuffer(boundsBuffer);
				rm->destroyBuffer(indirectBufferOut);
				rm->destroyBuffer(drawDataOut);
				rm->destroyBuffer(drawCountBuffer);
			});

			// Reset draw count to 0
			vkCmdFillBuffer(cmd, drawCountBuffer.m_buffer, 0, sizeof(uint32_t), 0);
			vkinit::memoryBarrier(cmd,
			    VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			    VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);

			// Get BDAs
			VkBufferDeviceAddressInfo bdaInfo {};
			bdaInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;

			bdaInfo.buffer = boundsBuffer.m_buffer;
			VkDeviceAddress boundsBDA = vkGetBufferDeviceAddress(m_device, &bdaInfo);

			bdaInfo.buffer = indirectBuffer.m_buffer;
			VkDeviceAddress indirectInBDA = vkGetBufferDeviceAddress(m_device, &bdaInfo);

			bdaInfo.buffer = indirectBufferOut.m_buffer;
			VkDeviceAddress indirectOutBDA = vkGetBufferDeviceAddress(m_device, &bdaInfo);

			bdaInfo.buffer = drawDataBuffer.m_buffer;
			VkDeviceAddress drawDataInBDA = drawDataBDA; // already computed above

			bdaInfo.buffer = drawDataOut.m_buffer;
			VkDeviceAddress drawDataOutBDA = vkGetBufferDeviceAddress(m_device, &bdaInfo);

			bdaInfo.buffer = drawCountBuffer.m_buffer;
			VkDeviceAddress drawCountBDA = vkGetBufferDeviceAddress(m_device, &bdaInfo);

			// Bind cull compute pipeline
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cullPipeline);

			uint32_t     cullBufferIndex = 0;
			VkDeviceSize cullOffset      = sceneDescriptorOffset;
			vkCmdSetDescriptorBufferOffsetsEXT(
			cmd,
			VK_PIPELINE_BIND_POINT_COMPUTE,
			m_cullPipelineLayout,
			0, 1,
			&cullBufferIndex,
			&cullOffset);

			// Push constants
			CullPushConstants cullPC {};
			cullPC.m_boundsBufferPtr      = boundsBDA;
			cullPC.m_indirectBufferInPtr  = indirectInBDA;
			cullPC.m_indirectBufferOutPtr = indirectOutBDA;
			cullPC.m_drawDataInPtr        = drawDataInBDA;
			cullPC.m_drawDataOutPtr       = drawDataOutBDA;
			cullPC.m_drawCountPtr         = drawCountBDA;
			cullPC.m_drawCount            = opaqueCount;
			cullPC.m_hizEnabled           = (m_hizReady && m_hizOcclusionEnabled) ? 1 : 0;
			cullPC.m_hizWidth             = m_hizImage.m_imageExtent.width;
			cullPC.m_hizHeight            = m_hizImage.m_imageExtent.height;
			vkCmdPushConstants(cmd,
			                   m_cullPipelineLayout,
			                   VK_SHADER_STAGE_COMPUTE_BIT,
			                   0,
			                   sizeof(CullPushConstants),
			                   &cullPC);

			// Dispatch (256 threads per group)
			vkCmdDispatch(cmd, (opaqueCount + 255) / 256, 1, 1);

			// Barrier: compute write -> (indirect read + shader storage read)
			vkinit::memoryBarrier(cmd,
			    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT,
			    VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
			    VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT);

			// Store compacted output for opaque draw calls below
			// Keep original buffers for transparent draws (they're not culled)
			opaqueIndirectBuffer = indirectBufferOut;
			opaqueDrawDataBDA   = drawDataOutBDA;
		}
	}

	// =========================================================
	// Begin render pass
	// =========================================================
	VkRenderingAttachmentInfo colorAttachment =
	vkinit::attachmentInfoMsaa(m_msaaColorImage.m_imageView,
	                           m_drawImage.m_imageView,
	                           nullptr,
	                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingAttachmentInfo depthAttachment = vkinit::depthAttachmentInfo(
	m_depthImage.m_imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	// Add MSAA depth resolve for Hi-Z pyramid
	if (m_hizOcclusionEnabled && m_hizDownsamplePipeline != VK_NULL_HANDLE &&
	    m_depthResolveImage.m_image != VK_NULL_HANDLE)
	{
		depthAttachment.resolveMode       = VK_RESOLVE_MODE_MIN_BIT;
		depthAttachment.resolveImageView  = m_depthResolveImage.m_imageView;
		depthAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	}

	VkRenderingInfo renderInfo =
	vkinit::renderingInfo(m_drawExtent, &colorAttachment, &depthAttachment);
	// Begin pipeline statistics query (must be outside render pass)
	uint32_t writeIdx = m_statsFrameIndex;
	vkCmdResetQueryPool(cmd, m_statsQueryPool[writeIdx], 0, 1);
	vkCmdBeginQuery(cmd, m_statsQueryPool[writeIdx], 0, 0);

	vkCmdBeginRendering(cmd, &renderInfo);

	if (totalDraws > 0)
	{
		// Helper: bind pipeline state once per pass
		auto bindPipelineState = [&](MaterialPipeline* pipeline, VkDeviceAddress dataBDA)
		{
			vkCmdBindPipeline(
			cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->m_pipeline);

			// Bind all 4 descriptor buffer offsets (bindless)
			uint32_t     bufferIndices[4] = {0, 1, 2, 3};
			VkDeviceSize offsets[4]       = {sceneDescriptorOffset,
			                                 textureOffset,
			                                 samplerOffset,
			                                 materialOffset};
			vkCmdSetDescriptorBufferOffsetsEXT(
			cmd,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipeline->m_layout,
			0,
			4,
			bufferIndices,
			offsets);

			// Set dynamic viewport and scissor
			VkViewport viewport = {};
			viewport.x          = 0;
			viewport.y          = 0;
			viewport.width      = static_cast<float>(m_drawExtent.width);
			viewport.height     = static_cast<float>(m_drawExtent.height);
			viewport.minDepth   = 0.f;
			viewport.maxDepth   = 1.f;
			vkCmdSetViewport(cmd, 0, 1, &viewport);

			VkRect2D scissor      = {};
			scissor.offset.x      = 0;
			scissor.offset.y      = 0;
			scissor.extent.width  = m_drawExtent.width;
			scissor.extent.height = m_drawExtent.height;
			vkCmdSetScissor(cmd, 0, 1, &scissor);

			// Push indirect draw constants (BDA to draw data)
			IndirectDrawPushConstants pushConst;
			pushConst.m_drawDataBufferPtr = dataBDA;
			vkCmdPushConstants(cmd,
			                   pipeline->m_layout,
			                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			                   0,
			                   sizeof(IndirectDrawPushConstants),
			                   &pushConst);
		};

		// Bind global index buffer once for all draws
		vkCmdBindIndexBuffer(cmd, m_resourceManager->getGlobalIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

		constexpr uint32_t stride = sizeof(VkDrawIndexedIndirectCommand);

		// === Draw opaque (compacted by GPU cull) ===
		if (!opaqueDraws.empty())
		{
#ifdef TRACY_ENABLE
			ZoneScopedN("Draw Opaque Indirect");
#endif
			const uint32_t opaqueCount = static_cast<uint32_t>(opaqueDraws.size());
			const RenderObject& firstOpaque =
			m_mainDrawContext.m_OpaqueSurfaces[opaqueDraws[0]];
			bindPipelineState(firstOpaque.m_material->m_pipeline, opaqueDrawDataBDA);

			if (drawCountBuffer.m_buffer != VK_NULL_HANDLE)
			{
				// Compacted path: draw count comes from GPU atomic counter
				vkCmdDrawIndexedIndirectCount(cmd,
				    opaqueIndirectBuffer.m_buffer, 0,
				    drawCountBuffer.m_buffer, 0,
				    opaqueCount, stride);
			}
			else
			{
				// Fallback: no cull pipeline, draw all
				vkCmdDrawIndexedIndirect(cmd, opaqueIndirectBuffer.m_buffer,
				                         0, opaqueCount, stride);
			}
			m_stats.m_drawcallCount++;
		}

		// === Draw transparent (uses original input buffers, not compacted) ===
		if (!transparentDraws.empty())
		{
#ifdef TRACY_ENABLE
			ZoneScopedN("Draw Transparent Indirect");
#endif
			const uint32_t transpCount = static_cast<uint32_t>(transparentDraws.size());
			const RenderObject& firstTransparent =
			m_mainDrawContext.m_TransparentSurfaces[transparentDraws[0]];
			bindPipelineState(firstTransparent.m_material->m_pipeline, drawDataBDA);

			VkDeviceSize transpOffset = transparentBase * stride;
			if (m_multiDrawIndirectSupported && m_multiDrawIndirectEnabled)
			{
				vkCmdDrawIndexedIndirect(cmd, indirectBuffer.m_buffer,
				                         transpOffset, transpCount, stride);
			}
			else
			{
				for (uint32_t i = 0; i < transpCount; i++)
					vkCmdDrawIndexedIndirect(cmd, indirectBuffer.m_buffer,
					                         transpOffset + i * stride, 1, stride);
			}
			m_stats.m_drawcallCount++;
		}
	}

	{
#ifdef TRACY_ENABLE
		ZoneScopedN("Draw Skybox");
#endif
		// Draw skybox last (after all geometry)
		// Pass frame buffer address so skybox can rebind its own descriptor
		// buffers
		m_skybox->draw(cmd,
		               sceneDescriptorOffset,
		               currentFrame.m_descriptorBuffer.getDeviceAddress(),
		               m_drawExtent,
		               m_resourceManager->getGlobalIndexBuffer());
	}

	vkCmdEndRendering(cmd);

	// End pipeline statistics query and advance frame index (must be outside render pass)
	vkCmdEndQuery(cmd, m_statsQueryPool[writeIdx], 0);
	m_statsFrameIndex = (m_statsFrameIndex + 1) % STATS_FRAME_OVERLAP;

	auto end = std::chrono::system_clock::now();

	// convert to microseconds (integer), and then come back to miliseconds
	auto elapsed =
	std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	m_stats.m_meshDrawTime = elapsed.count() / 1000.f;
}

void Renderer::updateScene(float /*deltaTime*/, VkExtent2D /*windowExtent*/)
{
#ifdef TRACY_ENABLE
	ZoneScoped;
#endif

	// begin clock
	auto start = std::chrono::system_clock::now();

	m_mainDrawContext.m_OpaqueSurfaces.clear();
	m_mainDrawContext.m_TransparentSurfaces.clear();
	m_mainDrawContext.m_PointLights.clear();
	m_mainDrawContext.m_SpotLights.clear();
	m_mainDrawContext.m_DirectionalLight =
	DirectionalLightData {}; // Reset directional light

	// Use active camera matrices (set by Application via setActiveCamera)
	glm::mat4 view       = m_activeCamView;
	glm::mat4 projection = m_activeCamProjection;
	glm::mat4 viewProj   = projection * view;

	// === QUERY RENDERABLES DIRECTLY FROM ECS ===
	if (m_world)
	{
		m_world->get()
		.query<const TransformComponent,
		       const agni::ecs::RenderMeshComponent,
		       const RenderableTag>()
		.each(
		[&](flecs::entity                         e,
		    const TransformComponent&             transform,
		    const agni::ecs::RenderMeshComponent& mesh,
		    const RenderableTag&                  renderable)
		{
			// Skip invisible or invalid meshes
			if (!renderable.visible || !mesh.visible || !mesh.meshAsset)
				return;

			// Create RenderObjects for each surface in the mesh
			for (const auto& surface : mesh.meshAsset->m_surfaces)
			{
				RenderObject obj;
				obj.m_indexCount = surface.m_count;
				obj.m_firstIndex = mesh.meshAsset->m_meshBuffers.m_globalIndexOffset
				                 + surface.m_startIndex;
				obj.m_material  = &surface.m_material->m_data;
				obj.m_bounds    = surface.m_bounds;
				obj.m_transform = transform.worldTransform;
				obj.m_vertexBufferAddress =
				mesh.meshAsset->m_meshBuffers.m_vertexBufferAddress;
				obj.m_entityID = e.id();

				// Sort into opaque or transparent based on material pass type
				if (surface.m_material->m_data.m_passType ==
				    MaterialPass::Transparent)
				{
					m_mainDrawContext.m_TransparentSurfaces.push_back(obj);
				}
				else
				{
					m_mainDrawContext.m_OpaqueSurfaces.push_back(obj);
				}
			}
		});

		// === QUERY LIGHTS DIRECTLY FROM ECS ===
		m_world->get()
		.query<const TransformComponent, const LightComponent>()
		.each(
		[&](const TransformComponent& transform, const LightComponent& light)
		{
			// Extract world position from transform matrix
			glm::vec3 worldPosition = glm::vec3(transform.worldTransform[3]);

			// Transform direction by rotation (upper 3x3 of the matrix)
			glm::vec3 worldDirection = glm::normalize(
			glm::mat3(transform.worldTransform) * light.direction);

			switch (light.type)
			{
				case LightType::Point:
				{
					if (m_mainDrawContext.m_PointLights.size() <
					    MAX_POINT_LIGHTS)
					{
						GPUPointLight gpuLight;
						gpuLight.m_position  = worldPosition;
						gpuLight.m_color     = light.color;
						gpuLight.m_intensity = light.intensity;
						gpuLight.m_radius    = light.radius;
						m_mainDrawContext.m_PointLights.push_back(gpuLight);
					}
					break;
				}

				case LightType::Directional:
				{
					// Only one directional light supported (last one wins)
					m_mainDrawContext.m_DirectionalLight.direction =
					worldDirection;
					m_mainDrawContext.m_DirectionalLight.color = light.color;
					m_mainDrawContext.m_DirectionalLight.intensity =
					light.intensity;
					m_mainDrawContext.m_DirectionalLight.active = true;
					break;
				}

				case LightType::Spot:
				{
					if (m_mainDrawContext.m_SpotLights.size() < MAX_SPOT_LIGHTS)
					{
						GPUSpotLight gpuLight;
						gpuLight.m_position  = worldPosition;
						gpuLight.m_direction = worldDirection;
						gpuLight.m_color     = light.color;
						gpuLight.m_intensity = light.intensity;
						gpuLight.m_radius    = light.radius;
						gpuLight.m_innerCutoff =
						glm::cos(glm::radians(light.innerConeAngle));
						gpuLight.m_outerCutoff =
						glm::cos(glm::radians(light.outerConeAngle));
						m_mainDrawContext.m_SpotLights.push_back(gpuLight);
					}
					break;
				}
			}
		});
	}

	m_sceneData.m_view     = view;
	m_sceneData.m_proj     = projection;
	m_sceneData.m_viewproj = viewProj;

	// Ambient lighting
	m_sceneData.m_ambientColor = glm::vec4(.1f);

	// Use directional light from DrawContext if active, otherwise use defaults
	if (m_mainDrawContext.m_DirectionalLight.active)
	{
		m_sceneData.m_sunlightDirection =
		glm::vec4(m_mainDrawContext.m_DirectionalLight.direction,
		          m_mainDrawContext.m_DirectionalLight.intensity);
		m_sceneData.m_sunlightColor =
		glm::vec4(m_mainDrawContext.m_DirectionalLight.color,
		          m_mainDrawContext.m_DirectionalLight.intensity);
	}
	else
	{
		// Default lighting when no directional light is in scene
		m_sceneData.m_sunlightDirection = glm::vec4(0, 1, 0.5, 1.f);
		m_sceneData.m_sunlightColor     = glm::vec4(1.f);
	}

	m_sceneData.m_cameraPosition = m_activeCamPosition;

	// Gribb-Hartmann: extract + normalize 6 frustum planes from viewProj
	// Each plane stored as (nx, ny, nz, d) where nx*x + ny*y + nz*z + d = 0
	{
		const glm::mat4& m = viewProj;
		// Left
		m_sceneData.m_frustumPlanes[0] = glm::vec4(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0], m[3][3] + m[3][0]);
		// Right
		m_sceneData.m_frustumPlanes[1] = glm::vec4(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0], m[3][3] - m[3][0]);
		// Bottom
		m_sceneData.m_frustumPlanes[2] = glm::vec4(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1], m[3][3] + m[3][1]);
		// Top
		m_sceneData.m_frustumPlanes[3] = glm::vec4(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1], m[3][3] - m[3][1]);
		// Near (reverse-Z: w+z for near plane)
		m_sceneData.m_frustumPlanes[4] = glm::vec4(m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2], m[3][3] + m[3][2]);
		// Far (reverse-Z: w-z for far plane)
		m_sceneData.m_frustumPlanes[5] = glm::vec4(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2], m[3][3] - m[3][2]);

		// Normalize each plane
		for (int i = 0; i < 6; i++)
		{
			float len = glm::length(glm::vec3(m_sceneData.m_frustumPlanes[i]));
			if (len > 0.0f)
				m_sceneData.m_frustumPlanes[i] /= len;
		}
	}

	// Calculate shadow mapping data for directional light
	if (m_shadowsEnabled && m_mainDrawContext.m_DirectionalLight.active)
	{
		glm::vec3 lightDir =
		glm::normalize(m_mainDrawContext.m_DirectionalLight.direction);
		m_sceneData.m_lightSpaceMatrix = calculateLightSpaceMatrix(lightDir);
		m_sceneData.m_shadowParams =
		glm::vec4(m_shadowBias,
		          m_shadowNormalBias,
		          1.0f / static_cast<float>(SHADOW_MAP_RESOLUTION),
		          1.0f); // enabled
	}
	else
	{
		m_sceneData.m_lightSpaceMatrix = glm::mat4(1.0f);
		m_sceneData.m_shadowParams =
		glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // disabled
	}

	// Calculate shadow mapping data for spot light (use first spot light)
	if (m_spotShadowsEnabled && !m_mainDrawContext.m_SpotLights.empty())
	{
		const GPUSpotLight& spotLight      = m_mainDrawContext.m_SpotLights[0];
		m_sceneData.m_spotLightSpaceMatrix = calculateSpotLightSpaceMatrix(
		spotLight.m_position,
		spotLight.m_direction,
		glm::degrees(
		glm::acos(spotLight.m_outerCutoff))); // Convert back to angle
		m_sceneData.m_spotShadowParams =
		glm::vec4(m_spotShadowBias,
		          m_spotShadowNormalBias,
		          0.0f,  // Spot light index (first one = 0)
		          1.0f); // enabled
	}
	else
	{
		m_sceneData.m_spotLightSpaceMatrix = glm::mat4(1.0f);
		m_sceneData.m_spotShadowParams =
		glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // disabled
	}

	// Calculate shadow mapping data for point light
	if (m_pointShadowsEnabled && !m_mainDrawContext.m_PointLights.empty() &&
	    m_pointShadowLightIndex <
	    static_cast<int>(m_mainDrawContext.m_PointLights.size()))
	{
		const GPUPointLight& pointLight =
		m_mainDrawContext.m_PointLights[m_pointShadowLightIndex];
		m_sceneData.m_pointLightShadowPos = pointLight.m_position;
		m_sceneData.m_pointShadowParams   = glm::vec4(
        m_pointShadowBias,
        m_pointShadowPCFEnabled ? m_pointShadowPCFRadius
		                          : 0.0f, // PCF radius (0 = hard shadows)
        m_pointShadowFarPlane,
        static_cast<float>(m_pointShadowLightIndex +
                           1)); // +1 so 0 means disabled
	}
	else
	{
		m_sceneData.m_pointLightShadowPos = glm::vec3(0.0f);
		m_sceneData.m_pointShadowParams   = glm::vec4(0.0f); // disabled
	}

	auto end = std::chrono::system_clock::now();

	// convert to microseconds (integer), and then come back to miliseconds
	auto elapsed =
	std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	m_stats.m_sceneUpdateTime = elapsed.count() / 1000.f;
}

void Renderer::initPickingResources(VkExtent2D windowExtent)
{
	// Create object ID render target (R32_UINT for direct integer storage)
	VkExtent3D        extent = {windowExtent.width, windowExtent.height, 1};
	VkImageUsageFlags colorUsage =
	VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	m_objectIDImage = m_resourceManager->createImage(
	extent,
	VK_FORMAT_R32G32_UINT, // Two 32-bit channels for full 64-bit entity ID
	colorUsage,
	false,
	VK_SAMPLE_COUNT_1_BIT); // No MSAA for picking

	// Create non-MSAA depth buffer for picking pass
	VkImageUsageFlags depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	m_pickingDepthImage          = m_resourceManager->createImage(
    extent,
    VK_FORMAT_D32_SFLOAT,
    depthUsage,
    false,
    VK_SAMPLE_COUNT_1_BIT); // No MSAA for picking

	// Create staging buffer for reading back pixel data (8 bytes for
	// R32G32_UINT)
	m_pickingStagingBuffer = m_resourceManager->createBuffer(
	8, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU);
}

void Renderer::initObjectIDPipeline()
{
	// Load object ID shaders
	VkShaderModule vertexShader;
	VkShaderModule fragmentShader;

	if (!vkutil::loadShaderModule(
	    resPath("shaders/slang/ObjectId.vert.spv").c_str(), m_device, &vertexShader))
	{
		AGNI_PRINT("Failed to load objectid vertex shader\n");
		return;
	}

	if (!vkutil::loadShaderModule(
	    resPath("shaders/slang/ObjectId.frag.spv").c_str(), m_device, &fragmentShader))
	{
		AGNI_PRINT("Failed to load objectid fragment shader\n");
		vkDestroyShaderModule(m_device, vertexShader, nullptr);
		return;
	}

	// Create pipeline layout with push constants
	VkPushConstantRange pushConstantRange {};
	pushConstantRange.stageFlags =
	VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size   = sizeof(ObjectIDPushConstants);

	VkPipelineLayoutCreateInfo layoutInfo {};
	layoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts    = &m_gpuSceneDataDescriptorLayout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges    = &pushConstantRange;

	VK_CHECK(vkCreatePipelineLayout(
	m_device, &layoutInfo, nullptr, &m_objectIDPipelineLayout));

	// Build pipeline
	PipelineBuilder builder;
	builder.setShaders(vertexShader, fragmentShader);
	builder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	builder.setPolygonMode(VK_POLYGON_MODE_FILL);
	builder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
	builder.setMultisamplingNone();
	builder.disableBlending();
	builder.enableDepthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL); // Reversed-Z
	builder.setColorAttachmentFormat(
	VK_FORMAT_R32G32_UINT); // 64-bit entity ID (2x uint32)
	builder.setDepthFormat(VK_FORMAT_D32_SFLOAT);
	builder.m_pipelineLayout = m_objectIDPipelineLayout;
	builder.enableDescriptorBuffer();

	m_objectIDPipeline = builder.buildPipeline(m_device);
	VkDebugName(m_device, VK_OBJECT_TYPE_PIPELINE, (uint64_t)m_objectIDPipeline, "ObjectIDPipeline");

	// Cleanup shader modules
	vkDestroyShaderModule(m_device, vertexShader, nullptr);
	vkDestroyShaderModule(m_device, fragmentShader, nullptr);
}

void Renderer::initDebugLinePipeline()
{
	VkShaderModule vertexShader;
	VkShaderModule fragmentShader;

	if (!vkutil::loadShaderModule(
	    resPath("shaders/slang/DebugLines.vert.spv").c_str(), m_device, &vertexShader))
	{
		AGNI_PRINT("Failed to load debug lines vertex shader\n");
		return;
	}

	if (!vkutil::loadShaderModule(
	    resPath("shaders/slang/DebugLines.frag.spv").c_str(), m_device, &fragmentShader))
	{
		AGNI_PRINT("Failed to load debug lines fragment shader\n");
		vkDestroyShaderModule(m_device, vertexShader, nullptr);
		return;
	}

	VkPushConstantRange pushConstantRange {};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstantRange.offset     = 0;
	pushConstantRange.size       = sizeof(DebugLinePushConstants);

	VkPipelineLayoutCreateInfo layoutInfo {};
	layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount         = 0;       // No descriptor sets — fully push-constant driven
	layoutInfo.pSetLayouts            = nullptr;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges    = &pushConstantRange;

	VK_CHECK(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_debugLinePipelineLayout));

	PipelineBuilder builder;
	builder.setShaders(vertexShader, fragmentShader);
	builder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
	builder.setPolygonMode(VK_POLYGON_MODE_FILL);
	builder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
	builder.enableMultisampling(m_msaaSamples);
	builder.enableBlendingAlphablend();
	builder.enableDepthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);
	builder.setColorAttachmentFormat(VK_FORMAT_R16G16B16A16_SFLOAT);
	builder.setDepthFormat(VK_FORMAT_D32_SFLOAT);
	builder.m_pipelineLayout = m_debugLinePipelineLayout;
	builder.enableDescriptorBuffer(); // Required: descriptor buffers are bound in drawGeometry

	m_debugLinePipeline = builder.buildPipeline(m_device);
	VkDebugName(m_device, VK_OBJECT_TYPE_PIPELINE, (uint64_t)m_debugLinePipeline, "DebugLinePipeline");

	vkDestroyShaderModule(m_device, vertexShader, nullptr);
	vkDestroyShaderModule(m_device, fragmentShader, nullptr);

	AGNI_PRINT("[Renderer] Debug line pipeline created\n");
}

void Renderer::drawDebugLines(VkCommandBuffer cmd, FrameData& currentFrame)
{
	if (m_debugLineVertexCount == 0 || !m_debugLineData || m_debugLinePipeline == VK_NULL_HANDLE)
		return;

	const size_t bufferSize = m_debugLineVertexCount * 16; // 16 bytes per LineVertex

	AllocatedBuffer lineBuffer = m_resourceManager->createBuffer(
	    bufferSize,
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	    VMA_MEMORY_USAGE_CPU_TO_GPU);

	std::memcpy(lineBuffer.m_info.pMappedData, m_debugLineData, bufferSize);

	VkDebugName(m_device, VK_OBJECT_TYPE_BUFFER,
	             (uint64_t)lineBuffer.m_buffer, "PerFrame_DebugLineBuffer");

	auto* rm = m_resourceManager;
	currentFrame.m_deletionQueue.push_function([rm, lineBuffer]() {
		rm->destroyBuffer(lineBuffer);
	});

	VkBufferDeviceAddressInfo addrInfo {};
	addrInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	addrInfo.buffer = lineBuffer.m_buffer;
	VkDeviceAddress lineBufferAddress = vkGetBufferDeviceAddress(m_device, &addrInfo);

	// Set viewport and scissor (required for new render pass)
	VkViewport viewport = {};
	viewport.width  = static_cast<float>(m_drawExtent.width);
	viewport.height = static_cast<float>(m_drawExtent.height);
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.extent = m_drawExtent;
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_debugLinePipeline);

	DebugLinePushConstants pc {};
	pc.m_viewproj     = m_sceneData.m_viewproj;
	pc.m_vertexBuffer = lineBufferAddress;
	vkCmdPushConstants(cmd, m_debugLinePipelineLayout,
	                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	                   0, sizeof(DebugLinePushConstants), &pc);

	vkCmdDraw(cmd, m_debugLineVertexCount, 1, 0, 0);
}

void Renderer::requestPicking(float x, float y)
{
	m_pickingRequested   = true;
	m_pickingScreenPos   = glm::vec2(x, y);
	m_pickingResultReady = false;
}

void Renderer::drawObjectIDPass(VkCommandBuffer cmd, FrameData& currentFrame)
{
	if (!m_pickingRequested)
		return;

	// Transition object ID image for rendering
	vkutil::transitionImage(cmd,
	                        m_objectIDImage.m_image,
	                        VK_IMAGE_LAYOUT_UNDEFINED,
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	// Transition picking depth image for rendering
	vkutil::transitionImage(cmd,
	                        m_pickingDepthImage.m_image,
	                        VK_IMAGE_LAYOUT_UNDEFINED,
	                        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	// Setup rendering attachment (clear to 0 = no entity)
	VkClearValue clearValue = {};
	clearValue.color        = {{0, 0, 0, 0}};

	VkRenderingAttachmentInfo colorAttachment =
	vkinit::attachmentInfo(m_objectIDImage.m_imageView,
	                       &clearValue,
	                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	// Use non-MSAA depth buffer for picking (matches color sample count)
	VkRenderingAttachmentInfo depthAttachment = vkinit::depthAttachmentInfo(
	m_pickingDepthImage.m_imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	VkRenderingInfo renderInfo =
	vkinit::renderingInfo(m_drawExtent, &colorAttachment, &depthAttachment);

	vkCmdBeginRendering(cmd, &renderInfo);

	// Bind pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_objectIDPipeline);

	// Set viewport and scissor
	VkViewport viewport = {};
	viewport.x          = 0;
	viewport.y          = 0;
	viewport.width      = static_cast<float>(m_drawExtent.width);
	viewport.height     = static_cast<float>(m_drawExtent.height);
	viewport.minDepth   = 0.f;
	viewport.maxDepth   = 1.f;
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset   = {0, 0};
	scissor.extent   = m_drawExtent;
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	// Create temporary scene data buffer (like drawGeometry does)
	AllocatedBuffer gpuSceneDataBuffer =
	m_resourceManager->createBuffer(sizeof(GPUSceneData),
	                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
	                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	                                VMA_MEMORY_USAGE_CPU_TO_GPU);

	// Create temporary light data buffer (required by descriptor layout)
	AllocatedBuffer gpuLightDataBuffer =
	m_resourceManager->createBuffer(sizeof(GPULightData),
	                                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
	                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	                                VMA_MEMORY_USAGE_CPU_TO_GPU);

	// Schedule deletion after frame is done
	ResourceManager* rm = m_resourceManager;
	currentFrame.m_deletionQueue.push_function(
	[rm, gpuSceneDataBuffer, gpuLightDataBuffer]()
	{
		rm->destroyBuffer(gpuSceneDataBuffer);
		rm->destroyBuffer(gpuLightDataBuffer);
	});

	// Write scene data
	GPUSceneData* sceneUniformData =
	(GPUSceneData*) gpuSceneDataBuffer.m_info.pMappedData;
	*sceneUniformData = m_sceneData;

	// Write empty light data (not used for picking, but required by layout)
	GPULightData* lightData =
	(GPULightData*) gpuLightDataBuffer.m_info.pMappedData;
	lightData->m_numPointLights = 0;
	lightData->m_numSpotLights  = 0;

	// Allocate descriptor buffer space for scene data
	VkDeviceSize sceneDescriptorOffset =
	currentFrame.m_descriptorBuffer.allocate(m_gpuSceneDataLayoutInfo);

	// Get buffer device addresses
	VkBufferDeviceAddressInfo addressInfo {};
	addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;

	addressInfo.buffer = gpuSceneDataBuffer.m_buffer;
	VkDeviceAddress sceneDataAddress =
	vkGetBufferDeviceAddress(m_device, &addressInfo);

	addressInfo.buffer = gpuLightDataBuffer.m_buffer;
	VkDeviceAddress lightDataAddress =
	vkGetBufferDeviceAddress(m_device, &addressInfo);

	// Write descriptors directly to descriptor buffer
	void* sceneDescriptorPtr =
	currentFrame.m_descriptorBuffer.getPtrAtOffset(sceneDescriptorOffset);

	m_descriptorBufferWriter.writeUniformBuffer(
	sceneDescriptorPtr,
	m_gpuSceneDataLayoutInfo.bindingOffsets[0],
	sceneDataAddress,
	sizeof(GPUSceneData));

	m_descriptorBufferWriter.writeStorageBuffer(
	sceneDescriptorPtr,
	m_gpuSceneDataLayoutInfo.bindingOffsets[1],
	lightDataAddress,
	sizeof(GPULightData));

	// Bind descriptor buffers (frame buffer only - no materials in picking
	// pass)
	VkDescriptorBufferBindingInfoEXT bufferBinding = {};
	bufferBinding.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
	bufferBinding.address = currentFrame.m_descriptorBuffer.getDeviceAddress();
	bufferBinding.usage   = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
	                      VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;

	vkCmdBindDescriptorBuffersEXT(cmd, 1, &bufferBinding);

	// Set descriptor buffer offset for scene data (set 0)
	uint32_t bufferIndex = 0;
	vkCmdSetDescriptorBufferOffsetsEXT(cmd,
	                                   VK_PIPELINE_BIND_POINT_GRAPHICS,
	                                   m_objectIDPipelineLayout,
	                                   0, // first set
	                                   1, // descriptor count
	                                   &bufferIndex,
	                                   &sceneDescriptorOffset);

	// Bind global index buffer once for all object ID draws
	vkCmdBindIndexBuffer(cmd, m_resourceManager->getGlobalIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

	// Draw all opaque objects with their entity IDs
	for (const auto& obj : m_mainDrawContext.m_OpaqueSurfaces)
	{
		ObjectIDPushConstants pushConstants;
		pushConstants.m_worldMatrix  = obj.m_transform;
		pushConstants.m_vertexBuffer = obj.m_vertexBufferAddress;
		pushConstants.m_entityID =
		obj.m_entityID; // Full 64-bit ID, no truncation

		vkCmdPushConstants(cmd,
		                   m_objectIDPipelineLayout,
		                   VK_SHADER_STAGE_VERTEX_BIT |
		                   VK_SHADER_STAGE_FRAGMENT_BIT,
		                   0,
		                   sizeof(ObjectIDPushConstants),
		                   &pushConstants);

		vkCmdDrawIndexed(cmd, obj.m_indexCount, 1, obj.m_firstIndex, 0, 0);
	}

	vkCmdEndRendering(cmd);

	// Transition for copy
	vkutil::transitionImage(cmd,
	                        m_objectIDImage.m_image,
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	// Copy the pixel at the mouse position to the staging buffer
	int pixelX = static_cast<int>(m_pickingScreenPos.x);
	int pixelY = static_cast<int>(m_pickingScreenPos.y);

	// Clamp to image bounds
	pixelX = std::clamp(pixelX, 0, static_cast<int>(m_drawExtent.width) - 1);
	pixelY = std::clamp(pixelY, 0, static_cast<int>(m_drawExtent.height) - 1);

	VkBufferImageCopy copyRegion               = {};
	copyRegion.bufferOffset                    = 0;
	copyRegion.bufferRowLength                 = 0;
	copyRegion.bufferImageHeight               = 0;
	copyRegion.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.imageSubresource.mipLevel       = 0;
	copyRegion.imageSubresource.baseArrayLayer = 0;
	copyRegion.imageSubresource.layerCount     = 1;
	copyRegion.imageOffset                     = {pixelX, pixelY, 0};
	copyRegion.imageExtent                     = {1, 1, 1};

	vkCmdCopyImageToBuffer(cmd,
	                       m_objectIDImage.m_image,
	                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	                       m_pickingStagingBuffer.m_buffer,
	                       1,
	                       &copyRegion);

	m_pickingRequested = false;
	m_pickingFramesLeft =
	2; // Wait 2 frames for GPU to complete (FRAME_OVERLAP)
}

void Renderer::processPickingResult()
{
	if (m_pickingFramesLeft <= 0)
		return;

	m_pickingFramesLeft--;

	if (m_pickingFramesLeft == 0)
	{
		// GPU has finished, read the staging buffer
		void* data;
		vmaMapMemory(m_resourceManager->getAllocator(),
		             m_pickingStagingBuffer.m_allocation,
		             &data);

		// Reconstruct 64-bit entity ID from R32G32_UINT format
		uint32_t* pixel        = static_cast<uint32_t*>(data);
		uint64_t  entityIDLow  = pixel[0];
		uint64_t  entityIDHigh = pixel[1];
		m_lastPickedEntityID   = (entityIDHigh << 32) | entityIDLow;

		vmaUnmapMemory(m_resourceManager->getAllocator(),
		               m_pickingStagingBuffer.m_allocation);

		m_pickingResultReady = true;
	}
}
