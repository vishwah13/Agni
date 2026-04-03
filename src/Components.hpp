#pragma once

#include <Types.hpp>
#include <Texture.hpp>
#include <Reflection/TypeDesc.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <Material.hpp>

// Forward declarations
struct GeoSurface;
struct MaterialInstance;

// Entity ID type - compatible with Flecs entity IDs
using EntityID = uint64_t;
constexpr EntityID NULL_ENTITY = 0;

// ============================================================================
// Transform Component
// ============================================================================

struct TransformComponent
{
	glm::mat4 localTransform {1.0f};
	glm::mat4 worldTransform {1.0f};

	// For hierarchy (future ECS will use this instead of pointers)
	EntityID parent {NULL_ENTITY};

	static void ReflectType(agni::TypeDesc<TransformComponent>& desc)
	{
		desc.SetName("TransformComponent");
		desc.SetCategory("Core");
		// Transform is edited via gizmos, not raw fields. Show read-only for debugging.
		desc.AddMember(&TransformComponent::localTransform, "localTransform", "Local Transform").SetHidden();
		desc.AddMember(&TransformComponent::worldTransform, "worldTransform", "World Transform").SetReadOnly().SetNoSerialize();
		desc.AddMember(&TransformComponent::parent, "parent", "Parent").SetReadOnly().SetNoSerialize();
	}
};

// ============================================================================
// Camera Component
// ============================================================================

struct CameraComponent
{
	glm::vec3 position {0.0f, 0.0f, 0.0f};
	glm::vec3 velocity {0.0f, 0.0f, 0.0f};

	float pitch {0.0f};
	float yaw {0.0f};

	float fov      {70.0f};   // Vertical FOV in degrees
	float nearPlane {0.1f};
	float farPlane  {10000.0f};

	float speed {35.0f};
	float mouseSensitivity {1.0f};

	// Vulkan projection: reversed-Z, Y-flipped
	glm::mat4 buildProjection(VkExtent2D extent) const
	{
		float aspect = (float)extent.width / (float)extent.height;
		glm::mat4 proj = glm::perspective(glm::radians(fov), aspect, farPlane, nearPlane);
		proj[1][1] *= -1;
		return proj;
	}

	// OpenGL-style projection (for ImGuizmo): standard near/far, no Y-flip
	glm::mat4 buildProjectionOpenGL(VkExtent2D extent) const
	{
		float aspect = (float)extent.width / (float)extent.height;
		return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
	}

	static void ReflectType(agni::TypeDesc<CameraComponent>& desc)
	{
		desc.SetName("CameraComponent");
		desc.SetCategory("Rendering");
		desc.AddMember(&CameraComponent::position, "position", "Position");
		desc.AddMember(&CameraComponent::velocity, "velocity", "Velocity").SetReadOnly().SetNoSerialize();
		desc.AddMember(&CameraComponent::pitch, "pitch", "Pitch").SetRange(-89.0f, 89.0f).SetUnit("deg");
		desc.AddMember(&CameraComponent::yaw, "yaw", "Yaw").SetRange(-360.0f, 360.0f).SetUnit("deg");
		desc.AddMember(&CameraComponent::fov, "fov", "FOV").SetRange(1.0f, 179.0f).SetUnit("deg");
		desc.AddMember(&CameraComponent::nearPlane, "nearPlane", "Near Plane").SetRange(0.001f, 1000.0f).SetUnit("m");
		desc.AddMember(&CameraComponent::farPlane, "farPlane", "Far Plane").SetRange(1.0f, 100000.0f).SetUnit("m");
		desc.AddMember(&CameraComponent::speed, "speed", "Speed").SetRange(0.1f, 500.0f).SetUnit("m/s");
		desc.AddMember(&CameraComponent::mouseSensitivity, "mouseSensitivity", "Mouse Sensitivity").SetRange(0.01f, 10.0f);
	}
};

// ============================================================================
// Mesh Component
// ============================================================================

struct MeshComponent
{
	GPUMeshBuffers          meshBuffers;
	std::vector<GeoSurface> surfaces;
	Bounds                  bounds;
	std::string             name;
};

// ============================================================================
// Material Component
// ============================================================================

struct MaterialComponent
{
	MaterialInstance* materialInstance {nullptr};
	MaterialPass      passType = MaterialPass::MainColor;
};

// ============================================================================
// Skybox Component
// ============================================================================

struct SkyboxComponent
{
	GPUMeshBuffers meshBuffers;
	Texture        cubemapTexture;
	uint32_t       indexCount {0};
	uint32_t       firstIndex {0};
};

// ============================================================================
// Light Component
// ============================================================================

enum class LightType : uint8_t
{
	Point,
	Directional,
	Spot,
	// Future: Area
};

struct LightComponent
{
	LightType type {LightType::Point};
	glm::vec3 color {1.0f, 1.0f, 1.0f};
	float     intensity {1.0f};
	float     radius {10.0f};                // Attenuation radius for point/spot lights
	glm::vec3 direction {0.0f, -1.0f, 0.0f}; // Direction for directional/spot lights
	float     innerConeAngle {12.5f};        // Inner cone angle in degrees (spot lights)
	float     outerConeAngle {17.5f};        // Outer cone angle in degrees (spot lights)

	static void ReflectType(agni::TypeDesc<LightComponent>& desc)
	{
		static agni::EnumDesc<LightType> lightTypeEnum;
		if (lightTypeEnum.constants.empty())
		{
			lightTypeEnum.name = "LightType";
			lightTypeEnum.Add(LightType::Point, "Point")
			             .Add(LightType::Directional, "Directional")
			             .Add(LightType::Spot, "Spot");
		}

		desc.SetName("LightComponent");
		desc.SetCategory("Rendering");
		desc.AddMember(&LightComponent::type, "type", "Light Type").SetEnum(&lightTypeEnum);
		desc.AddMember(&LightComponent::color, "color", "Color").SetAsColor();
		desc.AddMember(&LightComponent::intensity, "intensity", "Intensity").SetRange(0.0f, 100.0f);
		desc.AddMember(&LightComponent::radius, "radius", "Radius", "Attenuation radius").SetRange(0.0f, 200.0f).SetUnit("m");
		desc.AddMember(&LightComponent::direction, "direction", "Direction");
		desc.AddMember(&LightComponent::innerConeAngle, "innerConeAngle", "Inner Cone Angle").SetRange(0.0f, 90.0f).SetUnit("deg");
		desc.AddMember(&LightComponent::outerConeAngle, "outerConeAngle", "Outer Cone Angle").SetRange(0.0f, 90.0f).SetUnit("deg");
	}
};

// ============================================================================
// Renderable Tag (for ECS queries)
// ============================================================================

struct RenderableTag
{
	bool visible {true};

	static void ReflectType(agni::TypeDesc<RenderableTag>& desc)
	{
		desc.SetName("RenderableTag");
		desc.SetCategory("Rendering");
		desc.AddMember(&RenderableTag::visible, "visible", "Visible");
	}
};

// ============================================================================
// Physics Components (Jolt)
// ============================================================================

enum class RigidBodyType : uint8_t
{
	Static,    // Immovable (terrain, walls)
	Dynamic,   // Fully simulated (boxes, spheres)
	Kinematic  // Animation-driven
};

struct RigidBodyComponent
{
	RigidBodyType type {RigidBodyType::Dynamic};
	float         mass {1.0f};
	float         friction {0.5f};
	float         restitution {0.0f}; // Bounciness (0 = no bounce, 1 = perfect bounce)
	bool          useGravity {true};

	// Velocity (synced from Jolt after simulation)
	glm::vec3 linearVelocity {0.0f};
	glm::vec3 angularVelocity {0.0f};

	// Jolt BodyID (0 = invalid)
	uint32_t joltBodyID {0};

	static void ReflectType(agni::TypeDesc<RigidBodyComponent>& desc)
	{
		static agni::EnumDesc<RigidBodyType> bodyTypeEnum;
		if (bodyTypeEnum.constants.empty())
		{
			bodyTypeEnum.name = "RigidBodyType";
			bodyTypeEnum.Add(RigidBodyType::Static, "Static")
			            .Add(RigidBodyType::Dynamic, "Dynamic")
			            .Add(RigidBodyType::Kinematic, "Kinematic");
		}

		desc.SetName("RigidBodyComponent");
		desc.SetCategory("Physics");
		desc.AddMember(&RigidBodyComponent::type, "type", "Body Type").SetEnum(&bodyTypeEnum);
		desc.AddMember(&RigidBodyComponent::mass, "mass", "Mass").SetRange(0.0f, 10000.0f).SetUnit("kg");
		desc.AddMember(&RigidBodyComponent::friction, "friction", "Friction").SetRange(0.0f, 1.0f);
		desc.AddMember(&RigidBodyComponent::restitution, "restitution", "Restitution", "Bounciness (0=none, 1=perfect)").SetRange(0.0f, 1.0f);
		desc.AddMember(&RigidBodyComponent::useGravity, "useGravity", "Use Gravity");
		desc.AddMember(&RigidBodyComponent::linearVelocity, "linearVelocity", "Linear Velocity").SetReadOnly().SetNoSerialize();
		desc.AddMember(&RigidBodyComponent::angularVelocity, "angularVelocity", "Angular Velocity").SetReadOnly().SetNoSerialize();
		desc.AddMember(&RigidBodyComponent::joltBodyID, "joltBodyID", "Jolt Body ID").SetReadOnly().SetHidden().SetNoSerialize();
	}
};

enum class ColliderType : uint8_t
{
	Box,
	Sphere,
	Capsule
};

struct ColliderComponent
{
	ColliderType type {ColliderType::Box};

	// Shape parameters (use based on type)
	glm::vec3 boxHalfExtents {0.5f, 0.5f, 0.5f};
	float     sphereRadius {0.5f};
	float     capsuleRadius {0.5f};
	float     capsuleHalfHeight {1.0f};

	// Local offset from entity transform
	glm::vec3 center {0.0f};

	// Collision flags
	bool isTrigger {false};

	static void ReflectType(agni::TypeDesc<ColliderComponent>& desc)
	{
		static agni::EnumDesc<ColliderType> colliderTypeEnum;
		if (colliderTypeEnum.constants.empty())
		{
			colliderTypeEnum.name = "ColliderType";
			colliderTypeEnum.Add(ColliderType::Box, "Box")
			                .Add(ColliderType::Sphere, "Sphere")
			                .Add(ColliderType::Capsule, "Capsule");
		}

		desc.SetName("ColliderComponent");
		desc.SetCategory("Physics");
		desc.AddMember(&ColliderComponent::type, "type", "Collider Type").SetEnum(&colliderTypeEnum);
		desc.AddMember(&ColliderComponent::boxHalfExtents, "boxHalfExtents", "Box Half Extents");
		desc.AddMember(&ColliderComponent::sphereRadius, "sphereRadius", "Sphere Radius").SetRange(0.01f, 50.0f).SetUnit("m");
		desc.AddMember(&ColliderComponent::capsuleRadius, "capsuleRadius", "Capsule Radius").SetRange(0.01f, 50.0f).SetUnit("m");
		desc.AddMember(&ColliderComponent::capsuleHalfHeight, "capsuleHalfHeight", "Capsule Half Height").SetRange(0.01f, 50.0f).SetUnit("m");
		desc.AddMember(&ColliderComponent::center, "center", "Center Offset");
		desc.AddMember(&ColliderComponent::isTrigger, "isTrigger", "Is Trigger");
	}
};

// Tag for entities participating in physics simulation
struct PhysicsEnabledTag {};

// ============================================================================
// Character Controller Component (Jolt CharacterVirtual)
// ============================================================================

struct CharacterControllerComponent
{
	// Settings (editable in inspector, serialized)
	float height        {1.8f};     // Capsule total height (m)
	float radius        {0.3f};     // Capsule radius (m)
	float mass          {70.0f};    // Character mass (kg)
	float maxSlopeAngle {50.0f};    // Steepest climbable slope (degrees)
	float maxSpeed      {5.0f};     // Walk speed (m/s)
	float jumpSpeed     {6.0f};     // Jump impulse (m/s)
	float stairStepUp   {0.4f};     // Max stair step height (m)

	// Runtime state (not serialized, managed by CharacterSystem)
	uint64_t  characterHandle {0};
	bool      onGround        {false};
	bool      wantsJump       {false};
	glm::vec3 inputDirection  {0.0f}; // Set by game systems each frame

	static void ReflectType(agni::TypeDesc<CharacterControllerComponent>& desc)
	{
		desc.SetName("CharacterControllerComponent");
		desc.SetCategory("Physics");
		desc.AddMember(&CharacterControllerComponent::height, "height", "Height").SetRange(0.5f, 5.0f).SetUnit("m");
		desc.AddMember(&CharacterControllerComponent::radius, "radius", "Radius").SetRange(0.1f, 2.0f).SetUnit("m");
		desc.AddMember(&CharacterControllerComponent::mass, "mass", "Mass").SetRange(1.0f, 500.0f).SetUnit("kg");
		desc.AddMember(&CharacterControllerComponent::maxSlopeAngle, "maxSlopeAngle", "Max Slope Angle").SetRange(0.0f, 89.0f).SetUnit("deg");
		desc.AddMember(&CharacterControllerComponent::maxSpeed, "maxSpeed", "Max Speed").SetRange(0.1f, 50.0f).SetUnit("m/s");
		desc.AddMember(&CharacterControllerComponent::jumpSpeed, "jumpSpeed", "Jump Speed").SetRange(0.0f, 20.0f).SetUnit("m/s");
		desc.AddMember(&CharacterControllerComponent::stairStepUp, "stairStepUp", "Stair Step Up").SetRange(0.0f, 1.0f).SetUnit("m");
		desc.AddMember(&CharacterControllerComponent::onGround, "onGround", "On Ground").SetReadOnly().SetHidden().SetNoSerialize();
		desc.AddMember(&CharacterControllerComponent::characterHandle, "characterHandle", "Handle").SetReadOnly().SetHidden().SetNoSerialize();
	}
};

// ============================================================================
// Asset Reference Component (for scene serialization)
// ============================================================================

struct AssetReferenceComponent
{
	std::string assetPath;  // Relative path to asset file (e.g., "assets/models/car.glb")
	std::string meshName;   // Mesh name within asset (e.g., "Wheel_FL")
	std::string assetType;  // "gltf", "primitive", or "procedural"

	static void ReflectType(agni::TypeDesc<AssetReferenceComponent>& desc)
	{
		desc.SetName("AssetReferenceComponent");
		desc.SetCategory("Assets");
		desc.AddMember(&AssetReferenceComponent::assetPath, "assetPath", "Asset Path");
		desc.AddMember(&AssetReferenceComponent::meshName, "meshName", "Mesh Name");
		desc.AddMember(&AssetReferenceComponent::assetType, "assetType", "Asset Type");
	}
};

// ============================================================================
// Entity Info Component (Unity-style display name + metadata)
// ============================================================================

struct EntityInfoComponent
{
	std::string displayName;         // User-visible name (can duplicate, like Unity)
	bool        isPrefabInstance {false};  // Whether instantiated from a prefab
	// Note: GUID is entity.id() - no need to store separately

	static void ReflectType(agni::TypeDesc<EntityInfoComponent>& desc)
	{
		desc.SetName("EntityInfoComponent");
		desc.SetCategory("Core");
		desc.AddMember(&EntityInfoComponent::displayName, "displayName", "Display Name");
		desc.AddMember(&EntityInfoComponent::isPrefabInstance, "isPrefabInstance", "Is Prefab Instance").SetReadOnly();
	}
};
