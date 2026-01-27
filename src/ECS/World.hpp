#pragma once

#include <Interfaces/IWorld.hpp>
#include <Components.hpp>

#include <flecs.h>

#include <memory>

// Forward declarations
struct MeshAsset;
struct DrawContext;

namespace agni::ecs
{
class EntityManager;
} // namespace agni::ecs

namespace agni
{
namespace ecs
{

// Tag components for entity types
struct MeshEntityTag {};
struct LightEntityTag {};
struct CameraEntityTag {};
struct StaticTag {};
struct DynamicTag {};

// Scene node component for hierarchy with dirty flags
struct SceneNodeComponent
{
	EntityID              parent {NULL_ENTITY};
	std::vector<EntityID> children;
	bool                  dirtyWorld {true};
	uint32_t              depth {0}; // For sorted traversal (0 = root)
};

// Render mesh component - references shared asset
struct RenderMeshComponent
{
	std::shared_ptr<MeshAsset> meshAsset;
	bool                       visible {true};
};

class World : public IWorld
{
public:
	World();
	~World() override;
	World(const World& other)            = delete;
	World(World&& other)                 = delete;
	World& operator=(const World& other) = delete;
	World& operator=(World&& other)      = delete;

	// Access the underlying flecs world
	flecs::world&       get() { return m_world; }
	const flecs::world& get() const { return m_world; }

	// IWorld interface implementation
	EntityHandle createEntity(const char* name = nullptr) override;
	void         destroyEntity(EntityHandle entity) override;
	bool         isValid(EntityHandle entity) const override;

	void      setLocalTransform(EntityHandle entity, const glm::mat4& transform) override;
	glm::mat4 getLocalTransform(EntityHandle entity) const override;
	glm::mat4 getWorldTransform(EntityHandle entity) const override;

	void      setPosition(EntityHandle entity, const glm::vec3& position) override;
	glm::vec3 getPosition(EntityHandle entity) const override;

	void         setParent(EntityHandle child, EntityHandle parent) override;
	EntityHandle getParent(EntityHandle entity) const override;
	void         removeParent(EntityHandle entity) override;

	void progress(float deltaTime) override;

	// Cleanup all entities (releases all component data)
	void clearAllEntities();

	// Entity creation helpers (return flecs entity for internal use)
	flecs::entity createMeshEntity(const char* name = nullptr);
	flecs::entity createLightEntity(const char* name = nullptr);
	flecs::entity createCameraEntity(const char* name = nullptr);

	// Get component data (mutable)
	template<typename T>
	T* getComponent(EntityHandle entity)
	{
		auto e = m_world.entity(entity);
		if (e.has<T>())
		{
			return &e.ensure<T>();
		}
		return nullptr;
	}

	// Get component data (const)
	template<typename T>
	const T* getComponent(EntityHandle entity) const
	{
		auto e = m_world.entity(entity);
		return e.try_get<T>();
	}

	// Add component to entity
	template<typename T>
	void addComponent(EntityHandle entity, const T& component)
	{
		auto e = m_world.entity(entity);
		e.set<T>(component);
	}

	// Entity Manager (central hub for entity creation)
	EntityManager& getEntityManager();

private:
	flecs::world                       m_world;
	std::unique_ptr<EntityManager> m_entityManager;

	void registerComponents();
	void registerSystems();

	// Helper to update hierarchy depth
	void updateDepth(flecs::entity entity, uint32_t depth);
};

} // namespace ecs
} // namespace agni
