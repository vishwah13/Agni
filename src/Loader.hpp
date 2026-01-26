#pragma once
#include <BindlessResources.hpp>
#include <Components.hpp>
#include <DescriptorBuffer.hpp>
#include <Material.hpp>
#include <Scene.hpp>
#include <Types.hpp>

#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <unordered_map>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

struct GLTFMaterial
{
	MaterialInstance m_data;
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

struct LoadedGLTF
{
	// storage for all the data on a given glTF file
	std::unordered_map<std::string, std::shared_ptr<MeshAsset>>    meshes;
	std::unordered_map<std::string, std::shared_ptr<Node>>         nodes;
	std::unordered_map<std::string, AllocatedImage>                m_images;
	std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

	// nodes that dont have a parent, for iterating through the file in tree
	// order
	std::vector<std::shared_ptr<Node>> m_topNodes;

	// Source file path for serialization (set after loading)
	std::filesystem::path sourcePath;

	AgniEngine* m_creator;

	~LoadedGLTF()
	{
		clearAll();
	};

private:
	void clearAll();
};

// ============================================================================
// Async Loading Structures
// ============================================================================

// Progress callback - called from loading thread
using LoadProgressCallback = std::function<void(float progress, const std::string& stage)>;

// Internal structure for parallel image decoding (CPU work only)
struct ImageDecodeTask
{
	size_t         imageIndex = 0;
	unsigned char* decodedData = nullptr;
	int            width       = 0;
	int            height      = 0;
	int            channels    = 0;
	bool           success     = false;
	std::string    name;
};

// Internal structure for parallel mesh processing (CPU work only)
struct MeshProcessTask
{
	size_t                  meshIndex = 0;
	std::string             name;
	std::vector<Vertex>     vertices;
	std::vector<uint32_t>   indices;
	std::vector<GeoSurface> surfaces;
	bool                    success = false;
};

// Async loading handle - returned by loadGltfAsync()
struct AsyncLoadHandle
{
	std::filesystem::path      filePath;
	std::atomic<float>         progress {0.0f};
	std::atomic<bool>          cancelled {false};
	std::atomic<bool>          cpuWorkComplete {false};
	std::atomic<bool>          gpuUploadComplete {false};
	std::string                currentStage;
	std::shared_ptr<LoadedGLTF> result; // Set when fully complete
};

class AssetLoader
{
public:
	void init(ResourceManager* resourceManager, VkDevice device);
	void cleanup();
	void buildPipelines(AgniEngine* engine);

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
	const Texture& getDefaultNormalTexture() const
	{
		return m_defaultNormalTexture;
	}

	// Default material getter (for glTF files without materials)
	std::shared_ptr<GLTFMaterial> getDefaultMaterial() const
	{
		return m_defaultMaterial;
	}

	// Mesh resources (kept alive but not rendered, e.g., primitives for lights)
	std::shared_ptr<LoadedGLTF>& getMeshResources()
	{
		return m_meshResources;
	}

	// Register default textures with bindless TextureRegistry (call after init)
	void registerDefaultTextures(TextureRegistry& textureRegistry);

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

	// glTF loading (blocking - uses internal parallelism)
	std::optional<std::shared_ptr<LoadedGLTF>>
	loadGltf(AgniEngine* engine, std::filesystem::path filePath);

	// Async glTF loading - returns immediately, loading happens in background
	// For blocking behavior: loop until handle->gpuUploadComplete is true
	std::shared_ptr<AsyncLoadHandle>
	loadGltfAsync(AgniEngine*                engine,
	              std::filesystem::path      filePath,
	              LoadProgressCallback       progressCallback = nullptr);

	// MUST call every frame from main thread - handles GPU uploads for completed CPU work
	void processCompletedLoads();

	// PBR Material system (used by all glTF materials)
	GltfPbrMaterial& getMaterialSystem()
	{
		return m_metalRoughMaterial;
	}

private:
	// Default textures
	Texture m_whiteTexture;
	Texture m_blackTexture;
	Texture m_greyTexture;
	Texture m_errorCheckerboardTexture;
	Texture m_defaultNormalTexture; // Flat normal (0.5, 0.5, 1.0)

	// Shared samplers
	VkSampler m_linearSampler        = VK_NULL_HANDLE;
	VkSampler m_nearestSampler       = VK_NULL_HANDLE;
	VkSampler m_linearMipmapSampler  = VK_NULL_HANDLE;
	VkSampler m_nearestMipmapSampler = VK_NULL_HANDLE;

	// PBR Material system (shared pipeline for all glTF materials)
	GltfPbrMaterial m_metalRoughMaterial;

	// Default material (for glTF files without materials)
	std::shared_ptr<GLTFMaterial> m_defaultMaterial;

	// Mesh resources (kept alive but not rendered)
	std::shared_ptr<LoadedGLTF> m_meshResources;

	ResourceManager* m_resourceManager = nullptr;
	VkDevice         m_device          = VK_NULL_HANDLE;

	// ========================================================================
	// Async Loading Infrastructure
	// ========================================================================

	// Queue of loads with CPU work done, waiting for GPU upload on main thread
	struct PendingGPUUpload
	{
		std::shared_ptr<AsyncLoadHandle> handle;
		AgniEngine*                      engine;
		fastgltf::Asset                  gltfAsset;
		std::vector<ImageDecodeTask>     decodedImages;
		std::vector<MeshProcessTask>     processedMeshes;
		std::vector<uint32_t>            samplerIndexMapping;
		LoadProgressCallback             progressCallback;
	};

	std::mutex                    m_pendingMutex;
	std::vector<PendingGPUUpload> m_pendingUploads;

	// Mutex for thread-safe access to registries during async loading
	std::mutex m_registryMutex;

	// ========================================================================
	// Internal Parallel Loading Helpers
	// ========================================================================

	// Decode all images in parallel (CPU work only, no GPU)
	void decodeImagesParallel(fastgltf::Asset&              asset,
	                          std::vector<ImageDecodeTask>& tasks);

	// Process all meshes in parallel (vertex extraction + tangent generation, no GPU)
	void processMeshesParallel(fastgltf::Asset&               asset,
	                           std::vector<MeshProcessTask>&  tasks,
	                           const std::vector<std::shared_ptr<GLTFMaterial>>& materials);

	// Upload decoded images to GPU (must be called from main thread)
	std::vector<AllocatedImage> uploadImagesToGPU(
	    const std::vector<ImageDecodeTask>& decodedImages,
	    bool                                mipmapped);

	// Upload processed meshes to GPU (must be called from main thread)
	std::vector<GPUMeshBuffers> uploadMeshesToGPU(
	    const std::vector<MeshProcessTask>& processedMeshes);

	// Finalize a pending load on the main thread (GPU uploads + scene graph)
	void finalizePendingLoad(PendingGPUUpload& pending);
};
