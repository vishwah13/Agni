#pragma once
#include <Types.hpp>

namespace vkutil
{

	bool loadShaderModule(const char*     filePath,
	                      VkDevice        device,
	                      VkShaderModule* outShaderModule);
};

class PipelineBuilder
{
public:
	std::vector<VkPipelineShaderStageCreateInfo> m_shaderStages;

	VkPipelineInputAssemblyStateCreateInfo m_inputAssembly;
	VkPipelineRasterizationStateCreateInfo m_rasterizer;
	VkPipelineColorBlendAttachmentState    m_colorBlendAttachment;
	VkPipelineMultisampleStateCreateInfo   m_multisampling;
	VkPipelineLayout                       m_pipelineLayout;
	VkPipelineDepthStencilStateCreateInfo  m_depthStencil;
	// so all systems related to VkRenderPass will be completely skipped
	// Instead, we extend the VkGraphicsPipelineCreateInfo with a
	// VkPipelineRenderingCreateInfo added into its pNext chain. This structure
	// holds a list of the attachment formats the pipeline will use.
	VkPipelineRenderingCreateInfo m_renderInfo;
	VkFormat                      m_colorAttachmentformat;

	PipelineBuilder()
	{
		clear();
	}

	void clear();

	VkPipeline buildPipeline(VkDevice device);
	void setShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);
	void setInputTopology(VkPrimitiveTopology topology);
	void setPolygonMode(VkPolygonMode mode);
	void setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);
	void setMultisamplingNone();
	void enableMultisampling(VkSampleCountFlagBits numSample);
	void disableBlending();
	void setColorAttachmentFormat(VkFormat format);
	void setDepthFormat(VkFormat format);
	void disableDepthtest();
	void enableDepthtest(bool depthWriteEnable, VkCompareOp op);
	void enableBlendingAdditive();
	void enableBlendingAlphablend();
};