// Minimal Agni sample: loads a glTF, fills the renderer's DrawContext, flies the
// camera, and draws an ImGui stats overlay.
//
// This is the reference for driving the renderer. The renderer owns no scene
// representation — whoever owns the scene fills DrawContext every frame.

#include <AgniEngine.hpp>
#include <Application.hpp>
#include <ImGuiIntegration.hpp>
#include <Loader.hpp>

#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>

#include <memory>
#include <vector>
#include <cstdio>

namespace
{
constexpr const char* kSceneAsset = "assets/structure.glb";
}

class SampleApp : public agni::Application
{
	ImGuiIntegration            m_imgui;
	std::shared_ptr<LoadedGLTF> m_scene;
	bool                        m_showStats = true;

	// Draw list built once from the glTF node tree. The scene is static, so
	// there is no reason to rebuild it per frame.
	std::vector<RenderObject> m_opaque;
	std::vector<RenderObject> m_transparent;
	uint64_t                  m_nextObjectID = 1;

protected:
	void onInit() override
	{
		m_imgui.init(getEngine());
	}

	void onPostInit() override
	{
		AgniEngine& engine = getEngine();

		engine.m_renderer.m_uiDrawCallback =
		[this](VkCommandBuffer cmd, VkImageView view)
		{ m_imgui.draw(cmd, view, getEngine()); };

		// Load the scene asset. The renderer's loaded-scene map keeps the GPU
		// resources alive for as long as we draw from them.
		auto loaded = engine.m_assetLoader.loadGltf(&engine, resPath(kSceneAsset));
		if (!loaded.has_value())
		{
			AGNI_PRINT("[Sample] Failed to load {}\n", kSceneAsset);
			return;
		}
		m_scene                                        = loaded.value();
		engine.m_renderer.getLoadedScenes()[kSceneAsset] = m_scene;

		engine.m_mainCamera.m_position = glm::vec3(0.0f, 2.0f, 8.0f);

		// Walk the node tree to build the draw list (see collectNode).
		for (const auto& node : m_scene->m_topNodes)
			collectNode(node);

		AGNI_PRINT("[Sample] {}: {} top nodes -> {} opaque + {} transparent draws\n",
		           kSceneAsset,
		           m_scene->m_topNodes.size(),
		           m_opaque.size(),
		           m_transparent.size());
	}

	void onUpdate(float /*deltaTime*/) override
	{
		DrawContext& ctx = getEngine().m_renderer.getMainDrawContext();

		// The renderer owns no scene state — we hand it the frame's draw list.
		ctx.m_OpaqueSurfaces      = m_opaque;
		ctx.m_TransparentSurfaces = m_transparent;
		ctx.m_PointLights.clear();
		ctx.m_SpotLights.clear();

		// One directional key light so the shadow passes have something to do.
		ctx.m_DirectionalLight.direction = glm::normalize(glm::vec3(0.3f, -1.0f, -0.4f));
		ctx.m_DirectionalLight.color     = glm::vec3(1.0f, 0.98f, 0.92f);
		ctx.m_DirectionalLight.intensity = 3.0f;
		ctx.m_DirectionalLight.active    = true;
	}

	void onEvent(SDL_Event& e) override
	{
		m_imgui.processEvent(e);
	}

	void onBeginUIFrame() override
	{
		m_imgui.beginFrame();
	}

	void onRenderUI() override
	{
		if (!m_showStats)
			return;

		Renderer&          renderer = getEngine().m_renderer;
		const EngineStats& stats    = renderer.getStats();

		if (ImGui::Begin("Stats", &m_showStats))
		{
			ImGui::Text("frame      %.2f ms", stats.m_frametime);
			ImGui::Text("scene      %.2f ms", stats.m_sceneUpdateTime);
			ImGui::Text("mesh draw  %.2f ms", stats.m_meshDrawTime);
			ImGui::Separator();
			ImGui::Text("draw calls %d", stats.m_drawcallCount);
			ImGui::Text("submitted  %d tris", stats.m_triangleCount);
			ImGui::Text("rendered   %d prims", stats.m_renderedTriangles);
			ImGui::Separator();
			ImGui::Checkbox("Hi-Z occlusion", &renderer.getHiZOcclusionEnabled());
			ImGui::Checkbox("Shadows", &renderer.getShadowsEnabled());
		}
		ImGui::End();
	}

	void onEndUIFrame() override
	{
		m_imgui.endFrame();
	}

	bool wantCaptureMouse() override
	{
		return ImGui::GetIO().WantCaptureMouse;
	}

	bool wantCaptureKeyboard() override
	{
		return ImGui::GetIO().WantCaptureKeyboard;
	}

	void onCleanup() override
	{
		m_opaque.clear();
		m_transparent.clear();
		m_scene.reset();
		m_imgui.cleanup(getEngine());
	}

private:
	// Walk the glTF node tree, which is what carries per-mesh world transforms.
	// Iterating LoadedGLTF::meshes instead would be wrong twice over: that map is
	// keyed by mesh *name* (so same-named meshes overwrite each other) and it
	// carries no transform, so everything would stack at the origin.
	void collectNode(const std::shared_ptr<Node>& node)
	{
		if (!node)
			return;

		const glm::mat4& world = node->getWorldTransform();

		if (auto* meshNode = dynamic_cast<MeshNode*>(node.get()))
		{
			addMesh(meshNode->getMesh(), world);
		}
		else if (auto* lightNode = dynamic_cast<LightNode*>(node.get()))
		{
			// A light node may also carry a visual mesh.
			addMesh(lightNode->getMesh(), world);
		}

		for (const auto& child : node->getChildren())
			collectNode(child);
	}

	void addMesh(const std::shared_ptr<MeshAsset>& mesh, const glm::mat4& world)
	{
		if (!mesh)
			return;

		for (const GeoSurface& surface : mesh->m_surfaces)
		{
			if (!surface.m_material)
				continue;

			RenderObject obj;
			obj.m_indexCount = surface.m_count;
			obj.m_firstIndex =
			mesh->m_meshBuffers.m_globalIndexOffset + surface.m_startIndex;
			obj.m_material            = &surface.m_material->m_data;
			obj.m_bounds              = surface.m_bounds;
			obj.m_transform           = world;
			obj.m_vertexBufferAddress = mesh->m_meshBuffers.m_vertexBufferAddress;
			obj.m_entityID            = m_nextObjectID++;

			if (surface.m_material->m_data.m_passType == MaterialPass::Transparent)
				m_transparent.push_back(obj);
			else
				m_opaque.push_back(obj);
		}
	}
};

int main(int argc, char** argv)
{
	// Unbuffered stdout: otherwise a kill or crash swallows the log tail.
	setvbuf(stdout, nullptr, _IONBF, 0);

	SampleApp app;
	return app.run(argc, argv);
}
