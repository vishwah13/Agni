#include <Physics/JoltPhysicsManager.hpp>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <fmt/core.h>

// Jolt uses namespaces
using namespace JPH;

// Disable common warnings triggered by Jolt
JPH_SUPPRESS_WARNINGS

namespace agni
{
namespace physics
{

// Layer that objects can be in, determines which other objects it can collide with
namespace Layers
{
	static constexpr ObjectLayer NON_MOVING = 0;
	static constexpr ObjectLayer MOVING     = 1;
	static constexpr ObjectLayer NUM_LAYERS = 2;
}; // namespace Layers

// Class that determines if two object layers can collide
class ObjectLayerPairFilterImpl : public ObjectLayerPairFilter
{
public:
	virtual bool ShouldCollide(ObjectLayer inObject1, ObjectLayer inObject2) const override
	{
		switch (inObject1)
		{
		case Layers::NON_MOVING:
			return inObject2 == Layers::MOVING; // Non moving only collides with moving
		case Layers::MOVING:
			return true; // Moving collides with everything
		default:
			JPH_ASSERT(false);
			return false;
		}
	}
};

// BroadPhaseLayer maps object layers to broad phase layers
namespace BroadPhaseLayers
{
	static constexpr BroadPhaseLayer NON_MOVING(0);
	static constexpr BroadPhaseLayer MOVING(1);
	static constexpr uint             NUM_LAYERS(2);
}; // namespace BroadPhaseLayers

// BroadPhaseLayerInterface maps object layer to broad phase layer
class BPLayerInterfaceImpl final : public BroadPhaseLayerInterface
{
public:
	BPLayerInterfaceImpl()
	{
		// Create mapping table from object to broad phase layer
		m_ObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
		m_ObjectToBroadPhase[Layers::MOVING]     = BroadPhaseLayers::MOVING;
	}

	virtual uint GetNumBroadPhaseLayers() const override
	{
		return BroadPhaseLayers::NUM_LAYERS;
	}

	virtual BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer inLayer) const override
	{
		JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
		return m_ObjectToBroadPhase[inLayer];
	}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
	virtual const char* GetBroadPhaseLayerName(BroadPhaseLayer inLayer) const override
	{
		switch ((BroadPhaseLayer::Type) inLayer)
		{
		case (BroadPhaseLayer::Type) BroadPhaseLayers::NON_MOVING:
			return "NON_MOVING";
		case (BroadPhaseLayer::Type) BroadPhaseLayers::MOVING:
			return "MOVING";
		default:
			JPH_ASSERT(false);
			return "INVALID";
		}
	}
#endif

private:
	BroadPhaseLayer m_ObjectToBroadPhase[Layers::NUM_LAYERS];
};

/// Class that determines if an object layer can collide with a broadphase layer
class ObjectVsBroadPhaseLayerFilterImpl : public ObjectVsBroadPhaseLayerFilter
{
public:
	virtual bool ShouldCollide(ObjectLayer inLayer1, BroadPhaseLayer inLayer2) const override
	{
		switch (inLayer1)
		{
		case Layers::NON_MOVING:
			return inLayer2 == BroadPhaseLayers::MOVING;
		case Layers::MOVING:
			return true;
		default:
			JPH_ASSERT(false);
			return false;
		}
	}
};

// Helper: Convert glm to Jolt types
inline Vec3 toJoltVec3(const glm::vec3& v)
{
	return Vec3(v.x, v.y, v.z);
}

inline glm::vec3 toGlmVec3(const Vec3& v)
{
	return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}

inline Quat toJoltQuat(const glm::quat& q)
{
	return Quat(q.x, q.y, q.z, q.w);
}

inline glm::quat toGlmQuat(const Quat& q)
{
	return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

JoltPhysicsManager::JoltPhysicsManager() = default;

JoltPhysicsManager::~JoltPhysicsManager()
{
	shutdown();
}

bool JoltPhysicsManager::initialize(const PhysicsSettings& settings)
{
	m_settings = settings;

	// Register allocation hook (for memory tracking if needed)
	RegisterDefaultAllocator();

	// Install trace and assert callbacks
	Trace = [](const char* inFMT, ...) {}; // Disable traces
	JPH_IF_ENABLE_ASSERTS(AssertFailed = [](const char* inExpression, const char* inMessage, const char* inFile, uint inLine) -> bool {
		fmt::print("[Jolt Assert] {} at {}:{}\n  {}\n", inExpression, inFile, inLine, inMessage ? inMessage : "");
		return true; // Trigger breakpoint
	});

	// Create factory for creating physics objects
	Factory::sInstance = new Factory();

	// Register all Jolt physics types
	RegisterTypes();

	// Create temp allocator
	m_tempAllocator = std::make_unique<TempAllocatorImpl>(10 * 1024 * 1024); // 10 MB

	// Create job system (thread pool for physics)
	m_jobSystem = std::make_unique<JobSystemThreadPool>(
	    cMaxPhysicsJobs,
	    cMaxPhysicsBarriers,
	    std::thread::hardware_concurrency() - 1);

	// Create layer interfaces
	m_broadPhaseLayerInterface        = std::make_unique<BPLayerInterfaceImpl>();
	m_objectVsBroadPhaseLayerFilter   = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();
	m_objectLayerPairFilter           = std::make_unique<ObjectLayerPairFilterImpl>();

	// Create physics system
	m_physicsSystem = std::make_unique<PhysicsSystem>();
	m_physicsSystem->Init(
	    settings.maxBodies,
	    settings.numBodyMutexes,
	    settings.maxBodyPairs,
	    settings.maxContactConstraints,
	    *m_broadPhaseLayerInterface,
	    *m_objectVsBroadPhaseLayerFilter,
	    *m_objectLayerPairFilter);

	// Set gravity
	m_physicsSystem->SetGravity(toJoltVec3(settings.gravity));

	fmt::print("[JoltPhysicsManager] Initialized successfully\n");
	fmt::print("  Max bodies: {}\n", settings.maxBodies);
	fmt::print("  Gravity: ({:.2f}, {:.2f}, {:.2f})\n", settings.gravity.x, settings.gravity.y, settings.gravity.z);

	return true;
}

void JoltPhysicsManager::shutdown()
{
	if (m_physicsSystem)
	{
		m_physicsSystem.reset();
		m_jobSystem.reset();
		m_tempAllocator.reset();
		m_broadPhaseLayerInterface.reset();
		m_objectVsBroadPhaseLayerFilter.reset();
		m_objectLayerPairFilter.reset();

		// Unregister types
		UnregisterTypes();

		// Destroy factory
		delete Factory::sInstance;
		Factory::sInstance = nullptr;

		fmt::print("[JoltPhysicsManager] Shutdown complete\n");
	}
}

void JoltPhysicsManager::update(float deltaTime)
{
	if (!m_physicsSystem)
		return;

	// Use fixed timestep for stability (60 Hz physics tick)
	const float fixedTimestep = 1.0f / 60.0f;

	// Run physics step
	m_physicsSystem->Update(
	    fixedTimestep,
	    m_settings.collisionSteps,
	    m_tempAllocator.get(),
	    m_jobSystem.get());
}

uint32_t JoltPhysicsManager::createDynamicBody(const glm::vec3&        pos,
                                               const glm::quat&        rot,
                                               ColliderType            type,
                                               const ColliderComponent& collider,
                                               float                   mass,
                                               float                   friction,
                                               float                   restitution)
{
	if (!m_physicsSystem)
		return 0;

	// Create shape based on collider type
	RefConst<Shape> shape;
	switch (type)
	{
	case ColliderType::Box:
		shape = new BoxShape(toJoltVec3(collider.boxHalfExtents));
		break;
	case ColliderType::Sphere:
		shape = new SphereShape(collider.sphereRadius);
		break;
	case ColliderType::Capsule:
		shape = new CapsuleShape(collider.capsuleHalfHeight, collider.capsuleRadius);
		break;
	default:
		fmt::print("[JoltPhysicsManager] Unsupported collider type\n");
		return 0;
	}

	// Create body settings
	BodyCreationSettings bodySettings(
	    shape,
	    RVec3(pos.x, pos.y, pos.z),
	    toJoltQuat(rot),
	    EMotionType::Dynamic,
	    Layers::MOVING);

	bodySettings.mFriction    = friction;
	bodySettings.mRestitution = restitution;

	// Override mass (Jolt will calculate inertia from shape)
	bodySettings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
	bodySettings.mMassPropertiesOverride.mMass = mass;

	// Create and add body to physics system
	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	BodyID         bodyID        = bodyInterface.CreateAndAddBody(bodySettings, EActivation::Activate);

	if (bodyID.IsInvalid())
	{
		fmt::print("[JoltPhysicsManager] Failed to create dynamic body\n");
		return 0;
	}

	return bodyID.GetIndexAndSequenceNumber();
}

uint32_t JoltPhysicsManager::createStaticBody(const glm::vec3&        pos,
                                              const glm::quat&        rot,
                                              ColliderType            type,
                                              const ColliderComponent& collider,
                                              float                   friction,
                                              float                   restitution)
{
	if (!m_physicsSystem)
		return 0;

	// Create shape
	RefConst<Shape> shape;
	switch (type)
	{
	case ColliderType::Box:
		shape = new BoxShape(toJoltVec3(collider.boxHalfExtents));
		break;
	case ColliderType::Sphere:
		shape = new SphereShape(collider.sphereRadius);
		break;
	case ColliderType::Capsule:
		shape = new CapsuleShape(collider.capsuleHalfHeight, collider.capsuleRadius);
		break;
	default:
		return 0;
	}

	// Create static body settings
	BodyCreationSettings bodySettings(
	    shape,
	    RVec3(pos.x, pos.y, pos.z),
	    toJoltQuat(rot),
	    EMotionType::Static,
	    Layers::NON_MOVING);

	bodySettings.mFriction    = friction;
	bodySettings.mRestitution = restitution;

	// Create and add body
	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	BodyID         bodyID        = bodyInterface.CreateAndAddBody(bodySettings, EActivation::DontActivate);

	return bodyID.GetIndexAndSequenceNumber();
}

uint32_t JoltPhysicsManager::createKinematicBody(const glm::vec3&        pos,
                                                 const glm::quat&        rot,
                                                 ColliderType            type,
                                                 const ColliderComponent& collider)
{
	if (!m_physicsSystem)
		return 0;

	// Create shape
	RefConst<Shape> shape;
	switch (type)
	{
	case ColliderType::Box:
		shape = new BoxShape(toJoltVec3(collider.boxHalfExtents));
		break;
	case ColliderType::Sphere:
		shape = new SphereShape(collider.sphereRadius);
		break;
	case ColliderType::Capsule:
		shape = new CapsuleShape(collider.capsuleHalfHeight, collider.capsuleRadius);
		break;
	default:
		return 0;
	}

	// Create kinematic body settings
	BodyCreationSettings bodySettings(
	    shape,
	    RVec3(pos.x, pos.y, pos.z),
	    toJoltQuat(rot),
	    EMotionType::Kinematic,
	    Layers::MOVING);

	// Create and add body
	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	BodyID         bodyID        = bodyInterface.CreateAndAddBody(bodySettings, EActivation::Activate);

	return bodyID.GetIndexAndSequenceNumber();
}

void JoltPhysicsManager::removeBody(uint32_t bodyID)
{
	if (!m_physicsSystem || bodyID == 0)
		return;

	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	BodyID         joltID        = toJoltBodyID(bodyID);

	bodyInterface.RemoveBody(joltID);
	bodyInterface.DestroyBody(joltID);
}

void JoltPhysicsManager::setBodyTransform(uint32_t bodyID, const glm::mat4& transform)
{
	if (!m_physicsSystem || bodyID == 0)
		return;

	// Decompose matrix
	glm::vec3 scale, translation, skew;
	glm::quat rotation;
	glm::vec4 perspective;
	glm::decompose(transform, scale, rotation, translation, skew, perspective);

	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	BodyID         joltID        = toJoltBodyID(bodyID);

	bodyInterface.SetPositionAndRotation(joltID, RVec3(translation.x, translation.y, translation.z), toJoltQuat(rotation), EActivation::Activate);
}

glm::mat4 JoltPhysicsManager::getBodyTransform(uint32_t bodyID) const
{
	if (!m_physicsSystem || bodyID == 0)
		return glm::mat4(1.0f);

	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	BodyID         joltID        = toJoltBodyID(bodyID);

	RVec3 pos = bodyInterface.GetPosition(joltID);
	Quat  rot = bodyInterface.GetRotation(joltID);

	glm::vec3 position = glm::vec3(static_cast<float>(pos.GetX()), static_cast<float>(pos.GetY()), static_cast<float>(pos.GetZ()));
	glm::quat rotation = toGlmQuat(rot);

	return glm::translate(glm::mat4(1.0f), position) * glm::toMat4(rotation);
}

glm::vec3 JoltPhysicsManager::getBodyPosition(uint32_t bodyID) const
{
	if (!m_physicsSystem || bodyID == 0)
		return glm::vec3(0.0f);

	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	RVec3          pos           = bodyInterface.GetPosition(toJoltBodyID(bodyID));

	return glm::vec3(static_cast<float>(pos.GetX()), static_cast<float>(pos.GetY()), static_cast<float>(pos.GetZ()));
}

glm::quat JoltPhysicsManager::getBodyRotation(uint32_t bodyID) const
{
	if (!m_physicsSystem || bodyID == 0)
		return glm::quat(1, 0, 0, 0);

	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	return toGlmQuat(bodyInterface.GetRotation(toJoltBodyID(bodyID)));
}

glm::vec3 JoltPhysicsManager::getLinearVelocity(uint32_t bodyID) const
{
	if (!m_physicsSystem || bodyID == 0)
		return glm::vec3(0.0f);

	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	return toGlmVec3(bodyInterface.GetLinearVelocity(toJoltBodyID(bodyID)));
}

glm::vec3 JoltPhysicsManager::getAngularVelocity(uint32_t bodyID) const
{
	if (!m_physicsSystem || bodyID == 0)
		return glm::vec3(0.0f);

	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	return toGlmVec3(bodyInterface.GetAngularVelocity(toJoltBodyID(bodyID)));
}

void JoltPhysicsManager::setLinearVelocity(uint32_t bodyID, const glm::vec3& velocity)
{
	if (!m_physicsSystem || bodyID == 0)
		return;

	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	bodyInterface.SetLinearVelocity(toJoltBodyID(bodyID), toJoltVec3(velocity));
}

void JoltPhysicsManager::setAngularVelocity(uint32_t bodyID, const glm::vec3& angularVelocity)
{
	if (!m_physicsSystem || bodyID == 0)
		return;

	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	bodyInterface.SetAngularVelocity(toJoltBodyID(bodyID), toJoltVec3(angularVelocity));
}

void JoltPhysicsManager::addForce(uint32_t bodyID, const glm::vec3& force)
{
	if (!m_physicsSystem || bodyID == 0)
		return;

	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	bodyInterface.AddForce(toJoltBodyID(bodyID), toJoltVec3(force));
}

void JoltPhysicsManager::addImpulse(uint32_t bodyID, const glm::vec3& impulse)
{
	if (!m_physicsSystem || bodyID == 0)
		return;

	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	bodyInterface.AddImpulse(toJoltBodyID(bodyID), toJoltVec3(impulse));
}

void JoltPhysicsManager::setGravity(const glm::vec3& gravity)
{
	if (m_physicsSystem)
	{
		m_physicsSystem->SetGravity(toJoltVec3(gravity));
		m_settings.gravity = gravity;
	}
}

glm::vec3 JoltPhysicsManager::getGravity() const
{
	if (m_physicsSystem)
	{
		return toGlmVec3(m_physicsSystem->GetGravity());
	}
	return m_settings.gravity;
}

void JoltPhysicsManager::registerEntityBody(EntityID entity, uint32_t bodyID)
{
	m_bodyToEntity[bodyID] = entity;
	m_entityToBody[entity] = bodyID;
}

void JoltPhysicsManager::unregisterEntity(EntityID entity)
{
	auto it = m_entityToBody.find(entity);
	if (it != m_entityToBody.end())
	{
		m_bodyToEntity.erase(it->second);
		m_entityToBody.erase(it);
	}
}

EntityID JoltPhysicsManager::getEntityFromBody(uint32_t bodyID) const
{
	auto it = m_bodyToEntity.find(bodyID);
	return (it != m_bodyToEntity.end()) ? it->second : NULL_ENTITY;
}

JPH::BodyInterface* JoltPhysicsManager::getBodyInterface()
{
	return m_physicsSystem ? &m_physicsSystem->GetBodyInterface() : nullptr;
}

JPH::BodyID JoltPhysicsManager::toJoltBodyID(uint32_t bodyID) const
{
	return BodyID(bodyID);
}

} // namespace physics
} // namespace agni

