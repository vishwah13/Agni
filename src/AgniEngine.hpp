#pragma once
#include <renderdoc_app.h>

#include <vk_mem_alloc.h>

#include <Camera.hpp>
#include <Components.hpp>
#include <DescriptorBuffer.hpp>
#include <Descriptors.hpp>
#include <ECS/EntityFactory.hpp>
#include <ECS/World.hpp>
#include <Loader.hpp>
#include <Material.hpp>
#include <Renderer.hpp>
#include <ResourceManager.hpp>
#include <Scene.hpp>
#include <Skybox.hpp>
#include <SwapchainManager.hpp>
#include <Texture.hpp>
#include <Types.hpp>

#ifdef AGNI_HAS_JOLT
#include <ECS/Systems/PhysicsSystem.hpp>
#include <Physics/JoltPhysicsManager.hpp>
#endif

#include <deque>
#include <functional>
#include <memory>
#include <vector>

// Forward declarations
namespace agni
{
	namespace editor
	{
		class EditorManager;
	}
} // namespace agni

constexpr uint32_t FRAME_OVERLAP = 2;

struct FrameData
{

	VkCommandPool   m_commandPool       = VK_NULL_HANDLE;
	VkCommandBuffer m_mainCommandBuffer = VK_NULL_HANDLE;

	VkSemaphore m_swapchainSemaphore = VK_NULL_HANDLE;
	VkSemaphore m_renderSemaphore    = VK_NULL_HANDLE;
	VkFence     m_renderFence        = VK_NULL_HANDLE;

	DeletionQueue m_deletionQueue;
	// Descriptor buffer allocator for per-frame descriptors
	DescriptorBufferAllocator m_descriptorBuffer;
};

// ============================================================================
// Node Classes - Intermediate Representation for glTF Loading
// ============================================================================
// These classes are used during glTF file loading to build a scene graph,
// which is then converted to ECS entities. They are NOT used for rendering.
// See EntityFactory::convertNodeRecursive() for the conversion logic.
// ============================================================================

class MeshNode : public Node
{
public:
	// Accessor for mesh
	std::shared_ptr<MeshAsset>& getMesh()
	{
		return m_mesh;
	}
	const std::shared_ptr<MeshAsset>& getMesh() const
	{
		return m_mesh;
	}

protected:
	std::shared_ptr<MeshAsset> m_mesh;
};

class LightNode : public Node
{
public:
	// Light property accessors
	LightComponent& getLightComponent()
	{
		return m_light;
	}
	const LightComponent& getLightComponent() const
	{
		return m_light;
	}

	// Light type
	void setType(LightType type)
	{
		m_light.type = type;
	}
	LightType getType() const
	{
		return m_light.type;
	}

	// Convenience setters
	void setColor(const glm::vec3& color)
	{
		m_light.color = color;
	}
	void setIntensity(float intensity)
	{
		m_light.intensity = intensity;
	}
	void setRadius(float radius)
	{
		m_light.radius = radius;
	}
	void setDirection(const glm::vec3& direction)
	{
		m_light.direction = direction;
	}
	void setInnerConeAngle(float degrees)
	{
		m_light.innerConeAngle = degrees;
	}
	void setOuterConeAngle(float degrees)
	{
		m_light.outerConeAngle = degrees;
	}
	void setConeAngles(float innerDegrees, float outerDegrees)
	{
		m_light.innerConeAngle = innerDegrees;
		m_light.outerConeAngle = outerDegrees;
	}

	// Convenience getters
	glm::vec3 getColor() const
	{
		return m_light.color;
	}
	float getIntensity() const
	{
		return m_light.intensity;
	}
	float getRadius() const
	{
		return m_light.radius;
	}
	glm::vec3 getDirection() const
	{
		return m_light.direction;
	}
	float getInnerConeAngle() const
	{
		return m_light.innerConeAngle;
	}
	float getOuterConeAngle() const
	{
		return m_light.outerConeAngle;
	}

	// Optional visual mesh for the light (uses MeshNode internally)
	void                       setMesh(std::shared_ptr<MeshAsset> mesh);
	void                       setMeshScale(float scale);
	void                       setMeshScale(const glm::vec3& scale);
	std::shared_ptr<MeshAsset> getMesh() const;
	bool                       hasMesh() const
	{
		return m_meshNode != nullptr;
	}

protected:
	LightComponent            m_light;
	std::shared_ptr<MeshNode> m_meshNode; // Optional visual representation
};

class AgniEngine
{
public:
	AgniEngine();
	~AgniEngine();
	AgniEngine(const AgniEngine& other)           = delete;
	AgniEngine(AgniEngine&& other)                = delete;
	AgniEngine operator=(const AgniEngine& other) = delete;
	AgniEngine operator=(AgniEngine&& other)      = delete;

	bool       m_isInitialized {false};
	int        m_frameNumber {0};
	bool       m_stopRendering {false};
	VkExtent2D m_windowExtent {1600, 900};

	// Delta time tracking
	std::chrono::time_point<std::chrono::high_resolution_clock> m_lastFrameTime;
	float m_deltaTime {0.0f}; // Time between frames in seconds

	struct SDL_Window* m_window {nullptr};

	static AgniEngine& Get();

	ResourceManager  m_resourceManager;
	SwapchainManager m_swapchainManager;
	Texture          m_texture;

	VkInstance               m_instance       = VK_NULL_HANDLE; // Vulkan library handle
	VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE; // Vulkan debug output handle
	VkPhysicalDevice         m_chosenGPU     = VK_NULL_HANDLE; // GPU chosen as the default device
	VkDevice                 m_device        = VK_NULL_HANDLE; // Vulkan device for commands
	VkSurfaceKHR             m_surface       = VK_NULL_HANDLE; // Vulkan window surface
	VkDescriptorPool m_imguiPool {VK_NULL_HANDLE}; // ImGui descriptor pool

	// Descriptor buffer extension properties
	DescriptorBufferProperties m_descriptorBufferProps {};

	// Multi-draw indirect support
	bool m_multiDrawIndirectSupported {false}; // GPU capability (queried at init)
	bool m_multiDrawIndirectEnabled {true};   // User toggle (only effective if supported)

	FrameData m_frames[FRAME_OVERLAP];

	FrameData& getCurrentFrame()
	{
		return m_frames[m_frameNumber % FRAME_OVERLAP];
	}

	const FrameData& getCurrentFrame() const
	{
		return m_frames[m_frameNumber % FRAME_OVERLAP];
	}

	VkQueue  m_graphicsQueue       = VK_NULL_HANDLE;
	uint32_t m_graphicsQueueFamily = 0;

	DescriptorAllocatorGrowable m_globalDescriptorAllocator;

	Camera m_mainCamera;


	// initializes everything in the engine
	void init();

	// shuts down the engine
	void cleanup();

	// draw loop
	void draw();

	// run main loop
	void run();

	// Renderer (handles all rendering logic)
	Renderer m_renderer;

	// Asset loader (manages default textures, materials, and loading)
	AssetLoader m_assetLoader;

	// m_skybox
	Skybox m_skybox;

	// ECS World and related systems
	std::unique_ptr<agni::ecs::World>            m_ecsWorld;
	std::unique_ptr<agni::ecs::EntityFactory>    m_entityFactory;
	std::unique_ptr<agni::editor::EditorManager> m_editorManager;

	// Primitive meshes for editor entity creation
	std::shared_ptr<MeshAsset> m_cubeMesh;
	std::shared_ptr<MeshAsset> m_sphereMesh;
	std::shared_ptr<MeshAsset> m_planeMesh;
	std::shared_ptr<MeshAsset> m_suzanneMesh;
	std::shared_ptr<MeshAsset> m_cylinderMesh;
	std::shared_ptr<MeshAsset> m_torusMesh;
	std::shared_ptr<MeshAsset> m_coneMesh;

	// Quit flag
	bool m_shouldQuit = false;

	// ECS accessors
	agni::ecs::World& getECSWorld()
	{
		return *m_ecsWorld;
	}
	const agni::ecs::World& getECSWorld() const
	{
		return *m_ecsWorld;
	}
	agni::IWorld& getWorld()
	{
		return *m_ecsWorld;
	}
	const agni::IWorld& getWorld() const
	{
		return *m_ecsWorld;
	}
	agni::ecs::EntityFactory& getEntityFactory()
	{
		return *m_entityFactory;
	}
	const agni::ecs::EntityFactory& getEntityFactory() const
	{
		return *m_entityFactory;
	}

	// Renderer accessor
	Renderer& getRenderer()
	{
		return m_renderer;
	}
	const Renderer& getRenderer() const
	{
		return m_renderer;
	}

	// Camera accessor
	Camera& getCamera()
	{
		return m_mainCamera;
	}
	const Camera& getCamera() const
	{
		return m_mainCamera;
	}

	// Swapchain accessor
	SwapchainManager& getSwapchainManager()
	{
		return m_swapchainManager;
	}
	const SwapchainManager& getSwapchainManager() const
	{
		return m_swapchainManager;
	}

	// Window extent accessor
	VkExtent2D getWindowExtent() const
	{
		return m_windowExtent;
	}

	// Primitive mesh accessors
	std::shared_ptr<MeshAsset> getCubeMesh() const
	{
		return m_cubeMesh;
	}
	std::shared_ptr<MeshAsset> getSphereMesh() const
	{
		return m_sphereMesh;
	}
	std::shared_ptr<MeshAsset> getPlaneMesh() const
	{
		return m_planeMesh;
	}
	std::shared_ptr<MeshAsset> getSuzanneMesh() const
	{
		return m_suzanneMesh;
	}
	std::shared_ptr<MeshAsset> getCylinderMesh() const
	{
		return m_cylinderMesh;
	}
	std::shared_ptr<MeshAsset> getTorusMesh() const
	{
		return m_torusMesh;
	}
	std::shared_ptr<MeshAsset> getConeMesh() const
	{
		return m_coneMesh;
	}

	// Quit method
	void quit()
	{
		m_shouldQuit = true;
	}

#ifdef AGNI_HAS_JOLT
	// Physics Manager
	std::unique_ptr<agni::physics::JoltPhysicsManager> m_physicsManager;

	agni::physics::JoltPhysicsManager& getPhysicsManager()
	{
		return *m_physicsManager;
	}
	const agni::physics::JoltPhysicsManager& getPhysicsManager() const
	{
		return *m_physicsManager;
	}
#endif

private:
	RENDERDOC_API_1_1_2* m_rdocAPI = NULL;

	void initVulkan();
	void initSwapchain();
	void initCommands();
	void initSyncStructures();

	void initRenderDocAPI();
	void captureRenderDocFrame();
	void endRenderDocFrameCapture();

	void resizeSwapchain();

	void initDescriptors();

	void initImgui();

	void initPipelines();

	void initDefaultData();
};
