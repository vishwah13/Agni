#pragma once
#include <string>
#include <volk.h>


namespace vks
{

	namespace tools
	{
		/** @brief Returns an error code as a string */
		std::string errorString(VkResult errorCode);
	} // namespace tools
} // namespace vks