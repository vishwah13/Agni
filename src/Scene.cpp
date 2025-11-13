#include <Scene.hpp>

void Node::refreshTransform(const glm::mat4& parentMatrix)
{
	m_worldTransform = parentMatrix * m_localTransform;
	for (const auto& c : m_children)
	{
		c->refreshTransform(m_worldTransform);
	}
}

void Node::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
{
	// draw m_children
	for (auto& c : m_children)
	{
		c->Draw(topMatrix, ctx);
	}
}

// Accessors
glm::mat4& Node::getLocalTransform()
{
	return m_localTransform;
}
const glm::mat4& Node::getLocalTransform() const
{
	return m_localTransform;
}

glm::mat4& Node::getWorldTransform()
{
	return m_worldTransform;
}
const glm::mat4& Node::getWorldTransform() const
{
	return m_worldTransform;
}

std::vector<std::shared_ptr<Node>>& Node::getChildren()
{
	return m_children;
}
const std::vector<std::shared_ptr<Node>>& Node::getChildren() const
{
	return m_children;
}

std::weak_ptr<Node>& Node::getParent()
{
	return m_parent;
}
const std::weak_ptr<Node>& Node::getParent() const
{
	return m_parent;
}