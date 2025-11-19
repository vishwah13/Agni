#pragma once
#include <Descriptors.hpp>
#include <Material.hpp>
#include <Scene.hpp>
#include <Types.hpp>

#include <filesystem>
#include <unordered_map>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

struct GLTFMaterial
{
	MaterialInstance m_data;
};

struct Bounds
{
	glm::vec3 m_origin;
	float     m_sphereRadius;
	glm::vec3 m_extents;
};

struct GeoSurface
{
	uint32_t                      m_startIndex;
	uint32_t                      m_count;
	Bounds                        m_bounds;
	std::shared_ptr<GLTFMaterial> m_material;
};

struct MeshAsset
{
	std::string m_name;

	std::vector<GeoSurface> m_surfaces;
	GPUMeshBuffers          m_meshBuffers;
};

// forward declarations
class AgniEngine;

struct LoadedGLTF : public IRenderable
{

	// storage for all the data on a given glTF file
	std::unordered_map<std::string, std::shared_ptr<MeshAsset>>    meshes;
	std::unordered_map<std::string, std::shared_ptr<Node>>         nodes;
	std::unordered_map<std::string, AllocatedImage>                m_images;
	std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

	// nodes that dont have a parent, for iterating through the file in tree
	// order
	std::vector<std::shared_ptr<Node>> m_topNodes;

	DescriptorAllocatorGrowable m_descriptorPool;

	AllocatedBuffer m_materialDataBuffer;

	AgniEngine* m_creator;

	~LoadedGLTF()
	{
		clearAll();
	};

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;

private:
	void clearAll();
};

class AssetLoader
{
public:
	void init(ResourceManager* resourceManager, VkDevice device);
	void cleanup();

	// Default texture getters
	const Texture& getWhiteTexture() const
	{
		return m_whiteTexture;
	}
	const Texture& getBlackTexture() const
	{
		return m_blackTexture;
	}
	const Texture& getGreyTexture() const
	{
		return m_greyTexture;
	}
	const Texture& getErrorTexture() const
	{
		return m_errorCheckerboardTexture;
	}

	// Shared sampler getters
	VkSampler getLinearSampler() const
	{
		return m_linearSampler;
	}
	VkSampler getNearestSampler() const
	{
		return m_nearestSampler;
	}
	VkSampler getLinearMipmapSampler() const
	{
		return m_linearMipmapSampler;
	}
	VkSampler getNearestMipmapSampler() const
	{
		return m_nearestMipmapSampler;
	}

	// Image loading
	std::optional<AllocatedImage> loadImage(fastgltf::Asset& asset,
	                                        fastgltf::Image& image,
	                                        bool             mipmapped = false);

	// glTF loading
	std::optional<std::shared_ptr<LoadedGLTF>>
	loadGltf(AgniEngine* engine, std::filesystem::path filePath);

private:
	// Default textures
	Texture m_whiteTexture;
	Texture m_blackTexture;
	Texture m_greyTexture;
	Texture m_errorCheckerboardTexture;

	// Shared samplers
	VkSampler m_linearSampler        = VK_NULL_HANDLE;
	VkSampler m_nearestSampler       = VK_NULL_HANDLE;
	VkSampler m_linearMipmapSampler  = VK_NULL_HANDLE;
	VkSampler m_nearestMipmapSampler = VK_NULL_HANDLE;

	ResourceManager* m_resourceManager = nullptr;
	VkDevice         m_device          = VK_NULL_HANDLE;
};