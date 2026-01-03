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

// Entity ID type for future ECS integration
using EntityID = uint32_t;
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
