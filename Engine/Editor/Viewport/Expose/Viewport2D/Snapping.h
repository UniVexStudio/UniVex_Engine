#pragma once
#include "IViewportRenderer.h"
#include <vector>

namespace univex::viewport {

struct SnapSettings {
    bool  grid = false;            // snap to gridStep (world units)
    bool  pixel = false;           // snap to whole world units (game pixels)
    bool  guides = true;           // snap to ruler guides (smart snap)
    bool  rotation = false;        // snap rotation to rotationStepDeg (Shift also does it while dragging)
    float gridStep = 16.f;
    float rotationStepDeg = 15.f;
    float guidePx = 8.f;           // guide snap distance in screen pixels
};

struct Guides {
    std::vector<float> vertical;   // x positions
    std::vector<float> horizontal; // y positions
};

float snapToStep(float v, float step);
// Priority per axis: guides > grid > pixel.
Vec2 snapPoint(Vec2 world, const SnapSettings& s, const Guides& g, float zoom);

} // namespace univex::viewport
