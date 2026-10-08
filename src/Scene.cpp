#include <Scene.hpp>

#include <glm/gtc/matrix_transform.hpp>

void Node::refreshTransform(const glm::mat4& parentMatrix)
{
	m_worldTransform = parentMatrix * m_localTransform;
	for (const auto& c : m_children)
	{
		c->refreshTransform(m_worldTransform);
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

// ============================================================================
// LightNode — optional visual mesh attachment
// ============================================================================

void LightNode::setMesh(std::shared_ptr<MeshAsset> mesh)
{
	if (mesh)
	{
		m_meshNode            = std::make_shared<MeshNode>();
		m_meshNode->getMesh() = mesh;
		// MeshNode uses an identity local transform — position comes from the
		// owning LightNode.
	}
	else
	{
		m_meshNode = nullptr;
	}
}

void LightNode::setMeshScale(float scale)
{
	setMeshScale(glm::vec3(scale));
}

void LightNode::setMeshScale(const glm::vec3& scale)
{
	if (m_meshNode)
	{
		m_meshNode->getWorldTransform() = glm::scale(glm::mat4(1.0f), scale);
	}
}

std::shared_ptr<MeshAsset> LightNode::getMesh() const
{
	return m_meshNode ? m_meshNode->getMesh() : nullptr;
}
