#pragma once

#include <Types.hpp>
#include <volk.h>

// Forward declarations
class ResourceManager;

// Texture encapsulates an image and its sampler
// NOTE: This class uses shallow copy semantics (copies handles, not resources)
// Only the owner should call destroy()
class Texture
{
public:
	Texture()  = default;
	~Texture() = default;

	// Copy constructor (shallow copy - copies handles, not resources)
	Texture(const Texture& other)
	    : image(other.image)
	    , sampler(other.sampler)
	{
	}

	// Copy assignment (shallow copy - copies handles, not resources)
	Texture& operator=(const Texture& other)
	{
		if (this != &other)
		{
			image   = other.image;
			sampler = other.sampler;
		}
		return *this;
	}

	// Move constructor (transfers ownership)
	Texture(Texture&& other) noexcept
	    : image(other.image)
	    , sampler(other.sampler)
	{
		// Nullify the moved-from object
		other.image.m_image      = VK_NULL_HANDLE;
		other.image.m_imageView  = VK_NULL_HANDLE;
		other.image.m_allocation = VK_NULL_HANDLE;
		other.sampler            = VK_NULL_HANDLE;
	}

	// Move assignment (transfers ownership)
	Texture& operator=(Texture&& other) noexcept
	{
		if (this != &other)
		{
			image   = other.image;
			sampler = other.sampler;

			// Nullify the moved-from object
			other.image.m_image      = VK_NULL_HANDLE;
			other.image.m_imageView  = VK_NULL_HANDLE;
			other.image.m_allocation = VK_NULL_HANDLE;
			other.sampler            = VK_NULL_HANDLE;
		}
		return *this;
	}

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
	// WARNING: Only call this if you own the texture!
	// Shallow copies should not call destroy()
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
