#include <Editor/EditorUI.hpp>
#include <Editor/EditorManager.hpp>
#include <Editor/EditorWidgets.hpp>
#include <Editor/EditorIcons.hpp>
#include <AgniEngine.hpp>
#include <imgui.h>

namespace agni
{
namespace editor
{

EditorUI::EditorUI(AgniEngine& engine, EditorManager& editorManager)
    : m_engine(engine)
    , m_editorManager(editorManager)
{
}

void EditorUI::render()
{
	renderMainMenuBar();
	renderPerformanceWindow();
	renderRenderingWindow();
}

void EditorUI::renderMainMenuBar()
{
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene", "Ctrl+N"))
			{
				m_editorManager.newScene();
			}
			if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
			{
				m_editorManager.openScene();
			}
			if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
			{
				m_editorManager.saveScene();
			}
			if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
			{
				m_editorManager.saveSceneAs();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Exit", "Alt+F4"))
			{
				m_engine.quit();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Undo", "Ctrl+Z")) { /* TODO */ }
			if (ImGui::MenuItem("Redo", "Ctrl+Y")) { /* TODO */ }
			ImGui::Separator();
			if (ImGui::MenuItem("Copy", "Ctrl+C")) { /* TODO */ }
			if (ImGui::MenuItem("Paste", "Ctrl+V")) { /* TODO */ }
			if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
			{
				m_editorManager.duplicateSelectedEntity();
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Delete", "Delete"))
			{
				m_editorManager.deleteSelectedEntity();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Entity"))
		{
			// Calculate spawn position in front of camera
			glm::mat4 cameraRotation = m_engine.getCamera().getRotationMatrix();
			glm::vec3 forward = glm::vec3(cameraRotation * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
			glm::vec3 spawnPos = m_engine.getCamera().m_position + forward * 5.0f;

			if (ImGui::MenuItem("Create Empty", "Ctrl+Shift+N"))
			{
				m_editorManager.createEntity(EditorManager::EntityType::Empty, spawnPos);
			}
			ImGui::Separator();
			if (ImGui::BeginMenu("3D Object"))
			{
				if (ImGui::MenuItem("Cube"))
				{
					m_editorManager.createEntity(EditorManager::EntityType::Cube, spawnPos);
				}
				if (ImGui::MenuItem("Sphere"))
				{
					m_editorManager.createEntity(EditorManager::EntityType::Sphere, spawnPos);
				}
				if (ImGui::MenuItem("Plane"))
				{
					m_editorManager.createEntity(EditorManager::EntityType::Plane, spawnPos);
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Suzanne"))
				{
					m_editorManager.createEntity(EditorManager::EntityType::Suzanne, spawnPos);
				}
				if (ImGui::MenuItem("Cylinder"))
				{
					m_editorManager.createEntity(EditorManager::EntityType::Cylinder, spawnPos);
				}
				if (ImGui::MenuItem("Torus"))
				{
					m_editorManager.createEntity(EditorManager::EntityType::Torus, spawnPos);
				}
				if (ImGui::MenuItem("Cone"))
				{
					m_editorManager.createEntity(EditorManager::EntityType::Cone, spawnPos);
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Light"))
			{
				if (ImGui::MenuItem("Point Light"))
				{
					m_editorManager.createEntity(EditorManager::EntityType::PointLight, spawnPos);
				}
				if (ImGui::MenuItem("Directional Light"))
				{
					m_editorManager.createEntity(EditorManager::EntityType::DirectionalLight, spawnPos);
				}
				if (ImGui::MenuItem("Spot Light"))
				{
					m_editorManager.createEntity(EditorManager::EntityType::SpotLight, spawnPos);
				}
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Window"))
		{
			if (ImGui::MenuItem("Hierarchy", nullptr, m_showHierarchy))
			{
				m_showHierarchy = !m_showHierarchy;
			}
			if (ImGui::MenuItem("Inspector", nullptr, m_showInspector))
			{
				m_showInspector = !m_showInspector;
			}
			if (ImGui::MenuItem("Asset Browser", nullptr, m_showAssetBrowser))
			{
				m_showAssetBrowser = !m_showAssetBrowser;
			}
			if (ImGui::MenuItem("Performance", nullptr, m_showPerformance))
			{
				m_showPerformance = !m_showPerformance;
			}
			if (ImGui::MenuItem("Rendering", nullptr, m_showRendering))
			{
				m_showRendering = !m_showRendering;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Reset Layout"))
			{
				// Show all windows
				m_showHierarchy = true;
				m_showInspector = true;
				m_showAssetBrowser = true;
				m_showPerformance = true;
				m_showRendering = true;
			}
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Help"))
		{
			if (ImGui::MenuItem("About Agni")) { /* TODO */ }
			if (ImGui::MenuItem("Documentation")) { /* TODO */ }
			ImGui::Separator();
			if (ImGui::MenuItem("GitHub Repository")) { /* TODO */ }
			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}
}

void EditorUI::renderPerformanceWindow()
{
	if (m_showPerformance)
	{
		if (ImGui::Begin("Performance", &m_showPerformance))
		{
			if (widgets::CollapsibleSection("Frame Statistics"))
		{
			float frametime = m_engine.getRenderer().getStats().m_frametime;
			float fps = (frametime > 0.0f) ? 1000.0f / frametime : 0.0f;

			char fpsStr[32], frametimeStr[32];
			snprintf(fpsStr, sizeof(fpsStr), "%.1f", fps);
			snprintf(frametimeStr, sizeof(frametimeStr), "%.2f ms", frametime);

			widgets::StatDisplay("FPS", fpsStr);
			widgets::StatDisplay("Frame Time", frametimeStr);
		}

		widgets::Spacing(4.0f);

		if (widgets::CollapsibleSection("Render Statistics"))
		{
			char drawTimeStr[32], updateTimeStr[32], trisStr[32], drawsStr[32];
			snprintf(drawTimeStr, sizeof(drawTimeStr), "%.2f ms", m_engine.getRenderer().getStats().m_meshDrawTime);
			snprintf(updateTimeStr, sizeof(updateTimeStr), "%.2f ms", m_engine.getRenderer().getStats().m_sceneUpdateTime);
			snprintf(trisStr, sizeof(trisStr), "%d", m_engine.getRenderer().getStats().m_triangleCount);
			snprintf(drawsStr, sizeof(drawsStr), "%d", m_engine.getRenderer().getStats().m_drawcallCount);

			char renderedTrisStr[32];
			snprintf(renderedTrisStr, sizeof(renderedTrisStr), "%d", m_engine.getRenderer().getStats().m_renderedTriangles);

			widgets::StatDisplay("Draw Time", drawTimeStr);
			widgets::StatDisplay("Update Time", updateTimeStr);
			widgets::StatDisplay("Triangles (submitted)", trisStr);
			widgets::StatDisplay("Triangles (rendered)", renderedTrisStr);
			widgets::StatDisplay("Draw Calls", drawsStr);
		}
		}
		ImGui::End();
	}
}

void EditorUI::renderRenderingWindow()
{
	if (m_showRendering)
	{
		if (ImGui::Begin("Rendering", &m_showRendering))
		{
			// Quality Settings
		if (widgets::CollapsibleSection("Quality", icons::Quality))
		{
			ImGui::PushID("Quality");
			widgets::PropertyFloat("Render Scale", &m_engine.getRenderer().getRenderScale(), 0.3f, 1.0f, "%.1f");

			// MSAA selector
			const char* msaaOptions[] = {"1x (Off)", "2x", "4x", "8x"};
			int currentMsaa = 0;
			switch (m_engine.getRenderer().getMsaaSamples())
			{
				case VK_SAMPLE_COUNT_1_BIT: currentMsaa = 0; break;
				case VK_SAMPLE_COUNT_2_BIT: currentMsaa = 1; break;
				case VK_SAMPLE_COUNT_4_BIT: currentMsaa = 2; break;
				case VK_SAMPLE_COUNT_8_BIT: currentMsaa = 3; break;
				default: currentMsaa = 2; break;
			}

			if (widgets::PropertyCombo("MSAA", &currentMsaa, msaaOptions, 4))
			{
				VkSampleCountFlagBits samples[] = {
					VK_SAMPLE_COUNT_1_BIT, VK_SAMPLE_COUNT_2_BIT,
					VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_8_BIT
				};
				if (samples[currentMsaa] != m_engine.getRenderer().getMsaaSamples())
				{
					m_engine.getRenderer().getMsaaSamples() = samples[currentMsaa];
					m_engine.getSwapchainManager().requestResize();
				}
			}
			ImGui::PopID();
		}

		// Culling
		if (widgets::CollapsibleSection("Culling", icons::Quality))
		{
			ImGui::PushID("Culling");
			widgets::PropertyCheckbox("GPU Frustum Culling", &m_engine.getRenderer().getGpuCullingEnabled());
			widgets::PropertyCheckbox("Hi-Z Occlusion", &m_engine.getRenderer().getHiZOcclusionEnabled());
			ImGui::PopID();
		}

		// Directional Light Shadows
		if (widgets::CollapsibleSection("Directional Shadows", icons::Shadows))
		{
			ImGui::PushID("DirShadow");
			widgets::PropertyCheckbox("Enable", &m_engine.getRenderer().getShadowsEnabled());
			widgets::PropertyFloat("Bias", &m_engine.getRenderer().getShadowBias(), 0.0f, 0.05f, "%.4f");
			widgets::PropertyFloat("Normal Bias", &m_engine.getRenderer().getShadowNormalBias(), 0.0f, 0.1f, "%.4f");
			widgets::PropertyFloat("Ortho Size", &m_engine.getRenderer().getShadowOrthoSize(), 10.0f, 200.0f, "%.1f");
			ImGui::PopID();
		}

		// Spot Light Shadows
		if (widgets::CollapsibleSection("Spot Shadows", icons::Shadows))
		{
			ImGui::PushID("SpotShadow");
			widgets::PropertyCheckbox("Enable", &m_engine.getRenderer().getSpotShadowsEnabled());
			widgets::PropertyFloat("Bias", &m_engine.getRenderer().getSpotShadowBias(), 0.0f, 0.05f, "%.4f");
			widgets::PropertyFloat("Normal Bias", &m_engine.getRenderer().getSpotShadowNormalBias(), 0.0f, 0.1f, "%.4f");
			ImGui::PopID();
		}

		// Point Light Shadows
		if (widgets::CollapsibleSection("Point Shadows", icons::Shadows))
		{
			ImGui::PushID("PointShadow");
			widgets::PropertyCheckbox("Enable", &m_engine.getRenderer().getPointShadowsEnabled());
			widgets::PropertyFloat("Bias", &m_engine.getRenderer().getPointShadowBias(), 0.0f, 0.2f, "%.4f");
			widgets::PropertyFloat("Far Plane", &m_engine.getRenderer().getPointShadowFarPlane(), 10.0f, 200.0f, "%.1f");
			widgets::PropertyInt("Shadow Light", &m_engine.getRenderer().getPointShadowLightIndex(), 0, 10);

			widgets::PropertyCheckbox("PCF Soft Shadows", &m_engine.getRenderer().getPointShadowPCFEnabled());
			if (m_engine.getRenderer().getPointShadowPCFEnabled())
			{
				widgets::PropertyFloat("PCF Radius", &m_engine.getRenderer().getPointShadowPCFRadius(), 0.01f, 0.5f, "%.3f");
				ImGui::TextDisabled("  (20 samples per pixel)");
			}

			widgets::SeparatorText("Debug");
			auto& pointLights = m_engine.getRenderer().getMainDrawContext().m_PointLights;
			widgets::InfoRow("Point Lights", "%zu", pointLights.size());
			int idx = m_engine.getRenderer().getPointShadowLightIndex();
			if (idx < (int)pointLights.size())
			{
				auto& light = pointLights[idx];
				widgets::InfoRow("Position", "(%.1f, %.1f, %.1f)",
					light.m_position.x, light.m_position.y, light.m_position.z);
			}
			ImGui::PopID();
		}

		// Camera Settings
		if (widgets::CollapsibleSection("Camera", icons::Camera))
		{
			ImGui::PushID("Camera");
			widgets::PropertyFloat("Move Speed", &m_engine.getCamera().m_speed, 0.01f, 1.0f, "%.3f");
			widgets::PropertyFloat("Sensitivity", &m_engine.getCamera().m_mouseSensitivity, 0.1f, 1.0f, "%.2f");
			ImGui::PopID();
		}
		}
		ImGui::End();
	}
}

} // namespace editor
} // namespace agni
