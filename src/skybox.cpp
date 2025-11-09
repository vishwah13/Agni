#include "skybox.h"
#include "vk_engine.h"
#include "vk_images.h"
#include "vk_initializers.h"
#include "vk_pipelines.h"
#include <fmt/core.h>

void Skybox::init(AgniEngine*                        engine,
                  const std::array<std::string, 6>& cubemapFaces)
{
	// Create cubemap image
	cubemapImage = engine->createCubemap(cubemapFaces,
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

	vkCreateSampler(engine->_device, &cubemapSamplerInfo, nullptr, &cubemapSampler);

	// Create cube mesh
	createCubeMesh(engine);

	// Create material
	createMaterial(engine);
}

void Skybox::buildPipelines(AgniEngine* engine)
{
	VkShaderModule skyFragShader;
	if (!vkutil::loadShaderModule(
	    "../../shaders/glsl/skybox.frag.spv", engine->_device, &skyFragShader))
	{
		fmt::println("Error when building the skybox fragment shader module");
	}

	VkShaderModule skyVertexShader;
	if (!vkutil::loadShaderModule("../../shaders/glsl/skybox.vert.spv",
	                              engine->_device,
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

	skyboxMaterialLayout = layoutBuilder.build(
	engine->_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

	VkDescriptorSetLayout layouts[] = {engine->_gpuSceneDataDescriptorLayout,
	                                   skyboxMaterialLayout};

	VkPipelineLayoutCreateInfo mesh_layout_info =
	vkinit::pipeline_layout_create_info();
	mesh_layout_info.setLayoutCount         = 2;
	mesh_layout_info.pSetLayouts            = layouts;
	mesh_layout_info.pPushConstantRanges    = &matrixRange;
	mesh_layout_info.pushConstantRangeCount = 1;

	VkPipelineLayout newLayout;
	VK_CHECK(vkCreatePipelineLayout(
	engine->_device, &mesh_layout_info, nullptr, &newLayout));

	skyboxPipeline.layout = newLayout;

	// build the stage-create-info for both vertex and fragment stages. This
	// lets the pipeline know the shader modules per stage
	PipelineBuilder pipelineBuilder;
	pipelineBuilder.setShaders(skyVertexShader, skyFragShader);
	pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
	pipelineBuilder.setCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE);
	pipelineBuilder.enableMultisampling(engine->msaaSamples);
	pipelineBuilder.disableBlending();
	// turning off depth buffer writes for skybox, but enable depth test with
	// reversed-Z
	pipelineBuilder.enableDepthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

	// render format
	pipelineBuilder.setColorAttachmentFormat(
	engine->_msaaColorImage.imageFormat);
	pipelineBuilder.setDepthFormat(engine->_depthImage.imageFormat);

	// use the triangle layout we created
	pipelineBuilder._pipelineLayout = newLayout;

	// finally build the pipeline
	skyboxPipeline.pipeline = pipelineBuilder.buildPipeline(engine->_device);

	vkDestroyShaderModule(engine->_device, skyFragShader, nullptr);
	vkDestroyShaderModule(engine->_device, skyVertexShader, nullptr);
}

void Skybox::cleanup(AgniEngine* engine)
{
	// Cleanup pipeline resources
	clearPipelineResources(engine->_device);

	// Cleanup mesh buffers
	engine->destroyBuffer(meshBuffers.indexBuffer);
	engine->destroyBuffer(meshBuffers.vertexBuffer);

	// Cleanup material
	if (skyboxMaterial)
	{
		delete skyboxMaterial;
		skyboxMaterial = nullptr;
	}

	// Cleanup cubemap resources
	vkDestroySampler(engine->_device, cubemapSampler, nullptr);
	engine->destroyImage(cubemapImage);
}

void Skybox::draw(VkCommandBuffer cmd,
                  VkDescriptorSet sceneDescriptor,
                  VkExtent2D      drawExtent)
{
	// Bind skybox pipeline
	vkCmdBindPipeline(
	cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline.pipeline);

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
	                        skyboxPipeline.layout,
	                        0,
	                        1,
	                        &sceneDescriptor,
	                        0,
	                        nullptr);

	// Bind skybox material descriptor (set 1)
	vkCmdBindDescriptorSets(cmd,
	                        VK_PIPELINE_BIND_POINT_GRAPHICS,
	                        skyboxPipeline.layout,
	                        1,
	                        1,
	                        &skyboxMaterial->materialSet,
	                        0,
	                        nullptr);

	// Bind index buffer
	vkCmdBindIndexBuffer(
	cmd, meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	// Push constants for vertex buffer address
	SkyBoxPushConstants skyboxPush;
	skyboxPush.vertexBufferAddress = meshBuffers.vertexBufferAddress;

	vkCmdPushConstants(cmd,
	                   skyboxPipeline.layout,
	                   VK_SHADER_STAGE_VERTEX_BIT,
	                   0,
	                   sizeof(SkyBoxPushConstants),
	                   &skyboxPush);

	// Draw skybox
	vkCmdDrawIndexed(cmd, indexCount, 1, firstIndex, 0, 0);
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

	meshBuffers = engine->uploadMesh(cubeIndices, cubeVertices);
	indexCount  = static_cast<uint32_t>(cubeIndices.size());
	firstIndex  = 0;
}

void Skybox::createMaterial(AgniEngine* engine)
{
	// Create skybox material resources
	MaterialResources skyboxResources;
	skyboxResources.cubemapImage   = cubemapImage;
	skyboxResources.cubemapSampler = cubemapSampler;

	// Write skybox material
	skyboxMaterial = new MaterialInstance(writeMaterial(
	engine->_device, skyboxResources, engine->globalDescriptorAllocator));
}

void Skybox::clearPipelineResources(VkDevice device)
{
	vkDestroyDescriptorSetLayout(device, skyboxMaterialLayout, nullptr);
	vkDestroyPipelineLayout(device, skyboxPipeline.layout, nullptr);
	vkDestroyPipeline(device, skyboxPipeline.pipeline, nullptr);
}

MaterialInstance
Skybox::writeMaterial(VkDevice                     device,
                      const MaterialResources&     resources,
                      DescriptorAllocatorGrowable& descriptorAllocator)
{
	MaterialInstance matData;
	matData.passType = MaterialPass::Other;

	matData.materialSet =
	descriptorAllocator.allocate(device, skyboxMaterialLayout);


	writer.clear();
	writer.writeImage(/*binding*/ 0,
	                  resources.cubemapImage.imageView,
	                  resources.cubemapSampler,
	                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	// use the materialSet and update it here.
	writer.updateSet(device, matData.materialSet);

	return matData;
}
