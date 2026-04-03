#include <Physics/JoltDebugRenderer.hpp>

#ifdef JPH_DEBUG_RENDERER

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

} // namespace physics
} // namespace agni

#endif // JPH_DEBUG_RENDERER
