// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "scene_object_registry.h"

#include "prism/Icons.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace UVE::Editor {

/// The editor's own icons (Engine/Editor/EditorCore/assets/icons, embedded at build time) decoded to
/// PRISM's pixel format (0xAARRGGBB, straight alpha) at the size a widget draws them. PRISM only draws;
/// what an object or a content type looks like is the engine's.
class EditorIconsUVE final {
public:
    struct ImageUVE final {
        const std::uint32_t* pixels = nullptr;
        int size = 0;
        [[nodiscard]] explicit operator bool() const noexcept { return pixels != nullptr; }
    };

    /// Golden dark brown: how a folder reads in the Outliner and the Content Browser.
    static constexpr std::uint32_t kFolderTintUVE = 0xFFB07C2EU;

    /// The icon of a scene object kind (a Folder object uses the folder icon, tinted).
    [[nodiscard]] ImageUVE ObjectUVE(Scene::Objects::SceneObjectKindUVE kind, int size);
    /// A content-type icon by the Content Browser's type label ("Mesh", "Folder", "Viewport3D", ...).
    /// A type with no picture of its own gets `fallback` drawn at that size, never another type's picture.
    [[nodiscard]] ImageUVE ContentTypeUVE(std::string_view typeLabel, int size, prism::Icon fallback = prism::Icon::None);
    /// A PRISM glyph drawn once at `size` (light grey, straight alpha) and kept.
    [[nodiscard]] ImageUVE GlyphUVE(prism::Icon icon, int size);

private:
    [[nodiscard]] ImageUVE GetUVE(int group, std::string_view name, int size, bool folderTint);
    std::map<std::string, std::vector<std::uint32_t>> m_cache;
};

} // namespace UVE::Editor
