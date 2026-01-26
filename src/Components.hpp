#pragma once

#include <Types.hpp>
#include <Texture.hpp>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Forward declarations
struct GeoSurface;
struct MaterialInstance;
enum class MaterialPass : uint8_t;

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

	float speed {35.0f};
	float mouseSensitivity {1.0f};
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
	MaterialPass      passType;
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
};

// ============================================================================
// Renderable Tag (for ECS queries)
// ============================================================================

struct RenderableTag
{
	bool visible {true};
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
};

// Tag for entities participating in physics simulation
struct PhysicsEnabledTag {};

// ============================================================================
// Asset Reference Component (for scene serialization)
// ============================================================================

struct AssetReferenceComponent
{
	std::string assetPath;  // Relative path to asset file (e.g., "assets/models/car.glb")
	std::string meshName;   // Mesh name within asset (e.g., "Wheel_FL")
	std::string assetType;  // "gltf", "primitive", or "procedural"
};
