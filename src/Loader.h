#pragma once
#include "vk_descriptors.h"
#include <filesystem>
#include <unordered_map>
#include <vk_types.h>

struct GLTFMaterial
{
	MaterialInstance data;
};

struct Bounds
{
	glm::vec3 origin;
	float     sphereRadius;
	glm::vec3 extents;
};

struct GeoSurface
{
	uint32_t                      startIndex;
	uint32_t                      count;
	Bounds                        bounds;
	std::shared_ptr<GLTFMaterial> material;
};

struct MeshAsset
{
	std::string name;

	std::vector<GeoSurface> surfaces;
	GPUMeshBuffers          meshBuffers;
};

// forward declaration
class AgniEngine;


struct LoadedGLTF : public IRenderable
{

	// storage for all the data on a given glTF file
	std::unordered_map<std::string, std::shared_ptr<MeshAsset>>    meshes;
	std::unordered_map<std::string, std::shared_ptr<Node>>         nodes;
	std::unordered_map<std::string, AllocatedImage>                images;
	std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

	// nodes that dont have a parent, for iterating through the file in tree
	// order
	std::vector<std::shared_ptr<Node>> topNodes;

	std::vector<VkSampler> samplers;

	DescriptorAllocatorGrowable descriptorPool;

	AllocatedBuffer materialDataBuffer;

	AgniEngine* creator;

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