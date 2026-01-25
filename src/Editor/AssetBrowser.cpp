#include <Editor/AssetBrowser.hpp>
#include <Editor/EditorManager.hpp>
#include <Loader.hpp>
#include <Debug.hpp>

#include <imgui.h>
#include <algorithm>

namespace agni
{
namespace editor
{

AssetBrowser::AssetBrowser(EditorManager& editorManager)
    : m_editorManager(editorManager)
{
}

void AssetBrowser::render(bool& visible)
{
	if (!visible)
		return;

	ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Asset Browser", &visible))
	{
		renderToolbar();
		renderAssetGrid();
		renderLoadingProgress();
	}
	ImGui::End();
}

void AssetBrowser::clearDrag()
{
	m_isDragging = false;
	m_draggedAsset = nullptr;
	m_draggedAssetName.clear();
}

void AssetBrowser::renderToolbar()
{
	// Filter text input
	ImGui::Text("Filter:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(200.0f);
	ImGui::InputText("##Filter", m_filterText, sizeof(m_filterText));

	ImGui::SameLine();
	ImGui::Dummy(ImVec2(20, 0)); // Spacing

	// Thumbnail size slider
	ImGui::Text("Size:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120.0f);
	ImGui::SliderFloat("##ThumbnailSize", &m_thumbnailSize, 60.0f, 150.0f, "%.0f");

	ImGui::Separator();
}

void AssetBrowser::renderAssetGrid()
{
	// Leave space for progress bar at bottom
	ImGui::BeginChild("AssetGrid", ImVec2(0, -40), true);

	float windowWidth = ImGui::GetContentRegionAvail().x;
	float cellSize = m_thumbnailSize + 20.0f; // thumbnail + padding
	int columns = std::max(1, (int)(windowWidth / cellSize));

	int itemIndex = 0;
	const auto& loadedAssets = m_editorManager.getLoadedAssets();

	if (loadedAssets.empty())
	{
		ImGui::TextDisabled("No assets loaded. Drag and drop .gltf or .glb files to load them.");
	}
	else
	{
		for (const auto& [name, asset] : loadedAssets)
		{
			// Apply filter
			if (m_filterText[0] != '\0')
			{
				std::string lowerName = name;
				std::string lowerFilter = m_filterText;
				std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
				std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);

				if (lowerName.find(lowerFilter) == std::string::npos)
					continue;
			}

			// Grid layout
			if (itemIndex > 0 && itemIndex % columns != 0)
				ImGui::SameLine();

			renderAssetThumbnail(name, asset);
			itemIndex++;
		}
	}

	ImGui::EndChild();
}

void AssetBrowser::renderAssetThumbnail(const std::string& name,
                                         std::shared_ptr<LoadedGLTF> asset)
{
	ImGui::PushID(name.c_str());

	ImGui::BeginGroup();

	// Thumbnail placeholder (colored box for now)
	ImVec2 thumbSize(m_thumbnailSize, m_thumbnailSize);
	ImVec4 thumbColor = ImVec4(0.2f, 0.2f, 0.25f, 1.0f); // Surface color

	// Draw thumbnail button
	ImGui::PushStyleColor(ImGuiCol_Button, thumbColor);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.5f, 0.7f, 1.0f));

	if (ImGui::Button("##thumb", thumbSize))
	{
		// Selection on click (optional - could store selected asset)
	}

	// Drag source for drag-and-drop
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
	{
		m_isDragging = true;
		m_draggedAsset = asset;
		m_draggedAssetName = name;

		// Set payload (just the name as identifier)
		ImGui::SetDragDropPayload("ASSET_GLTF", name.c_str(), name.size() + 1);

		// Drag preview tooltip
		ImGui::Text("Drop to spawn: %s", name.c_str());

		ImGui::EndDragDropSource();
	}

	// Reset drag state if not dragging anymore
	if (m_isDragging && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		clearDrag();
	}

	ImGui::PopStyleColor(3);

	// Asset name below thumbnail (truncated)
	std::string displayName = name;
	if (displayName.length() > 12)
		displayName = displayName.substr(0, 10) + "...";

	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + m_thumbnailSize);
	ImGui::TextWrapped("%s", displayName.c_str());
	ImGui::PopTextWrapPos();

	// Tooltip with full name on hover
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::Text("%s", name.c_str());
		ImGui::Separator();
		ImGui::Text("Meshes: %zu", asset->meshes.size());
		ImGui::Text("Materials: %zu", asset->materials.size());
		ImGui::Text("Nodes: %zu", asset->nodes.size());
		ImGui::EndTooltip();
	}

	ImGui::EndGroup();
	ImGui::PopID();
}

void AssetBrowser::renderLoadingProgress()
{
	const auto& activeLoads = m_editorManager.getActiveLoads();

	if (activeLoads.empty())
		return;

	// Position at bottom of window
	ImGui::Separator();

	for (const auto& handle : activeLoads)
	{
		std::string filename = handle->filePath.filename().string();
		float progress = handle->progress.load();

		ImGui::Text("Loading: %s", filename.c_str());
		ImGui::SameLine();
		ImGui::ProgressBar(progress, ImVec2(-1, 0));
	}
}

} // namespace editor
} // namespace agni
