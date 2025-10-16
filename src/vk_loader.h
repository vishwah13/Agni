#pragma once
#include <filesystem>
#include <unordered_map>
#include <vk_types.h>

struct GeoSurface
{
	uint32_t startIndex;
	uint32_t count;
};

struct MeshAsset
{
	std::string name;

	std::vector<GeoSurface> surfaces;
	GPUMeshBuffers          meshBuffers;
};

// forward declaration
class AgniEngine;

std::optional<std::vector<std::shared_ptr<MeshAsset>>>
loadGltfMeshes(AgniEngine* engine, std::filesystem::path filePath);