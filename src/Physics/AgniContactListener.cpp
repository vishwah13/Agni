#include <Physics/AgniContactListener.hpp>

#include <Jolt/Physics/Body/Body.h>

using namespace JPH;

namespace agni
{
namespace physics
{

void AgniContactListener::pushContactEvent(CollisionEventType type,
                                            const Body& body1, const Body& body2,
                                            const ContactManifold& manifold)
{
	CollisionEvent event {};
	event.type = type;

	uint32_t id1 = body1.GetID().GetIndexAndSequenceNumber();
	uint32_t id2 = body2.GetID().GetIndexAndSequenceNumber();
	event.entityA = m_entityLookup ? m_entityLookup(id1) : NULL_ENTITY;
	event.entityB = m_entityLookup ? m_entityLookup(id2) : NULL_ENTITY;

	// First contact point in world space
	if (manifold.mRelativeContactPointsOn1.size() > 0)
	{
		RVec3 worldPoint = manifold.GetWorldSpaceContactPointOn1(0);
		event.contactPoint = glm::vec3(
		    static_cast<float>(worldPoint.GetX()),
		    static_cast<float>(worldPoint.GetY()),
		    static_cast<float>(worldPoint.GetZ()));
	}

	event.contactNormal = glm::vec3(
	    manifold.mWorldSpaceNormal.GetX(),
	    manifold.mWorldSpaceNormal.GetY(),
	    manifold.mWorldSpaceNormal.GetZ());

	event.penetrationDepth = manifold.mPenetrationDepth;
	event.isTrigger = body1.IsSensor() || body2.IsSensor();

	std::lock_guard<std::mutex> lock(m_mutex);
	m_events.push_back(event);
}

void AgniContactListener::OnContactAdded(const Body& inBody1, const Body& inBody2,
                                          const ContactManifold& inManifold,
                                          ContactSettings& /*ioSettings*/)
{
	pushContactEvent(CollisionEventType::Begin, inBody1, inBody2, inManifold);
}

void AgniContactListener::OnContactPersisted(const Body& inBody1, const Body& inBody2,
                                              const ContactManifold& inManifold,
                                              ContactSettings& /*ioSettings*/)
{
	pushContactEvent(CollisionEventType::Persist, inBody1, inBody2, inManifold);
}

void AgniContactListener::OnContactRemoved(const SubShapeIDPair& inSubShapePair)
{
	// No body access in OnContactRemoved — bodies may be destroyed
	CollisionEvent event {};
	event.type = CollisionEventType::End;

	uint32_t id1 = inSubShapePair.GetBody1ID().GetIndexAndSequenceNumber();
	uint32_t id2 = inSubShapePair.GetBody2ID().GetIndexAndSequenceNumber();
	event.entityA = m_entityLookup ? m_entityLookup(id1) : NULL_ENTITY;
	event.entityB = m_entityLookup ? m_entityLookup(id2) : NULL_ENTITY;

	std::lock_guard<std::mutex> lock(m_mutex);
	m_events.push_back(event);
}

std::vector<CollisionEvent> AgniContactListener::drainEvents()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::vector<CollisionEvent> events;
	events.swap(m_events);
	return events;
}

} // namespace physics
} // namespace agni
