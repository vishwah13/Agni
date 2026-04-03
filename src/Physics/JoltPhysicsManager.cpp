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
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Physics/AgniContactListener.hpp>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Body/BodyLock.h>

#ifdef JPH_DEBUG_RENDERER
#include <Physics/JoltDebugRenderer.hpp>
#include <Jolt/Physics/Body/BodyManager.h>
#endif

// Free function for Jolt trace callback (GCC cannot convert variadic lambdas to function pointers)
static void JoltTraceNoop(const char*, ...) {}

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include <Debug.hpp>

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

// Helper: Create a collision shape from ColliderComponent, applying center offset and scale
static RefConst<Shape> createCollisionShape(ColliderType            type,
                                            const ColliderComponent& collider,
                                            const glm::vec3&        scale = glm::vec3(1.0f))
{
	// Create base shape with scale applied to dimensions
	RefConst<Shape> baseShape;
	switch (type)
	{
	case ColliderType::Box:
		baseShape = new BoxShape(toJoltVec3(collider.boxHalfExtents * scale));
		break;
	case ColliderType::Sphere:
	{
		// Use max axis scale for uniform sphere scaling
		float uniformScale = std::max({scale.x, scale.y, scale.z});
		baseShape = new SphereShape(collider.sphereRadius * uniformScale);
		break;
	}
	case ColliderType::Capsule:
	{
		float radiusScale = std::max(scale.x, scale.z);
		baseShape = new CapsuleShape(collider.capsuleHalfHeight * scale.y,
		                             collider.capsuleRadius * radiusScale);
		break;
	}
	default:
		return nullptr;
	}

	// Wrap with center offset if non-zero
	if (collider.center.x != 0.0f || collider.center.y != 0.0f || collider.center.z != 0.0f)
	{
		RotatedTranslatedShapeSettings offsetSettings(
		    toJoltVec3(collider.center * scale),
		    Quat::sIdentity(),
		    baseShape);
		auto result = offsetSettings.Create();
		if (result.IsValid())
			return result.Get();
	}

	return baseShape;
}

struct JoltPhysicsManager::CharacterStorage
{
	std::unordered_map<uint64_t, Ref<CharacterVirtual>> characters;
	uint64_t nextHandle = 1;
};

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
	Trace = JoltTraceNoop;
	JPH_IF_ENABLE_ASSERTS(AssertFailed = [](const char* inExpression, const char* inMessage, const char* inFile, uint inLine) -> bool {
		AGNI_PRINT("[Jolt Assert] {} at {}:{}\n  {}\n", inExpression, inFile, inLine, inMessage ? inMessage : "");
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

	m_characters = std::make_unique<CharacterStorage>();

	// Register contact listener for collision events
	m_contactListener = std::make_unique<AgniContactListener>();
	m_contactListener->setEntityLookup([this](uint32_t bodyID) {
		return getEntityFromBody(bodyID);
	});
	m_physicsSystem->SetContactListener(m_contactListener.get());

	AGNI_PRINT("[JoltPhysicsManager] Initialized successfully\n");
	AGNI_PRINT("  Max bodies: {}\n", settings.maxBodies);
	AGNI_PRINT("  Gravity: ({:.2f}, {:.2f}, {:.2f})\n", settings.gravity.x, settings.gravity.y, settings.gravity.z);

#ifdef JPH_DEBUG_RENDERER
	m_debugRenderer = std::make_unique<JoltDebugRenderer>();
	DebugRenderer::sInstance = m_debugRenderer.get();
	AGNI_PRINT("[JoltPhysicsManager] Debug renderer created\n");
#endif

	return true;
}

void JoltPhysicsManager::shutdown()
{
	if (m_physicsSystem)
	{
#ifdef JPH_DEBUG_RENDERER
		// DebugRenderer destructor asserts sInstance == this, then clears it
		m_debugRenderer.reset();
#endif
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

		AGNI_PRINT("[JoltPhysicsManager] Shutdown complete\n");
	}
}

void JoltPhysicsManager::update(float deltaTime)
{
	if (!m_physicsSystem)
		return;

	const float fixedTimestep = 1.0f / 60.0f;

	// Accumulate frame time
	m_accumulator += deltaTime;

	// Cap to prevent spiral of death (e.g., after breakpoint or long hitch)
	if (m_accumulator > 0.1f)
		m_accumulator = 0.1f;

	// Step physics in fixed increments
	while (m_accumulator >= fixedTimestep)
	{
		m_physicsSystem->Update(
		    fixedTimestep,
		    m_settings.collisionSteps,
		    m_tempAllocator.get(),
		    m_jobSystem.get());
		m_accumulator -= fixedTimestep;
	}
}

uint32_t JoltPhysicsManager::createDynamicBody(const glm::vec3&        pos,
                                               const glm::quat&        rot,
                                               ColliderType            type,
                                               const ColliderComponent& collider,
                                               float                   mass,
                                               float                   friction,
                                               float                   restitution,
                                               bool                    useGravity,
                                               const glm::vec3&        scale)
{
	if (!m_physicsSystem)
		return 0;

	RefConst<Shape> shape = createCollisionShape(type, collider, scale);
	if (!shape)
	{
		AGNI_PRINT("[JoltPhysicsManager] Unsupported collider type\n");
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

	// Respect useGravity flag
	bodySettings.mGravityFactor = useGravity ? 1.0f : 0.0f;
	bodySettings.mIsSensor      = collider.isTrigger;

	// Create and add body to physics system
	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	BodyID         bodyID        = bodyInterface.CreateAndAddBody(bodySettings, EActivation::Activate);

	if (bodyID.IsInvalid())
	{
		AGNI_PRINT("[JoltPhysicsManager] Failed to create dynamic body\n");
		return 0;
	}

	return bodyID.GetIndexAndSequenceNumber();
}

uint32_t JoltPhysicsManager::createStaticBody(const glm::vec3&        pos,
                                              const glm::quat&        rot,
                                              ColliderType            type,
                                              const ColliderComponent& collider,
                                              float                   friction,
                                              float                   restitution,
                                              const glm::vec3&        scale)
{
	if (!m_physicsSystem)
		return 0;

	RefConst<Shape> shape = createCollisionShape(type, collider, scale);
	if (!shape)
		return 0;

	// Create static body settings
	BodyCreationSettings bodySettings(
	    shape,
	    RVec3(pos.x, pos.y, pos.z),
	    toJoltQuat(rot),
	    EMotionType::Static,
	    Layers::NON_MOVING);

	bodySettings.mFriction    = friction;
	bodySettings.mRestitution = restitution;
	bodySettings.mIsSensor    = collider.isTrigger;

	// Create and add body
	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	BodyID         bodyID        = bodyInterface.CreateAndAddBody(bodySettings, EActivation::DontActivate);

	return bodyID.GetIndexAndSequenceNumber();
}

uint32_t JoltPhysicsManager::createKinematicBody(const glm::vec3&        pos,
                                                 const glm::quat&        rot,
                                                 ColliderType            type,
                                                 const ColliderComponent& collider,
                                                 const glm::vec3&        scale)
{
	if (!m_physicsSystem)
		return 0;

	RefConst<Shape> shape = createCollisionShape(type, collider, scale);
	if (!shape)
		return 0;

	// Create kinematic body settings
	BodyCreationSettings bodySettings(
	    shape,
	    RVec3(pos.x, pos.y, pos.z),
	    toJoltQuat(rot),
	    EMotionType::Kinematic,
	    Layers::MOVING);

	bodySettings.mIsSensor = collider.isTrigger;

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

void JoltPhysicsManager::removeAllBodies()
{
	if (!m_physicsSystem)
		return;

	BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();

	for (auto& [bodyID, entityID] : m_bodyToEntity)
	{
		if (bodyID == 0) continue;
		BodyID joltID = toJoltBodyID(bodyID);
		bodyInterface.RemoveBody(joltID);
		bodyInterface.DestroyBody(joltID);
	}

	m_bodyToEntity.clear();
	m_entityToBody.clear();

	AGNI_PRINT("[JoltPhysics] Removed all bodies\n");
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

void JoltPhysicsManager::optimizeBroadPhase()
{
	if (m_physicsSystem)
		m_physicsSystem->OptimizeBroadPhase();
}

#ifdef JPH_DEBUG_RENDERER
void JoltPhysicsManager::drawDebug(const glm::vec3& cameraPos, const PhysicsDebugSettings& settings)
{
	if (!m_physicsSystem || !m_debugRenderer)
		return;

	m_debugRenderer->beginFrame(cameraPos);

	// Map our settings to Jolt's DrawSettings
	BodyManager::DrawSettings drawSettings;
	drawSettings.mDrawShape              = settings.drawShapes;
	drawSettings.mDrawShapeWireframe     = true; // Always wireframe (we only render lines)
	drawSettings.mDrawBoundingBox        = settings.drawBoundingBox;
	drawSettings.mDrawVelocity           = settings.drawVelocity;
	drawSettings.mDrawCenterOfMassTransform = settings.drawCenterOfMass;
	drawSettings.mDrawShapeColor         = BodyManager::EShapeColor::MotionTypeColor;

	m_physicsSystem->DrawBodies(drawSettings, m_debugRenderer.get());
}

void JoltPhysicsManager::drawDebugFromECS(
    const glm::vec3& cameraPos,
    const std::vector<std::tuple<TransformComponent, ColliderComponent, RigidBodyComponent>>& entities)
{
	if (!m_debugRenderer)
		return;

	m_debugRenderer->beginFrame(cameraPos);

	for (const auto& [transform, collider, rigidbody] : entities)
	{
		m_debugRenderer->drawColliderShape(transform, collider, rigidbody);
	}
}
#endif

bool JoltPhysicsManager::raycast(const glm::vec3& origin, const glm::vec3& direction,
                                  float maxDistance, RaycastHit& outHit) const
{
	if (!m_physicsSystem)
		return false;

	// Jolt ray: origin + direction*maxDistance (direction encodes length)
	RRayCast ray(RVec3(origin.x, origin.y, origin.z),
	             Vec3(direction.x, direction.y, direction.z) * maxDistance);

	ClosestHitCollisionCollector<CastRayCollector> collector;
	m_physicsSystem->GetNarrowPhaseQuery().CastRay(ray, RayCastSettings(), collector);

	if (!collector.HadHit())
		return false;

	const RayCastResult& result = collector.mHit;
	outHit.fraction = result.mFraction;
	outHit.bodyID   = result.mBodyID.GetIndexAndSequenceNumber();
	outHit.entity   = getEntityFromBody(outHit.bodyID);

	RVec3 hitPos    = ray.GetPointOnRay(result.mFraction);
	outHit.position = glm::vec3(static_cast<float>(hitPos.GetX()),
	                            static_cast<float>(hitPos.GetY()),
	                            static_cast<float>(hitPos.GetZ()));

	// Surface normal via body lock
	BodyLockRead lock(m_physicsSystem->GetBodyLockInterface(), result.mBodyID);
	if (lock.Succeeded())
	{
		Vec3 normal = lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, hitPos);
		outHit.normal = glm::vec3(normal.GetX(), normal.GetY(), normal.GetZ());
	}

	return true;
}

bool JoltPhysicsManager::raycastAll(const glm::vec3& origin, const glm::vec3& direction,
                                     float maxDistance, std::vector<RaycastHit>& outHits) const
{
	if (!m_physicsSystem)
		return false;

	RRayCast ray(RVec3(origin.x, origin.y, origin.z),
	             Vec3(direction.x, direction.y, direction.z) * maxDistance);

	AllHitCollisionCollector<CastRayCollector> collector;
	m_physicsSystem->GetNarrowPhaseQuery().CastRay(ray, RayCastSettings(), collector);

	if (!collector.HadHit())
		return false;

	collector.Sort();
	outHits.reserve(collector.mHits.size());

	for (const RayCastResult& result : collector.mHits)
	{
		RaycastHit hit {};
		hit.fraction = result.mFraction;
		hit.bodyID   = result.mBodyID.GetIndexAndSequenceNumber();
		hit.entity   = getEntityFromBody(hit.bodyID);

		RVec3 hitPos = ray.GetPointOnRay(result.mFraction);
		hit.position = glm::vec3(static_cast<float>(hitPos.GetX()),
		                         static_cast<float>(hitPos.GetY()),
		                         static_cast<float>(hitPos.GetZ()));

		BodyLockRead lock(m_physicsSystem->GetBodyLockInterface(), result.mBodyID);
		if (lock.Succeeded())
		{
			Vec3 normal = lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, hitPos);
			hit.normal = glm::vec3(normal.GetX(), normal.GetY(), normal.GetZ());
		}

		outHits.push_back(hit);
	}

	return true;
}

void JoltPhysicsManager::screenToWorldRay(const glm::mat4& invViewProj,
                                           const glm::vec2& screenPos,
                                           const glm::vec2& viewportSize,
                                           glm::vec3& outOrigin,
                                           glm::vec3& outDirection)
{
	// Screen to NDC [-1, 1]
	float ndcX = (2.0f * screenPos.x / viewportSize.x) - 1.0f;
	float ndcY = 1.0f - (2.0f * screenPos.y / viewportSize.y); // Flip Y

	// Unproject near and far points
	glm::vec4 nearNDC(ndcX, ndcY, 0.0f, 1.0f); // Near plane (reversed-Z: 1.0 is near)
	glm::vec4 farNDC(ndcX, ndcY, 1.0f, 1.0f);  // Far plane (reversed-Z: 0.0 is far)

	glm::vec4 nearWorld = invViewProj * nearNDC;
	glm::vec4 farWorld  = invViewProj * farNDC;

	nearWorld /= nearWorld.w;
	farWorld  /= farWorld.w;

	outOrigin    = glm::vec3(nearWorld);
	outDirection = glm::normalize(glm::vec3(farWorld) - glm::vec3(nearWorld));
}

uint64_t JoltPhysicsManager::createCharacterController(const glm::vec3& pos,
                                                        const CharacterControllerComponent& settings)
{
	if (!m_physicsSystem || !m_characters)
		return 0;

	// Create capsule shape — half height is (total height - 2*radius) / 2
	float halfHeight = std::max(0.0f, (settings.height - 2.0f * settings.radius) * 0.5f);

	CharacterVirtualSettings charSettings;
	charSettings.mShape = new CapsuleShape(halfHeight, settings.radius);
	charSettings.mMass  = settings.mass;
	charSettings.mMaxSlopeAngle = glm::radians(settings.maxSlopeAngle);
	charSettings.mMaxStrength   = 100.0f;
	charSettings.mPenetrationRecoverySpeed = 1.0f;

	Ref<CharacterVirtual> character = new CharacterVirtual(
	    &charSettings,
	    RVec3(pos.x, pos.y, pos.z),
	    Quat::sIdentity(),
	    0,
	    m_physicsSystem.get());

	uint64_t handle = m_characters->nextHandle++;
	m_characters->characters[handle] = character;

	AGNI_PRINT("[JoltPhysicsManager] Created character controller (handle: {})\n", handle);
	return handle;
}

void JoltPhysicsManager::updateCharacterController(uint64_t handle, float deltaTime,
                                                    const glm::vec3& inputDir, float maxSpeed,
                                                    bool jump, float jumpSpeed)
{
	if (!m_physicsSystem || !m_characters)
		return;

	auto it = m_characters->characters.find(handle);
	if (it == m_characters->characters.end())
		return;

	CharacterVirtual* character = it->second.GetPtr();

	Vec3 up = character->GetUp();
	Vec3 gravity = m_physicsSystem->GetGravity();

	// Current velocity
	Vec3 currentVelocity = character->GetLinearVelocity();
	Vec3 verticalVelocity = currentVelocity.Dot(up) * up;

	// Ground velocity (for moving platforms)
	Vec3 groundVelocity(0, 0, 0);
	if (character->GetGroundState() == CharacterVirtual::EGroundState::OnGround)
		groundVelocity = character->GetGroundVelocity();

	Vec3 newVelocity;
	if (character->GetGroundState() == CharacterVirtual::EGroundState::OnGround)
	{
		newVelocity = groundVelocity;
		if (jump)
			newVelocity += up * jumpSpeed;
	}
	else
	{
		// In air — preserve vertical velocity + apply gravity
		newVelocity = verticalVelocity + gravity * deltaTime;
	}

	// Add horizontal input
	Vec3 horizontalInput(inputDir.x, 0.0f, inputDir.z);
	float inputLength = horizontalInput.Length();
	if (inputLength > 1.0f)
		horizontalInput /= inputLength;
	newVelocity += horizontalInput * maxSpeed;

	character->SetLinearVelocity(newVelocity);

	// Extended update with stair stepping
	CharacterVirtual::ExtendedUpdateSettings updateSettings;
	updateSettings.mStickToFloorStepDown = Vec3(0, -0.5f, 0);
	updateSettings.mWalkStairsStepUp     = Vec3(0, 0.4f, 0);

	character->ExtendedUpdate(
	    deltaTime,
	    gravity,
	    updateSettings,
	    m_physicsSystem->GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
	    m_physicsSystem->GetDefaultLayerFilter(Layers::MOVING),
	    {},
	    {},
	    *m_tempAllocator);
}

glm::vec3 JoltPhysicsManager::getCharacterPosition(uint64_t handle) const
{
	if (!m_characters) return glm::vec3(0.0f);
	auto it = m_characters->characters.find(handle);
	if (it == m_characters->characters.end()) return glm::vec3(0.0f);
	RVec3 pos = it->second->GetPosition();
	return glm::vec3(static_cast<float>(pos.GetX()),
	                 static_cast<float>(pos.GetY()),
	                 static_cast<float>(pos.GetZ()));
}

glm::vec3 JoltPhysicsManager::getCharacterVelocity(uint64_t handle) const
{
	if (!m_characters) return glm::vec3(0.0f);
	auto it = m_characters->characters.find(handle);
	if (it == m_characters->characters.end()) return glm::vec3(0.0f);
	Vec3 vel = it->second->GetLinearVelocity();
	return glm::vec3(vel.GetX(), vel.GetY(), vel.GetZ());
}

bool JoltPhysicsManager::isCharacterOnGround(uint64_t handle) const
{
	if (!m_characters) return false;
	auto it = m_characters->characters.find(handle);
	if (it == m_characters->characters.end()) return false;
	return it->second->GetGroundState() == CharacterVirtual::EGroundState::OnGround;
}

void JoltPhysicsManager::destroyCharacterController(uint64_t handle)
{
	if (!m_characters) return;
	m_characters->characters.erase(handle);
}

void JoltPhysicsManager::destroyAllCharacterControllers()
{
	if (!m_characters) return;
	m_characters->characters.clear();
}

std::vector<CollisionEvent> JoltPhysicsManager::drainCollisionEvents()
{
	if (m_contactListener)
		return m_contactListener->drainEvents();
	return {};
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

