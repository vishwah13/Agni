#pragma once

#include <ECS/World.hpp>

#include <memory>

// Forward declarations
struct MeshAsset;
struct LoadedGLTF;
class Node;
class MeshNode;
class LightNode;

namespace agni
{
namespace ecs
{

// Factory for creating ECS entities from various sources
class EntityFactory
{
public:
	explicit EntityFactory(World& world);
	~EntityFactory() = default;

	// Create a mesh entity referencing a loaded mesh asset
	flecs::entity createMeshEntity(std::shared_ptr<MeshAsset> mesh,
	                               const glm::mat4&           transform = glm::mat4(1.0f),
	                               const char*                name      = nullptr);

	// Create a light entity
	flecs::entity createLightEntity(const LightComponent& light,
	                                const glm::mat4&      transform = glm::mat4(1.0f),
	                                const char*           name      = nullptr);

	// Create entities from a loaded glTF file
	// Returns the root entity containing all converted nodes
	flecs::entity createFromGltf(LoadedGLTF& gltf, const char* rootName = nullptr);

	// Create a single entity from an existing Node
	// Useful for incremental migration
	flecs::entity createFromNode(std::shared_ptr<Node> node,
	                             flecs::entity         parent = flecs::entity::null());

private:
	World& m_world;

	// Recursively convert a Node and its children to ECS entities
	flecs::entity convertNodeRecursive(std::shared_ptr<Node> node, flecs::entity parent);
};

} // namespace ecs
} // namespace agni
