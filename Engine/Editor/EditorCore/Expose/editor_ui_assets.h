#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "scene_object_registry.h"

namespace UVE::Editor {

/// Owns the exact-preservation raster assets used only by the native editor chrome.
/// The OpenGL texture implementation remains private to this editor-owned service.
class EditorUiAssets final {
public:
    EditorUiAssets() = default;
    EditorUiAssets(const EditorUiAssets&) = delete;
    EditorUiAssets& operator=(const EditorUiAssets&) = delete;
    EditorUiAssets(EditorUiAssets&&) = delete;
    EditorUiAssets& operator=(EditorUiAssets&&) = delete;
    ~EditorUiAssets() = default;

    [[nodiscard]] bool Initialize() noexcept;
    void Shutdown() noexcept;

    [[nodiscard]] bool IsReady() const noexcept;
    [[nodiscard]] std::uintptr_t GetLogoTextureId() const noexcept;
    [[nodiscard]] std::uintptr_t GetGeneralIconTextureId(std::string_view iconId) const noexcept;
    /// The icon of a scene object type. Every object type has one; 0 only before Initialize().
    [[nodiscard]] std::uintptr_t GetObjectIconTextureId(Scene::Objects::SceneObjectKind kind) const noexcept;
    /// The studio card icon of a scene object type (rectangular frame + auto badge, used in +Add and thumbnails).
    [[nodiscard]] std::uintptr_t GetObjectCardIconTextureId(Scene::Objects::SceneObjectKind kind) const noexcept;
    /// The icon of an object palette category, by its display name ("Physics"). 0 when unknown.
    [[nodiscard]] std::uintptr_t GetObjectCategoryIconTextureId(std::string_view category) const noexcept;
    /// The icon of a Asset Table type, by its AssetTableItemType label (editor_uve.h),
    /// e.g. "Mesh" or "Folder", or "folder_open" for an expanded folder. 0 when unknown.
    [[nodiscard]] std::uintptr_t GetContentTypeIconTextureId(std::string_view typeId) const noexcept;
    /// The studio card icon of a Asset Table type (used in thumbnails and +Add).
    [[nodiscard]] std::uintptr_t GetContentTypeCardIconTextureId(std::string_view typeId) const noexcept;
    /// The icon of an ECS component by component type name ("component_transform", "Transform", etc.).
    [[nodiscard]] std::uintptr_t GetComponentIconTextureId(std::string_view componentId) const noexcept;

    /// Uploads an arbitrary RGBA8 image (e.g. a decoded Asset::TextureAsset) as a standalone GL
    /// texture, for editor-owned dynamic content the fixed baked-icon set above doesn't cover
    /// (e.g. Asset Table thumbnails). Returns 0 on failure. Does not depend on instance state:
    /// callers manage the returned id's lifetime themselves via DeleteDynamicTexture().
    [[nodiscard]] static std::uintptr_t UploadDynamicTexture(const std::uint8_t* pixels, int width,
                                                                 int height) noexcept;
    /// Releases a texture previously returned by UploadDynamicTexture() and zeroes it. Safe to
    /// call with an already-zero id.
    static void DeleteDynamicTexture(std::uintptr_t& textureId) noexcept;

private:
    std::uintptr_t m_logoTextureId = 0U;
    std::array<std::uintptr_t, 2U> m_generalIconTextureIds{};
    // One per GetEditorIconSources() entry (editor_icon_set_uve.h), in the same order.
    std::vector<std::uintptr_t> m_iconTextureIds;
    // The same textures again, indexed by object kind: the hierarchy asks once per row per frame.
    std::array<std::uintptr_t, Scene::Objects::kMaximumSceneObjectDescriptors> m_objectIconTextureIds{};
};

} // namespace UVE::Editor
