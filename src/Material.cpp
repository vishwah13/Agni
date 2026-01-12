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
	    "../../shaders/slang/Mesh.frag.spv",
	    engine->m_device,
	    &meshFragShader,
	    FallbackShaders::meshFragSpv,
	    FallbackShaders::meshFragSpv_len))
	{
		AGNI_PRINT("Error when building the mesh fragment shader module\n");
	}

	VkShaderModule meshVertexShader;
	if (!vkutil::loadShaderModuleWithFallback(
	    "../../shaders/slang/Mesh.vert.spv",
	    engine->m_device,
	    &meshVertexShader,
	    FallbackShaders::meshVertSpv,
	    FallbackShaders::meshVertSpv_len))
	{
		AGNI_PRINT("Error when building the mesh vertex shader module\n");
	}

	VkPushConstantRange matrixRange {};
	matrixRange.offset     = 0;
	matrixRange.size       = sizeof(GPUDrawPushConstants);
	matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	// Only create descriptor set layout if it doesn't exist
	// This allows material descriptor sets to survive resize
	if (m_materialLayout == VK_NULL_HANDLE)
	{
		DescriptorLayoutBuilder layoutBuilder;
		layoutBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
		layoutBuilder.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		layoutBuilder.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		layoutBuilder.addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
		layoutBuilder.addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

		// Build for descriptor buffer
		m_materialLayoutInfo = layoutBuilder.buildForDescriptorBuffer(
			engine->m_device,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
		m_materialLayout = m_materialLayoutInfo.layout;

		// Initialize buffer writer
		m_bufferWriter.init(engine->m_device, engine->m_descriptorBufferProps);
	}

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
	// Only destroy pipelines and layout, NOT the descriptor set layout
	// This preserves material descriptor sets during resize
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

	// Also destroy the descriptor set layout (only on full cleanup/shutdown)
	if (m_materialLayout != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorSetLayout(device, m_materialLayout, nullptr);
		m_materialLayout = VK_NULL_HANDLE;
	}
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

MaterialInstance
GltfPbrMaterial::writeMaterialToBuffer(VkDevice                   device,
                                        MaterialPass               pass,
                                        const MaterialResources&   resources,
                                        DescriptorBufferAllocator& bufferAllocator)
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

	// Allocate space in descriptor buffer
	matData.m_descriptorOffset = bufferAllocator.allocate(m_materialLayoutInfo);

	// Get pointer to write descriptors
	void* bufferPtr = bufferAllocator.getPtrAtOffset(matData.m_descriptorOffset);

	// Get buffer device address for uniform buffer
	VkBufferDeviceAddressInfo addressInfo {};
	addressInfo.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	addressInfo.buffer = resources.m_dataBuffer;
	VkDeviceAddress bufferAddress = vkGetBufferDeviceAddress(device, &addressInfo);
	bufferAddress += resources.m_dataBufferOffset;

	// Write descriptors directly to buffer
	// Binding 0: Uniform buffer (material constants)
	m_bufferWriter.writeUniformBuffer(bufferPtr,
	                                   m_materialLayoutInfo.bindingOffsets[0],
	                                   bufferAddress,
	                                   sizeof(MaterialConstants));

	// Binding 1: Color texture
	m_bufferWriter.writeImageSampler(bufferPtr,
	                                  m_materialLayoutInfo.bindingOffsets[1],
	                                  resources.m_colorTexture.image.m_imageView,
	                                  resources.m_colorTexture.sampler,
	                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Binding 2: Metal-rough texture
	m_bufferWriter.writeImageSampler(bufferPtr,
	                                  m_materialLayoutInfo.bindingOffsets[2],
	                                  resources.m_metalRoughTexture.image.m_imageView,
	                                  resources.m_metalRoughTexture.sampler,
	                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Binding 3: Normal texture
	m_bufferWriter.writeImageSampler(bufferPtr,
	                                  m_materialLayoutInfo.bindingOffsets[3],
	                                  resources.m_normalTexture.image.m_imageView,
	                                  resources.m_normalTexture.sampler,
	                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Binding 4: AO texture
	m_bufferWriter.writeImageSampler(bufferPtr,
	                                  m_materialLayoutInfo.bindingOffsets[4],
	                                  resources.m_aoTexture.image.m_imageView,
	                                  resources.m_aoTexture.sampler,
	                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	return matData;
}
