// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_asset_thumbnail.h"

#include "editor.h"

#include "prism/Application.h"
#include "prism/Canvas.h"
#include "prism/Theme.h"

#include <GL/gl.h>

#include <algorithm>
#include <system_error>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

constexpr float kSmallUVE = 64.0F;   // as big as the standard slot thumbnail
constexpr float kLargeUVE = 240.0F;

/// The large look at a slot's preview: the picture and the asset's name.
class LargePreviewUVE final : public prism::Widget {
public:
    LargePreviewUVE(std::vector<std::uint32_t> pixels, const int width, const int height, std::string name)
        : m_pixels(std::move(pixels)), m_width(width), m_height(height), m_name(std::move(name)) {}
    prism::Vec2 measure(prism::Vec2) override { return m_desired = constrain(prism::Vec2(kLargeUVE + 12.0F, kLargeUVE + 34.0F)); }
    void paint(prism::Canvas& c) override {
        const prism::Theme& t = prism::theme();
        const prism::Rect r = rect();
        c.fillRoundRect(r, 5.0F, t.panel);
        c.strokeRoundRect(r, 5.0F, t.borderStrong, 1);
        const prism::Rect image{r.x + 6.0F, r.y + 6.0F, kLargeUVE, kLargeUVE};
        c.fillRect(image, t.window);
        c.blitBlend(m_pixels.data(), m_width, m_height, m_width, image);
        c.textIn(*t.small, prism::Rect{r.x + 8.0F, image.bottom() + 4.0F, r.w - 16.0F, 20.0F}, m_name, t.textBright,
                 prism::HAlign::Left, true);
    }
    bool onMouseDown(const prism::MouseEvent&) override {
        prism::Application::get().closePopup(this);
        return true;
    }

private:
    std::vector<std::uint32_t> m_pixels;
    int m_width = 0;
    int m_height = 0;
    std::string m_name;
};

} // namespace

bool ReadTexturePixelsUVE(const std::uintptr_t texture, const bool bottomUp, int& width, int& height,
                          std::vector<std::uint32_t>& pixels) {
    if (texture == 0U) {
        return false;
    }
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture));
    GLint w = 0;
    GLint h = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
        return false;
    }
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4U);
    GLint previousPack = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPack);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glPixelStorei(GL_PACK_ALIGNMENT, previousPack);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    width = w;
    height = h;
    pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        const int sy = bottomUp ? (h - 1 - y) : y;
        for (int x = 0; x < w; ++x) {
            const std::uint8_t* p =
                rgba.data() + (static_cast<std::size_t>(sy) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 4U;
            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] =
                (static_cast<std::uint32_t>(p[3]) << 24U) | (static_cast<std::uint32_t>(p[0]) << 16U) |
                (static_cast<std::uint32_t>(p[1]) << 8U) | static_cast<std::uint32_t>(p[2]);
        }
    }
    return true;
}

AssetThumbnailUVE::AssetThumbnailUVE(EditorUVE& editor, const KindUVE kind) : m_editor(editor), m_kind(kind) {
    setTooltip("Click to see it large");
}

void AssetThumbnailUVE::setAssetUVE(const std::filesystem::path& absolute) {
    std::filesystem::path relative;
    if (!absolute.empty()) {
        const std::filesystem::path root = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
        std::error_code error;
        relative = std::filesystem::relative(absolute, root, error);
        if (error) {
            relative.clear();
        }
    }
    const bool has = !relative.empty();
    if (relative != m_relative || has != m_hasAsset) {
        m_relative = relative;
        m_hasAsset = has;
        m_texture = 0U;
        m_pixels.clear();
        invalidate();
    }
    refreshUVE();
}

void AssetThumbnailUVE::refreshUVE() {
    if (!m_hasAsset) {
        return;
    }
    std::uintptr_t texture = 0U;
    switch (m_kind) {
        case KindUVE::Material: texture = m_editor.GetMaterialThumbnailUVE(m_relative); break;
        case KindUVE::Mesh: texture = m_editor.GetMeshThumbnailUVE(m_relative); break;
        case KindUVE::Texture: texture = m_editor.GetTextureThumbnailUVE(m_relative); break;
    }
    if (texture == 0U || texture == m_texture) {
        return;
    }
    // A material preview is a render target (bottom-up); a texture is an uploaded image, and the
    // mesh preview is drawn upright already (its projection is flipped for top-down images).
    if (ReadTexturePixelsUVE(texture, m_kind == KindUVE::Material, m_width, m_height, m_pixels)) {
        m_texture = texture;
        invalidate();
    }
}

prism::Vec2 AssetThumbnailUVE::measure(prism::Vec2) {
    const float side = kSmallUVE;
    return m_desired = constrain(prism::Vec2(side, side));
}

void AssetThumbnailUVE::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    const float side = std::min(rect().w, rect().h);
    const prism::Rect r{rect().x, rect().y, side, side};
    c.fillRect(r, t.window);
    if (m_hasAsset && !m_pixels.empty()) {
        c.blitBlend(m_pixels.data(), m_width, m_height, m_width, prism::Rect{r.x + 1.0F, r.y + 1.0F, r.w - 2.0F, r.h - 2.0F});
    } else {
        c.textIn(*t.small, r, m_hasAsset ? "..." : "Empty", t.textDim, prism::HAlign::Center, true);
    }
    c.strokeRect(r, hovered() ? t.accent : t.border, 1);
}

bool AssetThumbnailUVE::onMouseDown(const prism::MouseEvent& e) {
    if (e.button != prism::MouseButton::Left || m_pixels.empty() || !prism::Application::exists()) {
        return e.button == prism::MouseButton::Left;
    }
    // Large, in a popup beside the slot, the slot itself unchanged; a click anywhere closes it.
    auto large = prism::make<LargePreviewUVE>(m_pixels, m_width, m_height, m_relative.filename().string());
    prism::PopupOptions options{};
    options.placement = prism::PopupPlacement::Below;
    options.anchor = rect();
    prism::Application::get().showPopup(large, options);
    return true;
}

} // namespace UVE::Editor
