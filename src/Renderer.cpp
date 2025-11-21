#include <Renderer.hpp>

#include <AgniEngine.hpp>
#include <Images.hpp>
#include <Initializers.hpp>
#include <Pipelines.hpp>
#include <VulkanTools.hpp>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <chrono>

static bool isVisible(const RenderObject& obj, const glm::mat4& viewproj)
{
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

void Renderer::init(VkDevice                     device,
                    ResourceManager*             resourceManager,
                    SwapchainManager*            swapchainManager,
                    Camera*                      camera,
                    Skybox*                      skybox,
                    DescriptorAllocatorGrowable* globalDescriptorAllocator,
                    VkExtent2D                   windowExtent)
{
	m_device                     = device;
	m_resourceManager            = resourceManager;
	m_swapchainManager           = swapchainManager;
	m_camera                     = camera;
	m_skybox                     = skybox;
	m_globalDescriptorAllocator  = globalDescriptorAllocator;

	initRenderTargets(windowExtent);
	initDescriptors();
	initBackgroundPipelines();
}

void Renderer::cleanup()
{
	// Cleanup render targets
	m_resourceManager->destroyImage(m_drawImage);
	m_resourceManager->destroyImage(m_msaaColorImage);
	m_resourceManager->destroyImage(m_depthImage);

	// Cleanup pipelines
	vkDestroyPipelineLayout(m_device, m_gradientPipelineLayout, nullptr);
	for (auto& effect : m_backgroundEffects)
	{
		vkDestroyPipeline(m_device, effect.m_pipeline, nullptr);
	}

	// Cleanup descriptor layouts
	vkDestroyDescriptorSetLayout(m_device, m_drawImageDescriptorLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_gpuSceneDataDescriptorLayout, nullptr);

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
}

void Renderer::initRenderTargets(VkExtent2D windowExtent)
{
	VkExtent3D drawImageExtent = {
	windowExtent.width, windowExtent.height, 1};

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

void Renderer::initDescriptors()
{
	// Create descriptor set layout for draw image (compute shader)
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
		m_drawImageDescriptorLayout =
		builder.build(m_device, VK_SHADER_STAGE_COMPUTE_BIT);
	}

	m_drawImageDescriptors =
	m_globalDescriptorAllocator->allocate(m_device, m_drawImageDescriptorLayout);

	// Create descriptor set layout for GPU scene data
	{
		DescriptorLayoutBuilder builder;
		builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		m_gpuSceneDataDescriptorLayout = builder.build(
		m_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
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
	if (!vkutil::loadShaderModule("../../shaders/glsl/gradient_color.comp.spv",
	                              m_device,
	                              &gradientShader))
	{
		fmt::print("Error when building the compute shader \n");
	}

	VkShaderModule skyShader;
	if (!vkutil::loadShaderModule(
	    "../../shaders//glsl/sky.comp.spv", m_device, &skyShader))
	{
		fmt::print("Error when building the compute shader \n");
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
	computePipelineCreateInfo.pNext              = nullptr;
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

void Renderer::renderFrame(VkCommandBuffer cmd,
                            uint32_t        swapchainImageIndex,
                            FrameData&      currentFrame,
                            VkExtent2D      windowExtent)
{
	m_drawExtent.width =
	std::min(m_swapchainManager->getSwapchainExtent().width, m_drawImage.m_imageExtent.width) *
	m_renderScale;
	m_drawExtent.height =
	std::min(m_swapchainManager->getSwapchainExtent().height, m_drawImage.m_imageExtent.height) *
	m_renderScale;

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

	drawGeometry(cmd, currentFrame);

	// transtion the draw image and the swapchain image into their correct
	// transfer layouts
	vkutil::transitionImage(cmd,
	                        m_drawImage.m_image,
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	vkutil::transitionImage(cmd,
	                        m_swapchainManager->getSwapchainImages()[swapchainImageIndex],
	                        VK_IMAGE_LAYOUT_UNDEFINED,
	                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// execute a copy from the draw image into the swapchain
	vkutil::copyImageToImage(cmd,
	                         m_drawImage.m_image,
	                         m_swapchainManager->getSwapchainImages()[swapchainImageIndex],
	                         m_drawExtent,
	                         m_swapchainManager->getSwapchainExtent());

	vkutil::transitionImage(cmd,
	                        m_swapchainManager->getSwapchainImages()[swapchainImageIndex],
	                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	drawImgui(cmd, m_swapchainManager->getSwapchainImageViews()[swapchainImageIndex]);

	// make the swapchain image into presentable mode
	vkutil::transitionImage(cmd,
	                        m_swapchainManager->getSwapchainImages()[swapchainImageIndex],
	                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

void Renderer::drawBackground(VkCommandBuffer cmd)
{
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
	VkRenderingAttachmentInfo colorAttachment = vkinit::attachmentInfo(
	targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingInfo renderInfo =
	vkinit::renderingInfo(m_swapchainManager->getSwapchainExtent(), &colorAttachment, nullptr);

	vkCmdBeginRendering(cmd, &renderInfo);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRendering(cmd);
}

void Renderer::drawGeometry(VkCommandBuffer cmd, FrameData& currentFrame)
{
	// reset counters
	m_stats.m_drawcallCount = 0;
	m_stats.m_triangleCount = 0;
	// begin clock
	auto start = std::chrono::system_clock::now();

	std::vector<uint32_t> opaqueDraws;
	opaqueDraws.reserve(m_mainDrawContext.m_OpaqueSurfaces.size());
	std::vector<uint32_t> transparentDraws;
	transparentDraws.reserve(m_mainDrawContext.m_TransparentSurfaces.size());

	for (uint32_t i = 0; i < m_mainDrawContext.m_OpaqueSurfaces.size(); i++)
	{
		if (isVisible(m_mainDrawContext.m_OpaqueSurfaces[i],
		              m_sceneData.m_viewproj))
		{
			opaqueDraws.push_back(i);
		}
	}
	for (uint32_t i = 0; i < m_mainDrawContext.m_TransparentSurfaces.size();
	     i++)
	{
		if (isVisible(m_mainDrawContext.m_TransparentSurfaces[i],
		              m_sceneData.m_viewproj))
		{
			transparentDraws.push_back(i);
		}
	}

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

	// begin a render pass with MSAA images that resolve to draw image
	VkRenderingAttachmentInfo colorAttachment =
	vkinit::attachmentInfoMsaa(m_msaaColorImage.m_imageView,
	                           m_drawImage.m_imageView,
	                           nullptr,
	                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	VkRenderingAttachmentInfo depthAttachment = vkinit::depthAttachmentInfo(
	m_depthImage.m_imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	VkRenderingInfo renderInfo =
	vkinit::renderingInfo(m_drawExtent, &colorAttachment, &depthAttachment);
	vkCmdBeginRendering(cmd, &renderInfo);

	//  allocate a new uniform buffer for the scene data
	AllocatedBuffer gpuSceneDataBuffer =
	m_resourceManager->createBuffer(sizeof(GPUSceneData),
	                               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	                               VMA_MEMORY_USAGE_CPU_TO_GPU);

	// add it to the deletion queue of this frame so it gets deleted once its
	// been used
	ResourceManager* rm = m_resourceManager;
	currentFrame.m_deletionQueue.push_function(
	[rm, gpuSceneDataBuffer]() { rm->destroyBuffer(gpuSceneDataBuffer); });

	// write the buffer
	GPUSceneData* sceneUniformData =
	(GPUSceneData*) gpuSceneDataBuffer.m_info.pMappedData;
	*sceneUniformData = m_sceneData;

	// create a descriptor set that binds that buffer and update it
	VkDescriptorSet globalDescriptor =
	currentFrame.m_frameDescriptors.allocate(
	m_device, m_gpuSceneDataDescriptorLayout);

	DescriptorWriter writer;
	writer.writeBuffer(0,
	                   gpuSceneDataBuffer.m_buffer,
	                   sizeof(GPUSceneData),
	                   0,
	                   VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.updateSet(m_device, globalDescriptor);

	// keep track of what state we are binding
	MaterialPipeline* lastPipeline    = nullptr;
	MaterialInstance* lastMaterial    = nullptr;
	VkBuffer          lastIndexBuffer = VK_NULL_HANDLE;

	auto draw = [&](const RenderObject& r)
	{
		if (r.m_material != lastMaterial)
		{
			lastMaterial = r.m_material;

			// rebind pipeline and descriptors if the material changed
			if (r.m_material->m_pipeline != lastPipeline)
			{
				lastPipeline = r.m_material->m_pipeline;
				vkCmdBindPipeline(cmd,
				                  VK_PIPELINE_BIND_POINT_GRAPHICS,
				                  r.m_material->m_pipeline->m_pipeline);
				vkCmdBindDescriptorSets(cmd,
				                        VK_PIPELINE_BIND_POINT_GRAPHICS,
				                        r.m_material->m_pipeline->m_layout,
				                        0,
				                        1,
				                        &globalDescriptor,
				                        0,
				                        nullptr);

				// set dynamic viewport and scissor
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
			}

			vkCmdBindDescriptorSets(cmd,
			                        VK_PIPELINE_BIND_POINT_GRAPHICS,
			                        r.m_material->m_pipeline->m_layout,
			                        1,
			                        1,
			                        &r.m_material->m_materialSet,
			                        0,
			                        nullptr);
		}

		// rebind index buffer if needed
		if (r.m_indexBuffer != lastIndexBuffer)
		{
			lastIndexBuffer = r.m_indexBuffer;
			vkCmdBindIndexBuffer(cmd, r.m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		}

		// calculate final mesh matrix
		GPUDrawPushConstants push_constants;
		push_constants.m_worldMatrix  = r.m_transform;
		push_constants.m_vertexBuffer = r.m_vertexBufferAddress;

		vkCmdPushConstants(cmd,
		                   r.m_material->m_pipeline->m_layout,
		                   VK_SHADER_STAGE_VERTEX_BIT,
		                   0,
		                   sizeof(GPUDrawPushConstants),
		                   &push_constants);

		vkCmdDrawIndexed(cmd, r.m_indexCount, 1, r.m_firstIndex, 0, 0);

		// add counters for triangles and draws
		m_stats.m_drawcallCount++;
		m_stats.m_triangleCount += r.m_indexCount / 3;
	};

	for (auto& r : opaqueDraws)
	{
		draw(m_mainDrawContext.m_OpaqueSurfaces[r]);
	}

	for (auto& r : transparentDraws)
	{
		draw(m_mainDrawContext.m_TransparentSurfaces[r]);
	}

	// Draw skybox last (after all geometry)
	m_skybox->draw(cmd, globalDescriptor, m_drawExtent);

	vkCmdEndRendering(cmd);

	auto end = std::chrono::system_clock::now();

	// convert to microseconds (integer), and then come back to miliseconds
	auto elapsed =
	std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	m_stats.m_meshDrawTime = elapsed.count() / 1000.f;
}

void Renderer::updateScene(float deltaTime, VkExtent2D windowExtent)
{
	// begin clock
	auto start = std::chrono::system_clock::now();

	m_mainDrawContext.m_OpaqueSurfaces.clear();
	m_mainDrawContext.m_TransparentSurfaces.clear();

	m_camera->update(deltaTime);
	// camera view
	glm::mat4 view = m_camera->getViewMatrix();
	// camera projection
	glm::mat4 projection = glm::perspective(glm::radians(70.f),
	                                        (float) windowExtent.width /
	                                        (float) windowExtent.height,
	                                        10000.f,
	                                        0.1f);

	// invert the Y direction on projection matrix so that we are more similar
	// to opengl and gltf axis
	projection[1][1] *= -1;

	for (auto& [name, scene] : m_loadedScenes)
	{
		scene->Draw(glm::mat4 {1.f}, m_mainDrawContext);
	}

	m_sceneData.m_view = view;
	// camera projection
	m_sceneData.m_proj = projection;

	m_sceneData.m_viewproj = projection * view;

	// some default lighting parameters
	m_sceneData.m_ambientColor      = glm::vec4(.1f);
	m_sceneData.m_sunlightColor     = glm::vec4(1.f);
	m_sceneData.m_sunlightDirection = glm::vec4(0, 1, 0.5, 1.f);
	m_sceneData.m_cameraPosition    = m_camera->m_position;

	auto end = std::chrono::system_clock::now();

	// convert to microseconds (integer), and then come back to miliseconds
	auto elapsed =
	std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	m_stats.m_sceneUpdateTime = elapsed.count() / 1000.f;
}
