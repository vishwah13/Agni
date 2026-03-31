#pragma once
#include <Types.hpp>

namespace vkutil
{

	bool loadShaderModule(const char*     filePath,
	                      VkDevice        device,
	                      VkShaderModule* outShaderModule);

	bool loadShaderModuleWithFallback(const char*          filePath,
	                                   VkDevice             device,
	                                   VkShaderModule*      outShaderModule,
	                                   const unsigned char* fallbackSpv,
	                                   unsigned int         fallbackSize);
};

struct ComputePipelineResult
{
	VkPipeline       m_pipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_layout   = VK_NULL_HANDLE;
};

class ComputePipelineBuilder
{
public:
	explicit ComputePipelineBuilder(VkDevice device);

	ComputePipelineBuilder& setShader(const char* spvPath);
	ComputePipelineBuilder& addDescriptorSetLayout(VkDescriptorSetLayout layout);
	ComputePipelineBuilder& setPushConstantSize(uint32_t size);
	ComputePipelineBuilder& setLayout(VkPipelineLayout layout);

	ComputePipelineResult build();

private:
	VkDevice                           m_device;
	const char*                        m_shaderPath       = nullptr;
	std::vector<VkDescriptorSetLayout> m_setLayouts;
	uint32_t                           m_pushConstantSize = 0;
	VkPipelineLayout                   m_existingLayout   = VK_NULL_HANDLE;
};

class PipelineBuilder
{
public:
	std::vector<VkPipelineShaderStageCreateInfo> m_shaderStages;

	VkPipelineInputAssemblyStateCreateInfo m_inputAssembly {};
	VkPipelineRasterizationStateCreateInfo m_rasterizer {};
	VkPipelineColorBlendAttachmentState    m_colorBlendAttachment {};
	VkPipelineMultisampleStateCreateInfo   m_multisampling {};
	VkPipelineLayout                       m_pipelineLayout = VK_NULL_HANDLE;
	VkPipelineDepthStencilStateCreateInfo  m_depthStencil {};
	// so all systems related to VkRenderPass will be completely skipped
	// Instead, we extend the VkGraphicsPipelineCreateInfo with a
	// VkPipelineRenderingCreateInfo added into its pNext chain. This structure
	// holds a list of the attachment formats the pipeline will use.
	VkPipelineRenderingCreateInfo m_renderInfo {};
	VkFormat                      m_colorAttachmentformat = VK_FORMAT_UNDEFINED;
	VkPipelineCreateFlags         m_flags {0};

	PipelineBuilder()
	{
		clear();
	}

	void clear();

	VkPipeline buildPipeline(VkDevice device);
	void enableDescriptorBuffer();
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
	void enableDepthBias(float constantFactor, float slopeFactor, float clamp = 0.0f);
};