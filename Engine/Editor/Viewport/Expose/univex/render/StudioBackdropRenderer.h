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
    // Neutral, mid-dark, ash-grey. Three constraints set these values, in order of importance:
    //
    // 1. This backdrop is not a light source. There is no environment/IBL in the editor viewport
    //    - EditorMeshLayerUVE::SyncHeadlightUVE supplies a single directional headlight at 1.25
    //    against a default ambient of 0.05. A bright sky therefore promises light that nothing
    //    delivers: the subject reads dark against it, its silhouette loses edge, and the eye
    //    adapts to the bright surround and then misjudges material values. So the horizon sits at
    //    ~0.24 rather than ~0.9.
    // 2. Achromatic. The previous zenith was a saturated blue, which pulls colour judgement: an
    //    artist grading albedo is grading it against that blue. Every channel below is held within
    //    ~0.012 of its neighbours, which is far too little to tint anything and just enough to keep
    //    the grey from looking flat and dead.
    // 3. Dark enough to carry a highlight. The floor is ~0.29, below mid-grey, so a specular
    //    highlight on the subject still has somewhere to go. The old 0.93 floor had no headroom
    //    left and bloomed against everything standing on it.
    //
    // The sky is a shade cool and the floor a shade warm - the "ash" look - which separates them
    // at the horizon without introducing a hue either one could be mistaken for.
    StudioColor skyZenith{0.141f, 0.145f, 0.157f};  // #242528
    StudioColor skyHorizon{0.235f, 0.243f, 0.255f}; // #3C3E41
    StudioColor ground{0.118f, 0.122f, 0.133f};     // #1E1F22
    StudioColor floor{0.294f, 0.290f, 0.282f};      // #4B4A48
    StudioColor line{0.392f, 0.384f, 0.373f};       // #64625F
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
