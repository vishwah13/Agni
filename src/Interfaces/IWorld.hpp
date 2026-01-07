#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

namespace agni
{

// Abstract interface for ECS world
// Allows game layer to interact with engine without direct Flecs dependency
class IWorld
{
public:
	using EntityHandle = uint64_t;
	static constexpr EntityHandle InvalidEntity = 0;

	virtual ~IWorld() = default;

	// Entity lifecycle
	virtual EntityHandle createEntity(const char* name = nullptr)     = 0;
	virtual void         destroyEntity(EntityHandle entity)           = 0;
	virtual bool         isValid(EntityHandle entity) const           = 0;

	// Transform operations
	virtual void      setLocalTransform(EntityHandle entity, const glm::mat4& transform) = 0;
	virtual glm::mat4 getLocalTransform(EntityHandle entity) const                       = 0;
	virtual glm::mat4 getWorldTransform(EntityHandle entity) const                       = 0;

	// Position/rotation convenience
	virtual void      setPosition(EntityHandle entity, const glm::vec3& position) = 0;
	virtual glm::vec3 getPosition(EntityHandle entity) const                      = 0;

	// Hierarchy
	virtual void         setParent(EntityHandle child, EntityHandle parent) = 0;
	virtual EntityHandle getParent(EntityHandle entity) const               = 0;
	virtual void         removeParent(EntityHandle entity)                  = 0;

	// Frame progression
	virtual void progress(float deltaTime) = 0;
};

} // namespace agni
