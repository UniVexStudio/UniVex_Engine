// univex/render/StudioBackdropRenderer.h
// -----------------------------------------------------------------------
// A small photo studio for looking at a character: a sky over a ground
// colour meeting at the horizon, and a light floor with metre lines that
// fades into that ground colour at its edge, so the floor has no hard rim.
//
// It replaces the editor's grid and dark backdrop where a view is for
// presenting rather than editing (the Retarget window). The engine's own
// renderer has no fog that follows distance from a point on the floor, so
// the fade lives here, in the floor's own shader.
//
// Draw order: DrawBackdrop first (neither tests nor writes depth), then the
// scene, then DrawFloor, which depth-tests against the scene (a foot on the
// floor hides the floor under it) and blends its faded edge over the backdrop.
// -----------------------------------------------------------------------
#pragma once

#include <optional>
#include <string>

#include "univex/camera/OrbitCamera.h"
#include "univex/render/GlApi.h"
#include "univex/render/ShaderProgram.h"

namespace univex::render {

struct StudioColor {
    float r = 0.f, g = 0.f, b = 0.f;
};

struct StudioSettings {
    StudioColor skyZenith{0.40f, 0.58f, 0.80f};
    StudioColor skyHorizon{0.82f, 0.87f, 0.92f};
    StudioColor ground{0.60f, 0.61f, 0.60f};
    StudioColor floor{0.93f, 0.93f, 0.92f};
    StudioColor line{0.64f, 0.65f, 0.66f};
    /// Half the floor's side, in metres; the floor ends (fully faded) at this distance from the centre.
    float floorRadius = 5.0f;
    /// Where the fade begins, as a fraction of floorRadius.
    float fadeStart = 0.45f;
    /// Metres between major lines; minor lines are a quarter of that.
    float lineSpacing = 1.0f;
};

class StudioBackdropRenderer {
public:
    StudioBackdropRenderer() = default;
    ~StudioBackdropRenderer();

    StudioBackdropRenderer(const StudioBackdropRenderer&) = delete;
    StudioBackdropRenderer& operator=(const StudioBackdropRenderer&) = delete;
    StudioBackdropRenderer(StudioBackdropRenderer&& other) noexcept;
    StudioBackdropRenderer& operator=(StudioBackdropRenderer&& other) noexcept;

    /// Needs a current GL context. Nullopt, with `outError` filled, when a shader will not build.
    [[nodiscard]] static std::optional<StudioBackdropRenderer> Create(std::string& outError);

    void DrawBackdrop(const univex::camera::OrbitCamera& camera, int width, int height) const;
    void DrawFloor(const univex::camera::OrbitCamera& camera, int width, int height) const;

    [[nodiscard]] StudioSettings& Settings() { return settings_; }
    [[nodiscard]] const StudioSettings& Settings() const { return settings_; }

private:
    void Destroy() noexcept;

    ShaderProgram backdropProgram_;
    ShaderProgram floorProgram_;
    GLuint triangleVao_ = 0;
    GLuint triangleVbo_ = 0;
    GLuint floorVao_ = 0;
    GLuint floorVbo_ = 0;
    StudioSettings settings_{};
};

} // namespace univex::render
