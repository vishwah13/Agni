#include <iostream>
#include <variant>

#include <stb_image.h>

#include <AgniEngine.hpp>
#include <Initializers.hpp>
#include <Loader.hpp>
#include <Types.hpp>


#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

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

std::optional<AllocatedImage> loadImage(AgniEngine*      engine,
                                        fastgltf::Asset& asset,
                                        fastgltf::Image& image,
                                        bool             mipmapped = false)
{
	AllocatedImage newImage {};

	int width, height, nrChannels;

	std::visit(
	fastgltf::visitor {
	[](auto& arg) {},
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

			newImage = engine->createImage(data,
			                               imagesize,
			                               VK_FORMAT_R8G8B8A8_UNORM,
			                               VK_IMAGE_USAGE_SAMPLED_BIT,
			                               mipmapped);

			stbi_image_free(data);
		}
		else
		{
			fmt::print("Failed to load image: {} - Reason: {}\n",
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

			newImage = engine->createImage(data,
			                               imagesize,
			                               VK_FORMAT_R8G8B8A8_UNORM,
			                               VK_IMAGE_USAGE_SAMPLED_BIT,
			                               mipmapped);

			stbi_image_free(data);
		}
		else
		{
			fmt::print("Failed to load image from memory: {}\n",
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
		[](auto& arg) {},
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

				newImage = engine->createImage(data,
				                               imagesize,
				                               VK_FORMAT_R8G8B8A8_UNORM,
				                               VK_IMAGE_USAGE_SAMPLED_BIT,
				                               mipmapped);

				stbi_image_free(data);
			}
			else
			{
				fmt::print("Failed to load image from buffer: {}\n",
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

				newImage = engine->createImage(data,
				                               imagesize,
				                               VK_FORMAT_R8G8B8A8_UNORM,
				                               VK_IMAGE_USAGE_SAMPLED_BIT,
				                               mipmapped);

				stbi_image_free(data);
			}
			else
			{
				fmt::print("Failed to load image from buffer: {}\n",
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
loadGltf(AgniEngine* engine, std::filesystem::path filePath)
{
	fmt::print("Loading GLTF: {}\n", filePath.string());

	std::shared_ptr<LoadedGLTF> scene = std::make_shared<LoadedGLTF>();
	scene->m_creator                  = engine;
	LoadedGLTF& file                  = *scene.get();

	fastgltf::Parser parser {};

	constexpr auto gltfOptions =
	fastgltf::Options::DontRequireValidAssetMember |
	fastgltf::Options::AllowDouble | fastgltf::Options::LoadGLBBuffers |
	fastgltf::Options::LoadExternalBuffers;
	fastgltf::Options::LoadExternalImages;

	auto data = fastgltf::GltfDataBuffer::FromPath(filePath);

	if (data.error() != fastgltf::Error::None)
	{
		fmt::print("Failed to load glTF file: {} \n",
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
			fmt::print("Failed to parse glTF: {} \n",
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
			fmt::print("Failed to parse glTF: {} \n",
			           fastgltf::to_underlying(load.error()));
			return {};
		}
	}
	else
	{
		fmt::print("Failed to determine glTF container \n");
		return {};
	}

	// we can stimate the descriptors we will need accurately
	std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
	{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3},
	{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
	{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};

	file.m_descriptorPool.init(engine->m_device, gltf.materials.size(), sizes);

	// load samplers
	for (fastgltf::Sampler& sampler : gltf.samplers)
	{

		VkSamplerCreateInfo sampl = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = nullptr};
		sampl.maxLod = VK_LOD_CLAMP_NONE;
		sampl.minLod = 0;

		sampl.magFilter =
		extractFilter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
		sampl.minFilter =
		extractFilter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

		sampl.mipmapMode = extractMipmapMode(
		sampler.minFilter.value_or(fastgltf::Filter::Nearest));

		VkSampler newSampler;
		vkCreateSampler(engine->m_device, &sampl, nullptr, &newSampler);

		file.m_samplers.push_back(newSampler);
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
		loadImage(engine, gltf, image, true);

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
			images.push_back(engine->m_errorCheckerboardImage);
			std::cout << "gltf failed to load texture " << image.name
			          << std::endl;
		}
		imageIndex++;
	}

	// create buffer to hold the material data
	file.m_materialDataBuffer = engine->createBuffer(
	sizeof(GltfPbrMaterial::MaterialConstants) * gltf.materials.size(),
	VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
	VMA_MEMORY_USAGE_CPU_TO_GPU);
	int                                 dataIndex = 0;
	GltfPbrMaterial::MaterialConstants* sceneMaterialConstants =
	(GltfPbrMaterial::MaterialConstants*)
	file.m_materialDataBuffer.m_info.pMappedData;

	for (fastgltf::Material& mat : gltf.materials)
	{
		std::shared_ptr<GLTFMaterial> newMat = std::make_shared<GLTFMaterial>();
		materials.push_back(newMat);
		file.materials[mat.name.c_str()] = newMat;

		GltfPbrMaterial::MaterialConstants constants;
		constants.m_colorFactors.x = mat.pbrData.baseColorFactor[0];
		constants.m_colorFactors.y = mat.pbrData.baseColorFactor[1];
		constants.m_colorFactors.z = mat.pbrData.baseColorFactor[2];
		constants.m_colorFactors.w = mat.pbrData.baseColorFactor[3];

		constants.m_metal_rough_factors.x = mat.pbrData.metallicFactor;
		constants.m_metal_rough_factors.y = mat.pbrData.roughnessFactor;
		// write material parameters to buffer
		sceneMaterialConstants[dataIndex] = constants;

		MaterialPass passType = MaterialPass::MainColor;
		if (mat.alphaMode == fastgltf::AlphaMode::Blend)
		{
			passType = MaterialPass::Transparent;
		}

		GltfPbrMaterial::MaterialResources materialResources;
		// default the material textures
		materialResources.m_colorImage        = engine->m_whiteImage;
		materialResources.m_colorSampler      = engine->m_defaultSamplerLinear;
		materialResources.m_metalRoughImage   = engine->m_whiteImage;
		materialResources.m_metalRoughSampler = engine->m_defaultSamplerLinear;
		materialResources.m_normalImage       = engine->m_whiteImage;
		materialResources.m_normalSampler     = engine->m_defaultSamplerLinear;
		materialResources.m_aoImage           = engine->m_whiteImage;
		materialResources.m_aoSampler         = engine->m_defaultSamplerLinear;

		// set the uniform buffer for the material data
		materialResources.m_dataBuffer = file.m_materialDataBuffer.m_buffer;
		materialResources.m_dataBufferOffset =
		dataIndex * sizeof(GltfPbrMaterial::MaterialConstants);
		// grab textures from gltf file
		if (mat.pbrData.baseColorTexture.has_value())
		{
			size_t img =
			gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex]
			.imageIndex.value();
			size_t sampler =
			gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex]
			.samplerIndex.value();

			materialResources.m_colorImage   = images[img];
			materialResources.m_colorSampler = file.m_samplers[sampler];
		}
		if (mat.pbrData.metallicRoughnessTexture.has_value())
		{
			size_t img =
			gltf
			.textures[mat.pbrData.metallicRoughnessTexture.value().textureIndex]
			.imageIndex.value();
			size_t sampler =
			gltf
			.textures[mat.pbrData.metallicRoughnessTexture.value().textureIndex]
			.samplerIndex.value();

			materialResources.m_metalRoughImage   = images[img];
			materialResources.m_metalRoughSampler = file.m_samplers[sampler];
		}
		if (mat.normalTexture.has_value())
		{
			size_t img = gltf.textures[mat.normalTexture.value().textureIndex]
			             .imageIndex.value();
			size_t sampler =
			gltf.textures[mat.normalTexture.value().textureIndex]
			.samplerIndex.value();

			materialResources.m_normalImage   = images[img];
			materialResources.m_normalSampler = file.m_samplers[sampler];
		}
		if (mat.occlusionTexture.has_value())
		{
			size_t img =
			gltf.textures[mat.occlusionTexture.value().textureIndex]
			.imageIndex.value();
			size_t sampler =
			gltf.textures[mat.occlusionTexture.value().textureIndex]
			.samplerIndex.value();

			materialResources.m_aoImage   = images[img];
			materialResources.m_aoSampler = file.m_samplers[sampler];
		}
		// build material
		newMat->m_data = engine->m_metalRoughMaterial.writeMaterial(
		engine->m_device, passType, materialResources, file.m_descriptorPool);

		dataIndex++;
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

			size_t initial_vtx = vertices.size();

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
					fmt::print(
					"Warning: Failed to generate tangents for mesh: {}\n",
					mesh.name);
				}
			}

			if (p.materialIndex.has_value())
			{
				newSurface.m_material = materials[p.materialIndex.value()];
			}
			else
			{
				newSurface.m_material = materials[0];
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

		newmesh->m_meshBuffers = engine->uploadMesh(indices, vertices);
	}

	// load all nodes and their meshes
	for (fastgltf::Node& node : gltf.nodes)
	{
		std::shared_ptr<Node> newNode;

		// find if the node has a mesh, and if it does hook it to the mesh
		// pointer and allocate it with the meshnode class
		if (node.meshIndex.has_value())
		{
			newNode = std::make_shared<MeshNode>();
			static_cast<MeshNode*>(newNode.get())->getMesh() =
			meshes[*node.meshIndex];
		}
		else
		{
			newNode = std::make_shared<Node>();
		}

		nodes.push_back(newNode);
		file.nodes[node.name.c_str()];

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

void LoadedGLTF::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
{
	// create renderables from the scenenodes
	for (auto& n : m_topNodes)
	{
		n->Draw(topMatrix, ctx);
	}
}

void LoadedGLTF::clearAll()
{
	VkDevice dv = m_creator->m_device;

	m_descriptorPool.destroyPools(dv);
	m_creator->destroyBuffer(m_materialDataBuffer);

	for (auto& [k, v] : meshes)
	{

		m_creator->destroyBuffer(v->m_meshBuffers.m_indexBuffer);
		m_creator->destroyBuffer(v->m_meshBuffers.m_vertexBuffer);
	}

	for (auto& [k, v] : m_images)
	{

		if (v.m_image == m_creator->m_errorCheckerboardImage.m_image)
		{
			// dont destroy the default images
			continue;
		}
		m_creator->destroyImage(v);
	}


	for (auto& sampler : m_samplers)
	{
		vkDestroySampler(dv, sampler, nullptr);
	}
}
