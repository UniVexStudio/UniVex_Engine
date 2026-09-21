#include "univex/camera/ViewportMetrics.h"

#include <algorithm>
#include <cmath>

namespace univex::camera {

using univex::math::Dot;
using univex::math::Normalize;

float WorldPerPixelAtPointUVE(const OrbitCamera& camera, int framebufferHeight,
                              const Vec3& worldPoint) {
    if (framebufferHeight <= 0) return 0.f;

    if (camera.IsOrthographic()) {
        return (camera.OrthographicHalfHeight() * 2.f) / static_cast<float>(framebufferHeight);
    }

    const Vec3 eye = camera.Eye();
    const Vec3 forward = Normalize(camera.Target() - eye);
    const float depth = std::max(camera.NearPlane(), Dot(worldPoint - eye, forward));
    const float worldHeightAtDepth = 2.f * depth * std::tan(camera.Settings().fovYRadians * 0.5f);
    return worldHeightAtDepth / static_cast<float>(framebufferHeight);
}

float WorldPerPixelAtOrbitTargetUVE(const OrbitCamera& camera, int framebufferHeight) {
    return WorldPerPixelAtPointUVE(camera, framebufferHeight, camera.Target());
}

} // namespace univex::camera
