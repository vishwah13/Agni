#pragma once

#include <Components.hpp>

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <unordered_map>

// Forward declarations for Jolt types (avoid including Jolt headers in public header)
namespace JPH
{
class PhysicsSystem;
class TempAllocator;
class JobSystem;
class BodyInterface;
struct BodyID;
class BroadPhaseLayerInterface;
class ObjectVsBroadPhaseLayerFilter;
class ObjectLayerPairFilter;
} // namespace JPH

namespace agni
{
namespace physics
{

struct PhysicsSettings
{
	glm::vec3 gravity {0.0f, -9.81f, 0.0f};
	uint32_t  maxBodies {1024};
	uint32_t  numBodyMutexes {0};          // 0 = auto-determine
	uint32_t  maxBodyPairs {1024};
	uint32_t  maxContactConstraints {1024};
	int       collisionSteps {1};          // Number of solver iterations
};

class JoltPhysicsManager
{
public:
	JoltPhysicsManager();
	~JoltPhysicsManager();
	JoltPhysicsManager(const JoltPhysicsManager& other)            = delete;
	JoltPhysicsManager(JoltPhysicsManager&& other)                 = delete;
	JoltPhysicsManager& operator=(const JoltPhysicsManager& other) = delete;
	JoltPhysicsManager& operator=(JoltPhysicsManager&& other)      = delete;

	// Initialization
	bool initialize(const PhysicsSettings& settings = PhysicsSettings{});
	void shutdown();

	// Main simulation step
	void update(float deltaTime);

	// Body creation
	uint32_t createDynamicBody(const glm::vec3&        pos,
	                           const glm::quat&        rot,
	                           ColliderType            type,
	                           const ColliderComponent& collider,
	                           float                   mass,
	                           float                   friction,
	                           float                   restitution,
	                           bool                    useGravity = true,
	                           const glm::vec3&        scale = glm::vec3(1.0f));

	uint32_t createStaticBody(const glm::vec3&        pos,
	                          const glm::quat&        rot,
	                          ColliderType            type,
	                          const ColliderComponent& collider,
	                          float                   friction,
	                          float                   restitution,
	                          const glm::vec3&        scale = glm::vec3(1.0f));

	uint32_t createKinematicBody(const glm::vec3&        pos,
	                             const glm::quat&        rot,
	                             ColliderType            type,
	                             const ColliderComponent& collider,
	                             const glm::vec3&        scale = glm::vec3(1.0f));

	void removeBody(uint32_t bodyID);
	void removeAllBodies();

	// Broadphase optimization (call after bulk body creation)
	void optimizeBroadPhase();

	// Transform sync
	void      setBodyTransform(uint32_t bodyID, const glm::mat4& transform);
	glm::mat4 getBodyTransform(uint32_t bodyID) const;
	glm::vec3 getBodyPosition(uint32_t bodyID) const;
	glm::quat getBodyRotation(uint32_t bodyID) const;

	// Velocity
	glm::vec3 getLinearVelocity(uint32_t bodyID) const;
	glm::vec3 getAngularVelocity(uint32_t bodyID) const;
	void      setLinearVelocity(uint32_t bodyID, const glm::vec3& velocity);
	void      setAngularVelocity(uint32_t bodyID, const glm::vec3& angularVelocity);

	// Forces
	void addForce(uint32_t bodyID, const glm::vec3& force);
	void addImpulse(uint32_t bodyID, const glm::vec3& impulse);

	// Gravity control
	void      setGravity(const glm::vec3& gravity);
	glm::vec3 getGravity() const;

	// Entity <-> Body mapping
	void     registerEntityBody(EntityID entity, uint32_t bodyID);
	void     unregisterEntity(EntityID entity);
	EntityID getEntityFromBody(uint32_t bodyID) const;

	// Access to body interface (for advanced use)
	JPH::BodyInterface* getBodyInterface();

private:
	std::unique_ptr<JPH::PhysicsSystem>                m_physicsSystem;
	std::unique_ptr<JPH::TempAllocator>                m_tempAllocator;
	std::unique_ptr<JPH::JobSystem>                    m_jobSystem;
	std::unique_ptr<JPH::BroadPhaseLayerInterface>     m_broadPhaseLayerInterface;
	std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilter> m_objectVsBroadPhaseLayerFilter;
	std::unique_ptr<JPH::ObjectLayerPairFilter>        m_objectLayerPairFilter;

	std::unordered_map<uint32_t, EntityID> m_bodyToEntity;
	std::unordered_map<EntityID, uint32_t> m_entityToBody;

	PhysicsSettings m_settings;
	float           m_accumulator {0.0f}; // For fixed timestep

	// Helper to convert body ID
	JPH::BodyID toJoltBodyID(uint32_t bodyID) const;
};

} // namespace physics
} // namespace agni
