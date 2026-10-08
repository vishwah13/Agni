#pragma once
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <memory>
#include <vector>

// Forward declaration — MeshAsset is defined in Loader.hpp
struct MeshAsset;

// Scene node for transform hierarchy (used by glTF loader)
// No longer renders directly - ECS entities handle rendering
class Node
{
public:
	Node()                             = default;
	virtual ~Node()                    = default;
	Node(const Node& other)            = delete;
	Node(Node&& other)                 = delete;
	Node& operator=(const Node& other) = delete;
	Node& operator=(Node&& other)      = delete;

	// The Node class will hold the object matrix for the transforms. Both local
	// and world transform. The world transform needs to be updated, so whenever
	// the local Transform gets changed, refreshTransform must be called. This
	// will recursively go down the node tree and make sure the matrices are on
	// their correct places.
	void refreshTransform(const glm::mat4& parentMatrix);

	// Accessors
	glm::mat4&       getLocalTransform();
	const glm::mat4& getLocalTransform() const;

	glm::mat4&       getWorldTransform();
	const glm::mat4& getWorldTransform() const;

	std::vector<std::shared_ptr<Node>>&       getChildren();
	const std::vector<std::shared_ptr<Node>>& getChildren() const;

	std::weak_ptr<Node>&       getParent();
	const std::weak_ptr<Node>& getParent() const;


protected:
	glm::mat4 m_localTransform {1.0f};
	glm::mat4 m_worldTransform {1.0f};

	// m_parent pointer must be a weak pointer to avoid circular dependencies
	std::weak_ptr<Node>                m_parent;
	std::vector<std::shared_ptr<Node>> m_children;
};

// ============================================================================
// glTF loading intermediate representation
// ============================================================================
// The loader builds a Node tree so glTF scene structure survives import.
// These carry no rendering behaviour — consumers walk the tree and build
// whatever representation they want from it.

enum class LightType : uint8_t
{
	Point       = 0,
	Directional = 1,
	Spot        = 2
};

// Punctual light data carried by a glTF node (KHR_lights_punctual)
struct NodeLight
{
	LightType type {LightType::Point};
	glm::vec3 color {1.0f};
	float     intensity {1.0f};
	float     radius {10.0f};
	glm::vec3 direction {0.0f, 0.0f, -1.0f};
	float     innerConeAngle {0.0f};  // degrees
	float     outerConeAngle {45.0f}; // degrees
};

class MeshNode : public Node
{
public:
	std::shared_ptr<MeshAsset>&       getMesh() { return m_mesh; }
	const std::shared_ptr<MeshAsset>& getMesh() const { return m_mesh; }

protected:
	std::shared_ptr<MeshAsset> m_mesh;
};

class LightNode : public Node
{
public:
	NodeLight&       getLight() { return m_light; }
	const NodeLight& getLight() const { return m_light; }

	void setType(LightType type) { m_light.type = type; }
	void setColor(const glm::vec3& color) { m_light.color = color; }
	void setIntensity(float intensity) { m_light.intensity = intensity; }
	void setRadius(float radius) { m_light.radius = radius; }
	void setDirection(const glm::vec3& direction) { m_light.direction = direction; }
	void setConeAngles(float innerDegrees, float outerDegrees)
	{
		m_light.innerConeAngle = innerDegrees;
		m_light.outerConeAngle = outerDegrees;
	}

	// Optional visual mesh for the light (composition via MeshNode)
	void                       setMesh(std::shared_ptr<MeshAsset> mesh);
	void                       setMeshScale(float scale);
	void                       setMeshScale(const glm::vec3& scale);
	std::shared_ptr<MeshAsset> getMesh() const;
	bool                       hasMesh() const { return m_meshNode != nullptr; }

protected:
	NodeLight                 m_light;
	std::shared_ptr<MeshNode> m_meshNode;
};
