// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "particle_system_asset.h"

#include "prism/ColorPicker.h"
#include "prism/Widget.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace UVE::Editor {

class EditorUVE;

/// Built-in curve presets for VFX float curve inputs.
enum class VfxCurvePresetUVE : std::uint8_t {
    Linear = 0,
    EaseIn,
    EaseOut,
    Bell,
    FadeOut,
};

/// Adds or replaces a keyframe on `curve` at `(time, value)`, sorted by time, respecting
/// `kMaximumCurveKeysUVE`. Returns the index of the inserted/updated key.
std::size_t AddVfxCurveKeyUVE(Asset::ParticleCurveUVE& curve,
                              float time,
                              float value,
                              float inTangent = 0.0F,
                              float outTangent = 0.0F);

/// Moves keyframe `keyIndex` on `curve` to `(newTime, newValue)` and re-sorts keys by time.
/// Returns the key's new index after sorting.
std::size_t MoveVfxCurveKeyUVE(Asset::ParticleCurveUVE& curve,
                               std::size_t keyIndex,
                               float newTime,
                               float newValue);

/// Deletes keyframe `keyIndex` from `curve` (retaining at least 1 key if `keepMinimumOne` is true).
/// Returns true if a key was removed.
bool DeleteVfxCurveKeyUVE(Asset::ParticleCurveUVE& curve,
                          std::size_t keyIndex,
                          bool keepMinimumOne = true);

/// Adjusts the incoming and outgoing tangents of keyframe `keyIndex` on `curve`.
bool SetVfxCurveKeyTangentsUVE(Asset::ParticleCurveUVE& curve,
                               std::size_t keyIndex,
                               float inTangent,
                               float outTangent);

/// Replaces `curve` with one of the 5 standard presets (`Linear`, `EaseIn`, `EaseOut`, `Bell`, `FadeOut`).
void ApplyVfxCurvePresetUVE(Asset::ParticleCurveUVE& curve, VfxCurvePresetUVE preset);

/// Adds or updates a stop in `gradient` at `time`, keeping stops sorted in `[0, 1]` and capped at
/// `kMaximumGradientStopsUVE`. Returns the index of the inserted/updated stop.
std::size_t AddVfxGradientStopUVE(Asset::ParticleGradientUVE& gradient,
                                  float time,
                                  const Math::ColorUVE& color,
                                  float alpha = 1.0F);

/// Moves stop `stopIndex` in `gradient` to `newTime` and re-sorts stops. Returns the new index.
std::size_t MoveVfxGradientStopUVE(Asset::ParticleGradientUVE& gradient,
                                   std::size_t stopIndex,
                                   float newTime);

/// Deletes stop `stopIndex` from `gradient` (retaining at least 1 stop when `keepMinimumOne` is true).
bool DeleteVfxGradientStopUVE(Asset::ParticleGradientUVE& gradient,
                              std::size_t stopIndex,
                              bool keepMinimumOne = true);

/// Copies `Asset::ParticleGradientUVE` stops into a `prism::GradientEditor` widget.
void SyncGradientEditorFromAssetUVE(const Asset::ParticleGradientUVE& assetGrad,
                                    prism::GradientEditor& widget);

/// Rebuilds an `Asset::ParticleGradientUVE` from the colour and alpha stops of a `prism::GradientEditor`
/// (merging stops at unique times and sampling linear RGB + alpha at each stop time, capped at 16 stops).
void SyncAssetFromGradientEditorUVE(const prism::GradientEditor& widget,
                                    Asset::ParticleGradientUVE& outGrad);

/// Interactive 2D curve editor widget for `Asset::ParticleCurveUVE`:
/// - Double-click canvas to add a key
/// - Drag a key to move its `(time, value)`
/// - Hold `Alt` and drag a key to adjust its `inTangent` / `outTangent`
/// - Press `Delete` or `Backspace` to delete the selected key
class VfxCurveEditWidgetUVE final : public prism::Widget {
public:
    explicit VfxCurveEditWidgetUVE(Asset::ParticleCurveUVE curve = {});

    void setCurve(const Asset::ParticleCurveUVE& curve);
    [[nodiscard]] const Asset::ParticleCurveUVE& curve() const noexcept { return m_curve; }

    [[nodiscard]] int selectedKey() const noexcept { return m_selectedKey; }
    void setSelectedKey(int index);

    std::function<void(const Asset::ParticleCurveUVE&)> onChanged;

    prism::Vec2 measure(prism::Vec2 avail) override;
    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;
    bool onMouseMove(const prism::MouseEvent& e) override;
    bool onMouseUp(const prism::MouseEvent& e) override;
    bool onKeyDown(const prism::KeyEvent& e) override;
    bool focusable() const override { return true; }

private:
    struct ValueRangeUVE final {
        float minVal = 0.0F;
        float maxVal = 1.0F;
    };

    [[nodiscard]] ValueRangeUVE ComputeRangeUVE() const noexcept;
    [[nodiscard]] prism::Rect PlotRectUVE() const noexcept;
    [[nodiscard]] prism::Vec2 KeyToScreenUVE(float time, float value, const prism::Rect& plot, ValueRangeUVE range) const noexcept;
    [[nodiscard]] std::pair<float, float> ScreenToKeyUVE(prism::Vec2 pt, const prism::Rect& plot, ValueRangeUVE range) const noexcept;
    [[nodiscard]] int HitTestKeyUVE(prism::Vec2 pt, const prism::Rect& plot, ValueRangeUVE range) const noexcept;
    void NotifyChangedUVE();

    Asset::ParticleCurveUVE m_curve;
    int m_selectedKey = -1;
    bool m_draggingKey = false;
    bool m_draggingTangent = false;
    ValueRangeUVE m_dragRange{};
};

/// Builds the right-hand Inspector content for the selected module (`selectedModuleId != 0`),
/// the selected emitter (`selectedEmitterIndex`), and the particle system settings.
[[nodiscard]] prism::Ref<prism::Widget> BuildVfxInspectorContentUVE(
    EditorUVE& editor,
    std::size_t selectedEmitterIndex,
    std::uint32_t selectedModuleId,
    const std::function<void()>& onAssetMutated);

} // namespace UVE::Editor
