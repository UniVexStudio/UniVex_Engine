// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace UVE::Asset {
struct MaterialAssetUVE;
}

namespace UVE::Editor {

/// One decoded 8-bit RGBA image a material preview samples from. The editor decodes a material's
/// texture references itself (`.uvtex` assets, plus the raw PNG/JPEG/BMP/TGA sources the material
/// picker also accepts) and hands the bytes to the renderer, so this module never touches the
/// asset database or a file path.
struct MaterialThumbnailImageUVE final {
    std::vector<std::uint8_t> rgba8;
    int width = 0;
    int height = 0;
    /// Whether the bytes are display-encoded (sRGB) rather than linear. Albedo and emissive maps
    /// are sRGB; normal, metallic-roughness and occlusion maps are linear data. A `.uvtex` asset's
    /// own colour space decides; only raw source images fall back to the caller's hint.
    bool srgb = true;

    [[nodiscard]] bool IsValidUVE() const noexcept {
        if (width <= 0 || height <= 0) {
            return false;
        }
        const std::size_t expected =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
        return rgba8.size() == expected;
    }
};

/// The decoded texture set for one material. An invalid (default) image means that slot has no
/// texture and the material's flat value applies, exactly like `kInvalidAssetGuidUVE` in the asset.
struct MaterialThumbnailTexturesUVE final {
    MaterialThumbnailImageUVE albedo;
    MaterialThumbnailImageUVE normal;
    MaterialThumbnailImageUVE metallicRoughness;
    MaterialThumbnailImageUVE ao;
    MaterialThumbnailImageUVE emissive;
};

/// Renders a Content Browser preview of a material: a fixed preview sphere drawn with the
/// material's own albedo, normal, metallic-roughness, occlusion and emissive inputs through a
/// physically based (GGX/Cook-Torrance) shader, under a three-light studio rig plus a hemisphere
/// ambient that stands in for an environment probe (so metals show reflections instead of
/// turning black). The result is tone mapped and display-encoded, matching how the runtime
/// encodes its own output - a mid-grey surface previews as mid-grey, not as a washed-out blob.
///
/// Why this is not MeshThumbnailRendererUVE: that class deliberately renders geometry only,
/// with flat single-light Lambert shading and no material, because a mesh asset's own material
/// associations are a runtime concern. A material preview needs the opposite - one fixed shape
/// and full material fidelity - so the two share only the engine's GL loader contract
/// (uve/rhi_opengl/gl_functions_uve.h) and the same GL state save/restore discipline, not code.
/// The sphere, shader, studio rig and render target are fixed; only the material inputs change.
///
/// Fidelity notes: the preview honours albedo (colour, texture, UV scale/offset), metallic,
/// roughness, the packed metallic-roughness map, tangent-space normal mapping (scale, green
/// flip), occlusion strength, emissive (colour, energy, texture), unshaded, alpha cutoff and
/// straight-alpha transparency. It deliberately does not model render-pipeline concerns that a
/// single isolated surface cannot show - shadows, screen-space effects, custom shader code,
/// parallax, clearcoat, rim or subsurface terms, and per-pass blend modes beyond straight alpha.
///
/// Not thread-safe: must be driven from the same thread that owns the editor's GL context, and
/// only after that context has been made current (matching EditorUiAssetsUVE::InitializeUVE()'s
/// contract).
class MaterialThumbnailRendererUVE final {
public:
    MaterialThumbnailRendererUVE();
    MaterialThumbnailRendererUVE(const MaterialThumbnailRendererUVE&) = delete;
    MaterialThumbnailRendererUVE& operator=(const MaterialThumbnailRendererUVE&) = delete;
    MaterialThumbnailRendererUVE(MaterialThumbnailRendererUVE&&) = delete;
    MaterialThumbnailRendererUVE& operator=(MaterialThumbnailRendererUVE&&) = delete;
    ~MaterialThumbnailRendererUVE();

    /// Loads the GL function pointers this renderer needs, compiles its shader program and builds
    /// the preview sphere. A failure here (missing function pointers, shader compile/link failure)
    /// leaves the renderer permanently inert: RenderThumbnailUVE() returns 0 for every call
    /// without touching the GPU further.
    void InitializeUVE();

    /// Releases the shader program, the preview sphere buffers and the scratch framebuffer/
    /// depth-renderbuffer. Does not release any texture id previously returned by
    /// RenderThumbnailUVE() - callers own those and must delete them independently (e.g. via
    /// EditorUiAssetsUVE::DeleteDynamicTextureUVE()).
    void ShutdownUVE() noexcept;

    /// Renders `material` on the fixed preview sphere into a new `width` x `height` RGBA8 texture,
    /// ready for direct use as an ImGui image id (exactly like
    /// EditorUiAssetsUVE::UploadDynamicTextureUVE()'s return value). Returns 0 if `width`/`height`
    /// is not positive, InitializeUVE() previously failed, or any GL step fails. The caller owns
    /// the returned texture's lifetime.
    [[nodiscard]] std::uintptr_t RenderThumbnailUVE(const Asset::MaterialAssetUVE& material,
                                                    const MaterialThumbnailTexturesUVE& textures, int width,
                                                    int height);

    /// Opaque - defined only in material_thumbnail_renderer_uve.cpp, which is the sole translation
    /// unit allowed to name its members. Public (rather than private) purely so that file's own
    /// free helper functions can take a `GlStateUVE&` parameter; nothing outside that .cpp can form
    /// more than a pointer/reference to this incomplete type. Keeps every GL type (function-pointer
    /// typedefs, GLuint) out of this public header, matching this codebase's established GL-header
    /// confinement discipline (see uve/rhi_opengl/gl_functions_uve.h, the shared loader contract
    /// this renderer draws through).
    struct GlStateUVE;

private:
    std::unique_ptr<GlStateUVE> m_gl;
};

} // namespace UVE::Editor
