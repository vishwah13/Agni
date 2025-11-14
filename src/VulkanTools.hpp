#pragma once
#include <fmt/core.h>

#include <string>

#include <volk.h>

#define VK_CHECK(x)                                     \
	do                                                  \
	{                                                   \
		VkResult err = x;                               \
		if (err != VK_SUCCESS)                          \
		{                                               \
			fmt::println("Fatal Vulkan error: {}",      \
			             vks::tools::errorString(err)); \
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