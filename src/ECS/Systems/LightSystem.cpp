#include <ECS/Systems/LightSystem.hpp>
#include <ECS/World.hpp>
#include <Renderer.hpp>
#include <Types.hpp>

#include <glm/trigonometric.hpp>

namespace agni
{
namespace ecs
{

void LightSystem::gatherLights(World& world, DrawContext& ctx)
{
	auto& flecsWorld = world.get();

	// Build and run a query for all light entities
	flecsWorld.query<const TransformComponent, const LightComponent>()
	    .each([&ctx](const TransformComponent& transform, const LightComponent& light) {
		    // Extract world position from transform matrix
		    glm::vec3 worldPosition = glm::vec3(transform.worldTransform[3]);

		    // Transform direction by rotation (upper 3x3 of the matrix)
		    glm::vec3 worldDirection = glm::normalize(glm::mat3(transform.worldTransform) * light.direction);

		    switch (light.type)
		    {
		    case LightType::Point:
		    {
			    if (ctx.m_PointLights.size() < MAX_POINT_LIGHTS)
			    {
				    GPUPointLight gpuLight;
				    gpuLight.m_position  = worldPosition;
				    gpuLight.m_color     = light.color;
				    gpuLight.m_intensity = light.intensity;
				    gpuLight.m_radius    = light.radius;
				    ctx.m_PointLights.push_back(gpuLight);
			    }
			    break;
		    }

		    case LightType::Directional:
		    {
			    // Only one directional light supported (last one wins)
			    ctx.m_DirectionalLight.direction = worldDirection;
			    ctx.m_DirectionalLight.color     = light.color;
			    ctx.m_DirectionalLight.intensity = light.intensity;
			    ctx.m_DirectionalLight.active    = true;
			    break;
		    }

		    case LightType::Spot:
		    {
			    if (ctx.m_SpotLights.size() < MAX_SPOT_LIGHTS)
			    {
				    GPUSpotLight gpuLight;
				    gpuLight.m_position    = worldPosition;
				    gpuLight.m_direction   = worldDirection;
				    gpuLight.m_color       = light.color;
				    gpuLight.m_intensity   = light.intensity;
				    gpuLight.m_radius      = light.radius;
				    gpuLight.m_innerCutoff = glm::cos(glm::radians(light.innerConeAngle));
				    gpuLight.m_outerCutoff = glm::cos(glm::radians(light.outerConeAngle));
				    ctx.m_SpotLights.push_back(gpuLight);
			    }
			    break;
		    }
		    }
	    });
}

} // namespace ecs
} // namespace agni
