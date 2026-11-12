// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_prism_icons.h"

#include "editor_icon_set.h"

#include "prism/Canvas.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace UVE::Editor {

EditorPrismIconsUVE::ImageUVE EditorPrismIconsUVE::ObjectUVE(const Scene::Objects::SceneObjectKindUVE kind, const int size) {
    if (kind == Scene::Objects::SceneObjectKindUVE::Folder) {
        return GetUVE(static_cast<int>(EditorIconGroupUVE::ContentType), "folder", size, true);
    }
    const Scene::Objects::SceneObjectDescriptorUVE* const descriptor = Scene::Objects::FindSceneObjectDescriptorUVE(kind);
    if (descriptor == nullptr) {
        return {};
    }
    if (ImageUVE image = GetUVE(static_cast<int>(EditorIconGroupUVE::Object), descriptor->typeId, size, false)) {
        return image;
    }
    // Kinds with no picture of their own get their own glyph.
    switch (kind) {
        case Scene::Objects::SceneObjectKindUVE::MontagePlayer: return GlyphUVE(prism::Icon::MontagePlayer, size);
        default: return {};
    }
}

EditorPrismIconsUVE::ImageUVE EditorPrismIconsUVE::ContentTypeUVE(const std::string_view typeLabel, const int size, const prism::Icon fallback) {
    // Viewport3D is the label of a scene asset; its icon source keeps the identifier spelling.
    const std::string_view name = typeLabel == "Viewport3D" ? std::string_view{"viewport_3d"} : typeLabel;
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool folder = lower == "folder" || lower == "folder_open";
    ImageUVE image = GetUVE(static_cast<int>(EditorIconGroupUVE::ContentType), name, size, folder);
    if (!image && fallback != prism::Icon::None) {
        image = GlyphUVE(fallback, size);
    }
    if (!image) {
        image = GetUVE(static_cast<int>(EditorIconGroupUVE::ContentType), "file", size, false);
    }
    return image;
}

EditorPrismIconsUVE::ImageUVE EditorPrismIconsUVE::GlyphUVE(const prism::Icon icon, const int size) {
    if (icon == prism::Icon::None || size <= 0) {
        return {};
    }
    const std::string key = "glyph/" + std::to_string(static_cast<int>(icon)) + "/" + std::to_string(size);
    if (const auto it = m_cache.find(key); it != m_cache.end()) {
        return it->second.empty() ? ImageUVE{} : ImageUVE{it->second.data(), size};
    }
    // White on black, then the brightness becomes the alpha of a light grey glyph.
    prism::Canvas canvas;
    canvas.resize(size, size);
    canvas.clear(prism::Color(0, 0, 0));
    const float pad = static_cast<float>(size) * 0.12F;
    prism::drawIcon(canvas, icon, prism::Rect{pad, pad, static_cast<float>(size) - 2.0F * pad, static_cast<float>(size) - 2.0F * pad},
                    prism::Color(255, 255, 255), std::max(1.0F, static_cast<float>(size) / 16.0F * 1.2F));
    std::vector<std::uint32_t>& out = m_cache[key];
    out.resize(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
    for (std::size_t i = 0U; i < out.size(); ++i) {
        const std::uint32_t pixel = canvas.pixels()[i];
        const std::uint32_t a = std::max({(pixel >> 16U) & 0xFFU, (pixel >> 8U) & 0xFFU, pixel & 0xFFU});
        out[i] = (a << 24U) | 0x00D8DCE4U;
    }
    return ImageUVE{out.data(), size};
}

EditorPrismIconsUVE::ImageUVE EditorPrismIconsUVE::GetUVE(const int group, const std::string_view name, const int size,
                                                          const bool folderTint) {
    const std::string key = std::to_string(group) + "/" + std::string(name) + "/" + std::to_string(size);
    if (const auto it = m_cache.find(key); it != m_cache.end()) {
        return it->second.empty() ? ImageUVE{} : ImageUVE{it->second.data(), size};
    }
    std::vector<std::uint32_t>& out = m_cache[key];
    const EditorIconSourceUVE* const source = FindEditorIconSourceUVE(static_cast<EditorIconGroupUVE>(group), name);
    if (source == nullptr) {
        return {};
    }
    std::vector<EditorIconLevelUVE> levels;
    try {
        levels = DecodeEditorIconUVE(source->png);
    } catch (...) {
        return {};
    }
    // The mip level whose size matches (the chain halves down to 1x1); else the nearest larger one.
    const EditorIconLevelUVE* level = nullptr;
    for (const EditorIconLevelUVE& candidate : levels) {
        if (candidate.size >= size && (level == nullptr || candidate.size < level->size)) {
            level = &candidate;
        }
    }
    if (level == nullptr || level->size <= 0) {
        return {};
    }
    // Nearest-sample to the requested size when the chain has no exact level.
    const int src = level->size;
    out.resize(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
    const std::uint32_t tr = (kFolderTintUVE >> 16) & 0xFFU;
    const std::uint32_t tg = (kFolderTintUVE >> 8) & 0xFFU;
    const std::uint32_t tb = kFolderTintUVE & 0xFFU;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const std::size_t si = (static_cast<std::size_t>(y * src / size) * static_cast<std::size_t>(src) +
                                    static_cast<std::size_t>(x * src / size)) * 4U;
            std::uint32_t r = level->rgba[si];
            std::uint32_t g = level->rgba[si + 1U];
            std::uint32_t b = level->rgba[si + 2U];
            const std::uint32_t a = level->rgba[si + 3U];
            if (folderTint) {
                // Keep the icon's shading, recolour it golden dark brown.
                const std::uint32_t lum = std::max({r, g, b});
                const std::uint32_t shade = 110U + (lum * 145U) / 255U;
                r = (tr * shade) / 255U;
                g = (tg * shade) / 255U;
                b = (tb * shade) / 255U;
            }
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)] =
                (a << 24U) | (r << 16U) | (g << 8U) | b;
        }
    }
    return ImageUVE{out.data(), size};
}

} // namespace UVE::Editor
