// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "prism/Widget.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace UVE::Editor {

class EditorUVE;

/// Reads an engine GL texture back into ARGB pixels (top row first). `bottomUp` for render targets
/// (material and mesh previews). False when there is no readable texture.
bool ReadTexturePixelsUVE(std::uintptr_t texture, bool bottomUp, int& width, int& height,
                          std::vector<std::uint32_t>& pixels);

/// The thumbnail beside an asset slot in Details, as Unreal shows one: the engine's own preview of
/// the asset in the slot - a material on its lit PBR sphere, a model, a texture - small, and a click
/// shows it large in a popup. It follows the slot: a new asset, or the material saved again, shows at once.
class AssetThumbnailUVE final : public prism::Widget {
public:
    enum class KindUVE : std::uint8_t { Material, Mesh, Texture };
    AssetThumbnailUVE(EditorUVE& editor, KindUVE kind);
    /// The asset shown (absolute path, empty for none); re-reads the preview when it changed.
    void setAssetUVE(const std::filesystem::path& absolute);
    /// Picks up a preview that was rendered or re-rendered since the last call.
    void refreshUVE();
    prism::Vec2 measure(prism::Vec2) override;
    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;

private:
    EditorUVE& m_editor;
    KindUVE m_kind;
    std::filesystem::path m_relative;
    bool m_hasAsset = false;
    std::uintptr_t m_texture = 0U;
    int m_width = 0;
    int m_height = 0;
    std::vector<std::uint32_t> m_pixels;
};

} // namespace UVE::Editor
