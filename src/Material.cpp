#include <Material.hpp>

#include <AgniEngine.hpp>
#include <FallbackShaders.hpp>
#include <Initializers.hpp>
#include <Pipelines.hpp>
#include <VulkanTools.hpp>

#include <fmt/core.h>

void GltfPbrMaterial::buildPipelines(AgniEngine* engine)
{
	VkShaderModule meshFragShader;
	if (!vkutil::loadShaderModuleWithFallback(
	    "../../shaders/glsl/mesh.frag.spv",
	    engine->m_device,
	    &meshFragShader,
	    FallbackShaders::mesh_frag_spv,
	    FallbackShaders::mesh_frag_spv_len))
	{
		fmt::println("Error when building the mesh fragment shader module");
	}

	VkShaderModule meshVertexShader;
	if (!vkutil::loadShaderModuleWithFallback(
	    "../../shaders/glsl/mesh.vert.spv",
	    engine->m_device,
	    &meshVertexShader,
	    FallbackShaders::mesh_vert_spv,
	    FallbackShaders::mesh_vert_spv_len))
	{
		fmt::println("Error when building the mesh vertex shader module");
	}

	VkPushConstantRange matrixRange {};
	matrixRange.offset     = 0;
	matrixRange.size       = sizeof(GPUDrawPushConstants);
	matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	DescriptorLayoutBuilder layoutBuilder;
	layoutBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	layoutBuilder.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	layoutBuilder.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	layoutBuilder.addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	layoutBuilder.addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	m_materialLayout = layoutBuilder.build(engine->m_device,
	                                       VK_SHADER_STAGE_VERTEX_BIT |
	                                       VK_SHADER_STAGE_FRAGMENT_BIT);

	VkDescriptorSetLayout layouts[] = {
	engine->m_renderer.getGpuSceneDataDescriptorLayout(), m_materialLayout};

	VkPipelineLayoutCreateInfo mesh_layout_info =
	vkinit::pipelineLayoutCreateInfo();
	mesh_layout_info.setLayoutCount         = 2;
	mesh_layout_info.pSetLayouts            = layouts;
	mesh_layout_info.pPushConstantRanges    = &matrixRange;
	mesh_layout_info.pushConstantRangeCount = 1;

	VkPipelineLayout newLayout;
	VK_CHECK(vkCreatePipelineLayout(
	engine->m_device, &mesh_layout_info, nullptr, &newLayout));

	m_opaquePipeline.m_layout      = newLayout;
	m_transparentPipeline.m_layout = newLayout;

	// build the stage-create-info for both vertex and fragment stages. This
	// lets the pipeline know the shader modules per stage
	PipelineBuilder pipelineBuilder;
	pipelineBuilder.setShaders(meshVertexShader, meshFragShader);
	pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
	pipelineBuilder.setCullMode(VK_CULL_MODE_FRONT_BIT,
	                            VK_FRONT_FACE_CLOCKWISE);
	pipelineBuilder.enableMultisampling(engine->m_renderer.getMsaaSamples());
	pipelineBuilder.disableBlending();
	pipelineBuilder.enableDepthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

	// render format
	pipelineBuilder.setColorAttachmentFormat(
	engine->m_renderer.getMsaaColorImage().m_imageFormat);
	pipelineBuilder.setDepthFormat(
	engine->m_renderer.getDepthImage().m_imageFormat);

	// use the triangle layout we created
	pipelineBuilder.m_pipelineLayout = newLayout;

	// finally build the pipeline
	m_opaquePipeline.m_pipeline =
	pipelineBuilder.buildPipeline(engine->m_device);

	// create the transparent variant
	pipelineBuilder.enableBlendingAdditive();
	// turning off depth buffer writes for transparent objects
	pipelineBuilder.enableDepthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

	m_transparentPipeline.m_pipeline =
	pipelineBuilder.buildPipeline(engine->m_device);

	vkDestroyShaderModule(engine->m_device, meshFragShader, nullptr);
	vkDestroyShaderModule(engine->m_device, meshVertexShader, nullptr);
}

void GltfPbrMaterial::clearResources(VkDevice device)
{
	vkDestroyDescriptorSetLayout(device, m_materialLayout, nullptr);
	vkDestroyPipelineLayout(device, m_transparentPipeline.m_layout, nullptr);

	vkDestroyPipeline(device, m_transparentPipeline.m_pipeline, nullptr);
	vkDestroyPipeline(device, m_opaquePipeline.m_pipeline, nullptr);
}

MaterialInstance
GltfPbrMaterial::writeMaterial(VkDevice                     device,
                               MaterialPass                 pass,
                               const MaterialResources&     resources,
                               DescriptorAllocatorGrowable& descriptorAllocator)
{
	MaterialInstance matData;
	matData.m_passType = pass;
	if (pass == MaterialPass::Transparent)
	{
		matData.m_pipeline = &m_transparentPipeline;
	}
	else
	{
		matData.m_pipeline = &m_opaquePipeline;
	}

	matData.m_materialSet =
	descriptorAllocator.allocate(device, m_materialLayout);


	m_writer.clear();
	m_writer.writeBuffer(/*binding*/ 0,
	                     resources.m_dataBuffer,
	                     sizeof(MaterialConstants),
	                     resources.m_dataBufferOffset,
	                     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	m_writer.writeImage(/*binding*/ 1,
	                    resources.m_colorTexture.image.m_imageView,
	                    resources.m_colorTexture.sampler,
	                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	m_writer.writeImage(/*binding*/ 2,
	                    resources.m_metalRoughTexture.image.m_imageView,
	                    resources.m_metalRoughTexture.sampler,
	                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	m_writer.writeImage(/*binding*/ 3,
	                    resources.m_normalTexture.image.m_imageView,
	                    resources.m_normalTexture.sampler,
	                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	m_writer.writeImage(/*binding*/ 4,
	                    resources.m_aoTexture.image.m_imageView,
	                    resources.m_aoTexture.sampler,
	                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	// use the materialSet and update it here.
	m_writer.updateSet(device, matData.m_materialSet);

	return matData;
}
