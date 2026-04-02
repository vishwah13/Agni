#include <Application.hpp>
#include <AgniEngine.hpp>
#include "ImGuiIntegration.hpp"

#include <imgui.h>

#include <Editor/EditorManager.hpp>

class EditorApp : public agni::Application
{
	ImGuiIntegration m_imgui;
	std::unique_ptr<agni::editor::EditorManager> m_editor;

protected:
	void onInit() override
	{
		auto& engine = getEngine();
		m_imgui.init(engine);
	}

	void onPostInit() override
	{
		auto& engine = getEngine();
		m_editor = std::make_unique<agni::editor::EditorManager>(engine);
		m_editor->init();

		// Wire up the UI draw callback so the renderer calls ImGui
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
		if (m_editor) m_editor->render();
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

	void onEntityPicked(uint64_t entityID) override
	{
		if (m_editor) m_editor->setSelectedEntity(entityID);
	}

	void onCleanup() override
	{
		m_editor.reset();
		m_imgui.cleanup(getEngine());
	}
};

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
	EditorApp app;
	return app.run(argc, argv);
}
