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
	    resPath("shaders/slang/Mesh.frag.spv").c_str(),
	    engine->m_device,
	    &meshFragShader,
	    FallbackShaders::meshFragSpv,
	    FallbackShaders::meshFragSpv_len))
	{
		AGNI_PRINT("Error when building the mesh fragment shader module\n");
	}

	VkShaderModule meshVertexShader;
	if (!vkutil::loadShaderModuleWithFallback(
	    resPath("shaders/slang/Mesh.vert.spv").c_str(),
	    engine->m_device,
	    &meshVertexShader,
	    FallbackShaders::meshVertSpv,
	    FallbackShaders::meshVertSpv_len))
	{
		AGNI_PRINT("Error when building the mesh vertex shader module\n");
	}

	VkPushConstantRange matrixRange {};
	matrixRange.offset     = 0;
	matrixRange.size       = sizeof(IndirectDrawPushConstants);
	matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	// Bindless pipeline layout: Scene + Textures + Samplers + Materials
	VkDescriptorSetLayout layouts[] = {
		engine->m_renderer.getGpuSceneDataDescriptorLayout(),        // Set 0: Scene data
		engine->m_renderer.getTextureRegistry().getLayout(),         // Set 1: Texture array
		engine->m_renderer.getSamplerRegistry().getLayout(),         // Set 2: Sampler array
		engine->m_renderer.getMaterialRegistry().getLayout()         // Set 3: Material SSBO
	};

	VkPipelineLayoutCreateInfo mesh_layout_info =
	vkinit::pipelineLayoutCreateInfo();
	mesh_layout_info.setLayoutCount         = 4;  // Scene + Textures + Samplers + Materials
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

	// Enable descriptor buffer extension
	pipelineBuilder.enableDescriptorBuffer();

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

void GltfPbrMaterial::clearPipelines(VkDevice device)
{
	if (m_transparentPipeline.m_layout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(device, m_transparentPipeline.m_layout, nullptr);
		m_transparentPipeline.m_layout = VK_NULL_HANDLE;
		m_opaquePipeline.m_layout      = VK_NULL_HANDLE; // Same layout, shared
	}

	if (m_transparentPipeline.m_pipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(device, m_transparentPipeline.m_pipeline, nullptr);
		m_transparentPipeline.m_pipeline = VK_NULL_HANDLE;
	}

	if (m_opaquePipeline.m_pipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(device, m_opaquePipeline.m_pipeline, nullptr);
		m_opaquePipeline.m_pipeline = VK_NULL_HANDLE;
	}
}

void GltfPbrMaterial::clearResources(VkDevice device)
{
	clearPipelines(device);
}
