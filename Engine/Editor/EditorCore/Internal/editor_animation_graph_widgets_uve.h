// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <initializer_list>
#include <string>
#include <vector>

#include <imgui.h>

#include "uve/component/animation_tree_component_uve.h"

namespace UVE::Editor {

// ---- The Anim Graph's colours, shared by the tree canvas and the state view ------------------------

inline constexpr ImU32 kCanvasUVE = IM_COL32(26, 29, 35, 255);
inline constexpr ImU32 kGridMinorUVE = IM_COL32(255, 255, 255, 10);
inline constexpr ImU32 kGridMajorUVE = IM_COL32(255, 255, 255, 22);
inline constexpr ImU32 kBodyUVE = IM_COL32(40, 44, 52, 245);
inline constexpr ImU32 kBorderUVE = IM_COL32(18, 20, 24, 255);
inline constexpr ImU32 kSelectedUVE = IM_COL32(236, 170, 72, 255);
inline constexpr ImU32 kWireUVE = IM_COL32(170, 178, 190, 220);
inline constexpr ImU32 kWireGoodUVE = IM_COL32(110, 210, 140, 255);
inline constexpr ImU32 kWireBadUVE = IM_COL32(230, 96, 86, 255);
inline constexpr ImU32 kTextUVE = IM_COL32(230, 232, 236, 255);
inline constexpr ImU32 kTextDimUVE = IM_COL32(150, 156, 166, 255);
inline constexpr ImU32 kActiveUVE = IM_COL32(110, 210, 140, 255);

/// A header colour per kind, so the graph reads at a glance: sources, mixers, control.
[[nodiscard]] inline ImU32 KindColourUVE(const Scene::AnimationGraphNodeKindUVE kind) noexcept {
    using Kind = Scene::AnimationGraphNodeKindUVE;
    switch (kind) {
        case Kind::Output: return IM_COL32(170, 72, 72, 255);
        case Kind::Clip: return IM_COL32(58, 110, 170, 255);
        case Kind::Blend2:
        case Kind::BlendSpace1D: return IM_COL32(64, 138, 102, 255);
        case Kind::Additive: return IM_COL32(120, 100, 170, 255);
        case Kind::OneShot: return IM_COL32(176, 118, 52, 255);
        case Kind::TimeScale: return IM_COL32(96, 104, 118, 255);
        case Kind::StateMachine: return IM_COL32(150, 84, 136, 255);
        case Kind::BlendSpace2D: return IM_COL32(52, 128, 120, 255);
        case Kind::Select: return IM_COL32(150, 128, 56, 255);
        case Kind::LayeredBlend: return IM_COL32(96, 90, 170, 255);
        case Kind::TimeSeek: return IM_COL32(84, 112, 132, 255);
    }
    return IM_COL32(96, 104, 118, 255);
}

/// Widgets the Anim Graph canvas and the Inspector's graph drawer share, so a transition or a
/// parameter pick looks and behaves the same in both.

/// A combo over the parameters of the `wanted` types, with `noneLabel` meaning no parameter (the
/// fixed value). A name no parameter has any more reads "(missing)". True when the pick changed.
[[nodiscard]] bool PickParameterUVE(const char* id, const std::vector<Scene::AnimationParameterUVE>& parameters,
                                    std::initializer_list<Scene::AnimationParameterTypeUVE> wanted,
                                    const char* noneLabel, std::string& inOutName);

/// What a pass of DrawAnimationTransitionUVE did to the transition.
struct TransitionEditUVE final {
    /// A discrete change (a pick, a toggle, a condition added or removed): record it now.
    bool changed = false;
    /// A drag is under way: show it, but record it once, when `released`.
    bool dragging = false;
    bool released = false;
};

/// A transition's settings, one labelled row each: its conditions (all must hold), how far the
/// state must play first, where the next state starts, the fade and its curve, and whether it can
/// be interrupted or is switched off. From and To are the caller's: a list picks them, a view draws
/// them. Edits `transition` in place.
TransitionEditUVE DrawAnimationTransitionUVE(Scene::AnimationTransitionUVE& transition,
                                             const std::vector<Scene::AnimationParameterUVE>& parameters);

} // namespace UVE::Editor
