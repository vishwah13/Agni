#include <Physics/JoltDebugRenderer.hpp>

#ifdef JPH_DEBUG_RENDERER

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

using namespace JPH;

namespace agni
{
namespace physics
{

JoltDebugRenderer::JoltDebugRenderer()
{
	Initialize();
	m_lines.reserve(32768); // Pre-allocate for ~16K lines
}

void JoltDebugRenderer::beginFrame(const glm::vec3& cameraPos)
{
	m_lines.clear();
	SetCameraPos(RVec3(cameraPos.x, cameraPos.y, cameraPos.z));
	NextFrame();
}

void JoltDebugRenderer::DrawLine(RVec3Arg inFrom, RVec3Arg inTo, ColorArg inColor)
{
	m_lines.push_back({
	    glm::vec3(static_cast<float>(inFrom.GetX()),
	              static_cast<float>(inFrom.GetY()),
	              static_cast<float>(inFrom.GetZ())),
	    inColor.GetUInt32()});

	m_lines.push_back({
	    glm::vec3(static_cast<float>(inTo.GetX()),
	              static_cast<float>(inTo.GetY()),
	              static_cast<float>(inTo.GetZ())),
	    inColor.GetUInt32()});
}

void JoltDebugRenderer::DrawTriangle(RVec3Arg inV1, RVec3Arg inV2, RVec3Arg inV3,
                                     ColorArg inColor, ECastShadow /*inCastShadow*/)
{
	// Draw as 3 wireframe edges
	DrawLine(inV1, inV2, inColor);
	DrawLine(inV2, inV3, inColor);
	DrawLine(inV3, inV1, inColor);
}

void JoltDebugRenderer::DrawText3D(RVec3Arg /*inPosition*/, const JPH::string_view& /*inString*/,
                                   ColorArg /*inColor*/, float /*inHeight*/)
{
	// No-op — text rendering not implemented yet
}

void JoltDebugRenderer::drawColliderShape(const TransformComponent& transform,
                                          const ColliderComponent& collider,
                                          const RigidBodyComponent& rigidbody)
{
	// Decompose world transform to get position, rotation, scale
	glm::vec3 scale, translation, skew;
	glm::quat rotation;
	glm::vec4 perspective;
	glm::decompose(transform.worldTransform, scale, rotation, translation, skew, perspective);

	// Build Jolt transform (position + rotation, applying collider center offset)
	glm::vec3 center = translation + rotation * (collider.center * scale);
	RVec3 joltPos(center.x, center.y, center.z);
	Quat  joltRot(rotation.x, rotation.y, rotation.z, rotation.w);
	RMat44 bodyTransform = RMat44::sRotationTranslation(joltRot, joltPos);

	// Color based on body type: static=grey, dynamic=green, kinematic=blue
	Color color(128, 128, 128, 255);
	switch (rigidbody.type)
	{
	case RigidBodyType::Static:    color = Color(128, 128, 128, 255); break;
	case RigidBodyType::Dynamic:   color = Color(0, 255, 128, 255);   break;
	case RigidBodyType::Kinematic: color = Color(64, 128, 255, 255);  break;
	}

	// Draw the shape using inherited DebugRenderer helpers
	switch (collider.type)
	{
	case ColliderType::Box:
	{
		glm::vec3 halfExt = collider.boxHalfExtents * scale;
		AABox box(Vec3(-halfExt.x, -halfExt.y, -halfExt.z),
		          Vec3(halfExt.x, halfExt.y, halfExt.z));
		DrawWireBox(bodyTransform, box, color);
		break;
	}
	case ColliderType::Sphere:
	{
		float uniformScale = std::max({scale.x, scale.y, scale.z});
		float r = collider.sphereRadius * uniformScale;
		DrawWireSphere(joltPos, r, color);
		break;
	}
	case ColliderType::Capsule:
	{
		float radiusScale = std::max(scale.x, scale.z);
		float halfHeight = collider.capsuleHalfHeight * scale.y;
		float radius = collider.capsuleRadius * radiusScale;
		// Jolt capsule is along Y axis
		DrawCapsule(bodyTransform, halfHeight, radius, color, ECastShadow::Off, EDrawMode::Wireframe);
		break;
	}
	}
}

} // namespace physics
} // namespace agni

#endif // JPH_DEBUG_RENDERER
