#include "SceneSerializer.hpp"

#include <AgniEngine.hpp>
#include <Components.hpp>
#include <Debug.hpp>
#include <ECS/World.hpp>
#include <Loader.hpp>
#include <Reflection/ComponentRegistry.hpp>

#include <fmt/format.h>
#include <glm/gtc/type_ptr.hpp>
#include <simdjson.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace agni::scene
{

	// Helper to escape JSON strings
	static std::string escapeJsonString(const std::string& s)
	{
		std::string result;
		result.reserve(s.size());
		for (char c : s)
		{
			switch (c)
			{
				case '"':
					result += "\\\"";
					break;
				case '\\':
					result += "\\\\";
					break;
				case '\b':
					result += "\\b";
					break;
				case '\f':
					result += "\\f";
					break;
				case '\n':
					result += "\\n";
					break;
				case '\r':
					result += "\\r";
					break;
				case '\t':
					result += "\\t";
					break;
				default:
					result += c;
			}
		}
		return result;
	}

	// Helper to format mat4 as JSON array
	static std::string mat4ToJson(const glm::mat4& m)
	{
		const float* p = glm::value_ptr(m);
		return fmt::format("[{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}]",
		                   p[0],
		                   p[1],
		                   p[2],
		                   p[3],
		                   p[4],
		                   p[5],
		                   p[6],
		                   p[7],
		                   p[8],
		                   p[9],
		                   p[10],
		                   p[11],
		                   p[12],
		                   p[13],
		                   p[14],
		                   p[15]);
	}

	// Helper to format vec3 as JSON array
	static std::string vec3ToJson(const glm::vec3& v)
	{
		return fmt::format("[{},{},{}]", v.x, v.y, v.z);
	}

	// Helper to parse mat4 from JSON array
	static glm::mat4 jsonToMat4(simdjson::ondemand::array arr)
	{
		glm::mat4 m(1.0f);
		float*    p = glm::value_ptr(m);
		size_t    i = 0;
		for (auto val : arr)
		{
			if (i < 16)
			{
				p[i++] = static_cast<float>(val.get_double().value());
			}
		}
		return m;
	}

	// Helper to parse vec3 from JSON array
	static glm::vec3 jsonToVec3(simdjson::ondemand::array arr)
	{
		glm::vec3 v(0.0f);
		size_t    i = 0;
		for (auto val : arr)
		{
			if (i == 0)
				v.x = static_cast<float>(val.get_double().value());
			else if (i == 1)
				v.y = static_cast<float>(val.get_double().value());
			else if (i == 2)
				v.z = static_cast<float>(val.get_double().value());
			i++;
		}
		return v;
	}

	// ================================================================
	// Reflection-based serialization helpers
	// ================================================================

	static std::string serializeReflectedComponent(
	    const agni::ComponentDescriptor& desc,
	    const void*                      data,
	    const std::string&               indent)
	{
		std::string json;
		bool        firstField = true;

		for (const auto& prop : desc.properties)
		{
			if (prop.noSerialize) continue;

			const void* fieldPtr = static_cast<const char*>(data) + prop.offset;

			if (!firstField) json += ",\n";
			firstField = false;

			json += indent + "  ";

			switch (prop.type)
			{
			case agni::PropertyType::Float:
				json += fmt::format("\"{}\": {}", prop.name, *static_cast<const float*>(fieldPtr));
				break;
			case agni::PropertyType::Int:
				json += fmt::format("\"{}\": {}", prop.name, *static_cast<const int*>(fieldPtr));
				break;
			case agni::PropertyType::UInt32:
				json += fmt::format("\"{}\": {}", prop.name, *static_cast<const uint32_t*>(fieldPtr));
				break;
			case agni::PropertyType::Bool:
				json += fmt::format("\"{}\": {}", prop.name,
				                    *static_cast<const bool*>(fieldPtr) ? "true" : "false");
				break;
			case agni::PropertyType::String:
				json += fmt::format("\"{}\": \"{}\"", prop.name,
				                    escapeJsonString(*static_cast<const std::string*>(fieldPtr)));
				break;
			case agni::PropertyType::Vec3:
			case agni::PropertyType::Color3:
				json += fmt::format("\"{}\": {}", prop.name,
				                    vec3ToJson(*static_cast<const glm::vec3*>(fieldPtr)));
				break;
			case agni::PropertyType::Vec4:
			case agni::PropertyType::Color4:
			{
				const auto* v = static_cast<const glm::vec4*>(fieldPtr);
				json += fmt::format("\"{}\": [{},{},{},{}]", prop.name, v->x, v->y, v->z, v->w);
				break;
			}
			case agni::PropertyType::Mat4:
				json += fmt::format("\"{}\": {}", prop.name,
				                    mat4ToJson(*static_cast<const glm::mat4*>(fieldPtr)));
				break;
			case agni::PropertyType::Enum:
			{
				if (prop.enumDesc)
				{
					int val = 0;
					std::memcpy(&val, fieldPtr, std::min(prop.size, sizeof(int)));
					const char* enumName = prop.enumDesc->nameFromValue(val);
					json += fmt::format("\"{}\": \"{}\"", prop.name, enumName ? enumName : "Unknown");
				}
				break;
			}
			case agni::PropertyType::EntityID:
				json += fmt::format("\"{}\": {}", prop.name, *static_cast<const uint64_t*>(fieldPtr));
				break;
			default:
				break;
			}
		}

		return json;
	}

	static void deserializeReflectedComponent(
	    const agni::ComponentDescriptor& desc,
	    void*                            data,
	    simdjson::ondemand::object&      obj)
	{
		for (const auto& prop : desc.properties)
		{
			if (prop.noSerialize) continue;

			void* fieldPtr = static_cast<char*>(data) + prop.offset;

			auto fieldResult = obj.find_field_unordered(prop.name);
			if (fieldResult.error()) continue;

			auto field = fieldResult.value();

			switch (prop.type)
			{
			case agni::PropertyType::Float:
			{
				auto val = field.get_double();
				if (!val.error()) *static_cast<float*>(fieldPtr) = static_cast<float>(val.value());
				break;
			}
			case agni::PropertyType::Int:
			{
				auto val = field.get_int64();
				if (!val.error()) *static_cast<int*>(fieldPtr) = static_cast<int>(val.value());
				break;
			}
			case agni::PropertyType::UInt32:
			{
				auto val = field.get_uint64();
				if (!val.error()) *static_cast<uint32_t*>(fieldPtr) = static_cast<uint32_t>(val.value());
				break;
			}
			case agni::PropertyType::Bool:
			{
				auto val = field.get_bool();
				if (!val.error()) *static_cast<bool*>(fieldPtr) = val.value();
				break;
			}
			case agni::PropertyType::String:
			{
				auto val = field.get_string();
				if (!val.error()) *static_cast<std::string*>(fieldPtr) = std::string(val.value());
				break;
			}
			case agni::PropertyType::Vec3:
			case agni::PropertyType::Color3:
			{
				auto arr = field.get_array();
				if (!arr.error()) *static_cast<glm::vec3*>(fieldPtr) = jsonToVec3(arr.value());
				break;
			}
			case agni::PropertyType::Vec4:
			case agni::PropertyType::Color4:
			{
				auto arr = field.get_array();
				if (!arr.error())
				{
					auto*  v = static_cast<glm::vec4*>(fieldPtr);
					size_t i = 0;
					for (auto val : arr.value())
					{
						if (i < 4)
						{
							float f = static_cast<float>(val.get_double().value());
							(*v)[static_cast<int>(i)] = f;
						}
						i++;
					}
				}
				break;
			}
			case agni::PropertyType::Mat4:
			{
				auto arr = field.get_array();
				if (!arr.error()) *static_cast<glm::mat4*>(fieldPtr) = jsonToMat4(arr.value());
				break;
			}
			case agni::PropertyType::Enum:
			{
				if (prop.enumDesc)
				{
					auto val = field.get_string();
					if (!val.error())
					{
						int64_t enumVal = 0;
						if (prop.enumDesc->valueFromName(
						        std::string(val.value()).c_str(), enumVal))
						{
							std::memcpy(fieldPtr, &enumVal, std::min(prop.size, sizeof(int)));
						}
					}
				}
				break;
			}
			case agni::PropertyType::EntityID:
			{
				auto val = field.get_uint64();
				if (!val.error()) *static_cast<uint64_t*>(fieldPtr) = val.value();
				break;
			}
			default:
				break;
			}
		}
	}

	SceneSerializer::SceneSerializer(AgniEngine& engine) : m_engine(engine) {}

	bool SceneSerializer::saveScene(const std::filesystem::path& filePath,
	                                const SceneSaveOptions&      options)
	{
		std::string json = serializeScene(options);

		std::ofstream file(filePath);
		if (!file.is_open())
		{
			m_lastError = fmt::format("Failed to open file for writing: {}",
			                          filePath.string());
			return false;
		}

		file << json;
		file.close();

		m_currentScenePath  = filePath;
		m_hasUnsavedChanges = false;

		return true;
	}

	bool SceneSerializer::loadScene(const std::filesystem::path& filePath,
	                                const SceneLoadOptions&      options)
	{
		std::ifstream file(filePath);
		if (!file.is_open())
		{
			m_lastError = fmt::format("Failed to open file for reading: {}",
			                          filePath.string());
			return false;
		}

		std::stringstream buffer;
		buffer << file.rdbuf();
		std::string json = buffer.str();
		file.close();

		if (!deserializeScene(json, options))
		{
			return false;
		}

		m_currentScenePath  = filePath;
		m_hasUnsavedChanges = false;

		return true;
	}

	std::string SceneSerializer::serializeScene(const SceneSaveOptions& options)
	{
		auto&       world   = m_engine.getECSWorld();
		std::string indent  = options.prettyPrint ? "  " : "";
		std::string newline = options.prettyPrint ? "\n" : "";
		std::string indent2 = options.prettyPrint ? "    " : "";
		std::string indent3 = options.prettyPrint ? "      " : "";
		std::string indent4 = options.prettyPrint ? "        " : "";

		std::string json = "{" + newline;

		// Version
		json += indent + "\"version\": \"1.0\"," + newline;

		// Metadata
		if (options.includeMetadata)
		{
			auto    now   = std::chrono::system_clock::now();
			auto    timeT = std::chrono::system_clock::to_time_t(now);
			std::tm tm {};
	#ifdef _MSC_VER
		localtime_s(&tm, &timeT);
#else
		localtime_r(&timeT, &tm);
#endif
			std::string timestamp =
			fmt::format("{:04d}-{:02d}-{:02d}T{:02d}:{:02d}:{:02d}",
			            tm.tm_year + 1900,
			            tm.tm_mon + 1,
			            tm.tm_mday,
			            tm.tm_hour,
			            tm.tm_min,
			            tm.tm_sec);

			json += indent + "\"metadata\": {" + newline;
			json += indent2 + "\"savedAt\": \"" + timestamp + "\"," + newline;
			json += indent2 + "\"engine\": \"Agni\"" + newline;
			json += indent + "}," + newline;
		}

		// Entities
		json += indent + "\"entities\": [" + newline;

		bool                  firstEntity = true;
		std::vector<EntityID> entityIds;

		// Collect all entities with TransformComponent (our scene entities)
		world.get().query<const TransformComponent>().each(
		[&](flecs::entity e, const TransformComponent&)
		{ entityIds.push_back(e.id()); });
		AGNI_PRINT("[SceneSerializer] Saving {} entities\n", entityIds.size());

		for (EntityID entityId : entityIds)
		{
			auto e = world.get().entity(entityId);

			if (!firstEntity)
			{
				json += "," + newline;
			}
			firstEntity = false;

			json += indent2 + "{" + newline;

			// Entity name
			const char* name = e.name().c_str();
			json += indent3 +
			        fmt::format("\"name\": \"{}\",",
			                    escapeJsonString(name ? name : "")) +
			        newline;

			// Entity ID (for hierarchy references)
			json += indent3 + fmt::format("\"id\": {},", entityId) + newline;

			// Components
			json += indent3 + "\"components\": {" + newline;

			bool firstComponent = true;

			// TransformComponent
			if (const auto* tc = e.try_get<TransformComponent>())
			{
				if (!firstComponent)
					json += "," + newline;
				firstComponent = false;

				json += indent4 + "\"TransformComponent\": {" + newline;
				json += indent4 + "  \"localTransform\": " +
				        mat4ToJson(tc->localTransform) + newline;
				json += indent4 + "}";
			}

			// SceneNodeComponent (hierarchy)
			if (const auto* snc = e.try_get<agni::ecs::SceneNodeComponent>())
			{
				if (!firstComponent)
					json += "," + newline;
				firstComponent = false;

				json += indent4 + "\"SceneNodeComponent\": {" + newline;
				json += indent4 +
				        fmt::format("  \"parent\": {},", snc->parent) + newline;
				json += indent4 + "  \"children\": [";
				for (size_t i = 0; i < snc->children.size(); i++)
				{
					if (i > 0)
						json += ", ";
					json += std::to_string(snc->children[i]);
				}
				json += "]," + newline;
				json +=
				indent4 + fmt::format("  \"depth\": {}", snc->depth) + newline;
				json += indent4 + "}";
			}

			// AssetReferenceComponent
			if (const auto* arc = e.try_get<AssetReferenceComponent>())
			{
				if (!firstComponent)
					json += "," + newline;
				firstComponent = false;

				json += indent4 + "\"AssetReferenceComponent\": {" + newline;
				json += indent4 +
				        fmt::format("  \"assetPath\": \"{}\",",
				                    escapeJsonString(arc->assetPath)) +
				        newline;
				json += indent4 +
				        fmt::format("  \"meshName\": \"{}\",",
				                    escapeJsonString(arc->meshName)) +
				        newline;
				json += indent4 +
				        fmt::format("  \"assetType\": \"{}\"",
				                    escapeJsonString(arc->assetType)) +
				        newline;
				json += indent4 + "}";
			}

			// RenderMeshComponent (only visibility, mesh is handled by
			// AssetReferenceComponent)
			if (const auto* rmc = e.try_get<agni::ecs::RenderMeshComponent>())
			{
				if (!firstComponent)
					json += "," + newline;
				firstComponent = false;

				json += indent4 + "\"RenderMeshComponent\": {" + newline;
				json += indent4 +
				        fmt::format("  \"visible\": {}",
				                    rmc->visible ? "true" : "false") +
				        newline;
				json += indent4 + "}";
			}

			// === Reflection-based component serialization ===
			// Automatically serializes all registered components (except
			// TransformComponent, SceneNodeComponent, AssetReferenceComponent,
			// RenderMeshComponent which are handled above as special cases).
			for (const auto* desc : agni::ComponentRegistry::Instance().GetAll())
			{
				// Skip components already handled above
				if (std::strcmp(desc->name, "TransformComponent") == 0) continue;
				if (std::strcmp(desc->name, "AssetReferenceComponent") == 0) continue;

				const void* compData = desc->getConst(e);
				if (!compData) continue;

				if (!firstComponent)
					json += "," + newline;
				firstComponent = false;

				json += indent4 + fmt::format("\"{}\":", desc->name) + " {" + newline;
				json += serializeReflectedComponent(*desc, compData, indent4);
				json += newline + indent4 + "}";
			}

			json += newline + indent3 + "}," + newline;

			// Tags
			json += indent3 + "\"tags\": [";
			bool firstTag = true;

			if (e.has<agni::ecs::MeshEntityTag>())
			{
				if (!firstTag)
					json += ", ";
				json += "\"MeshEntityTag\"";
				firstTag = false;
			}
			if (e.has<agni::ecs::LightEntityTag>())
			{
				if (!firstTag)
					json += ", ";
				json += "\"LightEntityTag\"";
				firstTag = false;
			}
			if (e.has<agni::ecs::CameraEntityTag>())
			{
				if (!firstTag)
					json += ", ";
				json += "\"CameraEntityTag\"";
				firstTag = false;
			}
			if (e.has<agni::ecs::StaticTag>())
			{
				if (!firstTag)
					json += ", ";
				json += "\"StaticTag\"";
				firstTag = false;
			}
			if (e.has<agni::ecs::DynamicTag>())
			{
				if (!firstTag)
					json += ", ";
				json += "\"DynamicTag\"";
				firstTag = false;
			}
			if (e.has<PhysicsEnabledTag>())
			{
				if (!firstTag)
					json += ", ";
				json += "\"PhysicsEnabledTag\"";
				firstTag = false;
			}

			json += "]" + newline;
			json += indent2 + "}";
		}

		json += newline + indent + "]" + newline;
		json += "}";

		return json;
	}

	bool SceneSerializer::deserializeScene(const std::string&      json,
	                                       const SceneLoadOptions& options)
	{
		simdjson::ondemand::parser parser;
		simdjson::padded_string    paddedJson(json);

		auto docResult = parser.iterate(paddedJson);
		if (docResult.error())
		{
			m_lastError = fmt::format(
			"JSON parse error: {}", simdjson::error_message(docResult.error()));
			return false;
		}

		auto doc = std::move(docResult).value();

		// Clear existing entities if requested
		if (options.clearExisting)
		{
			m_engine.getECSWorld().clearAllEntities();
		}

		if (options.progressCallback)
		{
			options.progressCallback(0.1f, "Parsing scene...");
		}

		// Map old entity IDs to new entity IDs
		std::unordered_map<EntityID, EntityID> idMapping;
		std::vector<std::string>               assetPaths;

		// Parse entities array
		auto entitiesResult = doc["entities"].get_array();
		if (entitiesResult.error())
		{
			m_lastError = "Missing 'entities' array in scene file";
			return false;
		}

		auto& world = m_engine.getECSWorld();

		// First pass: Create all entities and collect asset paths
		for (auto entityObj : entitiesResult.value())
		{
			std::string_view name  = "";
			EntityID         oldId = 0;

			auto nameResult = entityObj["name"].get_string();
			if (!nameResult.error())
				name = nameResult.value();

			auto idResult = entityObj["id"].get_uint64();
			if (!idResult.error())
				oldId = idResult.value();

			// Create entity
			flecs::entity e  = name.empty()
			                   ? world.get().entity()
			                   : world.get().entity(std::string(name).c_str());
			idMapping[oldId] = e.id();

			// Parse components
			auto componentsResult = entityObj["components"].get_object();
			if (!componentsResult.error())
			{
				auto components = componentsResult.value();

				// TransformComponent
				auto tcResult = components["TransformComponent"].get_object();
				if (!tcResult.error())
				{
					TransformComponent tc {};
					auto               ltResult =
					tcResult.value()["localTransform"].get_array();
					if (!ltResult.error())
					{
						tc.localTransform = jsonToMat4(ltResult.value());
					}
					tc.worldTransform =
					tc.localTransform; // Will be recalculated
					e.set<TransformComponent>(tc);
				}

				// SceneNodeComponent (stored for second pass)
				auto sncResult = components["SceneNodeComponent"].get_object();
				if (!sncResult.error())
				{
					agni::ecs::SceneNodeComponent snc {};
					auto                          parentResult =
					sncResult.value()["parent"].get_uint64();
					if (!parentResult.error())
					{
						snc.parent = parentResult.value();
					}
					auto depthResult = sncResult.value()["depth"].get_uint64();
					if (!depthResult.error())
					{
						snc.depth = static_cast<uint32_t>(depthResult.value());
					}
					// Children will be rebuilt in second pass
					snc.dirtyWorld = true;
					e.set<agni::ecs::SceneNodeComponent>(snc);
				}

				// AssetReferenceComponent
				auto arcResult =
				components["AssetReferenceComponent"].get_object();
				if (!arcResult.error())
				{
					AssetReferenceComponent arc {};
					auto                    pathResult =
					arcResult.value()["assetPath"].get_string();
					if (!pathResult.error())
					{
						arc.assetPath = std::string(pathResult.value());
						if (!arc.assetPath.empty() && arc.assetPath != "")
						{
							assetPaths.push_back(arc.assetPath);
						}
					}
					auto meshResult =
					arcResult.value()["meshName"].get_string();
					if (!meshResult.error())
					{
						arc.meshName = std::string(meshResult.value());
					}
					auto typeResult =
					arcResult.value()["assetType"].get_string();
					if (!typeResult.error())
					{
						arc.assetType = std::string(typeResult.value());
					}
					e.set<AssetReferenceComponent>(arc);
				}

				// RenderMeshComponent
				auto rmcResult = components["RenderMeshComponent"].get_object();
				if (!rmcResult.error())
				{
					agni::ecs::RenderMeshComponent rmc {};
					auto visResult = rmcResult.value()["visible"].get_bool();
					if (!visResult.error())
					{
						rmc.visible = visResult.value();
					}
					e.set<agni::ecs::RenderMeshComponent>(rmc);
				}

				// === Reflection-based component deserialization ===
				// Automatically deserializes all registered components (except
				// TransformComponent, SceneNodeComponent, AssetReferenceComponent,
				// RenderMeshComponent which are handled above).
				for (const auto* desc : agni::ComponentRegistry::Instance().GetAll())
				{
					// Skip components already handled above
					if (std::strcmp(desc->name, "TransformComponent") == 0) continue;
					if (std::strcmp(desc->name, "AssetReferenceComponent") == 0) continue;

					auto compResult = components[desc->name].get_object();
					if (compResult.error()) continue;

					// Construct default component on stack
					alignas(16) uint8_t buffer[512];
					assert(desc->typeSize <= sizeof(buffer));
					desc->construct(buffer);

					// Deserialize fields from JSON into the buffer
					auto compObj = compResult.value();
					deserializeReflectedComponent(*desc, buffer, compObj);

					// Set the component on the entity
					desc->set(e, buffer);

					// Destruct the temporary
					desc->destruct(buffer);
				}
			}

			// Parse tags
			auto tagsResult = entityObj["tags"].get_array();
			if (!tagsResult.error())
			{
				for (auto tag : tagsResult.value())
				{
					auto tagStr = tag.get_string();
					if (!tagStr.error())
					{
						std::string_view t = tagStr.value();
						if (t == "MeshEntityTag")
							e.add<agni::ecs::MeshEntityTag>();
						else if (t == "LightEntityTag")
							e.add<agni::ecs::LightEntityTag>();
						else if (t == "CameraEntityTag")
							e.add<agni::ecs::CameraEntityTag>();
						else if (t == "StaticTag")
							e.add<agni::ecs::StaticTag>();
						else if (t == "DynamicTag")
							e.add<agni::ecs::DynamicTag>();
						else if (t == "PhysicsEnabledTag")
							e.add<PhysicsEnabledTag>();
					}
				}
			}
		}

		if (options.progressCallback)
		{
			options.progressCallback(0.3f, "Rebuilding hierarchy...");
		}

		// Second pass: Rebuild hierarchy using ID mapping
		world.get().defer_begin();

		world.get().query<agni::ecs::SceneNodeComponent>().each(
		[&](flecs::entity e, agni::ecs::SceneNodeComponent& snc)
		{
			if (snc.parent != NULL_ENTITY)
			{
				auto it = idMapping.find(snc.parent);
				if (it != idMapping.end())
				{
					// Update parent to new ID
					EntityID newParentId = it->second;
					snc.parent           = newParentId;

					// Use World::setParent to properly set up hierarchy
					world.setParent(e.id(), newParentId);
				}
				else
				{
					snc.parent = NULL_ENTITY;
				}
			}
			snc.dirtyWorld = true;
		});

		world.get().defer_end();

		if (options.progressCallback)
		{
			options.progressCallback(0.4f, "Loading assets...");
		}

		// Third pass: Reload assets if requested
		if (options.reloadAssets && !assetPaths.empty())
		{
			// Remove duplicates
			std::sort(assetPaths.begin(), assetPaths.end());
			assetPaths.erase(std::unique(assetPaths.begin(), assetPaths.end()),
			                 assetPaths.end());

			reloadAssets(assetPaths, options);
		}

		if (options.progressCallback)
		{
			options.progressCallback(0.8f, "Reconnecting meshes...");
		}

		// Fourth pass: Reconnect mesh assets
		reconnectMeshAssets();

		// Recalculate world transforms
		world.progress(0);

		if (options.progressCallback)
		{
			options.progressCallback(1.0f, "Complete");
		}

		return true;
	}

	std::string
	SceneSerializer::makeRelativePath(const std::filesystem::path& absolutePath)
	{
		// Try to make path relative to current working directory
		auto cwd = std::filesystem::current_path();
		if (absolutePath.string().find(cwd.string()) == 0)
		{
			return std::filesystem::relative(absolutePath, cwd).string();
		}
		return absolutePath.string();
	}

	std::filesystem::path
	SceneSerializer::resolveAssetPath(const std::string& relativePath)
	{
		// First try relative to scene file
		if (!m_currentScenePath.empty())
		{
			auto sceneDir  = m_currentScenePath.parent_path();
			auto candidate = sceneDir / relativePath;
			if (std::filesystem::exists(candidate))
			{
				return candidate;
			}
		}

		// Then try relative to current working directory
		auto cwd       = std::filesystem::current_path();
		auto candidate = cwd / relativePath;
		if (std::filesystem::exists(candidate))
		{
			return candidate;
		}

		// Return as-is (might be absolute)
		return relativePath;
	}

	void
	SceneSerializer::reloadAssets(const std::vector<std::string>& assetPaths,
	                              const SceneLoadOptions&         options)
	{
		auto& loadedScenes = m_engine.getRenderer().getLoadedScenes();

		for (size_t i = 0; i < assetPaths.size(); i++)
		{
			const auto& path = assetPaths[i];

			if (options.progressCallback)
			{
				float progress = 0.4f + (0.4f * static_cast<float>(i) /
				                         static_cast<float>(assetPaths.size()));
				options.progressCallback(progress,
				                         fmt::format("Loading {}", path));
			}

			// Check if already loaded
			if (loadedScenes.find(path) != loadedScenes.end())
			{
				continue;
			}

			auto fullPath = resolveAssetPath(path);
			if (!std::filesystem::exists(fullPath))
			{
				AGNI_PRINT("[SceneSerializer] WARNING: Asset not found: {}\n",
				           fullPath.string());
				AGNI_PRINT("[SceneSerializer]          Searched path: {}\n",
				           path);
				continue;
			}

			// Load synchronously for now (could use async later)
			AGNI_PRINT("[SceneSerializer] Loading asset: {}\n",
			           fullPath.string());
			auto result = m_engine.m_assetLoader.loadGltf(&m_engine, fullPath);
			if (result.has_value())
			{
				loadedScenes[path] = result.value();
				AGNI_PRINT("[SceneSerializer] Asset loaded successfully\n");
			}
			else
			{
				AGNI_PRINT(
				"[SceneSerializer] WARNING: Failed to load asset: {}\n",
				fullPath.string());
			}
		}
	}

	void SceneSerializer::reconnectMeshAssets()
	{
		auto& world        = m_engine.getECSWorld();
		auto& loadedScenes = m_engine.getRenderer().getLoadedScenes();

		world.get().each(
		[&](agni::ecs::RenderMeshComponent& rmc,
		    const AssetReferenceComponent&  arc)
		{
			if (arc.assetType == "primitive")
			{
				rmc.meshAsset = getPrimitiveMesh(arc.meshName);
			}
			else if (arc.assetType == "gltf")
			{
				auto it = loadedScenes.find(arc.assetPath);
				if (it != loadedScenes.end())
				{
					auto meshIt = it->second->meshes.find(arc.meshName);
					if (meshIt != it->second->meshes.end())
					{
						rmc.meshAsset = meshIt->second;
					}
					else
					{
						AGNI_PRINT(
						"[SceneSerializer] Mesh '{}' not found in asset '{}'\n",
						arc.meshName,
						arc.assetPath);
					}
				}
				else
				{
					AGNI_PRINT("[SceneSerializer] Asset '{}' not loaded\n",
					           arc.assetPath);
				}
			}
		});
	}

	std::string SceneSerializer::serializeToString(const SceneSaveOptions& options)
	{
		return serializeScene(options);
	}

	bool SceneSerializer::deserializeFromString(const std::string& json,
	                                            const SceneLoadOptions& options)
	{
		return deserializeScene(json, options);
	}

	std::shared_ptr<MeshAsset>
	SceneSerializer::getPrimitiveMesh(const std::string& name)
	{
		if (name == "Cube")
			return m_engine.getCubeMesh();
		if (name == "Sphere")
			return m_engine.getSphereMesh();
		if (name == "Plane")
			return m_engine.getPlaneMesh();
		if (name == "Suzanne")
			return m_engine.getSuzanneMesh();
		if (name == "Cylinder")
			return m_engine.getCylinderMesh();
		if (name == "Torus")
			return m_engine.getTorusMesh();
		if (name == "Cone")
			return m_engine.getConeMesh();
		return nullptr;
	}

} // namespace agni::scene
