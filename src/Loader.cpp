#include <iostream>
#include <variant>

#include <stb_image.h>

#include <AgniEngine.hpp>
#include <Debug.hpp>
#include <Initializers.hpp>
#include <Loader.hpp>
#include <ThreadPool.hpp>
#include <Types.hpp>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <mikktspace.h>

// Returns either VK_FILTER_NEAREST or VK_FILTER_LINEAR
VkFilter extractFilter(fastgltf::Filter filter)
{
	switch (filter)
	{
		// nearest samplers
		case fastgltf::Filter::Nearest:
		case fastgltf::Filter::NearestMipMapNearest:
		case fastgltf::Filter::NearestMipMapLinear:
			return VK_FILTER_NEAREST;

		// linear samplers
		case fastgltf::Filter::Linear:
		case fastgltf::Filter::LinearMipMapNearest:
		case fastgltf::Filter::LinearMipMapLinear:
		default:
			return VK_FILTER_LINEAR;
	}
}

//  Return as VK_SAMPLER_MIPMAP_MODE_NEAREST or VK_SAMPLER_MIPMAP_MODE_LINEAR.
//  Linear will blend mipmaps, while nearest will use a single one with no
//  blending.
VkSamplerMipmapMode extractMipmapMode(fastgltf::Filter filter)
{
	switch (filter)
	{
		case fastgltf::Filter::NearestMipMapNearest:
		case fastgltf::Filter::LinearMipMapNearest:
			return VK_SAMPLER_MIPMAP_MODE_NEAREST;

		case fastgltf::Filter::NearestMipMapLinear:
		case fastgltf::Filter::LinearMipMapLinear:
		default:
			return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
}

// ============================================================================
// AssetLoader Implementation
// ============================================================================

void AssetLoader::init(ResourceManager* resourceManager, VkDevice device)
{
	m_resourceManager = resourceManager;
	m_device          = device;

	// Create default textures
	m_whiteTexture.createSolidColor(*m_resourceManager,
	                                m_device,
	                                1.0f,
	                                1.0f,
	                                1.0f,
	                                1.0f,
	                                VK_FILTER_LINEAR);
	m_greyTexture.createSolidColor(*m_resourceManager,
	                               m_device,
	                               0.66f,
	                               0.66f,
	                               0.66f,
	                               1.0f,
	                               VK_FILTER_LINEAR);
	m_blackTexture.createSolidColor(*m_resourceManager,
	                                m_device,
	                                0.0f,
	                                0.0f,
	                                0.0f,
	                                0.0f,
	                                VK_FILTER_LINEAR);
	m_errorCheckerboardTexture.createCheckerboard(*m_resourceManager,
	                                              m_device,
	                                              16,
	                                              16,
	                                              1.0f,
	                                              0.0f,
	                                              1.0f,
	                                              0.0f,
	                                              0.0f,
	                                              0.0f,
	                                              VK_FILTER_NEAREST);
	// Default normal texture (flat normal pointing up in tangent space: 0.5, 0.5, 1.0)
	m_defaultNormalTexture.createSolidColor(*m_resourceManager,
	                                        m_device,
	                                        0.5f,
	                                        0.5f,
	                                        1.0f,
	                                        1.0f,
	                                        VK_FILTER_LINEAR);

	// Create shared samplers
	VkSamplerCreateInfo samplerInfo = {
	.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = nullptr};
	samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
	samplerInfo.minLod = 0;

	// Linear sampler (no mipmaps)
	samplerInfo.magFilter  = VK_FILTER_LINEAR;
	samplerInfo.minFilter  = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	vkCreateSampler(m_device, &samplerInfo, nullptr, &m_linearSampler);

	// Nearest sampler (no mipmaps)
	samplerInfo.magFilter  = VK_FILTER_NEAREST;
	samplerInfo.minFilter  = VK_FILTER_NEAREST;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	vkCreateSampler(m_device, &samplerInfo, nullptr, &m_nearestSampler);

	// Linear sampler with mipmaps
	samplerInfo.magFilter  = VK_FILTER_LINEAR;
	samplerInfo.minFilter  = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	vkCreateSampler(m_device, &samplerInfo, nullptr, &m_linearMipmapSampler);

	// Nearest sampler with mipmaps
	samplerInfo.magFilter  = VK_FILTER_NEAREST;
	samplerInfo.minFilter  = VK_FILTER_NEAREST;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	vkCreateSampler(m_device, &samplerInfo, nullptr, &m_nearestMipmapSampler);
}

void AssetLoader::registerDefaultTextures(TextureRegistry& textureRegistry)
{
	// Register default textures with the bindless texture registry
	textureRegistry.whiteTextureIndex = textureRegistry.registerTexture(
	    m_whiteTexture.image.m_imageView);
	textureRegistry.blackTextureIndex = textureRegistry.registerTexture(
	    m_blackTexture.image.m_imageView);
	textureRegistry.greyTextureIndex = textureRegistry.registerTexture(
	    m_greyTexture.image.m_imageView);
	textureRegistry.errorTextureIndex = textureRegistry.registerTexture(
	    m_errorCheckerboardTexture.image.m_imageView);
	textureRegistry.defaultNormalIndex = textureRegistry.registerTexture(
	    m_defaultNormalTexture.image.m_imageView);
}

void AssetLoader::cleanup()
{
	// Destroy default textures
	m_whiteTexture.destroy(*m_resourceManager, m_device);
	m_greyTexture.destroy(*m_resourceManager, m_device);
	m_blackTexture.destroy(*m_resourceManager, m_device);
	m_errorCheckerboardTexture.destroy(*m_resourceManager, m_device);
	m_defaultNormalTexture.destroy(*m_resourceManager, m_device);

	// Destroy default material resources
	m_defaultMaterial.reset();

	// Destroy shared samplers
	if (m_linearSampler != VK_NULL_HANDLE)
	{
		vkDestroySampler(m_device, m_linearSampler, nullptr);
		m_linearSampler = VK_NULL_HANDLE;
	}
	if (m_nearestSampler != VK_NULL_HANDLE)
	{
		vkDestroySampler(m_device, m_nearestSampler, nullptr);
		m_nearestSampler = VK_NULL_HANDLE;
	}
	if (m_linearMipmapSampler != VK_NULL_HANDLE)
	{
		vkDestroySampler(m_device, m_linearMipmapSampler, nullptr);
		m_linearMipmapSampler = VK_NULL_HANDLE;
	}
	if (m_nearestMipmapSampler != VK_NULL_HANDLE)
	{
		vkDestroySampler(m_device, m_nearestMipmapSampler, nullptr);
		m_nearestMipmapSampler = VK_NULL_HANDLE;
	}

	if (m_meshResources)
	{
		m_meshResources.reset();
	}

	// Clear material system resources
	m_metalRoughMaterial.clearResources(m_device);
}

void AssetLoader::buildPipelines(AgniEngine* engine)
{
	m_metalRoughMaterial.buildPipelines(engine);

	// Check if this is initial setup or resize
	bool isResize = (m_defaultMaterial != nullptr);

	if (!isResize)
	{
		// Create the default material using bindless system
		TextureRegistry& texRegistry = engine->m_renderer.getTextureRegistry();
		MaterialRegistry& matRegistry = engine->m_renderer.getMaterialRegistry();

		GPUMaterialData defaultMatData {};
		defaultMatData.colorFactors       = glm::vec4(1.0f);
		defaultMatData.metalRoughFactors  = glm::vec4(0.0f, 0.5f, 0.0f, 0.0f);
		defaultMatData.colorTexIndex      = texRegistry.whiteTextureIndex;
		defaultMatData.metalRoughTexIndex = texRegistry.whiteTextureIndex;
		defaultMatData.normalTexIndex     = texRegistry.defaultNormalIndex;
		defaultMatData.aoTexIndex         = texRegistry.whiteTextureIndex;
		defaultMatData.samplerIndex       = static_cast<uint32_t>(BindlessSamplerType::LinearMipmap);

		m_defaultMaterial = std::make_shared<GLTFMaterial>();
		m_defaultMaterial->m_data.m_materialIndex = matRegistry.registerMaterial(defaultMatData);
		m_defaultMaterial->m_data.m_pipeline = &m_metalRoughMaterial.m_opaquePipeline;
		m_defaultMaterial->m_data.m_passType = MaterialPass::MainColor;
	}
	else
	{
		// Resize case - just update the pipeline pointer in existing material
		m_defaultMaterial->m_data.m_pipeline = &m_metalRoughMaterial.m_opaquePipeline;
	}
}

// ============================================================================
// MikkTSpace Implementation
// ============================================================================

// MikkTSpace implementation for tangent generation
struct MikkTSpaceUserData
{
	std::vector<Vertex>*   vertices;
	std::vector<uint32_t>* indices;
	size_t vertexOffset; // offset to the current primitive's vertices
};

// MikkTSpace callback: Get number of faces
int mikkGetNumFaces(const SMikkTSpaceContext* pContext)
{
	MikkTSpaceUserData* userData =
	static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);
	return static_cast<int>(userData->indices->size() / 3); // triangles only
}

// MikkTSpace callback: Get number of vertices per face (always 3 for triangles)
int mikkGetNumVerticesOfFace(const SMikkTSpaceContext* pContext,
                             const int                 iFace)
{
	(void)pContext;
	(void)iFace;
	return 3; // triangles
}

// MikkTSpace callback: Get position
void mikkGetPosition(const SMikkTSpaceContext* pContext,
                     float                     fvPosOut[],
                     const int                 iFace,
                     const int                 iVert)
{
	MikkTSpaceUserData* userData =
	static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);
	int           vertexIndex = (*userData->indices)[iFace * 3 + iVert];
	const Vertex& vertex      = (*userData->vertices)[vertexIndex];
	fvPosOut[0]               = vertex.m_position.x;
	fvPosOut[1]               = vertex.m_position.y;
	fvPosOut[2]               = vertex.m_position.z;
}

// MikkTSpace callback: Get normal
void mikkGetNormal(const SMikkTSpaceContext* pContext,
                   float                     fvNormOut[],
                   const int                 iFace,
                   const int                 iVert)
{
	MikkTSpaceUserData* userData =
	static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);
	int           vertexIndex = (*userData->indices)[iFace * 3 + iVert];
	const Vertex& vertex      = (*userData->vertices)[vertexIndex];
	fvNormOut[0]              = vertex.m_normal.x;
	fvNormOut[1]              = vertex.m_normal.y;
	fvNormOut[2]              = vertex.m_normal.z;
}

// MikkTSpace callback: Get texture coordinates
void mikkGetTexCoord(const SMikkTSpaceContext* pContext,
                     float                     fvTexcOut[],
                     const int                 iFace,
                     const int                 iVert)
{
	MikkTSpaceUserData* userData =
	static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);
	int           vertexIndex = (*userData->indices)[iFace * 3 + iVert];
	const Vertex& vertex      = (*userData->vertices)[vertexIndex];
	fvTexcOut[0]              = vertex.m_uv_x;
	fvTexcOut[1]              = vertex.m_uv_y;
}

// MikkTSpace callback: Set tangent space (basic version)
void mikkSetTSpaceBasic(const SMikkTSpaceContext* pContext,
                        const float               fvTangent[],
                        const float               fSign,
                        const int                 iFace,
                        const int                 iVert)
{
	MikkTSpaceUserData* userData =
	static_cast<MikkTSpaceUserData*>(pContext->m_pUserData);
	int     vertexIndex = (*userData->indices)[iFace * 3 + iVert];
	Vertex& vertex      = (*userData->vertices)[vertexIndex];
	vertex.m_tangent.x  = fvTangent[0];
	vertex.m_tangent.y  = fvTangent[1];
	vertex.m_tangent.z  = fvTangent[2];
	vertex.m_tangent.w  = fSign; // store handedness in w component
}

std::optional<AllocatedImage> AssetLoader::loadImage(fastgltf::Asset& asset,
                                                      fastgltf::Image& image,
                                                      bool             mipmapped)
{
	AllocatedImage newImage {};

	int width, height, nrChannels;

	std::visit(
	fastgltf::visitor {
	[](auto&) {},
	[&](fastgltf::sources::URI& filePath)
	{
		assert(filePath.fileByteOffset ==
		       0); // We don't support offsets with stbi.
		assert(filePath.uri.isLocalPath()); // We're only capable of loading
		                                    // local files.

		const std::string path(filePath.uri.path().begin(),
		                       filePath.uri.path().end()); // Thanks C++.
		unsigned char*    data =
		stbi_load(path.c_str(), &width, &height, &nrChannels, 4);
		if (data)
		{
			VkExtent3D imagesize;
			imagesize.width  = width;
			imagesize.height = height;
			imagesize.depth  = 1;

			newImage = m_resourceManager->createImage(data,
			                               imagesize,
			                               VK_FORMAT_R8G8B8A8_UNORM,
			                               VK_IMAGE_USAGE_SAMPLED_BIT,
			                               mipmapped);

			stbi_image_free(data);
		}
		else
		{
			AGNI_PRINT("Failed to load image: {} - Reason: {}\n",
			          path,
			          stbi_failure_reason());
		}
	},
	[&](fastgltf::sources::Vector& vector)
	{
		unsigned char* data = stbi_load_from_memory(
		reinterpret_cast<const stbi_uc*>(vector.bytes.data()),
		static_cast<int>(vector.bytes.size()),
		&width,
		&height,
		&nrChannels,
		4);
		if (data)
		{
			VkExtent3D imagesize;
			imagesize.width  = width;
			imagesize.height = height;
			imagesize.depth  = 1;

			newImage = m_resourceManager->createImage(data,
			                               imagesize,
			                               VK_FORMAT_R8G8B8A8_UNORM,
			                               VK_IMAGE_USAGE_SAMPLED_BIT,
			                               mipmapped);

			stbi_image_free(data);
		}
		else
		{
			AGNI_PRINT("Failed to load image from memory: {}\n",
			          stbi_failure_reason());
		}
	},
	[&](fastgltf::sources::BufferView& view)
	{
		auto& bufferView = asset.bufferViews[view.bufferViewIndex];
		auto& buffer     = asset.buffers[bufferView.bufferIndex];

		std::visit(
		fastgltf::visitor {
		// We only care about VectorWithMime here, because we
		// specify LoadExternalBuffers, meaning all buffers
		// are already loaded into a vector.
		// but only sources::Array ended up working here?
		// still need to have other variants handled
		[](auto&) {},
		[&](fastgltf::sources::Array& vector)
		{
			unsigned char* data = stbi_load_from_memory(
			reinterpret_cast<const stbi_uc*>(vector.bytes.data()) +
			bufferView.byteOffset,
			static_cast<int>(bufferView.byteLength),
			&width,
			&height,
			&nrChannels,
			4);
			if (data)
			{
				VkExtent3D imagesize;
				imagesize.width  = width;
				imagesize.height = height;
				imagesize.depth  = 1;

				newImage = m_resourceManager->createImage(data,
				                               imagesize,
				                               VK_FORMAT_R8G8B8A8_UNORM,
				                               VK_IMAGE_USAGE_SAMPLED_BIT,
				                               mipmapped);

				stbi_image_free(data);
			}
			else
			{
				AGNI_PRINT("Failed to load image from buffer: {}\n",
				          stbi_failure_reason());
			}
		},
		[&](fastgltf::sources::Vector& vector)
		{
			unsigned char* data = stbi_load_from_memory(
			reinterpret_cast<const stbi_uc*>(vector.bytes.data()) +
			bufferView.byteOffset,
			static_cast<int>(bufferView.byteLength),
			&width,
			&height,
			&nrChannels,
			4);
			if (data)
			{
				VkExtent3D imagesize;
				imagesize.width  = width;
				imagesize.height = height;
				imagesize.depth  = 1;

				newImage = m_resourceManager->createImage(data,
				                               imagesize,
				                               VK_FORMAT_R8G8B8A8_UNORM,
				                               VK_IMAGE_USAGE_SAMPLED_BIT,
				                               mipmapped);

				stbi_image_free(data);
			}
			else
			{
				AGNI_PRINT("Failed to load image from buffer: {}\n",
				          stbi_failure_reason());
			}
		}},
		buffer.data);
	},
	},
	image.data);

	// if any of the attempts to load the data failed, we havent written the
	// image so handle is null
	if (newImage.m_image == VK_NULL_HANDLE)
	{
		return {};
	}
	else
	{
		return newImage;
	}
}

std::optional<std::shared_ptr<LoadedGLTF>>
AssetLoader::loadGltf(AgniEngine* engine, std::filesystem::path filePath)
{
#ifdef TRACY_ENABLE
	ZoneScoped;
	ZoneText(filePath.string().c_str(), filePath.string().size());
#endif

	AGNI_PRINT("Loading GLTF: {}\n", filePath.string());

	std::shared_ptr<LoadedGLTF> scene = std::make_shared<LoadedGLTF>();
	scene->m_creator                  = engine;
	LoadedGLTF& file                  = *scene.get();

	fastgltf::Parser parser(fastgltf::Extensions::KHR_lights_punctual);

	constexpr auto gltfOptions =
	fastgltf::Options::DontRequireValidAssetMember |
	fastgltf::Options::AllowDouble |
	fastgltf::Options::LoadExternalBuffers |
	fastgltf::Options::LoadExternalImages;

	auto data = fastgltf::GltfDataBuffer::FromPath(filePath);

	if (data.error() != fastgltf::Error::None)
	{
		AGNI_PRINT("Failed to load glTF file: {} \n",
		          fastgltf::to_underlying(data.error()));
		return {};
	}

	fastgltf::Asset gltf;

	std::filesystem::path path = filePath;

	auto type = fastgltf::determineGltfFileType(data.get());
	if (type == fastgltf::GltfType::glTF)
	{
		auto load =
		parser.loadGltf(data.get(), path.parent_path(), gltfOptions);
		if (load)
		{
			gltf = std::move(load.get());
		}
		else
		{
			AGNI_PRINT("Failed to parse glTF: {} \n",
			          fastgltf::to_underlying(load.error()));
			return {};
		}
	}
	else if (type == fastgltf::GltfType::GLB)
	{
		auto load =
		parser.loadGltfBinary(data.get(), path.parent_path(), gltfOptions);
		if (load)
		{
			gltf = std::move(load.get());
		}
		else
		{
			AGNI_PRINT("Failed to parse glTF: {} \n",
			          fastgltf::to_underlying(load.error()));
			return {};
		}
	}
	else
	{
		AGNI_PRINT("Failed to determine glTF container \n");
		return {};
	}

	// Map glTF samplers to bindless sampler indices
	// Instead of storing VkSampler handles, we store indices into the SamplerRegistry
	std::vector<uint32_t> samplerIndexMapping;
	for (fastgltf::Sampler& sampler : gltf.samplers)
	{
		VkFilter magFilter =
		extractFilter(sampler.magFilter.value_or(fastgltf::Filter::Linear));
		VkFilter minFilter =
		extractFilter(sampler.minFilter.value_or(fastgltf::Filter::Linear));
		VkSamplerMipmapMode mipmapMode = extractMipmapMode(
		sampler.minFilter.value_or(fastgltf::Filter::Linear));

		// Select appropriate sampler index based on filter settings
		uint32_t samplerIndex;
		if (magFilter == VK_FILTER_LINEAR && minFilter == VK_FILTER_LINEAR)
		{
			samplerIndex = (mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR)
			               ? static_cast<uint32_t>(BindlessSamplerType::LinearMipmap)
			               : static_cast<uint32_t>(BindlessSamplerType::Linear);
		}
		else
		{
			samplerIndex = (mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR)
			               ? static_cast<uint32_t>(BindlessSamplerType::NearestMipmap)
			               : static_cast<uint32_t>(BindlessSamplerType::Nearest);
		}

		samplerIndexMapping.push_back(samplerIndex);
	}

	// temporal arrays for all the objects to use while creating the GLTF data
	std::vector<std::shared_ptr<MeshAsset>>    meshes;
	std::vector<std::shared_ptr<Node>>         nodes;
	std::vector<AllocatedImage>                images;
	std::vector<std::shared_ptr<GLTFMaterial>> materials;

	// we have to load everything in order. MeshNodes depend on meshes, meshes
	// depend on materials, and materials on textures.

	// load all textures
	int imageIndex = 0;
	for (fastgltf::Image& image : gltf.images)
	{
		std::optional<AllocatedImage> img =
		loadImage(gltf, image, true);

		// Generate a unique name for this image (use name if available,
		// otherwise use index)
		std::string imageName = image.name.c_str();
		if (imageName.empty())
		{
			imageName = "image_" + std::to_string(imageIndex);
		}

		if (img.has_value())
		{
			images.push_back(*img);
			file.m_images[imageName] =
			*img; // Always store in map with a valid key
		}
		else
		{
			// we failed to load, so lets give the slot a default white texture
			// to not completely break loading
			images.push_back(m_errorCheckerboardTexture.image);
			std::cout << "gltf failed to load texture " << image.name
			          << std::endl;
		}
		imageIndex++;
	}

	// Get references to bindless registries
	TextureRegistry&  textureRegistry  = engine->m_renderer.getTextureRegistry();
	MaterialRegistry& materialRegistry = engine->m_renderer.getMaterialRegistry();

	for (fastgltf::Material& mat : gltf.materials)
	{
		std::shared_ptr<GLTFMaterial> newMat = std::make_shared<GLTFMaterial>();
		materials.push_back(newMat);
		file.materials[mat.name.c_str()] = newMat;

		// Determine pass type
		MaterialPass passType = MaterialPass::MainColor;
		if (mat.alphaMode == fastgltf::AlphaMode::Blend)
		{
			passType = MaterialPass::Transparent;
		}

		// Set up the pipeline based on pass type
		if (passType == MaterialPass::Transparent)
		{
			newMat->m_data.m_pipeline = &m_metalRoughMaterial.getTransparentPipeline();
		}
		else
		{
			newMat->m_data.m_pipeline = &m_metalRoughMaterial.getOpaquePipeline();
		}
		newMat->m_data.m_passType = passType;

		// Build GPUMaterialData for bindless rendering
		GPUMaterialData matData {};
		matData.colorFactors.x = mat.pbrData.baseColorFactor[0];
		matData.colorFactors.y = mat.pbrData.baseColorFactor[1];
		matData.colorFactors.z = mat.pbrData.baseColorFactor[2];
		matData.colorFactors.w = mat.pbrData.baseColorFactor[3];

		matData.metalRoughFactors.x = mat.pbrData.metallicFactor;
		matData.metalRoughFactors.y = mat.pbrData.roughnessFactor;

		// Default texture indices (use fallbacks from TextureRegistry)
		matData.colorTexIndex      = textureRegistry.whiteTextureIndex;
		matData.metalRoughTexIndex = textureRegistry.whiteTextureIndex;
		matData.normalTexIndex     = textureRegistry.defaultNormalIndex;
		matData.aoTexIndex         = textureRegistry.whiteTextureIndex;
		matData.samplerIndex       = static_cast<uint32_t>(BindlessSamplerType::LinearMipmap);

		// Register textures from glTF file and get their indices
		if (mat.pbrData.baseColorTexture.has_value())
		{
			size_t texIdx = mat.pbrData.baseColorTexture.value().textureIndex;
			size_t img    = gltf.textures[texIdx].imageIndex.value();
			size_t samp   = gltf.textures[texIdx].samplerIndex.value();

			matData.colorTexIndex = textureRegistry.registerTexture(images[img].m_imageView);
			matData.samplerIndex  = samplerIndexMapping[samp];
		}
		if (mat.pbrData.metallicRoughnessTexture.has_value())
		{
			size_t texIdx = mat.pbrData.metallicRoughnessTexture.value().textureIndex;
			size_t img    = gltf.textures[texIdx].imageIndex.value();

			matData.metalRoughTexIndex = textureRegistry.registerTexture(images[img].m_imageView);
		}
		if (mat.normalTexture.has_value())
		{
			size_t texIdx = mat.normalTexture.value().textureIndex;
			size_t img    = gltf.textures[texIdx].imageIndex.value();

			matData.normalTexIndex = textureRegistry.registerTexture(images[img].m_imageView);
		}
		if (mat.occlusionTexture.has_value())
		{
			size_t texIdx = mat.occlusionTexture.value().textureIndex;
			size_t img    = gltf.textures[texIdx].imageIndex.value();

			matData.aoTexIndex = textureRegistry.registerTexture(images[img].m_imageView);
		}

		// Register material with bindless MaterialRegistry
		newMat->m_data.m_materialIndex = materialRegistry.registerMaterial(matData);
	}

	// use the same vectors for all meshes so that the memory doesnt reallocate
	// as
	// often
	std::vector<uint32_t> indices;
	std::vector<Vertex>   vertices;

	for (fastgltf::Mesh& mesh : gltf.meshes)
	{
		std::shared_ptr<MeshAsset> newmesh = std::make_shared<MeshAsset>();
		meshes.push_back(newmesh);
		file.meshes[mesh.name.c_str()] = newmesh;
		newmesh->m_name                = mesh.name;

		// clear the mesh arrays each mesh, we dont want to merge them by error
		indices.clear();
		vertices.clear();

		for (auto&& p : mesh.primitives)
		{
			GeoSurface newSurface;
			newSurface.m_startIndex = (uint32_t) indices.size();
			newSurface.m_count =
			(uint32_t) gltf.accessors[p.indicesAccessor.value()].count;

			uint32_t initial_vtx = static_cast<uint32_t>(vertices.size());

			// load indexes
			{
				fastgltf::Accessor& indexaccessor =
				gltf.accessors[p.indicesAccessor.value()];
				indices.reserve(indices.size() + indexaccessor.count);

				fastgltf::iterateAccessor<std::uint32_t>(
				gltf,
				indexaccessor,
				[&](std::uint32_t idx)
				{ indices.push_back(idx + initial_vtx); });
			}

			// load vertex positions
			{
				fastgltf::Accessor& posAccessor =
				gltf.accessors[p.findAttribute("POSITION")->accessorIndex];
				vertices.resize(vertices.size() + posAccessor.count);

				fastgltf::iterateAccessorWithIndex<glm::vec3>(
				gltf,
				posAccessor,
				[&](glm::vec3 v, size_t index)
				{
					Vertex newvtx;
					newvtx.m_position             = v;
					newvtx.m_normal               = {1, 0, 0};
					newvtx.m_color                = glm::vec4 {1.f};
					newvtx.m_uv_x                 = 0;
					newvtx.m_uv_y                 = 0;
					newvtx.m_tangent              = {0, 0, 0, 0};
					vertices[initial_vtx + index] = newvtx;
				});
			}

			// load vertex normals
			auto normals = p.findAttribute("NORMAL");
			if (normals != p.attributes.end())
			{

				fastgltf::iterateAccessorWithIndex<glm::vec3>(
				gltf,
				gltf.accessors[(*normals).accessorIndex],
				[&](glm::vec3 v, size_t index)
				{ vertices[initial_vtx + index].m_normal = v; });
			}

			// load UVs
			auto uv = p.findAttribute("TEXCOORD_0");
			if (uv != p.attributes.end())
			{

				fastgltf::iterateAccessorWithIndex<glm::vec2>(
				gltf,
				gltf.accessors[(*uv).accessorIndex],
				[&](glm::vec2 v, size_t index)
				{
					vertices[initial_vtx + index].m_uv_x = v.x;
					vertices[initial_vtx + index].m_uv_y = v.y;
				});
			}

			// load vertex colors
			auto colors = p.findAttribute("COLOR_0");
			if (colors != p.attributes.end())
			{

				fastgltf::iterateAccessorWithIndex<glm::vec4>(
				gltf,
				gltf.accessors[(*colors).accessorIndex],
				[&](glm::vec4 v, size_t index)
				{ vertices[initial_vtx + index].m_color = v; });
			}

			// load tangents if available, otherwise generate them
			auto tangents = p.findAttribute("TANGENT");
			if (tangents != p.attributes.end())
			{
				// Use tangents from GLTF file
				fastgltf::iterateAccessorWithIndex<glm::vec4>(
				gltf,
				gltf.accessors[(*tangents).accessorIndex],
				[&](glm::vec4 v, size_t index)
				{ vertices[initial_vtx + index].m_tangent = v; });
			}
			else
			{
				// Generate tangents using MikkTSpace
				// Create a temporary index buffer for this primitive only
				std::vector<uint32_t> primitiveIndices;
				primitiveIndices.reserve(newSurface.m_count);
				for (uint32_t i = newSurface.m_startIndex;
				     i < newSurface.m_startIndex + newSurface.m_count;
				     i++)
				{
					primitiveIndices.push_back(indices[i]);
				}

				// Setup MikkTSpace interface
				SMikkTSpaceInterface mikkInterface   = {};
				mikkInterface.m_getNumFaces          = mikkGetNumFaces;
				mikkInterface.m_getNumVerticesOfFace = mikkGetNumVerticesOfFace;
				mikkInterface.m_getPosition          = mikkGetPosition;
				mikkInterface.m_getNormal            = mikkGetNormal;
				mikkInterface.m_getTexCoord          = mikkGetTexCoord;
				mikkInterface.m_setTSpaceBasic       = mikkSetTSpaceBasic;

				// Setup user data
				MikkTSpaceUserData userData = {};
				userData.vertices           = &vertices;
				userData.indices            = &primitiveIndices;
				userData.vertexOffset       = initial_vtx;

				// Setup context
				SMikkTSpaceContext mikkContext = {};
				mikkContext.m_pInterface       = &mikkInterface;
				mikkContext.m_pUserData        = &userData;

				// Generate tangents
				if (!genTangSpaceDefault(&mikkContext))
				{
					AGNI_PRINT(
					"Warning: Failed to generate tangents for mesh: {}\n",
					mesh.name);
				}
			}

			if (p.materialIndex.has_value())
			{
				newSurface.m_material = materials[p.materialIndex.value()];
			}
			else if (!materials.empty())
			{
				newSurface.m_material = materials[0];
			}
			else
			{
				// No materials in glTF file, use engine's default material
				newSurface.m_material = m_defaultMaterial;
			}

			// loop the vertices of this surface, find min/max bounds
			glm::vec3 minpos = vertices[initial_vtx].m_position;
			glm::vec3 maxpos = vertices[initial_vtx].m_position;
			for (int i = initial_vtx; i < vertices.size(); i++)
			{
				minpos = glm::min(minpos, vertices[i].m_position);
				maxpos = glm::max(maxpos, vertices[i].m_position);
			}
			// calculate origin and extents from the min/max, use extent lenght
			// for radius
			newSurface.m_bounds.m_origin  = (maxpos + minpos) / 2.f;
			newSurface.m_bounds.m_extents = (maxpos - minpos) / 2.f;
			newSurface.m_bounds.m_sphereRadius =
			glm::length(newSurface.m_bounds.m_extents);

			newmesh->m_surfaces.push_back(newSurface);
		}

		newmesh->m_meshBuffers = engine->m_resourceManager.uploadMesh(indices, vertices);
	}

	// load all nodes and their meshes
	for (fastgltf::Node& node : gltf.nodes)
	{
		std::shared_ptr<Node> newNode;

		// Check for light first (KHR_lights_punctual extension)
		// LightNode uses composition for optional mesh attachment
		if (node.lightIndex.has_value())
		{
			auto lightNode = std::make_shared<LightNode>();

			// Get light data from glTF
			const fastgltf::Light& gltfLight = gltf.lights[*node.lightIndex];

			// Set common properties (explicit cast from num which may be double)
			lightNode->setColor(glm::vec3(
			    static_cast<float>(gltfLight.color[0]),
			    static_cast<float>(gltfLight.color[1]),
			    static_cast<float>(gltfLight.color[2])));
			lightNode->setIntensity(static_cast<float>(gltfLight.intensity));

			// Set range (with default fallback)
			float radius = gltfLight.range.has_value()
			    ? static_cast<float>(*gltfLight.range)
			    : 10.0f;

			// Map fastgltf::LightType to engine LightType and set type-specific properties
			switch (gltfLight.type)
			{
				case fastgltf::LightType::Point:
					lightNode->setType(LightType::Point);
					lightNode->setRadius(radius);
					break;

				case fastgltf::LightType::Spot:
				{
					lightNode->setType(LightType::Spot);
					lightNode->setRadius(radius);
					lightNode->setDirection(glm::vec3(0.0f, 0.0f, -1.0f));

					// Convert cone angles from radians to degrees
					float innerDegrees = glm::degrees(
					    static_cast<float>(gltfLight.innerConeAngle.value_or(0.0f)));
					float outerDegrees = glm::degrees(
					    static_cast<float>(gltfLight.outerConeAngle.value_or(glm::radians(45.0f))));
					lightNode->setConeAngles(innerDegrees, outerDegrees);
					break;
				}

				case fastgltf::LightType::Directional:
					lightNode->setType(LightType::Directional);
					lightNode->setDirection(glm::vec3(0.0f, 0.0f, -1.0f));
					// Directional lights don't use radius
					break;
			}

			// If node also has a mesh, attach it via composition
			if (node.meshIndex.has_value())
			{
				lightNode->setMesh(meshes[*node.meshIndex]);
			}

			newNode = lightNode;
		}
		// No light, check for mesh
		else if (node.meshIndex.has_value())
		{
			newNode = std::make_shared<MeshNode>();
			static_cast<MeshNode*>(newNode.get())->getMesh() =
			meshes[*node.meshIndex];
		}
		// Empty node
		else
		{
			newNode = std::make_shared<Node>();
		}

		nodes.push_back(newNode);
		file.nodes[node.name.c_str()] = newNode;

		std::visit(
		fastgltf::visitor {[&](fastgltf::math::fmat4x4 matrix)
		                   {
			                   memcpy(&newNode->getLocalTransform(),
			                          matrix.data(),
			                          sizeof(matrix));
		                   },
		                   [&](fastgltf::TRS transform)
		                   {
			                   glm::vec3 tl(transform.translation[0],
			                                transform.translation[1],
			                                transform.translation[2]);
			                   glm::quat rot(transform.rotation[3],
			                                 transform.rotation[0],
			                                 transform.rotation[1],
			                                 transform.rotation[2]);
			                   glm::vec3 sc(transform.scale[0],
			                                transform.scale[1],
			                                transform.scale[2]);

			                   glm::mat4 tm =
			                   glm::translate(glm::mat4(1.f), tl);
			                   glm::mat4 rm = glm::toMat4(rot);
			                   glm::mat4 sm = glm::scale(glm::mat4(1.f), sc);

			                   newNode->getLocalTransform() = tm * rm * sm;
		                   }},
		node.transform);
	}

	// run loop again to setup transform hierarchy
	for (int i = 0; i < gltf.nodes.size(); i++)
	{
		fastgltf::Node&        node      = gltf.nodes[i];
		std::shared_ptr<Node>& sceneNode = nodes[i];

		for (auto& c : node.children)
		{
			sceneNode->getChildren().push_back(nodes[c]);
			nodes[c]->getParent() = sceneNode;
		}
	}

	// find the top nodes, with no parents
	for (auto& node : nodes)
	{
		if (node->getParent().lock() == nullptr)
		{
			file.m_topNodes.push_back(node);
			node->refreshTransform(glm::mat4 {1.f});
		}
	}
	return scene;
}

void LoadedGLTF::clearAll()
{
	for (auto& [k, v] : meshes)
	{
		if (v)
		{
			m_creator->m_resourceManager.destroyBuffer(v->m_meshBuffers.m_indexBuffer);
			m_creator->m_resourceManager.destroyBuffer(v->m_meshBuffers.m_vertexBuffer);
		}
	}

	for (auto& [k, v] : m_images)
	{

		if (v.m_image == m_creator->m_assetLoader.getErrorTexture().image.m_image)
		{
			// dont destroy the default images
			continue;
		}
		m_creator->m_resourceManager.destroyImage(v);
	}

	// Note: Samplers are now shared and managed by AssetLoader, not per-file
}

// ============================================================================
// Parallel Image Decoding (CPU work only)
// ============================================================================

void AssetLoader::decodeImagesParallel(fastgltf::Asset&              asset,
                                       std::vector<ImageDecodeTask>& tasks)
{
#ifdef TRACY_ENABLE
	ZoneScopedN("decodeImagesParallel");
#endif

	tasks.resize(asset.images.size());

	// Initialize task metadata
	for (size_t i = 0; i < asset.images.size(); i++)
	{
		tasks[i].imageIndex = i;
		tasks[i].name       = asset.images[i].name.c_str();
		if (tasks[i].name.empty())
		{
			tasks[i].name = "image_" + std::to_string(i);
		}
	}

	// Decode images in parallel (CPU-bound work)
	agni::ThreadPool::ParallelLoop(
	    [&](uint32_t start, uint32_t end)
	    {
		    for (uint32_t i = start; i < end; i++)
		    {
			    auto& task  = tasks[i];
			    auto& image = asset.images[i];

			    std::visit(
			        fastgltf::visitor {
			            [](auto&) {},
			            [&](fastgltf::sources::URI& filePath)
			            {
				            assert(filePath.fileByteOffset == 0);
				            assert(filePath.uri.isLocalPath());

				            const std::string path(filePath.uri.path().begin(),
				                                   filePath.uri.path().end());
				            task.decodedData = stbi_load(path.c_str(),
				                                         &task.width,
				                                         &task.height,
				                                         &task.channels,
				                                         4);
				            task.success = (task.decodedData != nullptr);
				            if (!task.success)
				            {
					            AGNI_PRINT("[Async] Failed to load image: {} - {}\n",
					                      path,
					                      stbi_failure_reason());
				            }
			            },
			            [&](fastgltf::sources::Vector& vector)
			            {
				            task.decodedData = stbi_load_from_memory(
				                reinterpret_cast<const stbi_uc*>(vector.bytes.data()),
				                static_cast<int>(vector.bytes.size()),
				                &task.width,
				                &task.height,
				                &task.channels,
				                4);
				            task.success = (task.decodedData != nullptr);
			            },
			            [&](fastgltf::sources::BufferView& view)
			            {
				            auto& bufferView = asset.bufferViews[view.bufferViewIndex];
				            auto& buffer     = asset.buffers[bufferView.bufferIndex];

				            std::visit(
				                fastgltf::visitor {
				                    [](auto&) {},
				                    [&](fastgltf::sources::Array& arr)
				                    {
					                    task.decodedData = stbi_load_from_memory(
					                        reinterpret_cast<const stbi_uc*>(arr.bytes.data()) +
					                            bufferView.byteOffset,
					                        static_cast<int>(bufferView.byteLength),
					                        &task.width,
					                        &task.height,
					                        &task.channels,
					                        4);
					                    task.success = (task.decodedData != nullptr);
				                    },
				                    [&](fastgltf::sources::Vector& vec)
				                    {
					                    task.decodedData = stbi_load_from_memory(
					                        reinterpret_cast<const stbi_uc*>(vec.bytes.data()) +
					                            bufferView.byteOffset,
					                        static_cast<int>(bufferView.byteLength),
					                        &task.width,
					                        &task.height,
					                        &task.channels,
					                        4);
					                    task.success = (task.decodedData != nullptr);
				                    }},
				                buffer.data);
			            }},
			        image.data);
		    }
	    },
	    static_cast<uint32_t>(asset.images.size()));
}

// ============================================================================
// Parallel Mesh Processing (CPU work only - includes tangent generation)
// ============================================================================

void AssetLoader::processMeshesParallel(
    fastgltf::Asset&                                   asset,
    std::vector<MeshProcessTask>&                      tasks,
    const std::vector<std::shared_ptr<GLTFMaterial>>& materials)
{
#ifdef TRACY_ENABLE
	ZoneScopedN("processMeshesParallel");
#endif

	tasks.resize(asset.meshes.size());

	// Initialize task metadata
	for (size_t i = 0; i < asset.meshes.size(); i++)
	{
		tasks[i].meshIndex = i;
		tasks[i].name      = asset.meshes[i].name.c_str();
	}

	// Process meshes in parallel (CPU-bound work)
	agni::ThreadPool::ParallelLoop(
	    [&](uint32_t start, uint32_t end)
	    {
		    for (uint32_t meshIdx = start; meshIdx < end; meshIdx++)
		    {
			    auto& task = tasks[meshIdx];
			    auto& mesh = asset.meshes[meshIdx];

			    // Clear vectors for this mesh
			    task.indices.clear();
			    task.vertices.clear();
			    task.surfaces.clear();

			    for (auto&& p : mesh.primitives)
			    {
				    GeoSurface newSurface;
				    newSurface.m_startIndex = static_cast<uint32_t>(task.indices.size());
				    newSurface.m_count =
				        static_cast<uint32_t>(asset.accessors[p.indicesAccessor.value()].count);

				    uint32_t initial_vtx = static_cast<uint32_t>(task.vertices.size());

				    // Load indices
				    {
					    fastgltf::Accessor& indexaccessor =
					        asset.accessors[p.indicesAccessor.value()];
					    task.indices.reserve(task.indices.size() + indexaccessor.count);

					    fastgltf::iterateAccessor<std::uint32_t>(
					        asset,
					        indexaccessor,
					        [&](std::uint32_t idx) { task.indices.push_back(idx + initial_vtx); });
				    }

				    // Load vertex positions
				    {
					    fastgltf::Accessor& posAccessor =
					        asset.accessors[p.findAttribute("POSITION")->accessorIndex];
					    task.vertices.resize(task.vertices.size() + posAccessor.count);

					    fastgltf::iterateAccessorWithIndex<glm::vec3>(
					        asset,
					        posAccessor,
					        [&](glm::vec3 v, size_t index)
					        {
						        Vertex newvtx;
						        newvtx.m_position              = v;
						        newvtx.m_normal                = {1, 0, 0};
						        newvtx.m_color                 = glm::vec4 {1.f};
						        newvtx.m_uv_x                  = 0;
						        newvtx.m_uv_y                  = 0;
						        newvtx.m_tangent               = {0, 0, 0, 0};
						        task.vertices[initial_vtx + index] = newvtx;
					        });
				    }

				    // Load vertex normals
				    auto normals = p.findAttribute("NORMAL");
				    if (normals != p.attributes.end())
				    {
					    fastgltf::iterateAccessorWithIndex<glm::vec3>(
					        asset,
					        asset.accessors[(*normals).accessorIndex],
					        [&](glm::vec3 v, size_t index)
					        { task.vertices[initial_vtx + index].m_normal = v; });
				    }

				    // Load UVs
				    auto uv = p.findAttribute("TEXCOORD_0");
				    if (uv != p.attributes.end())
				    {
					    fastgltf::iterateAccessorWithIndex<glm::vec2>(
					        asset,
					        asset.accessors[(*uv).accessorIndex],
					        [&](glm::vec2 v, size_t index)
					        {
						        task.vertices[initial_vtx + index].m_uv_x = v.x;
						        task.vertices[initial_vtx + index].m_uv_y = v.y;
					        });
				    }

				    // Load vertex colors
				    auto colors = p.findAttribute("COLOR_0");
				    if (colors != p.attributes.end())
				    {
					    fastgltf::iterateAccessorWithIndex<glm::vec4>(
					        asset,
					        asset.accessors[(*colors).accessorIndex],
					        [&](glm::vec4 v, size_t index)
					        { task.vertices[initial_vtx + index].m_color = v; });
				    }

				    // Load or generate tangents
				    auto tangents = p.findAttribute("TANGENT");
				    if (tangents != p.attributes.end())
				    {
					    // Use tangents from GLTF file
					    fastgltf::iterateAccessorWithIndex<glm::vec4>(
					        asset,
					        asset.accessors[(*tangents).accessorIndex],
					        [&](glm::vec4 v, size_t index)
					        { task.vertices[initial_vtx + index].m_tangent = v; });
				    }
				    else
				    {
					    // Generate tangents using MikkTSpace
					    std::vector<uint32_t> primitiveIndices;
					    primitiveIndices.reserve(newSurface.m_count);
					    for (uint32_t i = newSurface.m_startIndex;
					         i < newSurface.m_startIndex + newSurface.m_count;
					         i++)
					    {
						    primitiveIndices.push_back(task.indices[i]);
					    }

					    // Setup MikkTSpace interface
					    SMikkTSpaceInterface mikkInterface   = {};
					    mikkInterface.m_getNumFaces          = mikkGetNumFaces;
					    mikkInterface.m_getNumVerticesOfFace = mikkGetNumVerticesOfFace;
					    mikkInterface.m_getPosition          = mikkGetPosition;
					    mikkInterface.m_getNormal            = mikkGetNormal;
					    mikkInterface.m_getTexCoord          = mikkGetTexCoord;
					    mikkInterface.m_setTSpaceBasic       = mikkSetTSpaceBasic;

					    // Setup user data
					    MikkTSpaceUserData userData = {};
					    userData.vertices           = &task.vertices;
					    userData.indices            = &primitiveIndices;
					    userData.vertexOffset       = initial_vtx;

					    // Setup context
					    SMikkTSpaceContext mikkContext = {};
					    mikkContext.m_pInterface       = &mikkInterface;
					    mikkContext.m_pUserData        = &userData;

					    // Generate tangents
					    if (!genTangSpaceDefault(&mikkContext))
					    {
						    AGNI_PRINT("[Async] Warning: Failed to generate tangents for mesh: {}\n",
						              mesh.name);
					    }
				    }

				    // Assign material
				    if (p.materialIndex.has_value() && !materials.empty())
				    {
					    newSurface.m_material = materials[p.materialIndex.value()];
				    }
				    else if (!materials.empty())
				    {
					    newSurface.m_material = materials[0];
				    }
				    else
				    {
					    newSurface.m_material = m_defaultMaterial;
				    }

				    // Calculate bounds
				    glm::vec3 minpos = task.vertices[initial_vtx].m_position;
				    glm::vec3 maxpos = task.vertices[initial_vtx].m_position;
				    for (size_t i = initial_vtx; i < task.vertices.size(); i++)
				    {
					    minpos = glm::min(minpos, task.vertices[i].m_position);
					    maxpos = glm::max(maxpos, task.vertices[i].m_position);
				    }
				    newSurface.m_bounds.m_origin       = (maxpos + minpos) / 2.f;
				    newSurface.m_bounds.m_extents      = (maxpos - minpos) / 2.f;
				    newSurface.m_bounds.m_sphereRadius = glm::length(newSurface.m_bounds.m_extents);

				    task.surfaces.push_back(newSurface);
			    }

			    task.success = true;
		    }
	    },
	    static_cast<uint32_t>(asset.meshes.size()));
}

// ============================================================================
// GPU Upload Functions (must be called from main thread)
// ============================================================================

std::vector<AllocatedImage> AssetLoader::uploadImagesToGPU(
    const std::vector<ImageDecodeTask>& decodedImages,
    bool                                mipmapped)
{
#ifdef TRACY_ENABLE
	ZoneScopedN("uploadImagesToGPU");
#endif

	std::vector<AllocatedImage> results(decodedImages.size());

	for (size_t i = 0; i < decodedImages.size(); i++)
	{
		const auto& task = decodedImages[i];

		if (!task.success || task.decodedData == nullptr)
		{
			// Use error texture as fallback
			results[i] = m_errorCheckerboardTexture.image;
			continue;
		}

		VkExtent3D imageSize;
		imageSize.width  = static_cast<uint32_t>(task.width);
		imageSize.height = static_cast<uint32_t>(task.height);
		imageSize.depth  = 1;

		results[i] = m_resourceManager->createImage(task.decodedData,
		                                            imageSize,
		                                            VK_FORMAT_R8G8B8A8_UNORM,
		                                            VK_IMAGE_USAGE_SAMPLED_BIT,
		                                            mipmapped);

		// Free the decoded data
		stbi_image_free(task.decodedData);
	}

	return results;
}

std::vector<GPUMeshBuffers> AssetLoader::uploadMeshesToGPU(
    const std::vector<MeshProcessTask>& processedMeshes)
{
#ifdef TRACY_ENABLE
	ZoneScopedN("uploadMeshesToGPU");
#endif

	std::vector<GPUMeshBuffers> results(processedMeshes.size());

	for (size_t i = 0; i < processedMeshes.size(); i++)
	{
		const auto& task = processedMeshes[i];

		if (!task.success)
		{
			continue;
		}

		results[i] = m_resourceManager->uploadMesh(
		    std::span<uint32_t>(const_cast<uint32_t*>(task.indices.data()), task.indices.size()),
		    std::span<Vertex>(const_cast<Vertex*>(task.vertices.data()), task.vertices.size()));
	}

	return results;
}

// ============================================================================
// Async Loading API
// ============================================================================

std::shared_ptr<AsyncLoadHandle> AssetLoader::loadGltfAsync(
    AgniEngine*            engine,
    std::filesystem::path  filePath,
    LoadProgressCallback   progressCallback)
{
#ifdef TRACY_ENABLE
	ZoneScopedN("loadGltfAsync");
#endif

	auto handle      = std::make_shared<AsyncLoadHandle>();
	handle->filePath = filePath;
	handle->progress = 0.0f;
	handle->currentStage = "Starting...";

	// Spawn background task for CPU work
	agni::ThreadPool::AddTask([this, engine, filePath, handle, progressCallback]()
	{
#ifdef TRACY_ENABLE
		ZoneScopedN("AsyncGLTFLoad_CPUWork");
#endif

		handle->currentStage = "Parsing GLTF...";
		if (progressCallback) progressCallback(0.05f, "Parsing GLTF...");

		// Parse GLTF file
		fastgltf::Parser parser(fastgltf::Extensions::KHR_lights_punctual);

		constexpr auto gltfOptions =
		    fastgltf::Options::DontRequireValidAssetMember |
		    fastgltf::Options::AllowDouble |
		    fastgltf::Options::LoadExternalBuffers |
		    fastgltf::Options::LoadExternalImages;

		auto data = fastgltf::GltfDataBuffer::FromPath(filePath);
		if (data.error() != fastgltf::Error::None)
		{
			AGNI_PRINT("[Async] Failed to load glTF file: {}\n",
			          fastgltf::to_underlying(data.error()));
			handle->cpuWorkComplete  = true;
			handle->gpuUploadComplete = true;
			return;
		}

		fastgltf::Asset gltf;
		std::filesystem::path path = filePath;

		auto type = fastgltf::determineGltfFileType(data.get());
		if (type == fastgltf::GltfType::glTF)
		{
			auto load = parser.loadGltf(data.get(), path.parent_path(), gltfOptions);
			if (load)
			{
				gltf = std::move(load.get());
			}
			else
			{
				AGNI_PRINT("[Async] Failed to parse glTF: {}\n",
				          fastgltf::to_underlying(load.error()));
				handle->cpuWorkComplete  = true;
				handle->gpuUploadComplete = true;
				return;
			}
		}
		else if (type == fastgltf::GltfType::GLB)
		{
			auto load = parser.loadGltfBinary(data.get(), path.parent_path(), gltfOptions);
			if (load)
			{
				gltf = std::move(load.get());
			}
			else
			{
				AGNI_PRINT("[Async] Failed to parse glTF: {}\n",
				          fastgltf::to_underlying(load.error()));
				handle->cpuWorkComplete  = true;
				handle->gpuUploadComplete = true;
				return;
			}
		}
		else
		{
			AGNI_PRINT("[Async] Failed to determine glTF container\n");
			handle->cpuWorkComplete  = true;
			handle->gpuUploadComplete = true;
			return;
		}

		handle->progress = 0.1f;
		handle->currentStage = "Decoding images...";
		if (progressCallback) progressCallback(0.1f, "Decoding images...");

		// Map samplers
		std::vector<uint32_t> samplerIndexMapping;
		for (fastgltf::Sampler& sampler : gltf.samplers)
		{
			VkFilter magFilter = extractFilter(sampler.magFilter.value_or(fastgltf::Filter::Linear));
			VkFilter minFilter = extractFilter(sampler.minFilter.value_or(fastgltf::Filter::Linear));
			VkSamplerMipmapMode mipmapMode =
			    extractMipmapMode(sampler.minFilter.value_or(fastgltf::Filter::Linear));

			uint32_t samplerIndex;
			if (magFilter == VK_FILTER_LINEAR && minFilter == VK_FILTER_LINEAR)
			{
				samplerIndex = (mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR)
				                   ? static_cast<uint32_t>(BindlessSamplerType::LinearMipmap)
				                   : static_cast<uint32_t>(BindlessSamplerType::Linear);
			}
			else
			{
				samplerIndex = (mipmapMode == VK_SAMPLER_MIPMAP_MODE_LINEAR)
				                   ? static_cast<uint32_t>(BindlessSamplerType::NearestMipmap)
				                   : static_cast<uint32_t>(BindlessSamplerType::Nearest);
			}
			samplerIndexMapping.push_back(samplerIndex);
		}

		// Decode images in parallel (CPU work)
		std::vector<ImageDecodeTask> decodedImages;
		decodeImagesParallel(gltf, decodedImages);

		handle->progress = 0.5f;
		handle->currentStage = "Processing meshes...";
		if (progressCallback) progressCallback(0.5f, "Processing meshes...");

		// Note: Materials will be created on main thread during GPU finalization
		// For now, pass empty materials - they'll be set up during finalization
		std::vector<std::shared_ptr<GLTFMaterial>> tempMaterials;

		// Process meshes in parallel (CPU work)
		std::vector<MeshProcessTask> processedMeshes;
		processMeshesParallel(gltf, processedMeshes, tempMaterials);

		handle->progress = 0.8f;
		handle->currentStage = "Waiting for GPU upload...";
		if (progressCallback) progressCallback(0.8f, "Waiting for GPU upload...");

		// Queue for GPU finalization on main thread
		{
			std::lock_guard<std::mutex> lock(m_pendingMutex);
			m_pendingUploads.push_back({
			    handle,
			    engine,
			    std::move(gltf),
			    std::move(decodedImages),
			    std::move(processedMeshes),
			    std::move(samplerIndexMapping),
			    progressCallback
			});
		}

		handle->cpuWorkComplete = true;
	});

	return handle;
}

void AssetLoader::processCompletedLoads()
{
	std::vector<PendingGPUUpload> toProcess;

	// Quickly grab pending uploads with lock
	{
		std::lock_guard<std::mutex> lock(m_pendingMutex);
		if (m_pendingUploads.empty())
		{
			return;
		}

		// Move all pending uploads to local vector
		toProcess = std::move(m_pendingUploads);
		m_pendingUploads.clear();
	}

	// Process each pending upload on main thread
	for (auto& pending : toProcess)
	{
		if (pending.handle->cancelled)
		{
			// Clean up decoded image data
			for (auto& img : pending.decodedImages)
			{
				if (img.decodedData)
				{
					stbi_image_free(img.decodedData);
				}
			}
			pending.handle->gpuUploadComplete = true;
			continue;
		}

		finalizePendingLoad(pending);
	}
}

void AssetLoader::finalizePendingLoad(PendingGPUUpload& pending)
{
#ifdef TRACY_ENABLE
	ZoneScopedN("finalizePendingLoad");
#endif

	pending.handle->currentStage = "Uploading to GPU...";
	if (pending.progressCallback) pending.progressCallback(0.85f, "Uploading to GPU...");

	auto scene      = std::make_shared<LoadedGLTF>();
	scene->m_creator = pending.engine;
	LoadedGLTF& file = *scene.get();

	fastgltf::Asset& gltf = pending.gltfAsset;

	// Upload images to GPU
	auto gpuImages = uploadImagesToGPU(pending.decodedImages, true);

	// Store images in LoadedGLTF
	for (size_t i = 0; i < pending.decodedImages.size(); i++)
	{
		file.m_images[pending.decodedImages[i].name] = gpuImages[i];
	}

	// Get references to bindless registries
	TextureRegistry&  textureRegistry  = pending.engine->m_renderer.getTextureRegistry();
	MaterialRegistry& materialRegistry = pending.engine->m_renderer.getMaterialRegistry();

	// Create materials (needs GPU images to be uploaded first)
	std::vector<std::shared_ptr<GLTFMaterial>> materials;
	for (fastgltf::Material& mat : gltf.materials)
	{
		auto newMat = std::make_shared<GLTFMaterial>();
		materials.push_back(newMat);
		file.materials[mat.name.c_str()] = newMat;

		// Determine pass type
		MaterialPass passType = MaterialPass::MainColor;
		if (mat.alphaMode == fastgltf::AlphaMode::Blend)
		{
			passType = MaterialPass::Transparent;
		}

		// Set pipeline
		if (passType == MaterialPass::Transparent)
		{
			newMat->m_data.m_pipeline = &m_metalRoughMaterial.getTransparentPipeline();
		}
		else
		{
			newMat->m_data.m_pipeline = &m_metalRoughMaterial.getOpaquePipeline();
		}
		newMat->m_data.m_passType = passType;

		// Build GPUMaterialData
		GPUMaterialData matData {};
		matData.colorFactors.x = mat.pbrData.baseColorFactor[0];
		matData.colorFactors.y = mat.pbrData.baseColorFactor[1];
		matData.colorFactors.z = mat.pbrData.baseColorFactor[2];
		matData.colorFactors.w = mat.pbrData.baseColorFactor[3];

		matData.metalRoughFactors.x = mat.pbrData.metallicFactor;
		matData.metalRoughFactors.y = mat.pbrData.roughnessFactor;

		// Default texture indices
		matData.colorTexIndex      = textureRegistry.whiteTextureIndex;
		matData.metalRoughTexIndex = textureRegistry.whiteTextureIndex;
		matData.normalTexIndex     = textureRegistry.defaultNormalIndex;
		matData.aoTexIndex         = textureRegistry.whiteTextureIndex;
		matData.samplerIndex       = static_cast<uint32_t>(BindlessSamplerType::LinearMipmap);

		// Register textures
		if (mat.pbrData.baseColorTexture.has_value())
		{
			size_t texIdx = mat.pbrData.baseColorTexture.value().textureIndex;
			size_t img    = gltf.textures[texIdx].imageIndex.value();
			size_t samp   = gltf.textures[texIdx].samplerIndex.value();

			std::lock_guard<std::mutex> lock(m_registryMutex);
			matData.colorTexIndex = textureRegistry.registerTexture(gpuImages[img].m_imageView);
			matData.samplerIndex  = pending.samplerIndexMapping[samp];
		}
		if (mat.pbrData.metallicRoughnessTexture.has_value())
		{
			size_t texIdx = mat.pbrData.metallicRoughnessTexture.value().textureIndex;
			size_t img    = gltf.textures[texIdx].imageIndex.value();

			std::lock_guard<std::mutex> lock(m_registryMutex);
			matData.metalRoughTexIndex = textureRegistry.registerTexture(gpuImages[img].m_imageView);
		}
		if (mat.normalTexture.has_value())
		{
			size_t texIdx = mat.normalTexture.value().textureIndex;
			size_t img    = gltf.textures[texIdx].imageIndex.value();

			std::lock_guard<std::mutex> lock(m_registryMutex);
			matData.normalTexIndex = textureRegistry.registerTexture(gpuImages[img].m_imageView);
		}
		if (mat.occlusionTexture.has_value())
		{
			size_t texIdx = mat.occlusionTexture.value().textureIndex;
			size_t img    = gltf.textures[texIdx].imageIndex.value();

			std::lock_guard<std::mutex> lock(m_registryMutex);
			matData.aoTexIndex = textureRegistry.registerTexture(gpuImages[img].m_imageView);
		}

		// Register material
		{
			std::lock_guard<std::mutex> lock(m_registryMutex);
			newMat->m_data.m_materialIndex = materialRegistry.registerMaterial(matData);
		}
	}

	// Now update mesh tasks with correct materials and upload to GPU
	for (size_t i = 0; i < pending.processedMeshes.size(); i++)
	{
		auto& task = pending.processedMeshes[i];
		auto& mesh = gltf.meshes[i];

		// Update surface materials
		size_t surfaceIdx = 0;
		for (auto&& p : mesh.primitives)
		{
			if (surfaceIdx < task.surfaces.size())
			{
				if (p.materialIndex.has_value() && !materials.empty())
				{
					task.surfaces[surfaceIdx].m_material = materials[p.materialIndex.value()];
				}
				else if (!materials.empty())
				{
					task.surfaces[surfaceIdx].m_material = materials[0];
				}
				else
				{
					task.surfaces[surfaceIdx].m_material = m_defaultMaterial;
				}
			}
			surfaceIdx++;
		}
	}

	// Upload meshes to GPU
	auto gpuMeshBuffers = uploadMeshesToGPU(pending.processedMeshes);

	// Create MeshAssets
	std::vector<std::shared_ptr<MeshAsset>> meshes;
	for (size_t i = 0; i < pending.processedMeshes.size(); i++)
	{
		auto& task   = pending.processedMeshes[i];
		auto newmesh = std::make_shared<MeshAsset>();
		meshes.push_back(newmesh);
		file.meshes[task.name] = newmesh;
		newmesh->m_name         = task.name;
		newmesh->m_surfaces     = std::move(task.surfaces);
		newmesh->m_meshBuffers  = gpuMeshBuffers[i];
	}

	pending.handle->currentStage = "Building scene graph...";
	if (pending.progressCallback) pending.progressCallback(0.95f, "Building scene graph...");

	// Build node hierarchy
	std::vector<std::shared_ptr<Node>> nodes;
	for (fastgltf::Node& node : gltf.nodes)
	{
		std::shared_ptr<Node> newNode;

		// Check for light first
		if (node.lightIndex.has_value())
		{
			auto lightNode = std::make_shared<LightNode>();

			const fastgltf::Light& gltfLight = gltf.lights[*node.lightIndex];

			lightNode->setColor(glm::vec3(
			    static_cast<float>(gltfLight.color[0]),
			    static_cast<float>(gltfLight.color[1]),
			    static_cast<float>(gltfLight.color[2])));
			lightNode->setIntensity(static_cast<float>(gltfLight.intensity));

			float radius = gltfLight.range.has_value()
			    ? static_cast<float>(*gltfLight.range)
			    : 10.0f;

			switch (gltfLight.type)
			{
				case fastgltf::LightType::Point:
					lightNode->setType(LightType::Point);
					lightNode->setRadius(radius);
					break;
				case fastgltf::LightType::Spot:
					lightNode->setType(LightType::Spot);
					lightNode->setRadius(radius);
					lightNode->setDirection(glm::vec3(0.0f, 0.0f, -1.0f));
					lightNode->setConeAngles(
					    glm::degrees(static_cast<float>(gltfLight.innerConeAngle.value_or(0.0f))),
					    glm::degrees(static_cast<float>(gltfLight.outerConeAngle.value_or(glm::radians(45.0f)))));
					break;
				case fastgltf::LightType::Directional:
					lightNode->setType(LightType::Directional);
					lightNode->setDirection(glm::vec3(0.0f, 0.0f, -1.0f));
					break;
			}

			if (node.meshIndex.has_value())
			{
				lightNode->setMesh(meshes[*node.meshIndex]);
			}

			newNode = lightNode;
		}
		else if (node.meshIndex.has_value())
		{
			newNode = std::make_shared<MeshNode>();
			static_cast<MeshNode*>(newNode.get())->getMesh() = meshes[*node.meshIndex];
		}
		else
		{
			newNode = std::make_shared<Node>();
		}

		nodes.push_back(newNode);
		file.nodes[node.name.c_str()] = newNode;

		// Set transform
		std::visit(
		    fastgltf::visitor {
		        [&](fastgltf::math::fmat4x4 matrix)
		        { memcpy(&newNode->getLocalTransform(), matrix.data(), sizeof(matrix)); },
		        [&](fastgltf::TRS transform)
		        {
			        glm::vec3 tl(transform.translation[0], transform.translation[1], transform.translation[2]);
			        glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]);
			        glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

			        glm::mat4 tm = glm::translate(glm::mat4(1.f), tl);
			        glm::mat4 rm = glm::toMat4(rot);
			        glm::mat4 sm = glm::scale(glm::mat4(1.f), sc);

			        newNode->getLocalTransform() = tm * rm * sm;
		        }},
		    node.transform);
	}

	// Setup parent-child relationships
	for (size_t i = 0; i < gltf.nodes.size(); i++)
	{
		fastgltf::Node&        node      = gltf.nodes[i];
		std::shared_ptr<Node>& sceneNode = nodes[i];

		for (auto& c : node.children)
		{
			sceneNode->getChildren().push_back(nodes[c]);
			nodes[c]->getParent() = sceneNode;
		}
	}

	// Find top nodes and refresh transforms
	for (auto& node : nodes)
	{
		if (node->getParent().lock() == nullptr)
		{
			file.m_topNodes.push_back(node);
			node->refreshTransform(glm::mat4 {1.f});
		}
	}

	pending.handle->progress = 1.0f;
	pending.handle->currentStage = "Complete";
	if (pending.progressCallback) pending.progressCallback(1.0f, "Complete");

	pending.handle->result           = scene;
	pending.handle->gpuUploadComplete = true;

	AGNI_PRINT("[Async] Loaded GLTF: {}\n", pending.handle->filePath.string());
}
