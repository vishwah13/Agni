#include <Editor/AssetBrowser.hpp>
#include <Editor/EditorManager.hpp>
#include <Loader.hpp>
#include <Debug.hpp>

#define DMON_IMPL
#include <dmon.h>

#include <imgui.h>
#include <algorithm>

namespace agni
{
namespace editor
{

// File watcher callback (free function for C API compatibility)
static void assetBrowserFileWatchCallback(dmon_watch_id watch_id, dmon_action action,
                                          const char* rootdir, const char* filepath,
                                          const char* oldfilepath, void* user)
{
	(void)watch_id;
	(void)rootdir;
	(void)filepath;
	(void)oldfilepath;

	auto* browser = static_cast<AssetBrowser*>(user);

	switch (action)
	{
	case DMON_ACTION_CREATE:
	case DMON_ACTION_DELETE:
	case DMON_ACTION_MODIFY:
		browser->markForRefresh();
		break;
	default:
		break;
	}
}

AssetBrowser::AssetBrowser(EditorManager& editorManager)
    : m_editorManager(editorManager)
    , m_assetsFolder("../../assets")
{
	// Initialize file watcher
	dmon_init();

	// Scan file system on construction
	scanFileSystem();

	// Start watching assets folder
	dmon_watch_id id = dmon_watch(m_assetsFolder.string().c_str(),
	                              assetBrowserFileWatchCallback,
	                              DMON_WATCHFLAGS_RECURSIVE, this);
	m_watchId = id.id;

	AGNI_PRINT("[AssetBrowser] Initialized - watching {}\n", m_assetsFolder.string());
}

void AssetBrowser::render(bool& visible)
{
	m_isVisible = visible;

	if (!visible)
		return;

	ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Asset Browser", &visible))
	{
		renderToolbar();

		// Split view: Folder tree (left) | File grid (right)
		float treeWidth = ImGui::GetContentRegionAvail().x * 0.3f;

		ImGui::BeginChild("FolderTree", ImVec2(treeWidth, -40), true);
		if (m_rootNode)
		{
			renderFolderTree(*m_rootNode);
		}
		ImGui::EndChild();

		ImGui::SameLine();

		ImGui::BeginChild("FileGrid", ImVec2(0, -40), true);
		renderFileGrid();
		ImGui::EndChild();

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

void AssetBrowser::renderFolderTree(FileNode& node)
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
	                           ImGuiTreeNodeFlags_OpenOnDoubleClick |
	                           ImGuiTreeNodeFlags_SpanFullWidth;

	// Highlight selected folder
	if (m_selectedFolder == &node)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	// Root node always open
	if (&node == m_rootNode.get())
	{
		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}

	// Icon + name
	ImGui::Text("%s", getFileIcon(node.fullPath, node.isDirectory));
	ImGui::SameLine();

	bool nodeOpen = ImGui::TreeNodeEx(node.name.c_str(), flags);

	// Click to select folder
	if (ImGui::IsItemClicked())
	{
		m_selectedFolder = &node;
	}

	if (nodeOpen)
	{
		// Recurse through subdirectories only
		for (auto& child : node.children)
		{
			if (child->isDirectory)
			{
				renderFolderTree(*child);
			}
		}
		ImGui::TreePop();
	}
}

void AssetBrowser::renderFileGrid()
{
	if (!m_selectedFolder)
	{
		ImGui::TextDisabled("Select a folder");
		return;
	}

	float thumbnailSize = m_thumbnailSize;
	int columnCount = calculateColumnCount(thumbnailSize);

	int fileIndex = 0;
	for (auto& child : m_selectedFolder->children)
	{
		// Skip directories in grid view
		if (child->isDirectory)
			continue;

		// Apply filter
		if (m_filterText[0] != '\0')
		{
			std::string lowerName = child->name;
			std::string lowerFilter = m_filterText;
			std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
			std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);

			if (lowerName.find(lowerFilter) == std::string::npos)
				continue;
		}

		// Grid layout
		if (fileIndex > 0 && fileIndex % columnCount != 0)
			ImGui::SameLine();

		renderFileThumbnail(*child, thumbnailSize);
		fileIndex++;
	}

	if (fileIndex == 0)
	{
		ImGui::TextDisabled("No files in this folder");
	}
}

void AssetBrowser::renderFileThumbnail(FileNode& file, float size)
{
	ImGui::PushID(file.fullPath.string().c_str());

	ImGui::BeginGroup();

	// Thumbnail placeholder (colored box)
	ImVec2 thumbSize(size, size);
	ImVec4 thumbColor = ImVec4(0.2f, 0.2f, 0.25f, 1.0f);

	ImGui::PushStyleColor(ImGuiCol_Button, thumbColor);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.35f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.5f, 0.7f, 1.0f));

	if (ImGui::Button("##thumb", thumbSize))
	{
		// Click handling
	}

	// Drag source for drag-and-drop
	if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
	{
		m_isDragging = true;

		// Use full path as payload
		std::string pathStr = file.fullPath.string();
		ImGui::SetDragDropPayload("ASSET_FILE", pathStr.c_str(), pathStr.size() + 1);

		// Drag preview
		ImGui::Text("%s %s", getFileIcon(file.fullPath, false), file.name.c_str());

		ImGui::EndDragDropSource();
	}

	// Reset drag state if not dragging anymore
	if (m_isDragging && !ImGui::IsMouseDragging(ImGuiMouseButton_Left))
	{
		clearDrag();
	}

	ImGui::PopStyleColor(3);

	// File icon + name below thumbnail
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + size);
	ImGui::Text("%s %s", getFileIcon(file.fullPath, false), file.name.c_str());
	ImGui::PopTextWrapPos();

	// Tooltip with full name on hover
	if (ImGui::IsItemHovered())
	{
		ImGui::BeginTooltip();
		ImGui::Text("%s", file.fullPath.filename().string().c_str());
		ImGui::Separator();
		ImGui::Text("Path: %s", file.fullPath.string().c_str());
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

void AssetBrowser::update()
{
	if (m_needsRefresh)
	{
		scanFileSystem();
		m_needsRefresh = false;
	}
}

void AssetBrowser::cleanup()
{
	if (m_watchId != 0)
	{
		dmon_watch_id id;
		id.id = m_watchId;
		dmon_unwatch(id);
		m_watchId = 0;
	}
	dmon_deinit();
}

void AssetBrowser::scanFileSystem()
{
	AGNI_PRINT("[AssetBrowser] Scanning file system: {}\n", m_assetsFolder.string());

	// Create root node
	m_rootNode = std::make_unique<FileNode>();
	m_rootNode->name = "Assets";
	m_rootNode->fullPath = m_assetsFolder;
	m_rootNode->isDirectory = true;

	// Scan recursively
	if (std::filesystem::exists(m_assetsFolder))
	{
		scanDirectoryRecursive(m_assetsFolder, *m_rootNode);
	}

	// Default selected folder to root
	if (!m_selectedFolder)
	{
		m_selectedFolder = m_rootNode.get();
	}
}

void AssetBrowser::scanDirectoryRecursive(const std::filesystem::path& dir, FileNode& node)
{
	try
	{
		for (const auto& entry : std::filesystem::directory_iterator(dir))
		{
			auto child = std::make_unique<FileNode>();
			child->name = entry.path().filename().string();
			child->fullPath = entry.path();
			child->isDirectory = entry.is_directory();
			child->parent = &node;

			if (entry.is_directory())
			{
				// Recurse into subdirectories
				scanDirectoryRecursive(entry.path(), *child);
			}

			node.children.push_back(std::move(child));
		}

		// Sort: directories first, then files, alphabetically
		std::sort(node.children.begin(), node.children.end(),
		          [](const std::unique_ptr<FileNode>& a, const std::unique_ptr<FileNode>& b) {
			          if (a->isDirectory != b->isDirectory)
				          return a->isDirectory > b->isDirectory;
			          return a->name < b->name;
		          });
	}
	catch (const std::filesystem::filesystem_error& e)
	{
		AGNI_PRINT("[AssetBrowser] Error scanning {}: {}\n", dir.string(), e.what());
	}
}

const char* AssetBrowser::getFileIcon(const std::filesystem::path& path, bool isDirectory)
{
	if (isDirectory)
		return "\xF0\x9F\x93\x81"; // 📁

	std::string ext = path.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

	if (ext == ".glb" || ext == ".gltf")
		return "\xF0\x9F\x8E\xA8"; // 🎨
	if (ext == ".jpg" || ext == ".png" || ext == ".bmp" || ext == ".tga")
		return "\xF0\x9F\x96\xBC"; // 🖼️
	if (ext == ".json")
		return "\xF0\x9F\x93\x84"; // 📄
	return "\xF0\x9F\x93\xA6"; // 📦
}

int AssetBrowser::calculateColumnCount(float thumbnailSize)
{
	float windowWidth = ImGui::GetContentRegionAvail().x;
	float cellSize = thumbnailSize + 20.0f;
	return std::max(1, (int)(windowWidth / cellSize));
}

void AssetBrowser::handleExternalDrop(const std::filesystem::path& externalPath)
{
	// Determine target folder (use selected folder or default to assets/models/)
	std::filesystem::path targetDir = m_selectedFolder && m_selectedFolder->isDirectory
	                                      ? m_selectedFolder->fullPath
	                                      : (m_assetsFolder / "models");

	// Ensure target directory exists
	std::filesystem::create_directories(targetDir);

	std::filesystem::path targetPath = targetDir / externalPath.filename();

	try
	{
		// Copy file
		std::filesystem::copy_file(externalPath, targetPath,
		                           std::filesystem::copy_options::overwrite_existing);

		AGNI_PRINT("[AssetBrowser] Imported: {} -> {}\n",
		           externalPath.string(), targetPath.string());
	}
	catch (const std::filesystem::filesystem_error& e)
	{
		AGNI_PRINT("[AssetBrowser] Failed to import file: {}\n", e.what());
	}
}

} // namespace editor
} // namespace agni
