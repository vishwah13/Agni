#pragma once

#include <memory>
#include <string>
#include <unordered_map>

struct LoadedGLTF;
struct AsyncLoadHandle;

namespace agni
{
namespace editor
{

class EditorManager;

class AssetBrowser
{
public:
	AssetBrowser(EditorManager& editorManager);

	void render(bool& visible);

	// Get currently dragged asset (nullptr if not dragging)
	std::shared_ptr<LoadedGLTF> getDraggedAsset() const { return m_draggedAsset; }
	bool isDragging() const { return m_isDragging; }
	void clearDrag();

private:
	EditorManager& m_editorManager;

	// UI state
	char m_filterText[128] = "";
	float m_thumbnailSize = 80.0f;

	// Drag state
	bool m_isDragging = false;
	std::shared_ptr<LoadedGLTF> m_draggedAsset;
	std::string m_draggedAssetName;

	void renderToolbar();
	void renderAssetGrid();
	void renderLoadingProgress();
	void renderAssetThumbnail(const std::string& name,
	                          std::shared_ptr<LoadedGLTF> asset);
};

} // namespace editor
} // namespace agni
