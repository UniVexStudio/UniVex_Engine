// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace UVE::Asset {
struct MeshAsset;
}

namespace UVE::Editor {

struct MaterialThumbnailImage;

/// How one material slot of a mesh looks in its thumbnail: full PBR inputs (albedo, metallic,
/// roughness, normal, AO, emissive, UV transform) so mesh thumbnails match real runtime materials.
/// A see-through slot (eyelashes, hair cards) is left out of the picture.
struct MeshThumbnailLook final {
    std::array<float, 3> colour{0.70F, 0.72F, 0.76F};
    float metallic = 0.0F;
    float roughness = 0.5F;
    float dielectricSpecular = 0.5F;
    float normalScale = 1.0F;
    bool flipGreen = false;
    float occlusionStrength = 1.0F;
    std::array<float, 3> emissiveColor{0.0F, 0.0F, 0.0F};
    float emissiveEnergy = 1.0F;
    std::array<float, 2> uvScale{1.0F, 1.0F};
    std::array<float, 2> uvOffset{0.0F, 0.0F};
    bool unshaded = false;
    const MaterialThumbnailImage* albedo = nullptr;
    const MaterialThumbnailImage* normal = nullptr;
    const MaterialThumbnailImage* metallicRoughness = nullptr;
    const MaterialThumbnailImage* ao = nullptr;
    const MaterialThumbnailImage* emissive = nullptr;
    bool skip = false;
};

/// One bone drawn over the thumbnail, from its joint to the next (model space).
struct MeshThumbnailBone final {
    std::array<float, 3> head{};
    std::array<float, 3> tail{};
};

/// Renders a small preview image of a mesh asset's own geometry - fixed default camera angle
/// auto-framed to the mesh's bounding box, key/fill light shading, each surface in its material's
/// albedo colour and map -
/// directly into a standalone GL texture, for Asset Table mesh thumbnails.
///
/// Mirrors EditorUiAssets's precedent of talking to raw OpenGL directly from the editor module
/// rather than going through IRenderDevice: Render::TextureHandle is fully opaque with no
/// accessor to its backing GLuint (by design - see gl_render_device_uve.h's header comment on
/// confining GL types to engine/render/src/), and the ECS mesh-rendering path
/// (Scene::MeshComponent/Render::MeshRenderer) requires a valid material reference with no
/// "no material" fallback, so neither can be reused for an isolated single-mesh, no-material
/// preview render. This class therefore owns its own minimal GL function-pointer loader, shader
/// program, and scratch framebuffer entirely independent of engine/render.
///
/// Not thread-safe: must be driven from the same thread that owns the editor's GL context, and
/// only after that context has been made current (matching EditorUiAssets::Initialize()'s
/// contract).
class MeshThumbnailRenderer final {
public:
    MeshThumbnailRenderer();
    MeshThumbnailRenderer(const MeshThumbnailRenderer&) = delete;
    MeshThumbnailRenderer& operator=(const MeshThumbnailRenderer&) = delete;
    MeshThumbnailRenderer(MeshThumbnailRenderer&&) = delete;
    MeshThumbnailRenderer& operator=(MeshThumbnailRenderer&&) = delete;
    ~MeshThumbnailRenderer();

    /// Loads the GL function pointers this renderer needs and compiles its shader program. A
    /// failure here (missing function pointers, shader compile/link failure) leaves the renderer
    /// permanently inert: RenderThumbnail() returns 0 for every call without touching the GPU
    /// further.
    void Initialize();

    /// Releases the shader program and the scratch framebuffer/depth-renderbuffer created lazily
    /// by RenderThumbnail(). Does not release any texture id previously returned by
    /// RenderThumbnail() - callers own those and must delete them independently (e.g. via
    /// EditorUiAssets::DeleteDynamicTexture()).
    void Shutdown() noexcept;

    /// Renders `mesh` from a fixed default viewing angle chosen to frame its own local-space
    /// bounding box into a new `width` x `height` RGBA8 texture, ready for direct use as a UI
    /// image id (exactly like EditorUiAssets::UploadDynamicTexture()'s return value).
    /// Returns 0 if `mesh` has no vertices/indices, `width`/`height` is not positive, Initialize()
    /// previously failed, or any GL step fails. The caller owns the returned texture's lifetime.
    [[nodiscard]] std::uintptr_t RenderThumbnail(const Asset::MeshAsset& mesh, int width, int height,
                                                    const MaterialThumbnailImage* albedoTexture = nullptr);
    /// The same, each surface shaded by its own material slot's look (`looksBySlot[slot]`); a
    /// surface whose slot has no look is drawn in a neutral grey. `viewDirection` (x, y, z: from
    /// the model toward the eye) is the thumbnail camera's angle; null keeps the three-quarter front.
    [[nodiscard]] std::uintptr_t RenderThumbnail(const Asset::MeshAsset& mesh, int width, int height,
                                                    const std::vector<MeshThumbnailLook>& looksBySlot,
                                                    const std::vector<MeshThumbnailBone>& bones = {},
                                                    const float* viewDirection = nullptr);

    /// Opaque - defined only in mesh_thumbnail_renderer_uve.cpp, which is the sole translation
    /// unit allowed to name its members. Public (rather than private) purely so that file's own
    /// free helper functions can take a `GlState&` parameter; nothing outside that .cpp can
    /// form more than a pointer/reference to this incomplete type. Keeps every GL type
    /// (function-pointer typedefs, GLuint) out of this public header, matching this codebase's
    /// established GL-header-confinement discipline (see uve/rhi_opengl/gl_functions_uve.h, the
    /// shared loader contract this renderer draws through).
    struct GlState;

private:
    std::unique_ptr<GlState> m_gl;
};

} // namespace UVE::Editor
