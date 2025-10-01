#pragma once
#include <vk_types.h>

namespace vkutil
{

	void transitionImage(VkCommandBuffer cmd,
	                     VkImage         image,
	                     VkImageLayout   currentLayout,
	                     VkImageLayout   newLayout);

};