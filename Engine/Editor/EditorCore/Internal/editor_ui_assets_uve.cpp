// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/editor/editor_ui_assets_uve.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <GL/gl.h>

#include "editor_icon_set_uve.h"

namespace UVE::Editor {

namespace {

constexpr int kLogoWidthUVE = 24;
constexpr int kLogoHeightUVE = 24;
constexpr int kEditorIconWidthUVE = 20;
constexpr int kEditorIconHeightUVE = 20;

#include "univex_logo_uve_display_bytes.inc"
#include "uve_general_icon_bytes.inc"

struct IconSourceUVE final {
    std::string_view key;
    const std::uint8_t* pixels = nullptr;
};

constexpr std::array<IconSourceUVE, 2U> kGeneralIconSourcesUVE{{
    {"plugin", uve_general_icon_plugin_rgba.data()},
    {"snap", uve_general_icon_snap_rgba.data()},
}};

[[nodiscard]] std::uintptr_t UploadTextureUVE(const std::uint8_t* const pixels, const int width,
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
[[nodiscard]] std::uintptr_t UploadIconTextureUVE(const std::span<const std::uint8_t> png) noexcept {
    std::vector<EditorIconLevelUVE> levels;
    try {
        levels = DecodeEditorIconUVE(png);
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

[[nodiscard]] std::uintptr_t FindIconTextureIdUVE(const std::vector<std::uintptr_t>& textureIds,
                                                  const EditorIconGroupUVE group,
                                                  const std::string_view name) noexcept {
    const EditorIconSourceUVE* const source = FindEditorIconSourceUVE(group, name);
    if (source == nullptr) {
        return 0U;
    }
    const auto index = static_cast<std::size_t>(source - GetEditorIconSourcesUVE().data());
    return index < textureIds.size() ? textureIds[index] : 0U;
}

void DeleteTextureUVE(std::uintptr_t& textureId) noexcept {
    if (textureId == 0U) {
        return;
    }
    const GLuint texture = static_cast<GLuint>(textureId);
    glDeleteTextures(1, &texture);
    textureId = 0U;
}

template <std::size_t Size>
void DeleteTextureSetUVE(std::array<std::uintptr_t, Size>& textureIds) noexcept {
    for (std::uintptr_t& textureId : textureIds) {
        DeleteTextureUVE(textureId);
    }
}

template <std::size_t Size>
[[nodiscard]] std::uintptr_t FindTextureIdUVE(const std::array<IconSourceUVE, Size>& sources,
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

bool EditorUiAssetsUVE::InitializeUVE() noexcept {
    if (IsReadyUVE()) {
        return true;
    }

    m_logoTextureId = UploadTextureUVE(univex_logo_uve_display_rgba.data(), kLogoWidthUVE, kLogoHeightUVE);
    for (std::size_t index = 0U; index < kGeneralIconSourcesUVE.size(); ++index) {
        m_generalIconTextureIds[index] = UploadTextureUVE(kGeneralIconSourcesUVE[index].pixels,
                                                           kEditorIconWidthUVE, kEditorIconHeightUVE);
    }
    const std::span<const EditorIconSourceUVE> icons = GetEditorIconSourcesUVE();
    try {
        m_iconTextureIds.assign(icons.size(), 0U);
    } catch (...) {
        ShutdownUVE();
        return false;
    }
    for (std::size_t index = 0U; index < icons.size(); ++index) {
        m_iconTextureIds[index] = UploadIconTextureUVE(icons[index].png);
    }
    for (const Scene::Nodes::SceneNodeDescriptorUVE& descriptor : Scene::Nodes::GetSceneNodeDescriptorsUVE()) {
        const auto kind = static_cast<std::size_t>(descriptor.kind);
        if (kind < m_nodeIconTextureIds.size()) {
            // A Folder in the Scene panel looks like the folders in the Content Browser.
            m_nodeIconTextureIds[kind] =
                descriptor.kind == Scene::Nodes::SceneNodeKindUVE::Folder
                    ? FindIconTextureIdUVE(m_iconTextureIds, EditorIconGroupUVE::ContentType, "folder")
                    : FindIconTextureIdUVE(m_iconTextureIds, EditorIconGroupUVE::Node, descriptor.typeId);
        }
    }

    if (!IsReadyUVE()) {
        ShutdownUVE();
        return false;
    }
    return true;
}

void EditorUiAssetsUVE::ShutdownUVE() noexcept {
    DeleteTextureUVE(m_logoTextureId);
    DeleteTextureSetUVE(m_generalIconTextureIds);
    for (std::uintptr_t& textureId : m_iconTextureIds) {
        DeleteTextureUVE(textureId);
    }
    m_iconTextureIds.clear();
    m_nodeIconTextureIds.fill(0U);
}

bool EditorUiAssetsUVE::IsReadyUVE() const noexcept {
    return m_logoTextureId != 0U &&
           std::all_of(m_generalIconTextureIds.cbegin(), m_generalIconTextureIds.cend(),
                       [](const std::uintptr_t textureId) { return textureId != 0U; }) &&
           !m_iconTextureIds.empty() &&
           std::all_of(m_iconTextureIds.cbegin(), m_iconTextureIds.cend(),
                       [](const std::uintptr_t textureId) { return textureId != 0U; });
}

std::uintptr_t EditorUiAssetsUVE::GetLogoTextureIdUVE() const noexcept {
    return m_logoTextureId;
}

std::uintptr_t EditorUiAssetsUVE::GetGeneralIconTextureIdUVE(const std::string_view iconId) const noexcept {
    return FindTextureIdUVE(kGeneralIconSourcesUVE, m_generalIconTextureIds, iconId);
}

std::uintptr_t EditorUiAssetsUVE::GetNodeIconTextureIdUVE(const Scene::Nodes::SceneNodeKindUVE kind) const noexcept {
    const auto index = static_cast<std::size_t>(kind);
    return index < m_nodeIconTextureIds.size() ? m_nodeIconTextureIds[index] : 0U;
}

std::uintptr_t EditorUiAssetsUVE::GetNodeCategoryIconTextureIdUVE(const std::string_view category) const noexcept {
    return FindIconTextureIdUVE(m_iconTextureIds, EditorIconGroupUVE::NodeCategory, category);
}

std::uintptr_t EditorUiAssetsUVE::GetContentTypeIconTextureIdUVE(const std::string_view typeId) const noexcept {
    return FindIconTextureIdUVE(m_iconTextureIds, EditorIconGroupUVE::ContentType, typeId);
}

std::uintptr_t EditorUiAssetsUVE::UploadDynamicTextureUVE(const std::uint8_t* const pixels, const int width,
                                                           const int height) noexcept {
    return UploadTextureUVE(pixels, width, height);
}

void EditorUiAssetsUVE::DeleteDynamicTextureUVE(std::uintptr_t& textureId) noexcept {
    DeleteTextureUVE(textureId);
}

} // namespace UVE::Editor
