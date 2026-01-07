#include <ECS/World.hpp>

#include <glm/gtc/matrix_transform.hpp>

namespace agni
{
namespace ecs
{

World::World()
{
	registerComponents();
	registerSystems();
}

World::~World() = default;

void World::registerComponents()
{
	// Register all component types with Flecs
	m_world.component<TransformComponent>();
	m_world.component<SceneNodeComponent>();
	m_world.component<RenderMeshComponent>();
	m_world.component<LightComponent>();
	m_world.component<CameraComponent>();
	m_world.component<RenderableTag>();

	// Register tag components
	m_world.component<MeshEntityTag>();
	m_world.component<LightEntityTag>();
	m_world.component<CameraEntityTag>();
	m_world.component<StaticTag>();
	m_world.component<DynamicTag>();
}

void World::registerSystems()
{
	// Transform hierarchy system - processes entities sorted by depth
	m_world.system<TransformComponent, SceneNodeComponent>("TransformHierarchy")
	    .kind(flecs::PreUpdate)
	    .each([this](flecs::entity e, TransformComponent& transform, SceneNodeComponent& node) {
		    if (!node.dirtyWorld)
			    return;

		    // Get parent's world transform
		    glm::mat4 parentWorld = glm::mat4(1.0f);
		    if (node.parent != NULL_ENTITY)
		    {
			    auto parentEntity = m_world.entity(node.parent);
			    if (parentEntity.is_valid())
			    {
				    const TransformComponent* parentTransform = parentEntity.try_get<TransformComponent>();
				    if (parentTransform)
				    {
					    parentWorld = parentTransform->worldTransform;
				    }
			    }
		    }

		    // Calculate world transform
		    transform.worldTransform = parentWorld * transform.localTransform;
		    node.dirtyWorld          = false;
	    });
}

IWorld::EntityHandle World::createEntity(const char* name)
{
	flecs::entity entity = name ? m_world.entity(name) : m_world.entity();
	entity.set<TransformComponent>({});
	entity.set<SceneNodeComponent>({});
	return entity.id();
}

void World::destroyEntity(EntityHandle entity)
{
	auto e = m_world.entity(entity);
	if (e.is_valid())
	{
		// Remove from parent's children list
		const SceneNodeComponent* node = e.try_get<SceneNodeComponent>();
		if (node && node->parent != NULL_ENTITY)
		{
			auto parent = m_world.entity(node->parent);
			if (parent.has<SceneNodeComponent>())
			{
				SceneNodeComponent& parentNode = parent.ensure<SceneNodeComponent>();
				auto&               children   = parentNode.children;
				children.erase(std::remove(children.begin(), children.end(), entity), children.end());
			}
		}

		// Destroy all children recursively
		if (node)
		{
			for (EntityID childId : node->children)
			{
				destroyEntity(childId);
			}
		}

		e.destruct();
	}
}

bool World::isValid(EntityHandle entity) const
{
	return m_world.entity(entity).is_valid();
}

void World::setLocalTransform(EntityHandle entity, const glm::mat4& transform)
{
	auto e = m_world.entity(entity);
	if (!e.is_valid())
		return;

	if (e.has<TransformComponent>())
	{
		TransformComponent& tc = e.ensure<TransformComponent>();
		tc.localTransform      = transform;
	}

	// Mark this entity and all children as dirty
	if (e.has<SceneNodeComponent>())
	{
		SceneNodeComponent& node = e.ensure<SceneNodeComponent>();
		node.dirtyWorld          = true;
		for (EntityID childId : node.children)
		{
			auto child = m_world.entity(childId);
			if (child.has<SceneNodeComponent>())
			{
				child.ensure<SceneNodeComponent>().dirtyWorld = true;
			}
		}
	}
}

glm::mat4 World::getLocalTransform(EntityHandle entity) const
{
	auto e = m_world.entity(entity);
	if (!e.is_valid())
		return glm::mat4(1.0f);

	const TransformComponent* tc = e.try_get<TransformComponent>();
	return tc ? tc->localTransform : glm::mat4(1.0f);
}

glm::mat4 World::getWorldTransform(EntityHandle entity) const
{
	auto e = m_world.entity(entity);
	if (!e.is_valid())
		return glm::mat4(1.0f);

	const TransformComponent* tc = e.try_get<TransformComponent>();
	return tc ? tc->worldTransform : glm::mat4(1.0f);
}

void World::setPosition(EntityHandle entity, const glm::vec3& position)
{
	auto e = m_world.entity(entity);
	if (!e.is_valid())
		return;

	if (e.has<TransformComponent>())
	{
		TransformComponent& tc    = e.ensure<TransformComponent>();
		tc.localTransform[3]      = glm::vec4(position, 1.0f);
	}

	// Mark dirty
	if (e.has<SceneNodeComponent>())
	{
		SceneNodeComponent& node = e.ensure<SceneNodeComponent>();
		node.dirtyWorld          = true;
		for (EntityID childId : node.children)
		{
			auto child = m_world.entity(childId);
			if (child.has<SceneNodeComponent>())
			{
				child.ensure<SceneNodeComponent>().dirtyWorld = true;
			}
		}
	}
}

glm::vec3 World::getPosition(EntityHandle entity) const
{
	auto e = m_world.entity(entity);
	if (!e.is_valid())
		return glm::vec3(0.0f);

	const TransformComponent* tc = e.try_get<TransformComponent>();
	return tc ? glm::vec3(tc->localTransform[3]) : glm::vec3(0.0f);
}

void World::setParent(EntityHandle child, EntityHandle parent)
{
	auto childEntity  = m_world.entity(child);
	auto parentEntity = m_world.entity(parent);

	if (!childEntity.is_valid())
		return;

	if (!childEntity.has<SceneNodeComponent>())
		return;

	SceneNodeComponent& childNode = childEntity.ensure<SceneNodeComponent>();

	// Remove from old parent
	if (childNode.parent != NULL_ENTITY)
	{
		auto oldParent = m_world.entity(childNode.parent);
		if (oldParent.has<SceneNodeComponent>())
		{
			SceneNodeComponent& oldParentNode = oldParent.ensure<SceneNodeComponent>();
			auto&               children      = oldParentNode.children;
			children.erase(std::remove(children.begin(), children.end(), child), children.end());
		}
	}

	// Set new parent
	if (parentEntity.is_valid())
	{
		childNode.parent = parent;

		if (parentEntity.has<SceneNodeComponent>())
		{
			SceneNodeComponent& parentNode = parentEntity.ensure<SceneNodeComponent>();
			parentNode.children.push_back(child);
			updateDepth(childEntity, parentNode.depth + 1);
		}

		// Use Flecs built-in hierarchy too
		childEntity.child_of(parentEntity);
	}
	else
	{
		childNode.parent = NULL_ENTITY;
		updateDepth(childEntity, 0);
	}

	// Mark dirty
	childNode.dirtyWorld = true;
}

IWorld::EntityHandle World::getParent(EntityHandle entity) const
{
	auto e = m_world.entity(entity);
	if (!e.is_valid())
		return InvalidEntity;

	const SceneNodeComponent* node = e.try_get<SceneNodeComponent>();
	return node ? node->parent : InvalidEntity;
}

void World::removeParent(EntityHandle entity)
{
	setParent(entity, InvalidEntity);
}

void World::progress(float deltaTime)
{
	m_world.progress(deltaTime);
}

flecs::entity World::createMeshEntity(const char* name)
{
	flecs::entity entity = name ? m_world.entity(name) : m_world.entity();
	entity.set<TransformComponent>({});
	entity.set<SceneNodeComponent>({});
	entity.set<RenderMeshComponent>({});
	entity.set<RenderableTag>({.visible = true});
	entity.add<MeshEntityTag>();
	return entity;
}

flecs::entity World::createLightEntity(const char* name)
{
	flecs::entity entity = name ? m_world.entity(name) : m_world.entity();
	entity.set<TransformComponent>({});
	entity.set<SceneNodeComponent>({});
	entity.set<LightComponent>({});
	entity.add<LightEntityTag>();
	return entity;
}

flecs::entity World::createCameraEntity(const char* name)
{
	flecs::entity entity = name ? m_world.entity(name) : m_world.entity();
	entity.set<TransformComponent>({});
	entity.set<SceneNodeComponent>({});
	entity.set<CameraComponent>({});
	entity.add<CameraEntityTag>();
	return entity;
}

void World::updateDepth(flecs::entity entity, uint32_t depth)
{
	if (!entity.has<SceneNodeComponent>())
		return;

	SceneNodeComponent& node = entity.ensure<SceneNodeComponent>();
	node.depth               = depth;

	for (EntityID childId : node.children)
	{
		auto child = m_world.entity(childId);
		updateDepth(child, depth + 1);
	}
}

} // namespace ecs
} // namespace agni
