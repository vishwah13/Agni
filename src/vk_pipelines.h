#pragma once 
#include <vk_types.h>

namespace vkutil {

	bool loadShaderModule(const char*     filePath,
	                        VkDevice        device,
	                        VkShaderModule* outShaderModule);
};