// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_ui_assets.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gl_system_headers.h"

#include "editor_icon_set.h"

namespace UVE::Editor {

namespace {

constexpr int kLogoWidth = 24;
constexpr int kLogoHeight = 24;
constexpr int kEditorIconWidth = 20;
constexpr int kEditorIconHeight = 20;

#include "univex_logo_display_bytes.inc"
#include "uve_general_icon_bytes.inc"

struct IconSource final {
    std::string_view key;
    const std::uint8_t* pixels = nullptr;
};

constexpr std::array<IconSource, 2U> kGeneralIconSources{{
    {"plugin", uve_general_icon_plugin_rgba.data()},
    {"snap", uve_general_icon_snap_rgba.data()},
}};

[[nodiscard]] std::uintptr_t UploadTexture(const std::uint8_t* const pixels, const int width,
                                              const int height) noexcept {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return 0U;
    }

    GLint previousTexture = 0;
    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);

    GLuint texture = 0U;
    glGenTextures(1, &texture);
    if (texture == 0U) {
        return 0U;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    const GLenum uploadError = glGetError();
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    if (uploadError != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return 0U;
    }

    return static_cast<std::uintptr_t>(texture);
}

// An icon is drawn anywhere from 16 px in a tree row to 44 px on a content card, so it carries
// every mip level: sampled at 16 px from a 64 px texture, it reads the 16 px level built by
// averaging rather than skipping three pixels in four. The chain runs down to 1x1, which makes the
// texture complete without GL_TEXTURE_MAX_LEVEL (not in every platform's GL 1.1 header).
[[nodiscard]] std::uintptr_t UploadIconTexture(const std::span<const std::uint8_t> png) noexcept {
    std::vector<EditorIconLevel> levels;
    try {
        levels = DecodeEditorIcon(png);
    } catch (...) {
        return 0U;
    }
    if (levels.empty()) {
        return 0U;
    }

    GLint previousTexture = 0;
    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);

    GLuint texture = 0U;
    glGenTextures(1, &texture);
    if (texture == 0U) {
        return 0U;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (std::size_t level = 0U; level < levels.size(); ++level) {
        glTexImage2D(GL_TEXTURE_2D, static_cast<GLint>(level), GL_RGBA, levels[level].size, levels[level].size, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, levels[level].rgba.data());
    }

    const GLenum uploadError = glGetError();
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    if (uploadError != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return 0U;
    }
    return static_cast<std::uintptr_t>(texture);
}

[[nodiscard]] std::uintptr_t FindIconTextureId(const std::vector<std::uintptr_t>& textureIds,
                                                  const EditorIconGroup group,
                                                  const std::string_view name) noexcept {
    const EditorIconSource* const source = FindEditorIconSource(group, name);
    if (source == nullptr) {
        return 0U;
    }
    const auto index = static_cast<std::size_t>(source - GetEditorIconSources().data());
    return index < textureIds.size() ? textureIds[index] : 0U;
}

void DeleteTexture(std::uintptr_t& textureId) noexcept {
    if (textureId == 0U) {
        return;
    }
    const GLuint texture = static_cast<GLuint>(textureId);
    glDeleteTextures(1, &texture);
    textureId = 0U;
}

template <std::size_t Size>
void DeleteTextureSet(std::array<std::uintptr_t, Size>& textureIds) noexcept {
    for (std::uintptr_t& textureId : textureIds) {
        DeleteTexture(textureId);
    }
}

template <std::size_t Size>
[[nodiscard]] std::uintptr_t FindTextureId(const std::array<IconSource, Size>& sources,
                                               const std::array<std::uintptr_t, Size>& textureIds,
                                               const std::string_view key) noexcept {
    for (std::size_t index = 0U; index < sources.size(); ++index) {
        if (sources[index].key == key) {
            return textureIds[index];
        }
    }
    return 0U;
}

} // namespace

bool EditorUiAssets::Initialize() noexcept {
    if (IsReady()) {
        return true;
    }

    m_logoTextureId = UploadTexture(univex_logo_uve_display_rgba.data(), kLogoWidth, kLogoHeight);
    for (std::size_t index = 0U; index < kGeneralIconSources.size(); ++index) {
        m_generalIconTextureIds[index] = UploadTexture(kGeneralIconSources[index].pixels,
                                                           kEditorIconWidth, kEditorIconHeight);
    }
    const std::span<const EditorIconSource> icons = GetEditorIconSources();
    try {
        m_iconTextureIds.assign(icons.size(), 0U);
    } catch (...) {
        Shutdown();
        return false;
    }
    for (std::size_t index = 0U; index < icons.size(); ++index) {
        m_iconTextureIds[index] = UploadIconTexture(icons[index].png);
    }
    for (const Scene::Objects::SceneObjectDescriptor& descriptor : Scene::Objects::GetSceneObjectDescriptors()) {
        const auto kind = static_cast<std::size_t>(descriptor.kind);
        if (kind < m_objectIconTextureIds.size()) {
            // A Folder in the Scene panel looks like the folders in the Asset Table.
            m_objectIconTextureIds[kind] =
                descriptor.kind == Scene::Objects::SceneObjectKind::Folder
                    ? FindIconTextureId(m_iconTextureIds, EditorIconGroup::ContentType, "folder")
                    : FindIconTextureId(m_iconTextureIds, EditorIconGroup::Object, descriptor.typeId);

        }
    }

    if (!IsReady()) {
        Shutdown();
        return false;
    }
    return true;
}

void EditorUiAssets::Shutdown() noexcept {
    DeleteTexture(m_logoTextureId);
    DeleteTextureSet(m_generalIconTextureIds);
    for (std::uintptr_t& textureId : m_iconTextureIds) {
        DeleteTexture(textureId);
    }
    m_iconTextureIds.clear();
    m_objectIconTextureIds.fill(0U);
}

bool EditorUiAssets::IsReady() const noexcept {
    return m_logoTextureId != 0U &&
           std::all_of(m_generalIconTextureIds.cbegin(), m_generalIconTextureIds.cend(),
                       [](const std::uintptr_t textureId) { return textureId != 0U; }) &&
           !m_iconTextureIds.empty() &&
           std::all_of(m_iconTextureIds.cbegin(), m_iconTextureIds.cend(),
                       [](const std::uintptr_t textureId) { return textureId != 0U; });
}

std::uintptr_t EditorUiAssets::GetLogoTextureId() const noexcept {
    return m_logoTextureId;
}

std::uintptr_t EditorUiAssets::GetGeneralIconTextureId(const std::string_view iconId) const noexcept {
    const std::uintptr_t toolbarIcon = FindIconTextureId(m_iconTextureIds, EditorIconGroup::Toolbar, iconId);
    if (toolbarIcon != 0U) {
        return toolbarIcon;
    }
    return FindTextureId(kGeneralIconSources, m_generalIconTextureIds, iconId);
}

std::uintptr_t EditorUiAssets::GetObjectIconTextureId(const Scene::Objects::SceneObjectKind kind) const noexcept {
    const auto index = static_cast<std::size_t>(kind);
    return index < m_objectIconTextureIds.size() ? m_objectIconTextureIds[index] : 0U;
}

std::uintptr_t EditorUiAssets::GetObjectCardIconTextureId(const Scene::Objects::SceneObjectKind kind) const noexcept {
    return GetObjectIconTextureId(kind);
}

std::uintptr_t EditorUiAssets::GetObjectCategoryIconTextureId(const std::string_view category) const noexcept {
    return FindIconTextureId(m_iconTextureIds, EditorIconGroup::ObjectCategory, category);
}

std::uintptr_t EditorUiAssets::GetContentTypeIconTextureId(const std::string_view typeId) const noexcept {
    // Viewport3D is the user-facing label for a scene asset, while icon source names use the
    // stable identifier spelling shared by the catalogue and asset filenames.
    if (typeId == "Viewport3D") {
        return FindIconTextureId(m_iconTextureIds, EditorIconGroup::ContentType, "viewport_3d");
    }
    return FindIconTextureId(m_iconTextureIds, EditorIconGroup::ContentType, typeId);
}

std::uintptr_t EditorUiAssets::GetContentTypeCardIconTextureId(const std::string_view typeId) const noexcept {
    return GetContentTypeIconTextureId(typeId);
}

std::uintptr_t EditorUiAssets::GetComponentIconTextureId(const std::string_view componentId) const noexcept {
    std::string key{componentId};
    std::replace(key.begin(), key.end(), '.', '_');
    if (key.starts_with("component_")) {
        return FindIconTextureId(m_iconTextureIds, EditorIconGroup::Component, key);
    }
    const std::uintptr_t direct = FindIconTextureId(m_iconTextureIds, EditorIconGroup::Component, "component_" + key);
    if (direct != 0U) {
        return direct;
    }
    return FindIconTextureId(m_iconTextureIds, EditorIconGroup::Component, key);
}

std::uintptr_t EditorUiAssets::UploadDynamicTexture(const std::uint8_t* const pixels, const int width,
                                                           const int height) noexcept {
    return UploadTexture(pixels, width, height);
}

void EditorUiAssets::DeleteDynamicTexture(std::uintptr_t& textureId) noexcept {
    DeleteTexture(textureId);
}

} // namespace UVE::Editor
