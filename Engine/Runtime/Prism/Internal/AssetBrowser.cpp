// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/AssetBrowser.h"
#include "prism/Application.h"
#include "prism/PbrRenderer.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace prism {

namespace {

std::string cleanAssetName(const std::string& fullName) {
  std::string s = fullName;
  size_t dot = s.find_last_of('.');
  if (dot != std::string::npos) {
    s = s.substr(0, dot);
  }
  if (s == "Inter-Medium") s = "Inter";
  return s;
}

} // namespace

AssetBrowser::AssetBrowser() {
  m_searchInput = make<TextInput>("", "Search Content...");
  m_searchInput->setLeadingIcon(Icon::Search);
  m_searchInput->setClearable(true);
  m_searchInput->onChanged = [this](const std::string& query) {
    setSearchQuery(query);
  };
  addChild(m_searchInput);

  m_filterChips = make<WrapBox>(Orientation::Horizontal, 4.0f, 4.0f);
  for (const char* t : {"All", "Mesh", "Material", "Texture", "Shader", "Level", "Font"}) {
    auto b = make<Button>(t, Icon::None, ButtonStyle::Flat);
    std::string typeStr = t;
    b->onClick = [this, typeStr] { setFilterType(typeStr); };
    m_filterChips->add(b);
  }
  addChild(m_filterChips);

  rebuildFolders();
}

void AssetBrowser::addAsset(AssetRecord asset) {
  // Determine folder if empty
  if (asset.folder.empty() || asset.folder == "Content") {
    if (asset.path.find("Meshes") != std::string::npos || asset.type.find("Mesh") != std::string::npos) asset.folder = "Meshes";
    else if (asset.path.find("Materials") != std::string::npos || asset.type.find("Material") != std::string::npos) asset.folder = "Materials";
    else if (asset.path.find("Textures") != std::string::npos || asset.type.find("Texture") != std::string::npos) asset.folder = "Textures";
    else if (asset.path.find("Shaders") != std::string::npos || asset.type.find("Shader") != std::string::npos) asset.folder = "Shaders";
    else if (asset.path.find("Scenes") != std::string::npos || asset.type.find("Level") != std::string::npos || asset.type.find("Scene") != std::string::npos) asset.folder = "Maps";
    else if (asset.path.find("Fonts") != std::string::npos || asset.type.find("Font") != std::string::npos) asset.folder = "Fonts";
    else asset.folder = "Content";
  }

  // Determine badgeText & color
  if (asset.badgeText.empty() || asset.badgeText == "SM") {
    if (asset.type.find("Material") != std::string::npos) { asset.badgeText = "M"; asset.badgeColor = Color(40, 205, 65); }
    else if (asset.type.find("Texture") != std::string::npos) { asset.badgeText = "T2D"; asset.badgeColor = Color(225, 45, 55); }
    else if (asset.type.find("Level") != std::string::npos || asset.type.find("Scene") != std::string::npos) { asset.badgeText = "LVL"; asset.badgeColor = Color(255, 130, 20); }
    else if (asset.type.find("Shader") != std::string::npos) { asset.badgeText = "SH"; asset.badgeColor = Color(50, 150, 255); }
    else if (asset.type.find("Font") != std::string::npos) { asset.badgeText = "FNT"; asset.badgeColor = Color(245, 195, 35); }
    else { asset.badgeText = "SM"; asset.badgeColor = Color(0, 195, 230); }
  }

  m_assets.push_back(std::move(asset));
  rebuildFolders();
  rebuildFiltered();
}

void AssetBrowser::clearAssets() {
  m_assets.clear();
  m_filteredIndices.clear();
  m_selectedIdx = -1;
  m_hoverIdx = -1;
  rebuildFolders();
  invalidate();
}

void AssetBrowser::setFilterType(std::string type) {
  m_activeTypeFilter = std::move(type);
  rebuildFiltered();
}

void AssetBrowser::setSearchQuery(std::string query) {
  m_searchQuery = std::move(query);
  rebuildFiltered();
}

void AssetBrowser::setActiveFolder(std::string folder) {
  m_activeFolder = std::move(folder);
  rebuildFiltered();
}

const AssetRecord* AssetBrowser::selectedAsset() const {
  if (m_selectedIdx >= 0 && m_selectedIdx < int(m_filteredIndices.size())) {
    return &m_assets[m_filteredIndices[size_t(m_selectedIdx)]];
  }
  return nullptr;
}

void AssetBrowser::selectAsset(int index) {
  if (index >= 0 && index < int(m_filteredIndices.size())) {
    m_selectedIdx = index;
    invalidate();
    if (onAssetSelected) onAssetSelected(m_assets[m_filteredIndices[size_t(index)]]);
  }
}

void AssetBrowser::selectAssetByName(const std::string& name) {
  for (size_t i = 0; i < m_filteredIndices.size(); i++) {
    if (m_assets[m_filteredIndices[i]].name == name) {
      selectAsset(int(i));
      return;
    }
  }
}

void AssetBrowser::rebuildFolders() {
  m_folders.clear();
  int total = int(m_assets.size());
  m_folders.push_back({"All", "All", total, 0});
  m_folders.push_back({"Content", "Content", total, 1});

  std::map<std::string, int> counts;
  for (const auto& a : m_assets) {
    counts[a.folder]++;
  }

  for (const auto& f : {"Meshes", "Materials", "Textures", "Shaders", "Maps", "Fonts"}) {
    int cnt = counts[f];
    m_folders.push_back({f, f, cnt, 2});
  }
}

void AssetBrowser::rebuildFiltered() {
  m_filteredIndices.clear();

  std::string lowerQuery = m_searchQuery;
  std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(), ::tolower);

  for (size_t i = 0; i < m_assets.size(); i++) {
    const auto& a = m_assets[i];

    // Folder filter
    if (m_activeFolder != "All" && m_activeFolder != "Content") {
      if (a.folder != m_activeFolder && a.path.find(m_activeFolder) == std::string::npos) {
        continue;
      }
    }

    // Type filter
    if (m_activeTypeFilter != "All") {
      bool matches = (a.type.find(m_activeTypeFilter) != std::string::npos);
      if (m_activeTypeFilter == "Level" && (a.type.find("Scene") != std::string::npos || a.folder == "Maps")) matches = true;
      if (!matches) continue;
    }

    // Search query
    if (!lowerQuery.empty()) {
      std::string lowerName = a.name;
      std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
      if (lowerName.find(lowerQuery) == std::string::npos) {
        continue;
      }
    }

    m_filteredIndices.push_back(i);
  }

  m_selectedIdx = -1;
  m_hoverIdx = -1;
  invalidate();
}

Vec2 AssetBrowser::measure(Vec2 avail) {
  return m_desired = constrain(avail);
}

void AssetBrowser::arrange(Rect r) {
  m_rect = r;
  float pad = 6.0f;

  // Search input in top navigation bar on the right side
  float searchW = 190.0f;
  float searchH = 22.0f;
  m_searchInput->arrange(Rect{r.right() - searchW - pad, r.y + 5.0f, searchW, searchH});

  // Filter chips in the right pane above tiles
  float leftContentX = (m_showSourcesPanel ? r.x + m_sourcesW + 8.0f : r.x + 8.0f);
  float contentW = r.right() - leftContentX - 8.0f;
  m_filterChips->measure(Vec2(contentW, 26.0f));
  m_filterChips->arrange(Rect{leftContentX, r.y + 36.0f, contentW, 24.0f});
}

Rect AssetBrowser::tileRect(int index, Rect contentRect) const {
  int cols = std::max(1, int(std::floor((contentRect.w + m_tileSpacing) / (m_tileW + m_tileSpacing))));
  int col = index % cols;
  int row = index / cols;
  float x = contentRect.x + float(col) * (m_tileW + m_tileSpacing);
  float y = contentRect.y + float(row) * (m_tileH + m_tileSpacing) - m_scrollY;
  return Rect{x, y, m_tileW, m_tileH};
}

int AssetBrowser::tileAt(Vec2 p, Rect contentRect) const {
  if (!contentRect.contains(p)) return -1;
  for (size_t i = 0; i < m_filteredIndices.size(); i++) {
    if (tileRect(int(i), contentRect).contains(p)) {
      return int(i);
    }
  }
  return -1;
}

int AssetBrowser::folderAt(Vec2 p, Rect sourcesRect) const {
  if (!sourcesRect.contains(p)) return -1;
  float startY = sourcesRect.y + 24.0f;
  float itemH = 22.0f;
  for (size_t i = 0; i < m_folders.size(); i++) {
    Rect itemRect{sourcesRect.x + 4.0f, startY + float(i) * itemH, sourcesRect.w - 8.0f, itemH};
    if (itemRect.contains(p)) return int(i);
  }
  return -1;
}

// PRISM studio backdrop: matte well with a faint dot grid and a soft type-tinted floor line.
static void drawThumbBackdrop(Canvas& c, Rect thumb, Color tint) {
  c.fillRect(thumb, Color(19, 23, 31));
  c.pushClip(thumb);
  Color dot = Color(255, 255, 255, 14);
  for (float y = thumb.y + 6.0f; y < thumb.bottom(); y += 8.0f)
    for (float x = thumb.x + 6.0f; x < thumb.right(); x += 8.0f)
      c.fillRect(Rect{x, y, 1.0f, 1.0f}, dot);
  c.fillRect(Rect{thumb.x, thumb.bottom() - 10.0f, thumb.w, 10.0f}, tint.alpha(0.10f));
  c.popClip();
}

void AssetBrowser::drawAssetThumbnail(Canvas& c, const AssetRecord& asset, Rect thumb) {
  int tw = int(thumb.w);
  int th = int(thumb.h);

  if (asset.type.find("Material") != std::string::npos || asset.type.find("Mesh") != std::string::npos) {
    const uint32_t* px = PbrRenderer::getCachedThumbnail(asset.name, tw, th);
    if (px) {
      c.blit(px, tw, th, Vec2(thumb.x, thumb.y));
      return;
    }
  }

  // Studio backdrop: dark charcoal studio vignette
  drawThumbBackdrop(c, thumb, asset.badgeColor);

  // Studio horizon and soft floor bounce
  float floorY = thumb.bottom() - 10.0f;
  c.fillRect(Rect{thumb.x, floorY, thumb.w, thumb.bottom() - floorY}, Color(14, 18, 25, 220));
  c.line(Vec2(thumb.x, floorY), Vec2(thumb.right(), floorY), Color(42, 50, 68, 160), 1.0f);

  float cx = thumb.center().x;
  float cy = thumb.y + thumb.h * 0.44f;
  float r = 18.0f;

  if (asset.type.find("Mesh") != std::string::npos || asset.type.find("Material") != std::string::npos) {
    // Matte fallback glyph (the real previews come from PbrRenderer): flat fills, no specular/rim/bloom.
    Color base = asset.badgeColor;
    c.fillCircle(Vec2(cx, floorY + 1.0f), 17.0f, Color(0, 0, 0, 90));
    if (asset.name.find("Cube") != std::string::npos) {
      float s = 17.0f;
      Vec2 top[4] = { Vec2(cx, cy - s), Vec2(cx + s * 0.866f, cy - s * 0.5f), Vec2(cx, cy), Vec2(cx - s * 0.866f, cy - s * 0.5f) };
      Vec2 left[4] = { top[3], top[2], Vec2(cx, cy + s), Vec2(cx - s * 0.866f, cy + s * 0.5f) };
      Vec2 right[4] = { top[2], top[1], Vec2(cx + s * 0.866f, cy + s * 0.5f), Vec2(cx, cy + s) };
      c.fillPolygon(std::span<const Vec2>(top, 4), base.lighten(0.15f));
      c.fillPolygon(std::span<const Vec2>(left, 4), base);
      c.fillPolygon(std::span<const Vec2>(right, 4), base.darken(0.3f));
    } else if (asset.name.find("Cylinder") != std::string::npos) {
      Rect body{cx - 11.0f, cy - 10.0f, 22.0f, 24.0f};
      c.fillRect(body, base);
      c.fillRoundRect(Rect{body.x, body.y - 4.0f, body.w, 8.0f}, 4.0f, base.lighten(0.15f));
    } else if (asset.name.find("Cone") != std::string::npos) {
      Vec2 cone[3] = { Vec2(cx, cy - 17.0f), Vec2(cx - 14.0f, cy + 11.0f), Vec2(cx + 14.0f, cy + 11.0f) };
      c.fillPolygon(std::span<const Vec2>(cone, 3), base);
    } else if (asset.name.find("Plane") != std::string::npos) {
      Vec2 p[4] = { Vec2(cx - 19.0f, cy + 7.0f), Vec2(cx - 5.0f, cy - 9.0f), Vec2(cx + 19.0f, cy - 9.0f), Vec2(cx + 5.0f, cy + 7.0f) };
      c.fillPolygon(std::span<const Vec2>(p, 4), base);
    } else {
      c.fillCircle(Vec2(cx, cy), r, base);
      c.fillCircle(Vec2(cx + 3.0f, cy + 3.0f), r * 0.7f, base.darken(0.25f));
    }
  } else if (asset.type.find("Texture") != std::string::npos) {
    Rect texBox{cx - 20.0f, cy - 20.0f, 40.0f, 40.0f};
    if (asset.name.find("Normal") != std::string::npos) {
      // Tangent Normal Map (Base #8080FF with embossed normal sphere)
      c.fillRect(texBox, Color(128, 128, 255));
      // Embossed circle
      c.fillCircle(Vec2(cx, cy), 14.0f, Color(128, 128, 255));
      c.strokeCircle(Vec2(cx, cy), 14.0f, Color(190, 100, 255), 1.5f); // Red/Tangent rim
      c.strokeCircle(Vec2(cx - 1.0f, cy - 1.0f), 12.0f, Color(100, 210, 255), 1.5f); // Green/Bitangent rim
      c.fillCircle(Vec2(cx - 3.0f, cy - 3.0f), 7.0f, Color(160, 160, 255));
    } else {
      // Albedo UV 8x8 Checkerboard
      c.fillRect(texBox, Color(42, 48, 58));
      float chSz = 10.0f;
      for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
          if ((x + y) % 2 == 0) {
            c.fillRect(Rect{texBox.x + float(x) * chSz, texBox.y + float(y) * chSz, chSz, chSz}, Color(210, 215, 225));
          }
        }
      }
      // Axis crosshair lines
      c.line(Vec2(cx, texBox.y), Vec2(cx, texBox.bottom()), Color(255, 60, 60, 180), 1.0f);
      c.line(Vec2(texBox.x, cy), Vec2(texBox.right(), cy), Color(60, 220, 60, 180), 1.0f);
    }
    c.strokeRect(texBox, Color(68, 82, 105), 1.0f);

    // Resolution tag pill in bottom right
    Rect tag{texBox.right() - 15.0f, texBox.bottom() - 9.0f, 15.0f, 9.0f};
    c.fillRect(tag, Color(10, 14, 20, 230));
    c.textIn(*theme().small, Rect{tag.x, tag.y - 2.0f, tag.w, tag.h}, "1K", Color(200, 210, 225), HAlign::Center);
  } else if (asset.type.find("Shader") != std::string::npos) {
    // IDE Shader Preview Card
    Rect card{cx - 19.0f, cy - 20.0f, 38.0f, 40.0f};
    c.fillRoundRect(card, 3.0f, Color(15, 19, 26));
    c.strokeRoundRect(card, 3.0f, Color(48, 62, 85), 1.0f);
    // Tab header
    c.fillRect(Rect{card.x + 3.0f, card.y + 3.0f, 16.0f, 4.5f}, Color(34, 46, 68));
    // Line number column
    c.fillRect(Rect{card.x + 3.0f, card.y + 9.0f, 4.0f, card.h - 12.0f}, Color(28, 36, 48));
    // Syntax highlight bars
    c.fillRect(Rect{card.x + 9.0f, card.y + 10.0f, 9.0f, 2.2f}, Color(60, 160, 255)); // Keyword
    c.fillRect(Rect{card.x + 20.0f, card.y + 10.0f, 11.0f, 2.2f}, Color(215, 220, 230));
    c.fillRect(Rect{card.x + 11.0f, card.y + 15.0f, 20.0f, 2.2f}, Color(245, 165, 50)); // Function
    c.fillRect(Rect{card.x + 11.0f, card.y + 20.0f, 15.0f, 2.2f}, Color(85, 200, 110)); // Comment
    c.fillRect(Rect{card.x + 11.0f, card.y + 25.0f, 22.0f, 2.2f}, Color(185, 130, 245)); // Type
    c.fillRect(Rect{card.x + 9.0f, card.y + 31.0f, 8.0f, 2.2f}, Color(60, 160, 255));
  } else if (asset.type.find("Font") != std::string::npos) {
    // Typography Specimen
    c.line(Vec2(thumb.x + 8.0f, cy + 8.0f), Vec2(thumb.right() - 8.0f, cy + 8.0f), Color(0, 140, 230, 140), 1.0f); // Baseline
    c.line(Vec2(thumb.x + 8.0f, cy - 12.0f), Vec2(thumb.right() - 8.0f, cy - 12.0f), Color(0, 140, 230, 80), 1.0f); // Cap height
    c.textIn(*theme().semibold, Rect{thumb.x, cy - 15.0f, thumb.w, 28.0f}, "Aa", Color(245, 248, 255), HAlign::Center);
  } else if (asset.type.find("Level") != std::string::npos || asset.type.find("Scene") != std::string::npos) {
    // 3D Miniature Level World
    c.line(Vec2(thumb.x + 8.0f, floorY), Vec2(thumb.right() - 8.0f, floorY), Color(70, 180, 130), 1.5f);
    Rect box{cx - 11.0f, cy - 7.0f, 22.0f, 16.0f};
    c.strokeRect(box, Color(95, 215, 155), 1.5f);
    c.fillCircle(Vec2(cx + 13.0f, cy - 13.0f), 3.5f, Color(255, 215, 0));
    c.line(Vec2(cx + 13.0f, cy - 13.0f), Vec2(cx + 6.0f, cy - 6.0f), Color(255, 215, 0, 140), 1.0f);
  } else {
    drawIcon(c, asset.icon, Rect{cx - 13.0f, cy - 13.0f, 26.0f, 26.0f}, asset.badgeColor);
  }
}

void AssetBrowser::paint(Canvas& c) {
  const Theme& t = theme();

  // Background
  c.fillRect(m_rect, Color(16, 18, 24));

  // 1. Top Navigation Toolbar (32px)
  Rect navBar{m_rect.x, m_rect.y, m_rect.w, 32.0f};
  c.fillRect(navBar, Color(22, 26, 34));
  c.line(Vec2(navBar.x, navBar.bottom()), Vec2(navBar.right(), navBar.bottom()), Color(35, 42, 54), 1.0f);

  // Green "+ Add" Button (signature add button)
  Rect addBtn{navBar.x + 8.0f, navBar.y + 4.0f, 60.0f, 24.0f};
  c.fillRoundRect(addBtn, 3.0f, Color(24, 112, 54));
  c.strokeRoundRect(addBtn, 3.0f, Color(52, 178, 92), 1.0f);
  c.textIn(*t.semibold, Rect{addBtn.x, addBtn.y - 1.0f, addBtn.w, addBtn.h}, "+ Add", Color(255, 255, 255), HAlign::Center);

  // "Import" Button
  Rect impBtn{navBar.x + 72.0f, navBar.y + 4.0f, 54.0f, 24.0f};
  c.fillRoundRect(impBtn, 3.0f, Color(30, 36, 48));
  c.strokeRoundRect(impBtn, 3.0f, Color(48, 56, 72), 1.0f);
  c.textIn(*t.small, Rect{impBtn.x, impBtn.y - 1.0f, impBtn.w, impBtn.h}, "Import", Color(210, 215, 225), HAlign::Center);

  // "Save All" Button
  Rect saveBtn{navBar.x + 130.0f, navBar.y + 4.0f, 62.0f, 24.0f};
  c.fillRoundRect(saveBtn, 3.0f, Color(30, 36, 48));
  c.strokeRoundRect(saveBtn, 3.0f, Color(48, 56, 72), 1.0f);
  c.textIn(*t.small, Rect{saveBtn.x, saveBtn.y - 1.0f, saveBtn.w, saveBtn.h}, "Save All", Color(210, 215, 225), HAlign::Center);

  // Separator
  c.line(Vec2(navBar.x + 198.0f, navBar.y + 7.0f), Vec2(navBar.x + 198.0f, navBar.bottom() - 7.0f), Color(42, 50, 66), 1.0f);

  // "Folders" (Sources) Toggle Button
  Rect srcBtn{navBar.x + 204.0f, navBar.y + 4.0f, 74.0f, 24.0f};
  c.fillRoundRect(srcBtn, 3.0f, m_showSourcesPanel ? Color(36, 52, 76) : Color(28, 34, 46));
  c.strokeRoundRect(srcBtn, 3.0f, m_showSourcesPanel ? Color(65, 120, 205) : Color(45, 54, 70), 1.0f);
  c.textIn(*t.small, Rect{srcBtn.x, srcBtn.y - 1.0f, srcBtn.w, srcBtn.h}, "Folders", m_showSourcesPanel ? Color(235, 245, 255) : Color(180, 190, 205), HAlign::Center);

  // Breadcrumb Trail (content path)
  float breadX = navBar.x + 286.0f;
  std::string breadText = "Content > " + m_activeFolder;
  c.textIn(*t.small, Rect{breadX, navBar.y, 220.0f, navBar.h}, breadText, Color(165, 178, 195), HAlign::Left);

  // Search input in toolbar
  m_searchInput->paint(c);

  // 2. Left Sources Panel (Folder Tree)
  float contentTopY = navBar.bottom();
  float statusH = 20.0f;
  float contentH = std::max(0.0f, m_rect.h - navBar.h - statusH);

  float gridLeftX = m_rect.x + 8.0f;
  if (m_showSourcesPanel) {
    Rect srcRect{m_rect.x, contentTopY, m_sourcesW, contentH};
    c.fillRect(srcRect, Color(19, 22, 30));
    c.line(Vec2(srcRect.right(), srcRect.y), Vec2(srcRect.right(), srcRect.bottom()), Color(32, 38, 50), 1.0f);

    // Section header
    c.textIn(*t.small, Rect{srcRect.x + 10.0f, srcRect.y + 4.0f, srcRect.w - 20.0f, 18.0f}, "FOLDERS", Color(100, 112, 130), HAlign::Left);

    // Folder items
    float startY = srcRect.y + 24.0f;
    float itemH = 22.0f;
    for (size_t i = 0; i < m_folders.size(); i++) {
      const auto& folder = m_folders[i];
      Rect itemRect{srcRect.x + 4.0f, startY + float(i) * itemH, srcRect.w - 8.0f, itemH};
      bool isSel = (m_activeFolder == folder.name);
      bool isHov = (int(i) == m_hoverFolderIdx);

      if (isSel) {
        c.fillRoundRect(itemRect, 3.0f, Color(32, 54, 85));
      } else if (isHov) {
        c.fillRoundRect(itemRect, 3.0f, Color(26, 32, 44));
      }

      float indent = 8.0f + float(folder.depth) * 8.0f;
      std::string label = (folder.depth > 0 ? " " : "") + folder.name;
      c.textIn(*t.small, Rect{itemRect.x + indent, itemRect.y - 1.0f, itemRect.w - indent - 30.0f, itemRect.h}, label, isSel ? Color(255, 255, 255) : Color(205, 215, 230), HAlign::Left);

      // Count badge
      std::string cntStr = std::to_string(folder.count);
      c.textIn(*t.small, Rect{itemRect.right() - 28.0f, itemRect.y - 1.0f, 24.0f, itemRect.h}, cntStr, isSel ? Color(165, 205, 255) : Color(100, 112, 130), HAlign::Right);
    }

    gridLeftX = srcRect.right() + 8.0f;
  }

  // 3. Right Area: Filter Chips Row
  m_filterChips->paint(c);

  // 4. Asset Tile Cards Grid
  float tileAreaY = contentTopY + 34.0f;
  Rect contentRect{gridLeftX, tileAreaY, m_rect.right() - gridLeftX - 8.0f, std::max(0.0f, m_rect.bottom() - statusH - tileAreaY)};

  c.pushClip(contentRect);
  for (size_t i = 0; i < m_filteredIndices.size(); i++) {
    Rect tr = tileRect(int(i), contentRect);
    if (tr.y > contentRect.bottom() || tr.bottom() < contentRect.y) continue;

    const auto& asset = m_assets[m_filteredIndices[i]];
    bool isSel = (int(i) == m_selectedIdx);
    bool isHov = (int(i) == m_hoverIdx);

    const Color accent = t.accent;
    Color cardBg = isSel ? accent.alpha(0.16f).mix(Color(20, 24, 33), 0.0f) : (isHov ? Color(26, 32, 44) : Color(19, 22, 30));
    if (isSel) cardBg = Color(20, 24, 33).mix(accent, 0.14f);
    c.fillRoundRect(tr, 4.0f, cardBg);

    // Thumbnail well (86px)
    Rect thumbRect{tr.x + 3.0f, tr.y + 3.0f, tr.w - 6.0f, 86.0f};
    c.pushClip(thumbRect);
    drawAssetThumbnail(c, asset, thumbRect);
    if (isHov && !isSel) c.fillRect(thumbRect, Color(255, 255, 255, 10));
    c.popClip();
    c.strokeRect(thumbRect, Color(0, 0, 0, 90));

    // Type underline + chamfered type tab (PRISM facet marker: coloured, cut on the right)
    c.fillRect(Rect{thumbRect.x, thumbRect.bottom() - 2.0f, thumbRect.w, 2.0f}, asset.badgeColor.alpha(0.55f));
    float tabW = 34.0f, tabH = 15.0f, cut = 6.0f;
    Vec2 tab[4] = { Vec2(thumbRect.x, thumbRect.bottom() - tabH), Vec2(thumbRect.x + tabW, thumbRect.bottom() - tabH),
                    Vec2(thumbRect.x + tabW + cut, thumbRect.bottom()), Vec2(thumbRect.x, thumbRect.bottom()) };
    c.fillPolygon(std::span<const Vec2>(tab, 4), asset.badgeColor);
    Color tabText = asset.badgeColor.luminance() > 0.55f ? Color(14, 16, 22) : Color(255, 255, 255);
    c.textIn(*t.small, Rect{thumbRect.x, thumbRect.bottom() - tabH - 1.0f, tabW, tabH}, asset.badgeText, tabText, HAlign::Center);

    // Unsaved marker (top-right)
    if (asset.isModified) {
      Vec2 dc{thumbRect.right() - 9.0f, thumbRect.y + 9.0f};
      c.fillCircle(dc, 5.0f, Color(12, 14, 20, 220));
      c.fillCircle(dc, 3.2f, Color(245, 180, 60));
    }

    // Footer: left-aligned name + muted type
    Rect footerRect{tr.x + 3.0f, thumbRect.bottom() + 3.0f, tr.w - 6.0f, tr.bottom() - thumbRect.bottom() - 6.0f};
    Rect nameBox{footerRect.x + 2.0f, footerRect.y, footerRect.w - 4.0f, 16.0f};
    c.pushClip(nameBox);
    c.textIn(*t.medium, nameBox, cleanAssetName(asset.name), isSel ? Color(255, 255, 255) : Color(232, 237, 246), HAlign::Left);
    c.popClip();
    Rect typeBox{footerRect.x + 2.0f, footerRect.y + 17.0f, footerRect.w - 4.0f, 14.0f};
    c.pushClip(typeBox);
    c.textIn(*t.small, typeBox, asset.type, Color(128, 139, 156), HAlign::Left);
    c.popClip();

    // Frame: soft border; selection gets an accent frame with corner brackets
    c.strokeRoundRect(tr, 4.0f, isSel ? accent : (isHov ? Color(70, 86, 112) : Color(34, 40, 54)), isSel ? 1.5f : 1.0f);
    if (isSel) {
      float L = 9.0f;
      Color br = accent.lighten(0.25f);
      Rect q = tr.inflated(-1.0f);
      c.line(Vec2(q.x, q.y + L), Vec2(q.x, q.y), br, 2.0f);   c.line(Vec2(q.x, q.y), Vec2(q.x + L, q.y), br, 2.0f);
      c.line(Vec2(q.right() - L, q.y), Vec2(q.right(), q.y), br, 2.0f); c.line(Vec2(q.right(), q.y), Vec2(q.right(), q.y + L), br, 2.0f);
      c.line(Vec2(q.x, q.bottom() - L), Vec2(q.x, q.bottom()), br, 2.0f); c.line(Vec2(q.x, q.bottom()), Vec2(q.x + L, q.bottom()), br, 2.0f);
      c.line(Vec2(q.right() - L, q.bottom()), Vec2(q.right(), q.bottom()), br, 2.0f); c.line(Vec2(q.right(), q.bottom() - L), Vec2(q.right(), q.bottom()), br, 2.0f);
    }
  }

  if (m_filteredIndices.empty()) {
    c.textIn(*t.ui, contentRect, "No assets found matching filter in folder", t.textDim, HAlign::Center);
  }
  c.popClip();

  // Scrollbar indicator
  int cols = std::max(1, int(std::floor((contentRect.w + m_tileSpacing) / (m_tileW + m_tileSpacing))));
  int rows = int(std::ceil(float(m_filteredIndices.size()) / float(cols)));
  float totalH = float(rows) * (m_tileH + m_tileSpacing);
  float maxScroll = std::max(0.0f, totalH - contentRect.h);
  if (maxScroll > 0.0f) {
    float barH = std::max(20.0f, contentRect.h * (contentRect.h / totalH));
    float barY = contentRect.y + (m_scrollY / maxScroll) * (contentRect.h - barH);
    Rect thumbBar{contentRect.right() - 5.0f, barY, 4.0f, barH};
    c.fillRoundRect(thumbBar, 2.0f, Color(75, 90, 115, 200));
  }

  // 5. Bottom Status Bar (20px)
  Rect statusBar{m_rect.x, m_rect.bottom() - statusH, m_rect.w, statusH};
  c.fillRect(statusBar, Color(19, 22, 30));
  c.line(Vec2(statusBar.x, statusBar.y), Vec2(statusBar.right(), statusBar.y), Color(30, 36, 48), 1.0f);

  std::string statusText = std::to_string(m_filteredIndices.size()) + " items";
  if (m_selectedIdx >= 0) statusText += " (1 selected)";
  c.textIn(*t.small, Rect{statusBar.x + 8.0f, statusBar.y - 1.0f, 200.0f, statusBar.h}, statusText, Color(140, 150, 165), HAlign::Left);

  std::string viewText = "View: Tiles  ·  Size: 108px";
  c.textIn(*t.small, Rect{statusBar.right() - 208.0f, statusBar.y - 1.0f, 200.0f, statusBar.h}, viewText, Color(105, 115, 130), HAlign::Right);
}

Widget* AssetBrowser::hitTest(Vec2 p) {
  if (!m_visible || !m_rect.contains(p)) return nullptr;
  if (Widget* w = m_searchInput->hitTest(p)) return w;
  if (Widget* w = m_filterChips->hitTest(p)) return w;
  return this;
}

bool AssetBrowser::onMouseDown(const MouseEvent& e) {
  if (e.button == MouseButton::Left) {
    // Check Top Navigation Buttons
    Rect addBtn{m_rect.x + 8.0f, m_rect.y + 4.0f, 60.0f, 24.0f};
    if (addBtn.contains(e.pos)) {
      Application::get().notify("Open Add Asset Menu...", ToastLevel::Info);
      return true;
    }
    Rect impBtn{m_rect.x + 72.0f, m_rect.y + 4.0f, 54.0f, 24.0f};
    if (impBtn.contains(e.pos)) {
      Application::get().notify("Import Assets dialog...", ToastLevel::Info);
      return true;
    }
    Rect saveBtn{m_rect.x + 130.0f, m_rect.y + 4.0f, 62.0f, 24.0f};
    if (saveBtn.contains(e.pos)) {
      Application::get().notify("All content saved successfully.", ToastLevel::Success);
      return true;
    }
    Rect srcBtn{m_rect.x + 204.0f, m_rect.y + 4.0f, 74.0f, 24.0f};
    if (srcBtn.contains(e.pos)) {
      m_showSourcesPanel = !m_showSourcesPanel;
      arrange(m_rect);
      invalidate();
      return true;
    }

    // Check Left Sources Folder Tree
    if (m_showSourcesPanel) {
      float contentTopY = m_rect.y + 32.0f;
      float statusH = 20.0f;
      float contentH = std::max(0.0f, m_rect.h - 32.0f - statusH);
      Rect srcRect{m_rect.x, contentTopY, m_sourcesW, contentH};
      int fIdx = folderAt(e.pos, srcRect);
      if (fIdx >= 0) {
        setActiveFolder(m_folders[size_t(fIdx)].name);
        return true;
      }
    }

    // Check Tile selection
    float navH = 32.0f;
    float statusH = 20.0f;
    float tileAreaY = m_rect.y + navH + 34.0f;
    float gridLeftX = (m_showSourcesPanel ? m_rect.x + m_sourcesW + 8.0f : m_rect.x + 8.0f);
    Rect contentRect{gridLeftX, tileAreaY, m_rect.right() - gridLeftX - 8.0f, std::max(0.0f, m_rect.bottom() - statusH - tileAreaY)};

    int idx = tileAt(e.pos, contentRect);
    if (idx >= 0) {
      m_selectedIdx = idx;
      m_pressIdx = idx;
      m_pressPos = e.pos;
      m_dragging = false;
      invalidate();
      if (onAssetSelected) onAssetSelected(m_assets[m_filteredIndices[size_t(idx)]]);
      return true;
    }
  }
  return false;
}

bool AssetBrowser::onMouseMove(const MouseEvent& e) {
  float navH = 32.0f;
  float statusH = 20.0f;
  float tileAreaY = m_rect.y + navH + 34.0f;
  float gridLeftX = (m_showSourcesPanel ? m_rect.x + m_sourcesW + 8.0f : m_rect.x + 8.0f);
  Rect contentRect{gridLeftX, tileAreaY, m_rect.right() - gridLeftX - 8.0f, std::max(0.0f, m_rect.bottom() - statusH - tileAreaY)};

  // Check Drag initiation
  if (m_pressIdx >= 0 && !m_dragging && (e.pos - m_pressPos).length() > 6.0f) {
    m_dragging = true;
    const auto& asset = m_assets[m_filteredIndices[size_t(m_pressIdx)]];
    auto op = std::make_shared<DragDropOperation>("Asset: " + asset.name);
    op->setPayload(asset.path);

    // Custom thumbnail decorator
    auto badgeCol = asset.badgeColor;
    auto icon = asset.icon;
    auto name = asset.name;
    op->setDecorator([name, icon, badgeCol](Canvas& c, Vec2 mousePos) {
      Rect card{mousePos.x + 12.0f, mousePos.y + 12.0f, 130.0f, 32.0f};
      c.shadow(card, 2, 4, Color(0, 0, 0, 110));
      c.fillRoundRect(card, 4.0f, Color(35, 45, 60, 240));
      c.strokeRoundRect(card, 4.0f, badgeCol, 1);
      drawIcon(c, icon, Rect{card.x + 8.0f, card.y + 8.0f, 16.0f, 16.0f}, badgeCol);
      c.textIn(*theme().ui, Rect{card.x + 28.0f, card.y, card.w - 32.0f, card.h}, name, Color(240, 240, 240), HAlign::Left);
    });

    Application::get().startDragDrop(op);
    m_pressIdx = -1;
    return true;
  }

  // Hover over folders
  if (m_showSourcesPanel) {
    float contentTopY = m_rect.y + 32.0f;
    float contentH = std::max(0.0f, m_rect.h - 32.0f - statusH);
    Rect srcRect{m_rect.x, contentTopY, m_sourcesW, contentH};
    int hoverF = folderAt(e.pos, srcRect);
    if (hoverF != m_hoverFolderIdx) {
      m_hoverFolderIdx = hoverF;
      invalidate();
    }
  }

  // Hover over tiles
  int hover = tileAt(e.pos, contentRect);
  if (hover != m_hoverIdx) {
    m_hoverIdx = hover;
    invalidate();
  }
  return false;
}

bool AssetBrowser::onMouseUp(const MouseEvent&) {
  m_pressIdx = -1;
  m_dragging = false;
  return false;
}

bool AssetBrowser::onMouseWheel(const MouseEvent& e) {
  float navH = 32.0f;
  float statusH = 20.0f;
  float tileAreaY = m_rect.y + navH + 34.0f;
  float gridLeftX = (m_showSourcesPanel ? m_rect.x + m_sourcesW + 8.0f : m_rect.x + 8.0f);
  Rect contentRect{gridLeftX, tileAreaY, m_rect.right() - gridLeftX - 8.0f, std::max(0.0f, m_rect.bottom() - statusH - tileAreaY)};

  int cols = std::max(1, int(std::floor((contentRect.w + m_tileSpacing) / (m_tileW + m_tileSpacing))));
  int rows = int(std::ceil(float(m_filteredIndices.size()) / float(cols)));
  float totalH = float(rows) * (m_tileH + m_tileSpacing);
  float maxScroll = std::max(0.0f, totalH - contentRect.h);

  m_scrollY -= e.wheel.y * 36.0f;
  m_scrollY = std::clamp(m_scrollY, 0.0f, maxScroll);
  invalidate();
  return true;
}

void AssetBrowser::onMouseLeave() {
  m_hoverIdx = -1;
  m_hoverFolderIdx = -1;
  invalidate();
}

} // namespace prism
