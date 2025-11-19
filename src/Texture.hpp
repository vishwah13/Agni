#pragma once

#include <Types.hpp>
#include <volk.h>

// Forward declarations
class ResourceManager;

// Texture encapsulates an image and its sampler
class TextureData
{
public:
	TextureData()  = default;
	~TextureData() = default;

	// Create a solid color texture (1x1 pixel)
	void createSolidColor(ResourceManager& resourceManager,
	                      VkDevice         device,
	                      float            r,
	                      float            g,
	                      float            b,
	                      float            a,
	                      VkFilter         filter = VK_FILTER_LINEAR);

	// Create a checkerboard pattern texture
	void createCheckerboard(ResourceManager& resourceManager,
	                        VkDevice         device,
	                        int              width,
	                        int              height,
	                        float            color1R,
	                        float            color1G,
	                        float            color1B,
	                        float            color2R,
	                        float            color2G,
	                        float            color2B,
	                        VkFilter         filter = VK_FILTER_NEAREST);

	// Destroy the texture (image and sampler)
	void destroy(ResourceManager& resourceManager, VkDevice device);

	// Public members for direct access
	AllocatedImage image;
	VkSampler      sampler = VK_NULL_HANDLE;

private:
	// Helper: Create a sampler
	VkSampler createSampler(VkDevice             device,
	                        VkFilter             magFilter,
	                        VkFilter             minFilter,
	                        VkSamplerAddressMode addressMode =
	                        VK_SAMPLER_ADDRESS_MODE_REPEAT);
};

// Texture utility class (can be extended with additional helper methods)
class Texture
{
public:
	// Future utility methods can go here
};
