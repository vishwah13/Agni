#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct LoadedGLTF;
struct AsyncLoadHandle;

namespace agni
{
namespace editor
{

class EditorManager;

// File system tree node
struct FileNode
{
	std::string                             name;
	std::filesystem::path                   fullPath;
	bool                                    isDirectory {false};
	std::vector<std::unique_ptr<FileNode>>  children;
	FileNode*                               parent {nullptr};
};

class AssetBrowser
{
public:
	AssetBrowser(EditorManager& editorManager);

	void render(bool& visible);
	void update();
	void cleanup();

	// Get currently dragged asset (nullptr if not dragging)
	std::shared_ptr<LoadedGLTF> getDraggedAsset() const { return m_draggedAsset; }
	bool isDragging() const { return m_isDragging; }
	void clearDrag();

	// Window visibility state
	bool isVisible() const { return m_isVisible; }

	// External file import
	void handleExternalDrop(const std::filesystem::path& externalPath);

	// Mark for refresh (called by file watcher callback)
	void markForRefresh() { m_needsRefresh = true; }

private:
	EditorManager& m_editorManager;

	// UI state
	char m_filterText[128] = "";
	float m_thumbnailSize = 80.0f;

	// Drag state
	bool m_isDragging = false;
	std::shared_ptr<LoadedGLTF> m_draggedAsset;
	std::string m_draggedAssetName;

	// Window state
	bool m_isVisible = false;

	// File system tree
	std::unique_ptr<FileNode> m_rootNode;
	std::filesystem::path m_assetsFolder;
	FileNode* m_selectedFolder = nullptr;

	// File watcher (uint32_t ID, dmon_watch_id is struct with .id member)
	uint32_t m_watchId = 0;
	bool m_needsRefresh = false;

	// File system scanning
	void scanFileSystem();
	void scanDirectoryRecursive(const std::filesystem::path& dir, FileNode& node);

	// Rendering
	void renderToolbar();
	void renderFolderTree(FileNode& node);
	void renderFileGrid();
	void renderFileThumbnail(FileNode& file, float size);
	void renderAssetGrid();  // Old grid view (keep for compatibility)
	void renderLoadingProgress();
	void renderAssetThumbnail(const std::string& name,
	                          std::shared_ptr<LoadedGLTF> asset);

	// Utilities
	const char* getFileIcon(const std::filesystem::path& path, bool isDirectory);
	int calculateColumnCount(float thumbnailSize);

	// File watcher callback
	// Note: Defined as free function in cpp due to dmon C API requirements
};

} // namespace editor
} // namespace agni
