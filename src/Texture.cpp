#include <Texture.hpp>

#include <ResourceManager.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/packing.hpp>
#include <vector>

void Texture::createSolidColor(ResourceManager& resourceManager,
                                   VkDevice         device,
                                   float            r,
                                   float            g,
                                   float            b,
                                   float            a,
                                   VkFilter         filter)
{
	// Create image
	uint32_t color = glm::packUnorm4x8(glm::vec4(r, g, b, a));
	image          = resourceManager.createImage((void*) &color,
                                                VkExtent3D {1, 1, 1},
                                                VK_FORMAT_R8G8B8A8_UNORM,
                                                VK_IMAGE_USAGE_SAMPLED_BIT);

	// Create sampler
	sampler = createSampler(device, filter, filter);
}

void Texture::createCheckerboard(ResourceManager& resourceManager,
                                     VkDevice         device,
                                     int              width,
                                     int              height,
                                     float            color1R,
                                     float            color1G,
                                     float            color1B,
                                     float            color2R,
                                     float            color2G,
                                     float            color2B,
                                     VkFilter         filter)
{
	// Create checkerboard pattern
	uint32_t color1 = glm::packUnorm4x8(glm::vec4(color1R, color1G, color1B, 1.0f));
	uint32_t color2 = glm::packUnorm4x8(glm::vec4(color2R, color2G, color2B, 1.0f));

	std::vector<uint32_t> pixels(width * height);
	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			pixels[y * width + x] = ((x % 2) ^ (y % 2)) ? color1 : color2;
		}
	}

	// Create image
	image = resourceManager.createImage(pixels.data(),
	                                    VkExtent3D {static_cast<uint32_t>(width),
	                                                static_cast<uint32_t>(height),
	                                                1},
	                                    VK_FORMAT_R8G8B8A8_UNORM,
	                                    VK_IMAGE_USAGE_SAMPLED_BIT);

	// Create sampler
	sampler = createSampler(device, filter, filter);
}

void Texture::destroy(ResourceManager& resourceManager, VkDevice device)
{
	// Destroy sampler
	if (sampler != VK_NULL_HANDLE)
	{
		vkDestroySampler(device, sampler, nullptr);
		sampler = VK_NULL_HANDLE;
	}

	// Destroy image
	resourceManager.destroyImage(image);
}

VkSampler Texture::createSampler(VkDevice             device,
                                     VkFilter             magFilter,
                                     VkFilter             minFilter,
                                     VkSamplerAddressMode addressMode)
{
	VkSamplerCreateInfo samplerInfo = {
	.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
	.pNext                   = nullptr,
	.magFilter               = magFilter,
	.minFilter               = minFilter,
	.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR,
	.addressModeU            = addressMode,
	.addressModeV            = addressMode,
	.addressModeW            = addressMode,
	.mipLodBias              = 0.0f,
	.anisotropyEnable        = VK_FALSE,
	.maxAnisotropy           = 1.0f,
	.compareEnable           = VK_FALSE,
	.compareOp               = VK_COMPARE_OP_ALWAYS,
	.minLod                  = 0.0f,
	.maxLod                  = 0.0f,
	.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
	.unnormalizedCoordinates = VK_FALSE};

	VkSampler newSampler;
	vkCreateSampler(device, &samplerInfo, nullptr, &newSampler);
	return newSampler;
}
