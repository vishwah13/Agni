#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using EntityID = uint64_t;

// Forward declarations
class AgniEngine;
struct LoadedGLTF;
struct MeshAsset;

namespace agni::scene
{

// Progress callback for async operations
using SceneProgressCallback = std::function<void(float progress, const std::string& stage)>;

struct SceneSaveOptions
{
	bool prettyPrint     = true;
	bool includeMetadata = true;
};

struct SceneLoadOptions
{
	bool                   clearExisting    = true; // Clear world before loading
	bool                   reloadAssets     = true; // Auto-reload referenced glTF assets
	SceneProgressCallback  progressCallback = nullptr;
};

class SceneSerializer
{
public:
	explicit SceneSerializer(AgniEngine& engine);
	~SceneSerializer() = default;

	// Non-copyable
	SceneSerializer(const SceneSerializer&)            = delete;
	SceneSerializer& operator=(const SceneSerializer&) = delete;

	// Save scene to file
	bool saveScene(const std::filesystem::path& filePath,
	               const SceneSaveOptions&      options = {});

	// Load scene from file
	bool loadScene(const std::filesystem::path& filePath,
	               const SceneLoadOptions&      options = {});

	// Get last error message
	const std::string& getLastError() const { return m_lastError; }

	// Current scene path (empty if not saved yet)
	const std::filesystem::path& getCurrentScenePath() const { return m_currentScenePath; }
	void setCurrentScenePath(const std::filesystem::path& path) { m_currentScenePath = path; }

	// Check if scene has unsaved changes
	bool hasUnsavedChanges() const { return m_hasUnsavedChanges; }
	void markDirty() { m_hasUnsavedChanges = true; }
	void clearDirty() { m_hasUnsavedChanges = false; }

	// Snapshot/restore for Play/Stop mode
	std::string serializeToString(const SceneSaveOptions& options = {});
	bool deserializeFromString(const std::string& json, const SceneLoadOptions& options = {});

	// Single entity serialization (for prefab save)
	std::string serializeSingleEntity(EntityID entityId);

private:
	AgniEngine& m_engine;
	std::string m_lastError;
	std::filesystem::path m_currentScenePath;
	bool m_hasUnsavedChanges = false;

	// Serialization helpers
	std::string serializeScene(const SceneSaveOptions& options);
	bool deserializeScene(const std::string& json, const SceneLoadOptions& options);

	// Asset path helpers
	std::string makeRelativePath(const std::filesystem::path& absolutePath);
	std::filesystem::path resolveAssetPath(const std::string& relativePath);

	// Two-pass loading helpers
	void collectAssetPaths(std::vector<std::string>& outPaths);
	void reloadAssets(const std::vector<std::string>& assetPaths, const SceneLoadOptions& options);
	void reconnectMeshAssets();

	// Primitive mesh lookup
	std::shared_ptr<MeshAsset> getPrimitiveMesh(const std::string& name);
};

} // namespace agni::scene
