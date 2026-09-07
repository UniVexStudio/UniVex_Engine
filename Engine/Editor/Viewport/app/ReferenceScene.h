// app/ReferenceScene.h
// -----------------------------------------------------------------------
// A single unlit, flat-coloured cube sitting on the origin. It is not part
// of the grid module — it exists so the demo can show that the grid
// depth-composites correctly against real scene geometry: the cube's
// faces below the horizon hide the grid behind them, the grid shows
// through nothing, and neither one z-fights the other.
//
// No lighting math on purpose (per-face constant colours only).
// -----------------------------------------------------------------------
#pragma once

#include <optional>
#include <string>

#include "univex/math/Mat4.h"
#include "univex/render/GlApi.h"
#include "univex/render/ShaderProgram.h"
#include "univex/viewport/ViewportSettings.h"

namespace univex::app {

class ReferenceScene {
public:
    ReferenceScene() = default;
    ~ReferenceScene();

    ReferenceScene(const ReferenceScene&) = delete;
    ReferenceScene& operator=(const ReferenceScene&) = delete;
    ReferenceScene(ReferenceScene&& other) noexcept;
    ReferenceScene& operator=(ReferenceScene&& other) noexcept;

    [[nodiscard]] static std::optional<ReferenceScene> Create(std::string& outError);

    // Draws with depth test + depth write on, the way ordinary opaque
    // scene geometry would. `display` selects flat per-face colours,
    // wireframe, or a single unshaded colour. `model` places the cube in
    // world space (added so one ReferenceScene instance can draw a proxy
    // cube per engine entity at its own world transform, instead of always
    // at the origin) - identity draws exactly where the original
    // origin-relative overload used to.
    void Draw(const univex::math::Mat4& viewProjection, const univex::math::Mat4& model,
              float halfExtent, univex::viewport::DisplayMode display) const;

    // Convenience overload preserving the original origin-relative behavior.
    void Draw(const univex::math::Mat4& viewProjection, float halfExtent,
              univex::viewport::DisplayMode display) const {
        Draw(viewProjection, univex::math::Mat4::Identity(), halfExtent, display);
    }

private:
    void Destroy() noexcept;

    univex::render::ShaderProgram program_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLsizei vertexCount_ = 0;
};

} // namespace univex::app
