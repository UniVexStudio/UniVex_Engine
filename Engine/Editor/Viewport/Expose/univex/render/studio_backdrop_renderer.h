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

#include "univex/camera/orbit_camera.h"
#include "univex/render/gl_api.h"
#include "univex/render/shader_program.h"

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

struct PrototypeFloorSettings {
    StudioColor tileDark{0.155f, 0.175f, 0.210f};
    StudioColor tileLight{0.205f, 0.230f, 0.272f};
    StudioColor minorLine{0.255f, 0.285f, 0.335f};
    StudioColor majorLine{0.345f, 0.390f, 0.460f};
    StudioColor blockLine{0.470f, 0.545f, 0.655f};
    StudioColor horizonColor{0.165f, 0.190f, 0.232f};
    /// Minimum half-extent of the camera-following infinite floor quad in metres.
    float floorRadius = 160.0f;
    /// Radial distance from the camera where horizon fade starts / completes (metres).
    float fadeStart = 18.0f;
    float fadeEnd = 92.0f;
    /// Size of one primary prototype checkerboard tile in metres.
    float tileSize = 1.0f;
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
    void DrawBackdrop(const univex::camera::OrbitCamera& camera,
                      int width,
                      int height,
                      const StudioSettings& customSettings) const;
    void DrawFloor(const univex::camera::OrbitCamera& camera, int width, int height) const;
    void DrawPrototypeFloor(const univex::camera::OrbitCamera& camera,
                            int width,
                            int height,
                            const PrototypeFloorSettings& customSettings = PrototypeFloorSettings{}) const;

    [[nodiscard]] StudioSettings& Settings() { return settings_; }
    [[nodiscard]] const StudioSettings& Settings() const { return settings_; }

private:
    void Destroy() noexcept;

    ShaderProgram backdropProgram_;
    ShaderProgram floorProgram_;
    ShaderProgram prototypeFloorProgram_;
    GLuint triangleVao_ = 0;
    GLuint triangleVbo_ = 0;
    GLuint floorVao_ = 0;
    GLuint floorVbo_ = 0;
    StudioSettings settings_{};
};

} // namespace univex::render
