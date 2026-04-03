#pragma once

#include <Jolt/Jolt.h>

#ifdef JPH_DEBUG_RENDERER

#include <Jolt/Renderer/DebugRendererSimple.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace agni
{
namespace physics
{

class JoltDebugRenderer : public JPH::DebugRendererSimple
{
public:
	struct LineVertex
	{
		glm::vec3 pos;
		uint32_t  color; // Packed RGBA (R in low byte)
	};
	static_assert(sizeof(LineVertex) == 16, "LineVertex must be 16 bytes");

	JoltDebugRenderer();

	// Call at start of each frame before Jolt draw calls
	void beginFrame(const glm::vec3& cameraPos);

	// JPH::DebugRendererSimple overrides
	void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;
	void DrawTriangle(JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3,
	                  JPH::ColorArg inColor, ECastShadow inCastShadow) override;
	void DrawText3D(JPH::RVec3Arg inPosition, const JPH::string_view& inString,
	                JPH::ColorArg inColor, float inHeight) override;

	// Access collected line data for GPU upload
	const std::vector<LineVertex>& getLineVertices() const { return m_lines; }
	uint32_t                       getVertexCount() const { return static_cast<uint32_t>(m_lines.size()); }
	bool                           hasData() const { return !m_lines.empty(); }

private:
	std::vector<LineVertex> m_lines;
};

} // namespace physics
} // namespace agni

#endif // JPH_DEBUG_RENDERER
