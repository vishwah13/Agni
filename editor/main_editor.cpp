#include <Application.hpp>
#include <AgniEngine.hpp>
#include "ImGuiIntegration.hpp"

#include <imgui.h>

#ifdef AGNI_ENABLE_EDITOR_TESTS
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_ui.h>
extern void RegisterEditorTests(ImGuiTestEngine* engine);
#endif

#include <Editor/EditorManager.hpp>
#include <Scene/SceneSerializer.hpp>

class EditorApp : public agni::Application
{
	ImGuiIntegration m_imgui;
	std::unique_ptr<agni::editor::EditorManager> m_editor;

#ifdef AGNI_ENABLE_EDITOR_TESTS
	ImGuiTestEngine* m_testEngine = nullptr;
#endif

	// Play/Stop state
	enum class Mode { Editing, Playing, Paused };
	Mode        m_mode = Mode::Editing;
	std::string m_worldSnapshot;

public:
	void play()
	{
		auto& engine = getEngine();
		agni::scene::SceneSerializer serializer(engine);
		m_worldSnapshot = serializer.serializeToString();
		engine.m_simulationPaused = false;
		m_mode = Mode::Playing;
	}

	void pause()
	{
		getEngine().m_simulationPaused = true;
		m_mode = Mode::Paused;
	}

	void resume()
	{
		getEngine().m_simulationPaused = false;
		m_mode = Mode::Playing;
	}

	void stop()
	{
		auto& engine = getEngine();
		engine.m_simulationPaused = true;

#ifdef AGNI_HAS_JOLT
		// Remove all Jolt physics bodies before restoring snapshot
		// (prevents stale body accumulation)
		engine.m_physicsManager->removeAllBodies();
#endif

		if (!m_worldSnapshot.empty())
		{
			agni::scene::SceneSerializer serializer(engine);
			agni::scene::SceneLoadOptions opts;
			opts.clearExisting = true;
			opts.reloadAssets  = false;
			serializer.deserializeFromString(m_worldSnapshot, opts);
			m_worldSnapshot.clear();
		}

		m_mode = Mode::Editing;
	}

	Mode getMode() const { return m_mode; }

protected:
	void onInit() override
	{
		auto& engine = getEngine();
		m_imgui.init(engine);
		engine.m_simulationPaused = true;

#ifdef AGNI_ENABLE_EDITOR_TESTS
		m_testEngine = ImGuiTestEngine_CreateContext();
		ImGuiTestEngineIO& testIO = ImGuiTestEngine_GetIO(m_testEngine);
		testIO.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
		testIO.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
		testIO.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
		ImGuiTestEngine_Start(m_testEngine, ImGui::GetCurrentContext());
		ImGuiTestEngine_InstallDefaultCrashHandler();
		RegisterEditorTests(m_testEngine);
#endif
	}

	void onPostInit() override
	{
		auto& engine = getEngine();
		m_editor = std::make_unique<agni::editor::EditorManager>(engine);
		m_editor->init();

		engine.m_renderer.m_uiDrawCallback =
		    [this](VkCommandBuffer cmd, VkImageView view)
		{
			m_imgui.draw(cmd, view, getEngine());
		};
	}

	void onUpdate(float /*dt*/) override
	{
		if (m_editor) m_editor->update();
	}

	void onEvent(SDL_Event& e) override
	{
		m_imgui.processEvent(e);
		if (m_editor) m_editor->processInput(e);
	}

	void onBeginUIFrame() override
	{
		m_imgui.beginFrame();
	}

	void onRenderUI() override
	{
		// Play/Stop toolbar
		renderPlayStopToolbar();

		if (m_editor) m_editor->render();

#ifdef AGNI_ENABLE_EDITOR_TESTS
		ImGuiTestEngine_ShowTestEngineWindows(m_testEngine, nullptr);
#endif
	}

	void onEndUIFrame() override
	{
		m_imgui.endFrame();
#ifdef AGNI_ENABLE_EDITOR_TESTS
		ImGuiTestEngine_PostSwap(m_testEngine);
#endif
	}

	bool wantCaptureMouse() override
	{
		return ImGui::GetIO().WantCaptureMouse;
	}

	bool wantCaptureKeyboard() override
	{
		return ImGui::GetIO().WantCaptureKeyboard;
	}

	void onEntityPicked(uint64_t entityID) override
	{
		if (m_editor) m_editor->setSelectedEntity(entityID);
	}

	void onCleanup() override
	{
		if (m_mode != Mode::Editing)
			stop();

#ifdef AGNI_ENABLE_EDITOR_TESTS
		ImGuiTestEngine_Stop(m_testEngine);
#endif

		m_editor.reset();
		m_imgui.cleanup(getEngine());

#ifdef AGNI_ENABLE_EDITOR_TESTS
		// Destroy test engine AFTER ImGui context (so .ini data is saved)
		ImGuiTestEngine_DestroyContext(m_testEngine);
		m_testEngine = nullptr;
#endif
	}

private:
	void renderPlayStopToolbar()
	{
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
		                         ImGuiWindowFlags_NoMove |
		                         ImGuiWindowFlags_AlwaysAutoResize |
		                         ImGuiWindowFlags_NoSavedSettings;

		// Position toolbar at the top center of the viewport
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		float toolbarWidth = 150.0f;
		ImVec2 pos(viewport->WorkPos.x + (viewport->WorkSize.x - toolbarWidth) * 0.5f,
		           viewport->WorkPos.y + 2.0f);
		ImGui::SetNextWindowPos(pos);
		ImGui::SetNextWindowBgAlpha(0.85f);

		if (ImGui::Begin("##PlayStopToolbar", nullptr, flags))
		{
			if (m_mode == Mode::Editing)
			{
				if (ImGui::Button("  Play  "))
					play();
			}
			else
			{
				if (ImGui::Button("  Stop  "))
					stop();

				ImGui::SameLine();

				if (m_mode == Mode::Playing)
				{
					if (ImGui::Button(" Pause "))
						pause();
				}
				else // Paused
				{
					if (ImGui::Button("Resume"))
						resume();
				}
			}

			// Show mode indicator
			ImGui::SameLine();
			switch (m_mode)
			{
			case Mode::Editing: ImGui::TextDisabled("Edit"); break;
			case Mode::Playing: ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "Playing"); break;
			case Mode::Paused:  ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Paused"); break;
			}
		}
		ImGui::End();
	}
};

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
	EditorApp app;
	return app.run(argc, argv);
}
