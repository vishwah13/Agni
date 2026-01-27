#include "EntityManager.hpp"
#include "EntityBuilder.hpp"
#include "World.hpp"

namespace agni::ecs
{

EntityManager::EntityManager(World& world)
    : m_world(world)
{
}

void EntityManager::setMeshProvider(MeshProvider provider)
{
	m_meshProvider = std::move(provider);
}

EntityBuilder EntityManager::create()
{
	return EntityBuilder(m_world, *this);
}

std::string EntityManager::getUniqueName(const std::string& baseName)
{
	uint32_t& counter = m_nameCounters[baseName];
	++counter;
	return baseName + "_" + std::to_string(counter);
}

void EntityManager::resetCounters()
{
	m_nameCounters.clear();
}

void EntityManager::resetCounter(const std::string& baseName)
{
	m_nameCounters.erase(baseName);
}

void EntityManager::registerPreset(const std::string& id, EntityPreset preset)
{
	m_presets[id] = std::move(preset);
}

const EntityPreset* EntityManager::getPreset(const std::string& id) const
{
	auto it = m_presets.find(id);
	return (it != m_presets.end()) ? &it->second : nullptr;
}

bool EntityManager::hasPreset(const std::string& id) const
{
	return m_presets.find(id) != m_presets.end();
}

void EntityManager::registerBuiltinPresets()
{
	// ========================================================================
	// Empty entity (no mesh, no physics)
	// ========================================================================
	registerPreset("Empty", EntityPreset{
		.baseName = "Empty",
		.meshName = "",
		.isPrimitive = false,
		.rigidBody = std::nullopt,
		.collider = std::nullopt,
		.light = std::nullopt,
		.assetRef = {.assetPath = "", .meshName = "", .assetType = ""}
	});

	// ========================================================================
	// Primitive Meshes (with physics)
	// ========================================================================

	registerPreset("Cube", EntityPreset{
		.baseName = "Cube",
		.meshName = "Cube",
		.isPrimitive = true,
		.rigidBody = RigidBodyComponent{.type = RigidBodyType::Dynamic, .mass = 1.0f},
		.collider = ColliderComponent{.type = ColliderType::Box, .boxHalfExtents = glm::vec3(0.5f)},
		.light = std::nullopt,
		.assetRef = {.assetPath = "", .meshName = "Cube", .assetType = "primitive"}
	});

	registerPreset("Sphere", EntityPreset{
		.baseName = "Sphere",
		.meshName = "Sphere",
		.isPrimitive = true,
		.rigidBody = RigidBodyComponent{.type = RigidBodyType::Dynamic, .mass = 1.0f},
		.collider = ColliderComponent{.type = ColliderType::Sphere, .sphereRadius = 0.5f},
		.light = std::nullopt,
		.assetRef = {.assetPath = "", .meshName = "Sphere", .assetType = "primitive"}
	});

	registerPreset("Plane", EntityPreset{
		.baseName = "Plane",
		.meshName = "Plane",
		.isPrimitive = true,
		.rigidBody = RigidBodyComponent{.type = RigidBodyType::Static},
		.collider = ColliderComponent{.type = ColliderType::Box, .boxHalfExtents = glm::vec3(1.0f, 0.1f, 1.0f)},
		.light = std::nullopt,
		.assetRef = {.assetPath = "", .meshName = "Plane", .assetType = "primitive"}
	});

	registerPreset("Suzanne", EntityPreset{
		.baseName = "Suzanne",
		.meshName = "Suzanne",
		.isPrimitive = true,
		.rigidBody = RigidBodyComponent{.type = RigidBodyType::Dynamic, .mass = 1.0f},
		.collider = ColliderComponent{.type = ColliderType::Sphere, .sphereRadius = 0.8f},
		.light = std::nullopt,
		.assetRef = {.assetPath = "", .meshName = "Suzanne", .assetType = "primitive"}
	});

	registerPreset("Cylinder", EntityPreset{
		.baseName = "Cylinder",
		.meshName = "Cylinder",
		.isPrimitive = true,
		.rigidBody = RigidBodyComponent{.type = RigidBodyType::Dynamic, .mass = 1.0f},
		.collider = ColliderComponent{.type = ColliderType::Capsule, .capsuleRadius = 0.5f, .capsuleHalfHeight = 1.0f},
		.light = std::nullopt,
		.assetRef = {.assetPath = "", .meshName = "Cylinder", .assetType = "primitive"}
	});

	registerPreset("Torus", EntityPreset{
		.baseName = "Torus",
		.meshName = "Torus",
		.isPrimitive = true,
		.rigidBody = RigidBodyComponent{.type = RigidBodyType::Dynamic, .mass = 1.0f},
		.collider = ColliderComponent{.type = ColliderType::Sphere, .sphereRadius = 1.0f},
		.light = std::nullopt,
		.assetRef = {.assetPath = "", .meshName = "Torus", .assetType = "primitive"}
	});

	registerPreset("Cone", EntityPreset{
		.baseName = "Cone",
		.meshName = "Cone",
		.isPrimitive = true,
		.rigidBody = RigidBodyComponent{.type = RigidBodyType::Dynamic, .mass = 1.0f},
		.collider = ColliderComponent{.type = ColliderType::Sphere, .sphereRadius = 0.7f},
		.light = std::nullopt,
		.assetRef = {.assetPath = "", .meshName = "Cone", .assetType = "primitive"}
	});

	// ========================================================================
	// Lights (no mesh by default)
	// ========================================================================

	registerPreset("PointLight", EntityPreset{
		.baseName = "PointLight",
		.meshName = "",
		.isPrimitive = false,
		.rigidBody = std::nullopt,
		.collider = std::nullopt,
		.light = LightComponent{
			.type = LightType::Point,
			.color = glm::vec3(1.0f),
			.intensity = 10.0f,
			.radius = 20.0f
		},
		.assetRef = {}
	});

	registerPreset("DirectionalLight", EntityPreset{
		.baseName = "DirectionalLight",
		.meshName = "",
		.isPrimitive = false,
		.rigidBody = std::nullopt,
		.collider = std::nullopt,
		.light = LightComponent{
			.type = LightType::Directional,
			.color = glm::vec3(1.0f),
			.intensity = 1.0f,
			.direction = glm::vec3(0.0f, -1.0f, 0.0f)
		},
		.assetRef = {}
	});

	registerPreset("SpotLight", EntityPreset{
		.baseName = "SpotLight",
		.meshName = "",
		.isPrimitive = false,
		.rigidBody = std::nullopt,
		.collider = std::nullopt,
		.light = LightComponent{
			.type = LightType::Spot,
			.color = glm::vec3(1.0f),
			.intensity = 10.0f,
			.radius = 20.0f,
			.direction = glm::vec3(0.0f, -1.0f, 0.0f),
			.innerConeAngle = 25.0f,
			.outerConeAngle = 35.0f
		},
		.assetRef = {}
	});
}

std::shared_ptr<MeshAsset> EntityManager::getMesh(const std::string& meshName) const
{
	if (m_meshProvider && !meshName.empty())
	{
		return m_meshProvider(meshName);
	}
	return nullptr;
}

} // namespace agni::ecs
