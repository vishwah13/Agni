#pragma once
#include <Debug.hpp>

#include <string>

#include <volk.h>

#define VK_CHECK(x)                                     \
	do                                                  \
	{                                                   \
		VkResult err = x;                               \
		if (err != VK_SUCCESS)                          \
		{                                               \
			DBG_PRINT("Fatal Vulkan error: {}\n",       \
			          vks::tools::errorString(err));    \
			assert(err == VK_SUCCESS);                  \
		}                                               \
	} while (0)

namespace vks
{

	namespace tools
	{
		/** @brief Returns an error code as a string */
		std::string errorString(VkResult errorCode);
	} // namespace tools
} // namespace vks