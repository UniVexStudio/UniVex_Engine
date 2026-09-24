// univex/render/SelectionOutlineRenderer.h
// -----------------------------------------------------------------------
// Draws the selection outline: a band of colour just outside the silhouette
// of every selected mesh.
//
// Two passes. The selected triangles are drawn flat into an offscreen,
// one-channel mask the size of the viewport; then one fullscreen triangle
// reads the mask around each pixel and paints the band (see
// selection_outline.frag). Working from a silhouette rather than from
// triangle edges means the inner edges of a mesh - the diagonal across a
// cube's face - never show, only its outline.
//
// The outline draws through other geometry: the scene's depth is not
// available to the editor here (see EditorMeshLayer), so a selected object
// behind another still shows where it is.
//
// Draw order: after the scene and the grid, before the gizmos, which stay
// on top. The renderer sets the state it needs and puts back what it found,
// including the framebuffer binding and viewport.
// -----------------------------------------------------------------------
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "univex/math/Mat4.h"
#include "univex/render/GlApi.h"
#include "univex/render/SelectionOutline.h"
#include "univex/render/ShaderProgram.h"

namespace univex::render {

class SelectionOutlineRenderer {
public:
    SelectionOutlineRenderer() = default;
    ~SelectionOutlineRenderer();

    SelectionOutlineRenderer(const SelectionOutlineRenderer&) = delete;
    SelectionOutlineRenderer& operator=(const SelectionOutlineRenderer&) = delete;
    SelectionOutlineRenderer(SelectionOutlineRenderer&& other) noexcept;
    SelectionOutlineRenderer& operator=(SelectionOutlineRenderer&& other) noexcept;

    // Builds both programs from the shaders embedded from shaders/selection_*.{vert,frag}.
    // Requires a current GL context. Returns nullopt and fills `outError` on failure.
    [[nodiscard]] static std::optional<SelectionOutlineRenderer> CreateWithBuiltinShaders(std::string& outError);

    [[nodiscard]] bool Valid() const { return maskProgram_.Valid() && outlineProgram_.Valid(); }

    // Draws the outline of `triangles` (world space, three vertices per triangle) into the
    // framebuffer bound when called, which is `width` x `height` pixels. Nothing is drawn when the
    // settings hide the outline or there is nothing selected.
    void Draw(const univex::math::Mat4& viewProjection, int width, int height,
              const std::vector<SelectionOutlineVertex>& triangles, const SelectionOutlineSettings& settings);

private:
    [[nodiscard]] bool EnsureMask(int width, int height);
    void Destroy() noexcept;

    ShaderProgram maskProgram_;
    ShaderProgram outlineProgram_;
    GLuint triangleVao_ = 0;
    GLuint triangleVbo_ = 0;
    GLuint fullscreenVao_ = 0;
    GLuint fullscreenVbo_ = 0;
    GLuint maskFbo_ = 0;
    GLuint maskTexture_ = 0;
    int maskWidth_ = 0;
    int maskHeight_ = 0;
};

} // namespace univex::render
