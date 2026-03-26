#include <Debug.hpp>
#include <Initializers.hpp>
#include <Pipelines.hpp>
#include <fstream>

bool vkutil::loadShaderModule(const char*     filePath,
                              VkDevice        device,
                              VkShaderModule* outShaderModule)
{
	// open the file. With cursor at the end
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open())
	{
		AGNI_PRINT("Failed to open file for shader: {}\n", filePath);
		return false;
	}

	// find what the size of the file is by looking up the location of the
	// cursor because the cursor is at the end, it gives the size directly in
	// bytes
	size_t fileSize = (size_t) file.tellg();

	// spirv expects the buffer to be on uint32, so make sure to reserve a int
	// vector big enough for the entire file
	std::vector<uint32_t> m_buffer(fileSize / sizeof(uint32_t));

	// put file cursor at beginning
	file.seekg(0);

	// load the entire file into the buffer
	file.read((char*) m_buffer.data(), fileSize);

	// now that the file is loaded into the buffer, we can close it
	file.close();

	// create a new shader module, using the buffer we loaded
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	// codeSize has to be in bytes, so multply the ints in the buffer by size of
	// int to know the real size of the buffer
	createInfo.codeSize = m_buffer.size() * sizeof(uint32_t);
	createInfo.pCode    = m_buffer.data();

	// check that the creation goes well.
	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) !=
	    VK_SUCCESS)
	{
		return false;
	}

	AGNI_PRINT("Loaded shader file from: {}\n", filePath);

	*outShaderModule = shaderModule;
	return true;
}

bool vkutil::loadShaderModuleWithFallback(const char*          filePath,
                                           VkDevice             device,
                                           VkShaderModule*      outShaderModule,
                                           const unsigned char* fallbackSpv,
                                           unsigned int         fallbackSize)
{
	// Try loading from file first
	if (loadShaderModule(filePath, device, outShaderModule))
	{
		return true;
	}

	// Log warning that fallback is being used
	AGNI_PRINT("WARNING: Failed to load shader from {}, using embedded fallback shader\n", filePath);

	// Load from embedded fallback SPIR-V
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;
	createInfo.codeSize = fallbackSize;
	createInfo.pCode = reinterpret_cast<const uint32_t*>(fallbackSpv);

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		AGNI_PRINT("ERROR: Failed to create shader module even with fallback!\n");
		return false;
	}

	*outShaderModule = shaderModule;
	return true;
}

ComputePipelineBuilder::ComputePipelineBuilder(VkDevice device)
: m_device(device)
{
}

ComputePipelineBuilder& ComputePipelineBuilder::setShader(const char* spvPath)
{
	m_shaderPath = spvPath;
	return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::addDescriptorSetLayout(VkDescriptorSetLayout layout)
{
	m_setLayouts.push_back(layout);
	return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::setPushConstantSize(uint32_t size)
{
	m_pushConstantSize = size;
	return *this;
}

ComputePipelineBuilder& ComputePipelineBuilder::setLayout(VkPipelineLayout layout)
{
	m_existingLayout = layout;
	return *this;
}

ComputePipelineResult ComputePipelineBuilder::build()
{
	ComputePipelineResult result {};

	// Load shader module
	VkShaderModule shaderModule;
	if (!vkutil::loadShaderModule(m_shaderPath, m_device, &shaderModule))
	{
		AGNI_PRINT("ComputePipelineBuilder: failed to load shader {}\n", m_shaderPath);
		return result;
	}

	// Create or reuse pipeline layout
	VkPipelineLayout layout = m_existingLayout;
	if (layout == VK_NULL_HANDLE)
	{
		VkPipelineLayoutCreateInfo layoutInfo {};
		layoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = static_cast<uint32_t>(m_setLayouts.size());
		layoutInfo.pSetLayouts    = m_setLayouts.empty() ? nullptr : m_setLayouts.data();

		VkPushConstantRange pushConstantRange {};
		if (m_pushConstantSize > 0)
		{
			pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
			pushConstantRange.offset     = 0;
			pushConstantRange.size       = m_pushConstantSize;
			layoutInfo.pushConstantRangeCount = 1;
			layoutInfo.pPushConstantRanges    = &pushConstantRange;
		}

		if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &layout) != VK_SUCCESS)
		{
			AGNI_PRINT("ComputePipelineBuilder: failed to create pipeline layout\n");
			vkDestroyShaderModule(m_device, shaderModule, nullptr);
			return result;
		}
	}

	// Create compute pipeline
	VkPipelineShaderStageCreateInfo stageInfo {};
	stageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
	stageInfo.module = shaderModule;
	stageInfo.pName  = "main";

	VkComputePipelineCreateInfo pipelineInfo {};
	pipelineInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.flags  = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
	pipelineInfo.layout = layout;
	pipelineInfo.stage  = stageInfo;

	VkPipeline pipeline;
	if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
	{
		AGNI_PRINT("ComputePipelineBuilder: failed to create compute pipeline\n");
		vkDestroyShaderModule(m_device, shaderModule, nullptr);
		if (m_existingLayout == VK_NULL_HANDLE)
			vkDestroyPipelineLayout(m_device, layout, nullptr);
		return result;
	}

	vkDestroyShaderModule(m_device, shaderModule, nullptr);

	result.m_pipeline = pipeline;
	result.m_layout   = layout;
	return result;
}

void PipelineBuilder::clear()
{
	// clear all of the structs we need back to 0 with their correct stype

	m_inputAssembly = {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};

	m_rasterizer = {.sType =
	                VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};

	m_colorBlendAttachment = {};

	m_multisampling = {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};

	m_pipelineLayout = {};

	m_depthStencil = {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

	m_renderInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};

	m_shaderStages.clear();

	m_flags = 0;
}

VkPipeline PipelineBuilder::buildPipeline(VkDevice device)
{
	// make viewport state from our stored viewport and scissor.
	// at the moment we wont support multiple viewports or scissors
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.pNext = nullptr;

	viewportState.viewportCount = 1;
	viewportState.scissorCount  = 1;

	// setup dummy color blending. We arent using transparent objects yet
	// the blending is just "no blend", but we do write to the color attachment
	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType =
	VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.pNext = nullptr;

	colorBlending.logicOpEnable   = VK_FALSE;
	colorBlending.logicOp         = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments    = &m_colorBlendAttachment;

	// completely clear VertexInputStateCreateInfo, as we have no need for it
	VkPipelineVertexInputStateCreateInfo _vertexInputInfo = {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

	// build the actual pipeline
	// we now use all of the info structs we have been writing into into this
	// one to create the pipeline
	VkGraphicsPipelineCreateInfo pipelineInfo = {
	.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
	// connect the renderInfo to the pNext extension mechanism
	pipelineInfo.pNext = &m_renderInfo;
	pipelineInfo.flags = m_flags;

	pipelineInfo.stageCount          = (uint32_t) m_shaderStages.size();
	pipelineInfo.pStages             = m_shaderStages.data();
	pipelineInfo.pVertexInputState   = &_vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &m_inputAssembly;
	pipelineInfo.pViewportState      = &viewportState;
	pipelineInfo.pRasterizationState = &m_rasterizer;
	pipelineInfo.pMultisampleState   = &m_multisampling;
	pipelineInfo.pColorBlendState    = &colorBlending;
	pipelineInfo.pDepthStencilState  = &m_depthStencil;
	pipelineInfo.layout              = m_pipelineLayout;

	VkDynamicState state[] = {VK_DYNAMIC_STATE_VIEWPORT,
	                          VK_DYNAMIC_STATE_SCISSOR};

	VkPipelineDynamicStateCreateInfo dynamicInfo = {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
	dynamicInfo.pDynamicStates    = &state[0];
	dynamicInfo.dynamicStateCount = 2;

	pipelineInfo.pDynamicState = &dynamicInfo;

	VkPipeline newPipeline;
	if (vkCreateGraphicsPipelines(
	    device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline) !=
	    VK_SUCCESS)
	{
		AGNI_PRINT("failed to create pipeline\n");
		return VK_NULL_HANDLE; // failed to create graphics pipeline
	}
	else
	{
		return newPipeline;
	}
}

void PipelineBuilder::enableDescriptorBuffer()
{
	m_flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
}

void PipelineBuilder::setShaders(VkShaderModule vertexShader,
                                 VkShaderModule fragmentShader)
{
	m_shaderStages.clear();

	m_shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(
	VK_SHADER_STAGE_VERTEX_BIT, vertexShader));

	m_shaderStages.push_back(vkinit::pipelineShaderStageCreateInfo(
	VK_SHADER_STAGE_FRAGMENT_BIT, fragmentShader));
}

void PipelineBuilder::setInputTopology(VkPrimitiveTopology topology)
{
	m_inputAssembly.topology = topology;
	// we are not going to use primitive restart on the entire tutorial so leave
	// it on false
	m_inputAssembly.primitiveRestartEnable = VK_FALSE;
}

void PipelineBuilder::setPolygonMode(VkPolygonMode mode)
{
	m_rasterizer.polygonMode = mode;
	m_rasterizer.lineWidth   = 1.f;
}

void PipelineBuilder::setCullMode(VkCullModeFlags cullMode,
                                  VkFrontFace     frontFace)
{
	m_rasterizer.cullMode  = cullMode;
	m_rasterizer.frontFace = frontFace;
}

void PipelineBuilder::setMultisamplingNone()
{
	m_multisampling.sampleShadingEnable = VK_FALSE;
	// multisampling defaulted to no multisampling (1 sample per pixel)
	m_multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	m_multisampling.minSampleShading     = 1.0f;
	m_multisampling.pSampleMask          = nullptr;
	// no alpha to coverage either
	m_multisampling.alphaToCoverageEnable = VK_FALSE;
	m_multisampling.alphaToOneEnable      = VK_FALSE;
}

void PipelineBuilder::enableMultisampling(VkSampleCountFlagBits numSample)
{
	m_multisampling.sampleShadingEnable = VK_TRUE;
	// multisampling defaulted to no multisampling (1 sample per pixel)
	m_multisampling.rasterizationSamples = numSample;
	m_multisampling.minSampleShading     = 1.f;
	m_multisampling.pSampleMask          = nullptr;
	// no alpha to coverage either
	m_multisampling.alphaToCoverageEnable = VK_FALSE;
	m_multisampling.alphaToOneEnable      = VK_FALSE;
}

void PipelineBuilder::disableBlending()
{
	// default write mask
	m_colorBlendAttachment.colorWriteMask =
	VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	// no blending
	m_colorBlendAttachment.blendEnable = VK_FALSE;
}

void PipelineBuilder::setColorAttachmentFormat(VkFormat format)
{
	m_colorAttachmentformat = format;
	// connect the format to the renderInfo  structure
	m_renderInfo.colorAttachmentCount    = 1;
	m_renderInfo.pColorAttachmentFormats = &m_colorAttachmentformat;
}

void PipelineBuilder::setDepthFormat(VkFormat format)
{
	m_renderInfo.depthAttachmentFormat = format;
}

void PipelineBuilder::disableDepthtest()
{
	m_depthStencil.depthTestEnable       = VK_FALSE;
	m_depthStencil.depthWriteEnable      = VK_FALSE;
	m_depthStencil.depthCompareOp        = VK_COMPARE_OP_NEVER;
	m_depthStencil.depthBoundsTestEnable = VK_FALSE;
	m_depthStencil.stencilTestEnable     = VK_FALSE;
	m_depthStencil.front                 = {};
	m_depthStencil.back                  = {};
	m_depthStencil.minDepthBounds        = 0.f;
	m_depthStencil.maxDepthBounds        = 1.f;
}

void PipelineBuilder::enableDepthtest(bool depthWriteEnable, VkCompareOp op)
{
	m_depthStencil.depthTestEnable       = VK_TRUE;
	m_depthStencil.depthWriteEnable      = depthWriteEnable;
	m_depthStencil.depthCompareOp        = op;
	m_depthStencil.depthBoundsTestEnable = VK_FALSE;
	m_depthStencil.stencilTestEnable     = VK_FALSE;
	m_depthStencil.front                 = {};
	m_depthStencil.back                  = {};
	m_depthStencil.minDepthBounds        = 0.f;
	m_depthStencil.maxDepthBounds        = 1.f;
}

// formula for blending in vulkan: outColor = srcColor * srcColorBlendFactor
// <op> dstColor * dstColorBlendFactor;
// For additive : outColor = srcColor.rgb * srcColor.a + dstColor.rgb * 1.0
void PipelineBuilder::enableBlendingAdditive()
{
	m_colorBlendAttachment.colorWriteMask =
	VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	m_colorBlendAttachment.blendEnable         = VK_TRUE;
	m_colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	m_colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	m_colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
	m_colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	m_colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	m_colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
}

// For alphablend : outColor = srcColor.rgb * srcColor.a + dstColor.rgb * (1.0 -
// srcColor.a)
void PipelineBuilder::enableBlendingAlphablend()
{
	m_colorBlendAttachment.colorWriteMask =
	VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	m_colorBlendAttachment.blendEnable         = VK_TRUE;
	m_colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	m_colorBlendAttachment.dstColorBlendFactor =
	VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	m_colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
	m_colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	m_colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	m_colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
}

void PipelineBuilder::enableDepthBias(float constantFactor,
                                      float slopeFactor,
                                      float clamp)
{
	m_rasterizer.depthBiasEnable         = VK_TRUE;
	m_rasterizer.depthBiasConstantFactor = constantFactor;
	m_rasterizer.depthBiasSlopeFactor    = slopeFactor;
	m_rasterizer.depthBiasClamp          = clamp;
}
