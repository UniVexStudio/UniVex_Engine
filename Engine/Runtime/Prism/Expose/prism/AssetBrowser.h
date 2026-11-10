// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Asset Browser & Content Drawer.
// Editor-grade Content Browser with Sources folder panel, breadcrumbs,
// action toolbar (+ Add, Import, Save All), type filters, 3D PBR thumbnails,
// class badge pills, asset type color bars, and drag & drop.
#pragma once
#include "Widget.h"
#include "Containers.h"
#include "Controls.h"
#include "Layouts.h"
#include "DragDrop.h"
#include "TextInput.h"
#include <string>
#include <vector>

namespace prism {

struct AssetRecord {
  std::string path;
  std::string name;
  std::string type; // "Static Mesh", "Material", "Texture 2D", "Shader", "World Level", "Font"
  Icon icon = Icon::File;
  Color badgeColor = Color(0, 195, 230);
  size_t sizeBytes = 0;
  std::string folder = "Content";
  std::string badgeText = "SM";
  bool isModified = false;
};

// Draws `asset`'s thumbnail (PBR previews for meshes / materials, texture, shader, font and level cards,
// otherwise its icon) into `thumbRect`.
void drawAssetThumbnail(Canvas& c, const AssetRecord& asset, Rect thumbRect);
// Paints one content tile (thumbnail, type tab, name, type, selection frame) into `tile`
// (PRISM's tile size is 108 x 136).
void paintAssetTile(Canvas& c, Rect tile, const AssetRecord& asset, bool selected, bool hovered);

struct FolderItem {
  std::string name;
  std::string path;
  int count = 0;
  int depth = 0;
};

class AssetBrowser : public Widget {
public:
  AssetBrowser();

  void addAsset(AssetRecord asset);
  void clearAssets();
  void setFilterType(std::string type);
  void setSearchQuery(std::string query);
  void setActiveFolder(std::string folder);

  void selectAsset(int index);
  void selectAssetByName(const std::string& name);

  const std::vector<AssetRecord>& assets() const { return m_assets; }
  const AssetRecord* selectedAsset() const;

  Callback<const AssetRecord&> onAssetDoubleClicked;
  Callback<const AssetRecord&> onAssetSelected;

  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  Widget* hitTest(Vec2 p) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onMouseWheel(const MouseEvent& e) override;
  void onMouseLeave() override;

private:
  void rebuildFiltered();
  void rebuildFolders();
  Rect tileRect(int index, Rect contentRect) const;
  int tileAt(Vec2 p, Rect contentRect) const;
  int folderAt(Vec2 p, Rect sourcesRect) const;

  std::vector<AssetRecord> m_assets;
  std::vector<size_t> m_filteredIndices;
  std::vector<FolderItem> m_folders;

  std::string m_activeTypeFilter = "All";
  std::string m_activeFolder = "All";
  std::string m_searchQuery;
  int m_selectedIdx = -1;
  int m_hoverIdx = -1;
  int m_hoverFolderIdx = -1;
  bool m_showSourcesPanel = true;

  // Header & Controls
  Ref<TextInput> m_searchInput;
  Ref<WrapBox> m_filterChips;

  // Dragging state
  int m_pressIdx = -1;
  Vec2 m_pressPos;
  bool m_dragging = false;


  float m_sourcesW = 145.0f;
  float m_tileW = 108.0f;
  float m_tileH = 136.0f;
  float m_tileSpacing = 8.0f;
  float m_scrollY = 0.0f;
};

} // namespace prism
