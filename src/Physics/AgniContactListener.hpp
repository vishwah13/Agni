#pragma once

#include <Components.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ContactListener.h>

#include <glm/vec3.hpp>

#include <functional>
#include <mutex>
#include <vector>

namespace agni
{
namespace physics
{

enum class CollisionEventType : uint8_t
{
	Begin,   // New contact this frame
	Persist, // Contact continues from previous frame
	End      // Contact removed
};

struct CollisionEvent
{
	CollisionEventType type    = CollisionEventType::Begin;
	EntityID           entityA = NULL_ENTITY;
	EntityID           entityB = NULL_ENTITY;
	glm::vec3          contactPoint {0.0f};
	glm::vec3          contactNormal {0.0f};
	float              penetrationDepth = 0.0f;
	bool               isTrigger       = false;
};

class AgniContactListener : public JPH::ContactListener
{
public:
	using EntityLookup = std::function<EntityID(uint32_t)>;

	void setEntityLookup(EntityLookup lookup) { m_entityLookup = std::move(lookup); }

	// JPH::ContactListener overrides
	void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2,
	                     const JPH::ContactManifold& inManifold,
	                     JPH::ContactSettings& ioSettings) override;

	void OnContactPersisted(const JPH::Body& inBody1, const JPH::Body& inBody2,
	                         const JPH::ContactManifold& inManifold,
	                         JPH::ContactSettings& ioSettings) override;

	void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override;

	// Drain all queued events (call from main thread once per frame)
	std::vector<CollisionEvent> drainEvents();

private:
	void pushContactEvent(CollisionEventType type,
	                       const JPH::Body& body1, const JPH::Body& body2,
	                       const JPH::ContactManifold& manifold);

	std::mutex                  m_mutex;
	std::vector<CollisionEvent> m_events;
	EntityLookup                m_entityLookup;
};

} // namespace physics
} // namespace agni
