#pragma once
#include <Descriptors.hpp>
#include <Material.hpp>
#include <Scene.hpp>
#include <Types.hpp>

#include <filesystem>
#include <unordered_map>

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

// forward declaration
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

	std::vector<VkSampler> m_samplers;

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

// I can make a new class called AssetLoader or something that will have this
// loadGltf func and AgniEngine as an friend class so that i can access its
// internals
std::optional<std::shared_ptr<LoadedGLTF>>
loadGltf(AgniEngine* engine, std::filesystem::path filePath);