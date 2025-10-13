// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>

#define VK_NO_PROTOTYPES
#include <volk.h>
#include <vk_mem_alloc.h>

#include <fmt/core.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/type_ptr.hpp>


#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult err = x;                                               \
        if (err) {                                                      \
            fmt::println("Detected Vulkan error: {}", static_cast<int>(err)); \
            abort();                                                    \
        }                                                               \
    } while (0)

struct AllocatedImage
{
	VkImage       image;
	VkImageView   imageView;
	VmaAllocation allocation;
	VkExtent3D    imageExtent;
	VkFormat      imageFormat;
};