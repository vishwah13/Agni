#pragma once
#include <Components.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <memory>

struct DrawContext;

// base class for a renderable dynamic object
class IRenderable
{

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
};

// implementation of a drawable scene node.
// the scene node can hold m_children and will also keep a transform to
// propagate to them
class Node : public IRenderable
{
public:
	// The Node class will hold the object matrix for the transforms. Both local
	// and world transform. The world transform needs to be updated, so whenever
	// the local Transform gets changed, refreshTransform must be called. This
	// will recursively go down the node tree and make sure the matrices are on
	// their correct places.
	void refreshTransform(const glm::mat4& parentMatrix);

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx);

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
	// Transform component (ECS-ready)
	TransformComponent m_transformComponent;

	// m_parent pointer must be a weak pointer to avoid circular dependencies
	std::weak_ptr<Node>                m_parent;
	std::vector<std::shared_ptr<Node>> m_children;

	// Backwards compatibility accessors
	glm::mat4& m_localTransform = m_transformComponent.localTransform;
	glm::mat4& m_worldTransform = m_transformComponent.worldTransform;

public:
	// Direct component access for future ECS
	TransformComponent& getTransformComponent() { return m_transformComponent; }
	const TransformComponent& getTransformComponent() const { return m_transformComponent; }
};