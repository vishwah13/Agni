#include <Renderer.hpp>

#include <AgniEngine.hpp>
#include <Components.hpp>
#include <Debug.hpp>
#include <ECS/World.hpp>
#include <Images.hpp>
#include <Initializers.hpp>
#include <Pipelines.hpp>
#include <VulkanTools.hpp>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <chrono>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

static bool isVisible(const RenderObject& obj, const glm::mat4& viewproj)
{
#ifdef TRACY_ENABLE
	ZoneScoped;
#endif

	std::array<glm::vec3, 8> corners {
	glm::vec3 {1, 1, 1},
	glm::vec3 {1, 1, -1},
	glm::vec3 {1, -1, 1},
	glm::vec3 {1, -1, -1},
	glm::vec3 {-1, 1, 1},
	glm::vec3 {-1, 1, -1},
	glm::vec3 {-1, -1, 1},
	glm::vec3 {-1, -1, -1},
	};

	glm::mat4 matrix = viewproj * obj.m_transform;

	glm::vec3 min = {1.5, 1.5, 1.5};
	glm::vec3 max = {-1.5, -1.5, -1.5};

	for (int c = 0; c < 8; c++)
	{
		// project each corner into clip space
		glm::vec4 v = matrix * glm::vec4(obj.m_bounds.m_origin +
		                                 (corners[c] * obj.m_bounds.m_extents),
		                                 1.f);

		// perspective correction
		v.x = v.x / v.w;
		v.y = v.y / v.w;
		v.z = v.z / v.w;

		min = glm::min(glm::vec3 {v.x, v.y, v.z}, min);
		max = glm::max(glm::vec3 {v.x, v.y, v.z}, max);
	}

	// check the clip space box is within the view
	if (min.z > 1.f || max.z < 0.f || min.x > 1.f || max.x < -1.f ||
	    min.y > 1.f || max.y < -1.f)
	{
		return false;
	}
	else
	{
		return true;
	}
}

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
	initShadowResources();
	initPointShadowResources();
	initDescriptors();
	initBackgroundPipelines();
	initShadowPipeline();
	initPointShadowPipeline();
	initCullPipeline();

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
	VkPipelineLayoutCreateInfo computeLayout {};
	computeLayout.sType       = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computeLayout.pNext       = nullptr;
	computeLayout.pSetLayouts = &m_drawImageDescriptorLayout;
	computeLayout.setLayoutCount = 1;

	VkPushConstantRange pushConstant {};
	pushConstant.offset     = 0;
	pushConstant.size       = sizeof(ComputePushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	computeLayout.pPushConstantRanges    = &pushConstant;
	computeLayout.pushConstantRangeCount = 1;

	VK_CHECK(vkCreatePipelineLayout(
	m_device, &computeLayout, nullptr, &m_gradientPipelineLayout));

	VkShaderModule gradientShader;
	if (!vkutil::loadShaderModule(resPath("shaders/slang/gradient_color.comp.spv").c_str(),
	                              m_device,
	                              &gradientShader))
	{
		AGNI_PRINT("Error when building the compute shader \n");
	}

	VkShaderModule skyShader;
	if (!vkutil::loadShaderModule(
	    resPath("shaders/slang/sky.comp.spv").c_str(), m_device, &skyShader))
	{
		AGNI_PRINT("Error when building the compute shader \n");
	}

	VkPipelineShaderStageCreateInfo stageinfo {};
	stageinfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageinfo.pNext  = nullptr;
	stageinfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
	stageinfo.module = gradientShader;
	stageinfo.pName  = "main";

	VkComputePipelineCreateInfo computePipelineCreateInfo {};
	computePipelineCreateInfo.sType =
	VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineCreateInfo.pNext = nullptr;
	computePipelineCreateInfo.flags =
	VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
	computePipelineCreateInfo.layout             = m_gradientPipelineLayout;
	computePipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
	computePipelineCreateInfo.stage              = stageinfo;

	ComputeEffect gradient;
	gradient.m_layout = m_gradientPipelineLayout;
	gradient.m_name   = "gradient";
	gradient.m_data   = {};

	// default colors
	gradient.m_data.m_data1 = glm::vec4(1, 0, 0, 1);
	gradient.m_data.m_data2 = glm::vec4(0, 0, 1, 1);

	VK_CHECK(vkCreateComputePipelines(m_device,
	                                  VK_NULL_HANDLE,
	                                  1,
	                                  &computePipelineCreateInfo,
	                                  nullptr,
	                                  &gradient.m_pipeline));

	// change the shader module only to create the sky shader
	computePipelineCreateInfo.stage.module = skyShader;

	ComputeEffect sky;
	sky.m_layout = m_gradientPipelineLayout;
	sky.m_name   = "sky";
	sky.m_data   = {};
	// default sky parameters
	sky.m_data.m_data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

	VK_CHECK(vkCreateComputePipelines(m_device,
	                                  VK_NULL_HANDLE,
	                                  1,
	                                  &computePipelineCreateInfo,
	                                  nullptr,
	                                  &sky.m_pipeline));

	// add the 2 background effects into the array
	m_backgroundEffects.push_back(gradient);
	m_backgroundEffects.push_back(sky);

	vkDestroyShaderModule(m_device, gradientShader, nullptr);
	vkDestroyShaderModule(m_device, skyShader, nullptr);
}

void Renderer::initShadowPipeline()
{
	// Load shadow pass shader (vertex only, no fragment for depth-only pass)
	VkShaderModule shadowVertShader;
	if (!vkutil::loadShaderModule(
	    resPath("shaders/slang/shadow.vert.spv").c_str(), m_device, &shadowVertShader))
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

	vkDestroyShaderModule(m_device, shadowVertShader, nullptr);

	AGNI_PRINT("Shadow pipeline created successfully\n");
}

void Renderer::initPointShadowPipeline()
{
	// Load point shadow pass shaders (vertex + fragment for linear depth
	// output)
	VkShaderModule pointShadowVertShader;
	if (!vkutil::loadShaderModule(resPath("shaders/slang/point_shadow.vert.spv").c_str(),
	                              m_device,
	                              &pointShadowVertShader))
	{
		AGNI_PRINT("Failed to load point shadow vertex shader\n");
		return;
	}

	VkShaderModule pointShadowFragShader;
	if (!vkutil::loadShaderModule(resPath("shaders/slang/point_shadow.frag.spv").c_str(),
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

	vkDestroyShaderModule(m_device, pointShadowVertShader, nullptr);
	vkDestroyShaderModule(m_device, pointShadowFragShader, nullptr);

	AGNI_PRINT("Point shadow pipeline created successfully\n");
}

void Renderer::initCullPipeline()
{
	VkShaderModule cullShader;
	if (!vkutil::loadShaderModule(resPath("shaders/slang/frustum_cull.comp.spv").c_str(),
	                              m_device,
	                              &cullShader))
	{
		AGNI_PRINT("Failed to load frustum cull compute shader — GPU culling disabled\n");
		m_gpuCullingEnabled = false;
		return;
	}

	// Pipeline layout: 1 descriptor set (scene data), push constants for CullPushConstants
	VkPushConstantRange pushConstantRange {};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	pushConstantRange.offset     = 0;
	pushConstantRange.size       = sizeof(CullPushConstants);

	VkPipelineLayoutCreateInfo layoutInfo {};
	layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount         = 1;
	layoutInfo.pSetLayouts            = &m_gpuSceneDataDescriptorLayout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges    = &pushConstantRange;

	VK_CHECK(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &m_cullPipelineLayout));

	VkPipelineShaderStageCreateInfo stageInfo {};
	stageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
	stageInfo.module = cullShader;
	stageInfo.pName  = "main";

	VkComputePipelineCreateInfo pipelineInfo {};
	pipelineInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.flags  = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
	pipelineInfo.layout = m_cullPipelineLayout;
	pipelineInfo.stage  = stageInfo;

	VK_CHECK(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_cullPipeline));

	vkDestroyShaderModule(m_device, cullShader, nullptr);
	AGNI_PRINT("Frustum cull compute pipeline created successfully\n");
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

	// Create index array and sort by index buffer for batching
	std::vector<uint32_t> drawIndices(res.totalDraws);
	for (uint32_t i = 0; i < res.totalDraws; i++)
		drawIndices[i] = i;

	std::sort(drawIndices.begin(), drawIndices.end(),
	          [&surfaces](uint32_t a, uint32_t b)
	          { return surfaces[a].m_indexBuffer < surfaces[b].m_indexBuffer; });

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

	// Fill indirect commands and draw data
	auto* indirectCmds =
	static_cast<VkDrawIndexedIndirectCommand*>(res.indirectBuffer.m_info.pMappedData);
	auto* drawDataPtr =
	static_cast<GPUDrawData*>(res.drawDataBuffer.m_info.pMappedData);

	for (uint32_t i = 0; i < res.totalDraws; i++)
	{
		const RenderObject& r = surfaces[drawIndices[i]];

		indirectCmds[i].indexCount    = r.m_indexCount;
		indirectCmds[i].instanceCount = 1;
		indirectCmds[i].firstIndex    = r.m_firstIndex;
		indirectCmds[i].vertexOffset  = 0;
		indirectCmds[i].firstInstance = i; // maps to SV_InstanceID

		drawDataPtr[i].m_worldMatrix   = r.m_transform;
		drawDataPtr[i].m_vertexBuffer  = r.m_vertexBufferAddress;
		drawDataPtr[i].m_materialIndex = 0; // unused by shadow shaders
		drawDataPtr[i].m_padding       = 0;
	}

	// Get BDA for draw data buffer
	VkBufferDeviceAddressInfo drawDataAddrInfo {};
	drawDataAddrInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	drawDataAddrInfo.buffer = res.drawDataBuffer.m_buffer;
	res.drawDataBDA = vkGetBufferDeviceAddress(m_device, &drawDataAddrInfo);

	// Build batches by grouping consecutive draws with same index buffer
	IndirectBatch currentBatch;
	currentBatch.indexBuffer       = surfaces[drawIndices[0]].m_indexBuffer;
	currentBatch.firstCommandIndex = 0;
	currentBatch.commandCount      = 1;

	for (uint32_t i = 1; i < res.totalDraws; i++)
	{
		VkBuffer idxBuf = surfaces[drawIndices[i]].m_indexBuffer;
		if (idxBuf == currentBatch.indexBuffer)
		{
			currentBatch.commandCount++;
		}
		else
		{
			res.batches.push_back(currentBatch);
			currentBatch.indexBuffer       = idxBuf;
			currentBatch.firstCommandIndex = i;
			currentBatch.commandCount      = 1;
		}
	}
	res.batches.push_back(currentBatch);

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

	// Issue indirect draws by batch
	constexpr uint32_t stride = sizeof(VkDrawIndexedIndirectCommand);
	for (const auto& batch : shadowRes.batches)
	{
		vkCmdBindIndexBuffer(cmd, batch.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		VkDeviceSize offset = batch.firstCommandIndex * stride;

		if (m_multiDrawIndirectSupported && m_multiDrawIndirectEnabled)
			vkCmdDrawIndexedIndirect(cmd, shadowRes.indirectBuffer.m_buffer,
			                         offset, batch.commandCount, stride);
		else
			for (uint32_t i = 0; i < batch.commandCount; i++)
				vkCmdDrawIndexedIndirect(cmd, shadowRes.indirectBuffer.m_buffer,
				                         offset + i * stride, 1, stride);
	}

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

	// Issue indirect draws by batch
	constexpr uint32_t stride = sizeof(VkDrawIndexedIndirectCommand);
	for (const auto& batch : shadowRes.batches)
	{
		vkCmdBindIndexBuffer(cmd, batch.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		VkDeviceSize offset = batch.firstCommandIndex * stride;

		if (m_multiDrawIndirectSupported && m_multiDrawIndirectEnabled)
			vkCmdDrawIndexedIndirect(cmd, shadowRes.indirectBuffer.m_buffer,
			                         offset, batch.commandCount, stride);
		else
			for (uint32_t i = 0; i < batch.commandCount; i++)
				vkCmdDrawIndexedIndirect(cmd, shadowRes.indirectBuffer.m_buffer,
				                         offset + i * stride, 1, stride);
	}

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
		VkImageMemoryBarrier2 barrier {};
		barrier.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		barrier.srcAccessMask = 0;
		barrier.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		barrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout     = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		barrier.image         = m_pointShadowCubeMap.m_image;
		barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
		barrier.subresourceRange.baseMipLevel   = 0;
		barrier.subresourceRange.levelCount     = 1;
		barrier.subresourceRange.baseArrayLayer = face;
		barrier.subresourceRange.layerCount     = 1;

		VkDependencyInfo depInfo {};
		depInfo.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers    = &barrier;
		vkCmdPipelineBarrier2(cmd, &depInfo);

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

		// Issue indirect draws by batch
		constexpr uint32_t stride = sizeof(VkDrawIndexedIndirectCommand);
		for (const auto& batch : shadowRes.batches)
		{
			vkCmdBindIndexBuffer(cmd, batch.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
			VkDeviceSize offset = batch.firstCommandIndex * stride;

			if (m_multiDrawIndirectSupported && m_multiDrawIndirectEnabled)
				vkCmdDrawIndexedIndirect(cmd, shadowRes.indirectBuffer.m_buffer,
				                         offset, batch.commandCount, stride);
			else
				for (uint32_t i = 0; i < batch.commandCount; i++)
					vkCmdDrawIndexedIndirect(cmd, shadowRes.indirectBuffer.m_buffer,
					                         offset + i * stride, 1, stride);
		}

		vkCmdEndRendering(cmd);
	}

	// Transition entire cube map to shader read optimal for sampling
	VkImageMemoryBarrier2 finalBarrier {};
	finalBarrier.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	finalBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	finalBarrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	finalBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
	finalBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
	finalBarrier.oldLayout     = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	finalBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
	finalBarrier.image     = m_pointShadowCubeMap.m_image;
	finalBarrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT;
	finalBarrier.subresourceRange.baseMipLevel   = 0;
	finalBarrier.subresourceRange.levelCount     = 1;
	finalBarrier.subresourceRange.baseArrayLayer = 0;
	finalBarrier.subresourceRange.layerCount     = 6; // All 6 faces

	VkDependencyInfo finalDepInfo {};
	finalDepInfo.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	finalDepInfo.imageMemoryBarrierCount = 1;
	finalDepInfo.pImageMemoryBarriers    = &finalBarrier;
	vkCmdPipelineBarrier2(cmd, &finalDepInfo);
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

	drawImgui(
	cmd, m_swapchainManager->getSwapchainImageViews()[swapchainImageIndex]);

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

void Renderer::drawImgui(VkCommandBuffer cmd, VkImageView targetImageView)
{
#ifdef TRACY_ENABLE
	ZoneScoped;
#endif

	VkRenderingAttachmentInfo colorAttachment = vkinit::attachmentInfo(
	targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo = vkinit::renderingInfo(
	m_swapchainManager->getSwapchainExtent(), &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}

void Renderer::drawGeometry(VkCommandBuffer cmd, FrameData& currentFrame)
{
#ifdef TRACY_ENABLE
	ZoneScoped;
#endif

	// reset counters
	m_stats.m_drawcallCount  = 0;
	m_stats.m_triangleCount  = 0;
	m_stats.m_gpuCullingActive = false;

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

	const bool gpuCulling = m_gpuCullingEnabled && m_cullPipeline != VK_NULL_HANDLE;

	std::vector<uint32_t> opaqueDraws;
	opaqueDraws.reserve(m_mainDrawContext.m_OpaqueSurfaces.size());
	std::vector<uint32_t> transparentDraws;
	transparentDraws.reserve(m_mainDrawContext.m_TransparentSurfaces.size());

	{
#ifdef TRACY_ENABLE
		ZoneScopedN("Frustum Culling");
#endif
		if (gpuCulling)
		{
			// GPU culling — include ALL opaques, compute shader does the culling
			for (uint32_t i = 0; i < m_mainDrawContext.m_OpaqueSurfaces.size(); i++)
				opaqueDraws.push_back(i);
		}
		else
		{
			for (uint32_t i = 0; i < m_mainDrawContext.m_OpaqueSurfaces.size(); i++)
			{
				if (isVisible(m_mainDrawContext.m_OpaqueSurfaces[i],
				              m_sceneData.m_viewproj))
				{
					opaqueDraws.push_back(i);
				}
			}
		}
		// Transparent surfaces always use CPU culling
		for (uint32_t i = 0; i < m_mainDrawContext.m_TransparentSurfaces.size();
		     i++)
		{
			if (isVisible(m_mainDrawContext.m_TransparentSurfaces[i],
			              m_sceneData.m_viewproj))
			{
				transparentDraws.push_back(i);
			}
		}
	}

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
			          if (A.m_material == B.m_material)
			          {
				          return A.m_indexBuffer < B.m_indexBuffer;
			          }
			          else
			          {
				          return A.m_material < B.m_material;
			          }
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

	AllocatedBuffer indirectBuffer {};
	AllocatedBuffer drawDataBuffer {};
	AllocatedBuffer drawCountBuffer {};
	VkDeviceAddress drawDataBDA = 0;
	std::vector<IndirectBatch> opaqueBatches;
	std::vector<IndirectBatch> transparentBatches;
	uint32_t transparentBase = 0;

	if (totalDraws > 0)
	{
		// Allocate indirect command buffer (extra flags for GPU culling compute access)
		const VkDeviceSize indirectBufSize =
		totalDraws * sizeof(VkDrawIndexedIndirectCommand);

		VkBufferUsageFlags indirectUsage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		if (gpuCulling)
			indirectUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
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

		// Build batches by index buffer for a range of draws
		auto buildBatches = [&](uint32_t rangeStart,
		                        uint32_t rangeCount,
		                        const std::vector<uint32_t>&    drawIndices,
		                        const std::vector<RenderObject>& surfaces)
		-> std::vector<IndirectBatch>
		{
			std::vector<IndirectBatch> batches;
			if (rangeCount == 0)
				return batches;

			IndirectBatch currentBatch;
			currentBatch.indexBuffer       = surfaces[drawIndices[0]].m_indexBuffer;
			currentBatch.firstCommandIndex = rangeStart;
			currentBatch.commandCount      = 1;

			for (uint32_t i = 1; i < rangeCount; i++)
			{
				VkBuffer idxBuf = surfaces[drawIndices[i]].m_indexBuffer;
				if (idxBuf == currentBatch.indexBuffer)
				{
					currentBatch.commandCount++;
				}
				else
				{
					batches.push_back(currentBatch);
					currentBatch.indexBuffer       = idxBuf;
					currentBatch.firstCommandIndex = rangeStart + i;
					currentBatch.commandCount      = 1;
				}
			}
			batches.push_back(currentBatch);
			return batches;
		};

		// Build batches (needed before GPU cull for drawCountBuffer)
		opaqueBatches = buildBatches(
		0,
		static_cast<uint32_t>(opaqueDraws.size()),
		opaqueDraws,
		m_mainDrawContext.m_OpaqueSurfaces);

		transparentBatches = buildBatches(
		transparentBase,
		static_cast<uint32_t>(transparentDraws.size()),
		transparentDraws,
		m_mainDrawContext.m_TransparentSurfaces);

		// =========================================================
		// GPU frustum culling dispatch (before render pass)
		// =========================================================
		if (gpuCulling && !opaqueDraws.empty())
		{
#ifdef TRACY_ENABLE
			ZoneScopedN("GPU Frustum Cull Dispatch");
#endif
			const uint32_t opaqueCount = static_cast<uint32_t>(opaqueDraws.size());
			m_stats.m_gpuCullingActive = true;

			// Allocate bounds buffer (per-opaque draw)
			AllocatedBuffer boundsBuffer = m_resourceManager->createBuffer(
			opaqueCount * sizeof(GPUBoundsData),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VMA_MEMORY_USAGE_CPU_TO_GPU);

			// Fill bounds data
			auto* boundsPtr = static_cast<GPUBoundsData*>(boundsBuffer.m_info.pMappedData);
			for (uint32_t i = 0; i < opaqueCount; i++)
			{
				const RenderObject& r = m_mainDrawContext.m_OpaqueSurfaces[opaqueDraws[i]];
				boundsPtr[i].m_origin       = r.m_bounds.m_origin;
				boundsPtr[i].m_sphereRadius = r.m_bounds.m_sphereRadius;
				boundsPtr[i].m_worldMatrix  = r.m_transform;
			}

			// Allocate draw count buffer (one uint32_t per opaque batch)
			const uint32_t numOpaqueBatches = static_cast<uint32_t>(opaqueBatches.size());
			drawCountBuffer = m_resourceManager->createBuffer(
			numOpaqueBatches * sizeof(uint32_t),
			VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
			VMA_MEMORY_USAGE_CPU_TO_GPU);

			auto* countPtr = static_cast<uint32_t*>(drawCountBuffer.m_info.pMappedData);
			for (uint32_t i = 0; i < numOpaqueBatches; i++)
				countPtr[i] = opaqueBatches[i].commandCount;

			// Add to frame deletion queue
			currentFrame.m_deletionQueue.push_function(
			[rm, boundsBuffer, drawCountBuffer]()
			{
				rm->destroyBuffer(boundsBuffer);
				rm->destroyBuffer(drawCountBuffer);
			});

			// Get BDAs
			VkBufferDeviceAddressInfo bdaInfo {};
			bdaInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;

			bdaInfo.buffer = boundsBuffer.m_buffer;
			VkDeviceAddress boundsBDA = vkGetBufferDeviceAddress(m_device, &bdaInfo);

			bdaInfo.buffer = indirectBuffer.m_buffer;
			VkDeviceAddress indirectBDA = vkGetBufferDeviceAddress(m_device, &bdaInfo);

			// Bind cull compute pipeline
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_cullPipeline);

			// Set descriptor buffer offsets for compute bind point (set 0 = scene data)
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
			cullPC.m_boundsBufferPtr   = boundsBDA;
			cullPC.m_indirectBufferPtr = indirectBDA;
			cullPC.m_drawCount         = opaqueCount;
			vkCmdPushConstants(cmd,
			                   m_cullPipelineLayout,
			                   VK_SHADER_STAGE_COMPUTE_BIT,
			                   0,
			                   sizeof(CullPushConstants),
			                   &cullPC);

			// Dispatch
			vkCmdDispatch(cmd, (opaqueCount + 63) / 64, 1, 1);

			// Memory barrier: compute write -> indirect command read
			VkMemoryBarrier2 barrier {};
			barrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
			barrier.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			barrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
			barrier.dstStageMask  = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
			barrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;

			VkDependencyInfo depInfo {};
			depInfo.sType              = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
			depInfo.memoryBarrierCount = 1;
			depInfo.pMemoryBarriers    = &barrier;

			vkCmdPipelineBarrier2(cmd, &depInfo);
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
		auto bindPipelineState = [&](MaterialPipeline* pipeline)
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
			pushConst.m_drawDataBufferPtr = drawDataBDA;
			vkCmdPushConstants(cmd,
			                   pipeline->m_layout,
			                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			                   0,
			                   sizeof(IndirectDrawPushConstants),
			                   &pushConst);
		};

		// Helper: issue draws for batches
		auto issueBatchDraws = [&](const std::vector<IndirectBatch>& batches,
		                           bool useIndirectCount,
		                           uint32_t batchIndexOffset)
		{
			constexpr uint32_t stride = sizeof(VkDrawIndexedIndirectCommand);
			for (uint32_t bIdx = 0; bIdx < batches.size(); bIdx++)
			{
				const auto& batch = batches[bIdx];
				vkCmdBindIndexBuffer(
				cmd, batch.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

				VkDeviceSize bufferOffset =
				batch.firstCommandIndex * stride;

				if (useIndirectCount)
				{
					vkCmdDrawIndexedIndirectCount(cmd,
					                              indirectBuffer.m_buffer,
					                              bufferOffset,
					                              drawCountBuffer.m_buffer,
					                              (batchIndexOffset + bIdx) * sizeof(uint32_t),
					                              batch.commandCount,
					                              stride);
					m_stats.m_drawcallCount++;
				}
				else if (m_multiDrawIndirectSupported && m_multiDrawIndirectEnabled)
				{
					vkCmdDrawIndexedIndirect(cmd,
					                         indirectBuffer.m_buffer,
					                         bufferOffset,
					                         batch.commandCount,
					                         stride);
					m_stats.m_drawcallCount++;
				}
				else
				{
					for (uint32_t i = 0; i < batch.commandCount; i++)
					{
						vkCmdDrawIndexedIndirect(
						cmd,
						indirectBuffer.m_buffer,
						bufferOffset + i * stride,
						1,
						stride);
						m_stats.m_drawcallCount++;
					}
				}
			}
		};

		// === Draw opaque ===
		if (!opaqueDraws.empty())
		{
#ifdef TRACY_ENABLE
			ZoneScopedN("Draw Opaque Indirect");
#endif
			const RenderObject& firstOpaque =
			m_mainDrawContext.m_OpaqueSurfaces[opaqueDraws[0]];
			bindPipelineState(firstOpaque.m_material->m_pipeline);

			issueBatchDraws(opaqueBatches, gpuCulling, 0);
		}

		// === Draw transparent ===
		if (!transparentDraws.empty())
		{
#ifdef TRACY_ENABLE
			ZoneScopedN("Draw Transparent Indirect");
#endif
			const RenderObject& firstTransparent =
			m_mainDrawContext.m_TransparentSurfaces[transparentDraws[0]];
			bindPipelineState(firstTransparent.m_material->m_pipeline);

			// Transparent always uses regular indirect draws (CPU-culled)
			issueBatchDraws(transparentBatches, false, 0);
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
		               m_drawExtent);
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

void Renderer::updateScene(float deltaTime, VkExtent2D windowExtent)
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

	m_camera->update(deltaTime);
	// camera view
	glm::mat4 view = m_camera->getViewMatrix();
	// camera projection
	glm::mat4 projection =
	glm::perspective(glm::radians(70.f),
	                 (float) windowExtent.width / (float) windowExtent.height,
	                 10000.f,
	                 0.1f);

	// invert the Y direction on projection matrix so that we are more similar
	// to opengl and gltf axis
	projection[1][1] *= -1;

	glm::mat4 viewProj = projection * view;

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
				obj.m_firstIndex = surface.m_startIndex;
				obj.m_indexBuffer =
				mesh.meshAsset->m_meshBuffers.m_indexBuffer.m_buffer;
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

	m_sceneData.m_cameraPosition = m_camera->m_position;

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
	    resPath("shaders/slang/objectid.vert.spv").c_str(), m_device, &vertexShader))
	{
		AGNI_PRINT("Failed to load objectid vertex shader\n");
		return;
	}

	if (!vkutil::loadShaderModule(
	    resPath("shaders/slang/objectid.frag.spv").c_str(), m_device, &fragmentShader))
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

	// Cleanup shader modules
	vkDestroyShaderModule(m_device, vertexShader, nullptr);
	vkDestroyShaderModule(m_device, fragmentShader, nullptr);
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

	// Draw all opaque objects with their entity IDs
	VkBuffer lastIndexBuffer = VK_NULL_HANDLE;
	for (const auto& obj : m_mainDrawContext.m_OpaqueSurfaces)
	{
		if (obj.m_indexBuffer != lastIndexBuffer)
		{
			lastIndexBuffer = obj.m_indexBuffer;
			vkCmdBindIndexBuffer(
			cmd, obj.m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		}

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
