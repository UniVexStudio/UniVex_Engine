// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_vfx_inspector.h"

#include "editor.h"
#include "editor_chrome.h"
#include "editor_fit_width.h"
#include "vfx_values.h"

#include "prism/Application.h"
#include "prism/Canvas.h"
#include "prism/ColorPicker.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

constexpr float kCurveTimeEpsilonUVE = 1e-4F;

[[nodiscard]] prism::Ref<prism::NumberField> MakeTypedNumberFieldUVE(
    const float value,
    const float step,
    const int decimals,
    std::function<void(float)> onCommit) {
    auto field = prism::make<prism::NumberField>(value, step, decimals);
    field->setScrubbable(false);
    field->onChange = std::move(onCommit);
    return field;
}

[[nodiscard]] prism::Ref<prism::Vec3Field> MakeTypedVec3FieldUVE(
    const Math::Vector3UVE& v,
    std::function<void(const Math::Vector3UVE&)> onCommit) {
    auto field = prism::make<prism::Vec3Field>(0.1F, 2);
    field->setScrubbable(false);
    field->setValue(v.x, v.y, v.z);
    prism::Vec3Field* const raw = field.get();
    field->onChange = [raw, onCommit = std::move(onCommit)](const int, const float) {
        if (onCommit) {
            onCommit(Math::Vector3UVE{raw->get(0), raw->get(1), raw->get(2)});
        }
    };
    return field;
}

[[nodiscard]] Asset::ParticleModuleUVE* FindMutableModuleInEmitterUVE(
    Asset::ParticleEmitterAssetUVE& emitter,
    const std::uint32_t moduleId) noexcept {
    auto findIn = [moduleId](std::vector<Asset::ParticleModuleUVE>& list) -> Asset::ParticleModuleUVE* {
        for (Asset::ParticleModuleUVE& m : list) {
            if (m.id == moduleId) {
                return &m;
            }
        }
        return nullptr;
    };
    if (Asset::ParticleModuleUVE* m = findIn(emitter.emitterUpdate)) {
        return m;
    }
    if (Asset::ParticleModuleUVE* m = findIn(emitter.particleSpawn)) {
        return m;
    }
    if (Asset::ParticleModuleUVE* m = findIn(emitter.particleUpdate)) {
        return m;
    }
    if (Asset::ParticleModuleUVE* m = findIn(emitter.render)) {
        return m;
    }
    return findIn(emitter.detached);
}

[[nodiscard]] const Asset::ParticleModuleUVE* FindConstModuleInEmitterUVE(
    const Asset::ParticleEmitterAssetUVE& emitter,
    const std::uint32_t moduleId) noexcept {
    auto findIn = [moduleId](const std::vector<Asset::ParticleModuleUVE>& list) -> const Asset::ParticleModuleUVE* {
        for (const Asset::ParticleModuleUVE& m : list) {
            if (m.id == moduleId) {
                return &m;
            }
        }
        return nullptr;
    };
    if (const Asset::ParticleModuleUVE* m = findIn(emitter.emitterUpdate)) {
        return m;
    }
    if (const Asset::ParticleModuleUVE* m = findIn(emitter.particleSpawn)) {
        return m;
    }
    if (const Asset::ParticleModuleUVE* m = findIn(emitter.particleUpdate)) {
        return m;
    }
    if (const Asset::ParticleModuleUVE* m = findIn(emitter.render)) {
        return m;
    }
    return findIn(emitter.detached);
}

[[nodiscard]] std::vector<std::string> MatchingUserParametersUVE(
    const Asset::ParticleSystemAssetUVE& system,
    const Asset::ParticleUserParameterTypeUVE desiredType) {
    std::vector<std::string> names;
    for (const Asset::ParticleUserParameterUVE& param : system.parameters) {
        if (param.type == desiredType && !param.name.empty()) {
            names.push_back(param.name);
        }
    }
    return names;
}

} // namespace

// -----------------------------------------------------------------------------
// Curve & Gradient Pure Helpers
// -----------------------------------------------------------------------------

std::size_t AddVfxCurveKeyUVE(Asset::ParticleCurveUVE& curve,
                              const float time,
                              const float value,
                              const float inTangent,
                              const float outTangent) {
    const float clampedTime = std::clamp(std::isfinite(time) ? time : 0.0F, 0.0F, 1.0F);
    const float safeValue = std::isfinite(value) ? value : 0.0F;
    const float safeIn = std::isfinite(inTangent) ? inTangent : 0.0F;
    const float safeOut = std::isfinite(outTangent) ? outTangent : 0.0F;

    for (std::size_t i = 0U; i < curve.keys.size(); ++i) {
        if (std::fabs(curve.keys[i].time - clampedTime) <= kCurveTimeEpsilonUVE) {
            curve.keys[i].time = clampedTime;
            curve.keys[i].value = safeValue;
            curve.keys[i].inTangent = safeIn;
            curve.keys[i].outTangent = safeOut;
            return i;
        }
    }

    if (curve.keys.size() >= Asset::kMaximumCurveKeysUVE) {
        return curve.keys.size() - 1U;
    }

    Asset::ParticleCurveKeyUVE key{};
    key.time = clampedTime;
    key.value = safeValue;
    key.inTangent = safeIn;
    key.outTangent = safeOut;

    const auto insertPos = std::lower_bound(
        curve.keys.begin(), curve.keys.end(), clampedTime,
        [](const Asset::ParticleCurveKeyUVE& a, const float t) { return a.time < t; });
    const auto idx = static_cast<std::size_t>(std::distance(curve.keys.begin(), insertPos));
    curve.keys.insert(insertPos, key);
    return idx;
}

std::size_t MoveVfxCurveKeyUVE(Asset::ParticleCurveUVE& curve,
                               const std::size_t keyIndex,
                               const float newTime,
                               const float newValue) {
    if (keyIndex >= curve.keys.size()) {
        return 0U;
    }
    Asset::ParticleCurveKeyUVE moved = curve.keys[keyIndex];
    moved.time = std::clamp(std::isfinite(newTime) ? newTime : moved.time, 0.0F, 1.0F);
    moved.value = std::isfinite(newValue) ? newValue : moved.value;
    curve.keys.erase(curve.keys.begin() + static_cast<std::ptrdiff_t>(keyIndex));

    const auto insertPos = std::lower_bound(
        curve.keys.begin(), curve.keys.end(), moved.time,
        [](const Asset::ParticleCurveKeyUVE& a, const float t) { return a.time < t; });
    const auto newIdx = static_cast<std::size_t>(std::distance(curve.keys.begin(), insertPos));
    curve.keys.insert(insertPos, moved);
    return newIdx;
}

bool DeleteVfxCurveKeyUVE(Asset::ParticleCurveUVE& curve,
                          const std::size_t keyIndex,
                          const bool keepMinimumOne) {
    if (keyIndex >= curve.keys.size()) {
        return false;
    }
    if (keepMinimumOne && curve.keys.size() <= 1U) {
        return false;
    }
    curve.keys.erase(curve.keys.begin() + static_cast<std::ptrdiff_t>(keyIndex));
    return true;
}

bool SetVfxCurveKeyTangentsUVE(Asset::ParticleCurveUVE& curve,
                               const std::size_t keyIndex,
                               const float inTangent,
                               const float outTangent) {
    if (keyIndex >= curve.keys.size() || !std::isfinite(inTangent) || !std::isfinite(outTangent)) {
        return false;
    }
    curve.keys[keyIndex].inTangent = inTangent;
    curve.keys[keyIndex].outTangent = outTangent;
    return true;
}

void ApplyVfxCurvePresetUVE(Asset::ParticleCurveUVE& curve, const VfxCurvePresetUVE preset) {
    curve.keys.clear();
    curve.smooth = true;
    switch (preset) {
        case VfxCurvePresetUVE::Linear:
            curve.smooth = false;
            curve.keys.push_back(Asset::ParticleCurveKeyUVE{0.0F, 0.0F, 1.0F, 1.0F});
            curve.keys.push_back(Asset::ParticleCurveKeyUVE{1.0F, 1.0F, 1.0F, 1.0F});
            break;
        case VfxCurvePresetUVE::EaseIn:
            curve.keys.push_back(Asset::ParticleCurveKeyUVE{0.0F, 0.0F, 0.0F, 0.0F});
            curve.keys.push_back(Asset::ParticleCurveKeyUVE{1.0F, 1.0F, 2.0F, 2.0F});
            break;
        case VfxCurvePresetUVE::EaseOut:
            curve.keys.push_back(Asset::ParticleCurveKeyUVE{0.0F, 0.0F, 2.0F, 2.0F});
            curve.keys.push_back(Asset::ParticleCurveKeyUVE{1.0F, 1.0F, 0.0F, 0.0F});
            break;
        case VfxCurvePresetUVE::Bell:
            curve.keys.push_back(Asset::ParticleCurveKeyUVE{0.0F, 0.0F, 0.0F, 2.5F});
            curve.keys.push_back(Asset::ParticleCurveKeyUVE{0.5F, 1.0F, 0.0F, 0.0F});
            curve.keys.push_back(Asset::ParticleCurveKeyUVE{1.0F, 0.0F, -2.5F, 0.0F});
            break;
        case VfxCurvePresetUVE::FadeOut:
            curve.keys.push_back(Asset::ParticleCurveKeyUVE{0.0F, 1.0F, 0.0F, -0.5F});
            curve.keys.push_back(Asset::ParticleCurveKeyUVE{1.0F, 0.0F, -1.5F, 0.0F});
            break;
    }
}

std::size_t AddVfxGradientStopUVE(Asset::ParticleGradientUVE& gradient,
                                  const float time,
                                  const Math::ColorUVE& color,
                                  const float alpha) {
    const float clampedTime = std::clamp(std::isfinite(time) ? time : 0.0F, 0.0F, 1.0F);
    const float clampedAlpha = std::clamp(std::isfinite(alpha) ? alpha : 1.0F, 0.0F, 1.0F);
    Math::ColorUVE safeColor = color;
    if (!std::isfinite(safeColor.r) || safeColor.r < 0.0F) { safeColor.r = 0.0F; }
    if (!std::isfinite(safeColor.g) || safeColor.g < 0.0F) { safeColor.g = 0.0F; }
    if (!std::isfinite(safeColor.b) || safeColor.b < 0.0F) { safeColor.b = 0.0F; }

    for (std::size_t i = 0U; i < gradient.stops.size(); ++i) {
        if (std::fabs(gradient.stops[i].time - clampedTime) <= kCurveTimeEpsilonUVE) {
            gradient.stops[i].time = clampedTime;
            gradient.stops[i].color = safeColor;
            gradient.stops[i].alpha = clampedAlpha;
            return i;
        }
    }

    if (gradient.stops.size() >= Asset::kMaximumGradientStopsUVE) {
        return gradient.stops.size() - 1U;
    }

    Asset::ParticleGradientStopUVE stop{};
    stop.time = clampedTime;
    stop.color = safeColor;
    stop.alpha = clampedAlpha;

    const auto insertPos = std::lower_bound(
        gradient.stops.begin(), gradient.stops.end(), clampedTime,
        [](const Asset::ParticleGradientStopUVE& a, const float t) { return a.time < t; });
    const auto idx = static_cast<std::size_t>(std::distance(gradient.stops.begin(), insertPos));
    gradient.stops.insert(insertPos, stop);
    return idx;
}

std::size_t MoveVfxGradientStopUVE(Asset::ParticleGradientUVE& gradient,
                                   const std::size_t stopIndex,
                                   const float newTime) {
    if (stopIndex >= gradient.stops.size()) {
        return 0U;
    }
    Asset::ParticleGradientStopUVE moved = gradient.stops[stopIndex];
    moved.time = std::clamp(std::isfinite(newTime) ? newTime : moved.time, 0.0F, 1.0F);
    gradient.stops.erase(gradient.stops.begin() + static_cast<std::ptrdiff_t>(stopIndex));

    const auto insertPos = std::lower_bound(
        gradient.stops.begin(), gradient.stops.end(), moved.time,
        [](const Asset::ParticleGradientStopUVE& a, const float t) { return a.time < t; });
    const auto newIdx = static_cast<std::size_t>(std::distance(gradient.stops.begin(), insertPos));
    gradient.stops.insert(insertPos, moved);
    return newIdx;
}

bool DeleteVfxGradientStopUVE(Asset::ParticleGradientUVE& gradient,
                              const std::size_t stopIndex,
                              const bool keepMinimumOne) {
    if (stopIndex >= gradient.stops.size()) {
        return false;
    }
    if (keepMinimumOne && gradient.stops.size() <= 1U) {
        return false;
    }
    gradient.stops.erase(gradient.stops.begin() + static_cast<std::ptrdiff_t>(stopIndex));
    return true;
}

void SyncGradientEditorFromAssetUVE(const Asset::ParticleGradientUVE& assetGrad,
                                    prism::GradientEditor& widget) {
    std::vector<prism::GradientColorStop> cStops;
    std::vector<prism::GradientAlphaStop> aStops;
    if (assetGrad.stops.empty()) {
        cStops.push_back({0.0F, 1.0F, 1.0F, 1.0F});
        cStops.push_back({1.0F, 1.0F, 1.0F, 1.0F});
        aStops.push_back({0.0F, 1.0F});
        aStops.push_back({1.0F, 1.0F});
    } else {
        for (const Asset::ParticleGradientStopUVE& s : assetGrad.stops) {
            cStops.push_back({s.time, s.color.r, s.color.g, s.color.b});
            aStops.push_back({s.time, s.alpha});
        }
    }
    widget.setStops(std::move(cStops), std::move(aStops));
}

void SyncAssetFromGradientEditorUVE(const prism::GradientEditor& widget,
                                    Asset::ParticleGradientUVE& outGrad) {
    std::vector<float> times;
    for (const prism::GradientColorStop& cs : widget.colorStops()) {
        times.push_back(std::clamp(cs.t, 0.0F, 1.0F));
    }
    for (const prism::GradientAlphaStop& as : widget.alphaStops()) {
        times.push_back(std::clamp(as.t, 0.0F, 1.0F));
    }
    std::sort(times.begin(), times.end());
    std::vector<float> uniqueTimes;
    for (const float t : times) {
        if (uniqueTimes.empty() || std::fabs(uniqueTimes.back() - t) > 1e-3F) {
            uniqueTimes.push_back(t);
        }
    }
    if (uniqueTimes.empty()) {
        uniqueTimes.push_back(0.0F);
        uniqueTimes.push_back(1.0F);
    }
    while (uniqueTimes.size() > Asset::kMaximumGradientStopsUVE) {
        uniqueTimes.pop_back();
    }

    outGrad.stops.clear();
    outGrad.stops.reserve(uniqueTimes.size());
    for (const float t : uniqueTimes) {
        const prism::ColorHdr linear = widget.evaluateLinear(t);
        const float r = linear.r;
        const float g = linear.g;
        const float b = linear.b;
        const float a = linear.a;
        Asset::ParticleGradientStopUVE stop{};
        stop.time = t;
        stop.color = Math::ColorUVE{r, g, b};
        stop.alpha = a;
        outGrad.stops.push_back(stop);
    }
}

// -----------------------------------------------------------------------------
// VfxCurveEditWidgetUVE
// -----------------------------------------------------------------------------

VfxCurveEditWidgetUVE::VfxCurveEditWidgetUVE(Asset::ParticleCurveUVE curve)
    : m_curve(std::move(curve)) {
    if (m_curve.keys.empty()) {
        ApplyVfxCurvePresetUVE(m_curve, VfxCurvePresetUVE::Linear);
    }
    setMinSize(180.0F, 112.0F);
}

void VfxCurveEditWidgetUVE::setCurve(const Asset::ParticleCurveUVE& curve) {
    m_curve = curve;
    if (m_curve.keys.empty()) {
        ApplyVfxCurvePresetUVE(m_curve, VfxCurvePresetUVE::Linear);
    }
    if (m_selectedKey >= static_cast<int>(m_curve.keys.size())) {
        m_selectedKey = static_cast<int>(m_curve.keys.size()) - 1;
    }
    invalidate();
}

void VfxCurveEditWidgetUVE::setSelectedKey(const int index) {
    const int clamped = (index >= 0 && static_cast<std::size_t>(index) < m_curve.keys.size()) ? index : -1;
    if (m_selectedKey != clamped) {
        m_selectedKey = clamped;
        invalidate();
    }
}

prism::Vec2 VfxCurveEditWidgetUVE::measure(const prism::Vec2 avail) {
    return m_desired = constrain({std::max(180.0F, avail.x), 112.0F});
}

VfxCurveEditWidgetUVE::ValueRangeUVE VfxCurveEditWidgetUVE::ComputeRangeUVE() const noexcept {
    if (m_draggingKey) {
        return m_dragRange;
    }
    float lo = 0.0F;
    float hi = 1.0F;
    for (const Asset::ParticleCurveKeyUVE& k : m_curve.keys) {
        lo = std::min(lo, k.value);
        hi = std::max(hi, k.value);
    }
    if (hi - lo < 1e-3F) {
        hi = lo + 1.0F;
    }
    return ValueRangeUVE{lo, hi};
}

prism::Rect VfxCurveEditWidgetUVE::PlotRectUVE() const noexcept {
    return m_rect.deflated(8.0F);
}

prism::Vec2 VfxCurveEditWidgetUVE::KeyToScreenUVE(
    const float time, const float value, const prism::Rect& plot, const ValueRangeUVE range) const noexcept {
    const float span = std::max(1e-5F, range.maxVal - range.minVal);
    const float x = plot.x + std::clamp(time, 0.0F, 1.0F) * plot.w;
    const float y = plot.bottom() - ((value - range.minVal) / span) * plot.h;
    return {x, y};
}

std::pair<float, float> VfxCurveEditWidgetUVE::ScreenToKeyUVE(
    const prism::Vec2 pt, const prism::Rect& plot, const ValueRangeUVE range) const noexcept {
    const float t = plot.w > 1.0F ? std::clamp((pt.x - plot.x) / plot.w, 0.0F, 1.0F) : 0.0F;
    const float normY = plot.h > 1.0F ? std::clamp((plot.bottom() - pt.y) / plot.h, 0.0F, 1.0F) : 0.0F;
    const float v = range.minVal + normY * (range.maxVal - range.minVal);
    return {t, v};
}

int VfxCurveEditWidgetUVE::HitTestKeyUVE(
    const prism::Vec2 pt, const prism::Rect& plot, const ValueRangeUVE range) const noexcept {
    for (int i = 0; i < static_cast<int>(m_curve.keys.size()); ++i) {
        const prism::Vec2 kp = KeyToScreenUVE(
            m_curve.keys[static_cast<std::size_t>(i)].time,
            m_curve.keys[static_cast<std::size_t>(i)].value,
            plot, range);
        const float dx = pt.x - kp.x;
        const float dy = pt.y - kp.y;
        if (dx * dx + dy * dy <= 64.0F) {
            return i;
        }
    }
    return -1;
}

void VfxCurveEditWidgetUVE::NotifyChangedUVE() {
    invalidate();
    if (onChanged) {
        onChanged(m_curve);
    }
}

void VfxCurveEditWidgetUVE::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    c.fillRoundRect(m_rect, 4.0F, t.field);
    c.strokeRoundRect(m_rect, 4.0F, focused() ? t.accent : t.border, 1.0F);

    const prism::Rect plot = PlotRectUVE();
    if (plot.w <= 4.0F || plot.h <= 4.0F) {
        return;
    }

    const ValueRangeUVE range = ComputeRangeUVE();
    // Grid lines (25%, 50%, 75%).
    for (int i = 1; i <= 3; ++i) {
        const float f = static_cast<float>(i) * 0.25F;
        const float gx = plot.x + f * plot.w;
        const float gy = plot.y + f * plot.h;
        c.line({gx, plot.y}, {gx, plot.bottom()}, prism::Color(255, 255, 255, 18), 1.0F);
        c.line({plot.x, gy}, {plot.right(), gy}, prism::Color(255, 255, 255, 18), 1.0F);
    }

    // Sample curve across 64 segments.
    c.pushClip(m_rect.deflated(1.0F));
    constexpr int kSamples = 64;
    prism::Vec2 prevPt{};
    for (int s = 0; s <= kSamples; ++s) {
        const float u = static_cast<float>(s) / static_cast<float>(kSamples);
        const float val = VFX::EvaluateVfxCurveExactUVE(m_curve, u);
        const prism::Vec2 curPt = KeyToScreenUVE(u, val, plot, range);
        if (s > 0) {
            c.line(prevPt, curPt, t.accent, 2.0F);
        }
        prevPt = curPt;
    }

    // Draw keys and tangent handles for the selected key.
    for (int i = 0; i < static_cast<int>(m_curve.keys.size()); ++i) {
        const Asset::ParticleCurveKeyUVE& key = m_curve.keys[static_cast<std::size_t>(i)];
        const prism::Vec2 kp = KeyToScreenUVE(key.time, key.value, plot, range);
        const bool sel = (i == m_selectedKey);
        if (sel && m_curve.smooth) {
            const float dt = 0.08F;
            const prism::Vec2 outPt = KeyToScreenUVE(
                std::min(1.0F, key.time + dt), key.value + key.outTangent * dt, plot, range);
            const prism::Vec2 inPt = KeyToScreenUVE(
                std::max(0.0F, key.time - dt), key.value - key.inTangent * dt, plot, range);
            c.line(kp, outPt, t.textDim, 1.0F);
            c.line(kp, inPt, t.textDim, 1.0F);
            c.fillCircle(outPt, 3.0F, t.textBright);
            c.fillCircle(inPt, 3.0F, t.textBright);
        }
        c.fillCircle(kp, sel ? 5.0F : 4.0F, sel ? t.accent : t.textBright);
        c.strokeCircle(kp, sel ? 5.0F : 4.0F, prism::Color(20, 22, 26), 1.2F);
    }
    c.popClip();
}

bool VfxCurveEditWidgetUVE::onMouseDown(const prism::MouseEvent& e) {
    if (e.button != prism::MouseButton::Left) {
        return false;
    }
    if (prism::Application::exists()) {
        prism::Application::get().setFocus(this);
    }
    const prism::Rect plot = PlotRectUVE();
    const ValueRangeUVE range = ComputeRangeUVE();
    const int hit = HitTestKeyUVE(e.pos, plot, range);

    if (e.clicks >= 2 && hit < 0) {
        const auto [t, v] = ScreenToKeyUVE(e.pos, plot, range);
        m_selectedKey = static_cast<int>(AddVfxCurveKeyUVE(m_curve, t, v, 0.0F, 0.0F));
        NotifyChangedUVE();
        return true;
    }

    if (hit >= 0) {
        m_selectedKey = hit;
        m_dragRange = range;
        if ((e.mods & prism::ModAlt) != 0) {
            m_draggingTangent = true;
            m_draggingKey = false;
        } else {
            m_draggingKey = true;
            m_draggingTangent = false;
        }
        if (prism::Application::exists()) {
            prism::Application::get().captureMouse(this);
        }
        invalidate();
        return true;
    }

    m_selectedKey = -1;
    invalidate();
    return true;
}

bool VfxCurveEditWidgetUVE::onMouseMove(const prism::MouseEvent& e) {
    if (!m_draggingKey && !m_draggingTangent) {
        return false;
    }
    if (m_selectedKey < 0 || static_cast<std::size_t>(m_selectedKey) >= m_curve.keys.size()) {
        return false;
    }
    const prism::Rect plot = PlotRectUVE();
    const auto [t, v] = ScreenToKeyUVE(e.pos, plot, m_dragRange);

    if (m_draggingTangent) {
        const Asset::ParticleCurveKeyUVE& key = m_curve.keys[static_cast<std::size_t>(m_selectedKey)];
        const float dt = t - key.time;
        const float dv = v - key.value;
        const float slope = std::fabs(dt) > 1e-3F ? std::clamp(dv / dt, -20.0F, 20.0F) : 0.0F;
        SetVfxCurveKeyTangentsUVE(m_curve, static_cast<std::size_t>(m_selectedKey), slope, slope);
        m_curve.smooth = true;
        NotifyChangedUVE();
        return true;
    }

    m_selectedKey = static_cast<int>(
        MoveVfxCurveKeyUVE(m_curve, static_cast<std::size_t>(m_selectedKey), t, v));
    NotifyChangedUVE();
    return true;
}

bool VfxCurveEditWidgetUVE::onMouseUp(const prism::MouseEvent&) {
    if (m_draggingKey || m_draggingTangent) {
        m_draggingKey = false;
        m_draggingTangent = false;
        if (prism::Application::exists()) {
            prism::Application::get().releaseMouse();
        }
        return true;
    }
    return false;
}

bool VfxCurveEditWidgetUVE::onKeyDown(const prism::KeyEvent& e) {
    if ((e.key == prism::Key::Delete || e.key == prism::Key::Backspace) && m_selectedKey >= 0) {
        if (DeleteVfxCurveKeyUVE(m_curve, static_cast<std::size_t>(m_selectedKey), true)) {
            if (m_selectedKey >= static_cast<int>(m_curve.keys.size())) {
                m_selectedKey = static_cast<int>(m_curve.keys.size()) - 1;
            }
            NotifyChangedUVE();
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// BuildVfxInspectorContentUVE
// -----------------------------------------------------------------------------

prism::Ref<prism::Widget> BuildVfxInspectorContentUVE(
    EditorUVE& editor,
    const std::size_t selectedEmitterIndex,
    const std::uint32_t selectedModuleId,
    const std::function<void()>& onAssetMutated) {
    EditorUVE::VfxEditSessionUVE* const session = editor.VfxEditSessionPtrUVE();
    if (session == nullptr) {
        return prism::make<prism::Spacer>();
    }

    const Asset::ParticleSystemAssetUVE& sys = session->system;
    auto grid = prism::make<prism::PropertyGrid>();

    // 1. Selected Module section (if any valid module is selected).
    if (selectedEmitterIndex < sys.emitters.size() && selectedModuleId != 0U) {
        const Asset::ParticleEmitterAssetUVE& emitter = sys.emitters[selectedEmitterIndex];
        if (const Asset::ParticleModuleUVE* const mod = FindConstModuleInEmitterUVE(emitter, selectedModuleId)) {
            const Asset::ParticleModuleDescriptorUVE* const desc =
                Asset::FindParticleModuleDescriptorUVE(mod->type);
            const std::string title = desc != nullptr
                                          ? "Module: " + std::string(desc->displayName)
                                          : "Module #" + std::to_string(mod->id);
            const int modCat = grid->addCategory(title);

            if (desc != nullptr && !desc->description.empty()) {
                auto descLabel = prism::make<prism::Label>(std::string(desc->description), prism::FontRole::Small);
                descLabel->setColor(prism::ThemeColor(&prism::Theme::textDim));
                grid->addRow(modCat, "Summary", descLabel);
            }

            auto modEnabled = prism::make<prism::CheckBox>("", mod->enabled);
            modEnabled->onChange = [&editor, selectedEmitterIndex, selectedModuleId, onAssetMutated](const bool v) {
                editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                    if (selectedEmitterIndex < s.emitters.size()) {
                        if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                            m->enabled = v;
                        }
                    }
                });
                if (onAssetMutated) {
                    onAssetMutated();
                }
            };
            grid->addRow(modCat, "Enabled", modEnabled);

            if (desc != nullptr) {
                // Float inputs.
                for (const auto& [pinName, defVal] : desc->defaultFloats) {
                    const auto it = mod->floats.find(pinName);
                    const Asset::ParticleFloatInputUVE cur = (it != mod->floats.end()) ? it->second : defVal;

                    auto pinBox = prism::vbox(4.0F);
                    const int modeIdx = (cur.kind == Asset::ParticleValueKindUVE::RandomRange)     ? 1
                                        : (cur.kind == Asset::ParticleValueKindUVE::Curve)         ? 2
                                        : (cur.kind == Asset::ParticleValueKindUVE::UserParameter) ? 3
                                                                                                   : 0;
                    auto modeCombo = prism::make<prism::ComboBox>(
                        std::vector<std::string>{"Constant", "Random Range", "Curve", "User Parameter"}, modeIdx);
                    modeCombo->onChange = [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const int idx) {
                        editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                            if (selectedEmitterIndex >= s.emitters.size()) {
                                return;
                            }
                            Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId);
                            if (m == nullptr) {
                                return;
                            }
                            Asset::ParticleFloatInputUVE val = m->floats.count(pinName) ? m->floats[pinName] : defVal;
                            if (idx == 1) {
                                val.kind = Asset::ParticleValueKindUVE::RandomRange;
                                if (val.max < val.min) {
                                    val.max = val.min + 1.0F;
                                }
                            } else if (idx == 2) {
                                val.kind = Asset::ParticleValueKindUVE::Curve;
                                if (val.curve.keys.empty()) {
                                    ApplyVfxCurvePresetUVE(val.curve, VfxCurvePresetUVE::Linear);
                                }
                            } else if (idx == 3) {
                                val.kind = Asset::ParticleValueKindUVE::UserParameter;
                                const auto params = MatchingUserParametersUVE(s, Asset::ParticleUserParameterTypeUVE::Float);
                                if (val.parameter.empty() && !params.empty()) {
                                    val.parameter = params.front();
                                }
                            } else {
                                val.kind = Asset::ParticleValueKindUVE::Constant;
                            }
                            m->floats[pinName] = std::move(val);
                        });
                        if (onAssetMutated) {
                            onAssetMutated();
                        }
                    };
                    pinBox->add(modeCombo);

                    if (cur.kind == Asset::ParticleValueKindUVE::Constant || cur.kind == Asset::ParticleValueKindUVE::Wire) {
                        pinBox->add(MakeTypedNumberFieldUVE(cur.constant, 0.1F, 2,
                            [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const float v) {
                                editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                                    if (selectedEmitterIndex < s.emitters.size()) {
                                        if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                            Asset::ParticleFloatInputUVE val = m->floats.count(pinName) ? m->floats[pinName] : defVal;
                                            val.constant = v;
                                            m->floats[pinName] = std::move(val);
                                        }
                                    }
                                });
                                if (onAssetMutated) {
                                    onAssetMutated();
                                }
                            }));
                    } else if (cur.kind == Asset::ParticleValueKindUVE::RandomRange) {
                        auto minMaxRow = prism::hbox(4.0F);
                        minMaxRow->add(MakeTypedNumberFieldUVE(cur.min, 0.1F, 2,
                            [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const float v) {
                                editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                                    if (selectedEmitterIndex < s.emitters.size()) {
                                        if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                            Asset::ParticleFloatInputUVE val = m->floats.count(pinName) ? m->floats[pinName] : defVal;
                                            val.min = v;
                                            if (val.max < val.min) {
                                                val.max = val.min;
                                            }
                                            m->floats[pinName] = std::move(val);
                                        }
                                    }
                                });
                                if (onAssetMutated) {
                                    onAssetMutated();
                                }
                            }), prism::Slot{1.0F});
                        minMaxRow->add(MakeTypedNumberFieldUVE(cur.max, 0.1F, 2,
                            [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const float v) {
                                editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                                    if (selectedEmitterIndex < s.emitters.size()) {
                                        if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                            Asset::ParticleFloatInputUVE val = m->floats.count(pinName) ? m->floats[pinName] : defVal;
                                            val.max = std::max(val.min, v);
                                            m->floats[pinName] = std::move(val);
                                        }
                                    }
                                });
                                if (onAssetMutated) {
                                    onAssetMutated();
                                }
                            }), prism::Slot{1.0F});
                        pinBox->add(minMaxRow);
                    } else if (cur.kind == Asset::ParticleValueKindUVE::Curve) {
                        auto curveEditor = prism::make<VfxCurveEditWidgetUVE>(cur.curve);
                        curveEditor->onChanged = [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](
                                                     const Asset::ParticleCurveUVE& updatedCurve) {
                            editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                                if (selectedEmitterIndex < s.emitters.size()) {
                                    if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                        Asset::ParticleFloatInputUVE val = m->floats.count(pinName) ? m->floats[pinName] : defVal;
                                        val.kind = Asset::ParticleValueKindUVE::Curve;
                                        val.curve = updatedCurve;
                                        m->floats[pinName] = std::move(val);
                                    }
                                }
                            });
                            if (onAssetMutated) {
                                onAssetMutated();
                            }
                        };
                        pinBox->add(curveEditor);

                        // Preset bar + scale field.
                        auto presetRow = prism::hbox(3.0F);
                        const std::array<std::pair<const char*, VfxCurvePresetUVE>, 5> kPresets{{
                            {"Linear", VfxCurvePresetUVE::Linear},
                            {"Ease In", VfxCurvePresetUVE::EaseIn},
                            {"Ease Out", VfxCurvePresetUVE::EaseOut},
                            {"Bell", VfxCurvePresetUVE::Bell},
                            {"Fade Out", VfxCurvePresetUVE::FadeOut},
                        }};
                        for (const auto& [label, preset] : kPresets) {
                            auto btn = prism::make<prism::Button>(label, prism::Icon::None, prism::ButtonStyle::Flat);
                            btn->onClick = [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, preset, onAssetMutated] {
                                editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                                    if (selectedEmitterIndex < s.emitters.size()) {
                                        if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                            Asset::ParticleFloatInputUVE val = m->floats.count(pinName) ? m->floats[pinName] : defVal;
                                            val.kind = Asset::ParticleValueKindUVE::Curve;
                                            ApplyVfxCurvePresetUVE(val.curve, preset);
                                            m->floats[pinName] = std::move(val);
                                        }
                                    }
                                });
                                if (onAssetMutated) {
                                    onAssetMutated();
                                }
                            };
                            presetRow->add(btn);
                        }
                        pinBox->add(presetRow);

                        auto scaleRow = prism::hbox(4.0F);
                        scaleRow->add(prism::make<prism::Label>("Scale", prism::FontRole::Small),
                                      prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
                        scaleRow->add(MakeTypedNumberFieldUVE(cur.scale, 0.1F, 2,
                            [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const float v) {
                                editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                                    if (selectedEmitterIndex < s.emitters.size()) {
                                        if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                            Asset::ParticleFloatInputUVE val = m->floats.count(pinName) ? m->floats[pinName] : defVal;
                                            val.scale = v;
                                            m->floats[pinName] = std::move(val);
                                        }
                                    }
                                });
                                if (onAssetMutated) {
                                    onAssetMutated();
                                }
                            }), prism::Slot{1.0F});
                        pinBox->add(scaleRow);
                    } else if (cur.kind == Asset::ParticleValueKindUVE::UserParameter) {
                        std::vector<std::string> choices = MatchingUserParametersUVE(sys, Asset::ParticleUserParameterTypeUVE::Float);
                        if (choices.empty()) {
                            choices.push_back("(none)");
                        }
                        int selIdx = 0;
                        for (std::size_t i = 0U; i < choices.size(); ++i) {
                            if (choices[i] == cur.parameter) {
                                selIdx = static_cast<int>(i);
                                break;
                            }
                        }
                        auto paramCombo = prism::make<prism::ComboBox>(choices, selIdx);
                        paramCombo->onChange = [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, choices, onAssetMutated](const int idx) {
                            if (idx < 0 || static_cast<std::size_t>(idx) >= choices.size()) {
                                return;
                            }
                            const std::string chosen = choices[static_cast<std::size_t>(idx)] == "(none)" ? "" : choices[static_cast<std::size_t>(idx)];
                            editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                                if (selectedEmitterIndex < s.emitters.size()) {
                                    if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                        Asset::ParticleFloatInputUVE val = m->floats.count(pinName) ? m->floats[pinName] : defVal;
                                        val.parameter = chosen;
                                        m->floats[pinName] = std::move(val);
                                    }
                                }
                            });
                            if (onAssetMutated) {
                                onAssetMutated();
                            }
                        };
                        pinBox->add(paramCombo);
                    }
                    grid->addRow(modCat, pinName, pinBox);
                }

                // Vector inputs.
                for (const auto& [pinName, defVal] : desc->defaultVectors) {
                    const auto it = mod->vectors.find(pinName);
                    const Asset::ParticleVectorInputUVE cur = (it != mod->vectors.end()) ? it->second : defVal;

                    auto pinBox = prism::vbox(4.0F);
                    const int modeIdx = (cur.kind == Asset::ParticleValueKindUVE::RandomRange)     ? 1
                                        : (cur.kind == Asset::ParticleValueKindUVE::UserParameter) ? 2
                                                                                                   : 0;
                    auto modeCombo = prism::make<prism::ComboBox>(
                        std::vector<std::string>{"Constant", "Random Range", "User Parameter"}, modeIdx);
                    modeCombo->onChange = [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const int idx) {
                        editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                            if (selectedEmitterIndex >= s.emitters.size()) {
                                return;
                            }
                            Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId);
                            if (m == nullptr) {
                                return;
                            }
                            Asset::ParticleVectorInputUVE val = m->vectors.count(pinName) ? m->vectors[pinName] : defVal;
                            if (idx == 1) {
                                val.kind = Asset::ParticleValueKindUVE::RandomRange;
                            } else if (idx == 2) {
                                val.kind = Asset::ParticleValueKindUVE::UserParameter;
                                const auto params = MatchingUserParametersUVE(s, Asset::ParticleUserParameterTypeUVE::Vector);
                                if (val.parameter.empty() && !params.empty()) {
                                    val.parameter = params.front();
                                }
                            } else {
                                val.kind = Asset::ParticleValueKindUVE::Constant;
                            }
                            m->vectors[pinName] = std::move(val);
                        });
                        if (onAssetMutated) {
                            onAssetMutated();
                        }
                    };
                    pinBox->add(modeCombo);

                    if (cur.kind == Asset::ParticleValueKindUVE::RandomRange) {
                        pinBox->add(MakeTypedVec3FieldUVE(cur.min,
                            [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const Math::Vector3UVE& v) {
                                editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                                    if (selectedEmitterIndex < s.emitters.size()) {
                                        if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                            Asset::ParticleVectorInputUVE val = m->vectors.count(pinName) ? m->vectors[pinName] : defVal;
                                            val.min = v;
                                            val.max.x = std::max(val.min.x, val.max.x);
                                            val.max.y = std::max(val.min.y, val.max.y);
                                            val.max.z = std::max(val.min.z, val.max.z);
                                            m->vectors[pinName] = std::move(val);
                                        }
                                    }
                                });
                                if (onAssetMutated) {
                                    onAssetMutated();
                                }
                            }));
                        pinBox->add(MakeTypedVec3FieldUVE(cur.max,
                            [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const Math::Vector3UVE& v) {
                                editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                                    if (selectedEmitterIndex < s.emitters.size()) {
                                        if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                            Asset::ParticleVectorInputUVE val = m->vectors.count(pinName) ? m->vectors[pinName] : defVal;
                                            val.max.x = std::max(val.min.x, v.x);
                                            val.max.y = std::max(val.min.y, v.y);
                                            val.max.z = std::max(val.min.z, v.z);
                                            m->vectors[pinName] = std::move(val);
                                        }
                                    }
                                });
                                if (onAssetMutated) {
                                    onAssetMutated();
                                }
                            }));
                    } else if (cur.kind == Asset::ParticleValueKindUVE::UserParameter) {
                        std::vector<std::string> choices = MatchingUserParametersUVE(sys, Asset::ParticleUserParameterTypeUVE::Vector);
                        if (choices.empty()) {
                            choices.push_back("(none)");
                        }
                        int selIdx = 0;
                        for (std::size_t i = 0U; i < choices.size(); ++i) {
                            if (choices[i] == cur.parameter) {
                                selIdx = static_cast<int>(i);
                                break;
                            }
                        }
                        auto paramCombo = prism::make<prism::ComboBox>(choices, selIdx);
                        paramCombo->onChange = [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, choices, onAssetMutated](const int idx) {
                            if (idx < 0 || static_cast<std::size_t>(idx) >= choices.size()) {
                                return;
                            }
                            const std::string chosen = choices[static_cast<std::size_t>(idx)] == "(none)" ? "" : choices[static_cast<std::size_t>(idx)];
                            editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                                if (selectedEmitterIndex < s.emitters.size()) {
                                    if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                        Asset::ParticleVectorInputUVE val = m->vectors.count(pinName) ? m->vectors[pinName] : defVal;
                                        val.parameter = chosen;
                                        m->vectors[pinName] = std::move(val);
                                    }
                                }
                            });
                            if (onAssetMutated) {
                                onAssetMutated();
                            }
                        };
                        pinBox->add(paramCombo);
                    } else {
                        pinBox->add(MakeTypedVec3FieldUVE(cur.constant,
                            [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const Math::Vector3UVE& v) {
                                editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                                    if (selectedEmitterIndex < s.emitters.size()) {
                                        if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                            Asset::ParticleVectorInputUVE val = m->vectors.count(pinName) ? m->vectors[pinName] : defVal;
                                            val.constant = v;
                                            m->vectors[pinName] = std::move(val);
                                        }
                                    }
                                });
                                if (onAssetMutated) {
                                    onAssetMutated();
                                }
                            }));
                    }
                    grid->addRow(modCat, pinName, pinBox);
                }

                // Color inputs.
                for (const auto& [pinName, defVal] : desc->defaultColors) {
                    const auto it = mod->colors.find(pinName);
                    const Asset::ParticleColorInputUVE cur = (it != mod->colors.end()) ? it->second : defVal;

                    auto pinBox = prism::vbox(4.0F);
                    const int modeIdx = (cur.kind == Asset::ParticleValueKindUVE::Gradient)      ? 1
                                        : (cur.kind == Asset::ParticleValueKindUVE::RandomRange) ? 2
                                        : (cur.kind == Asset::ParticleValueKindUVE::UserParameter) ? 3
                                                                                                   : 0;
                    auto modeCombo = prism::make<prism::ComboBox>(
                        std::vector<std::string>{"Constant", "Gradient", "Random Range", "User Parameter"}, modeIdx);
                    modeCombo->onChange = [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const int idx) {
                        editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                            if (selectedEmitterIndex >= s.emitters.size()) {
                                return;
                            }
                            Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId);
                            if (m == nullptr) {
                                return;
                            }
                            Asset::ParticleColorInputUVE val = m->colors.count(pinName) ? m->colors[pinName] : defVal;
                            if (idx == 1) {
                                val.kind = Asset::ParticleValueKindUVE::Gradient;
                                if (val.gradient.stops.empty()) {
                                    AddVfxGradientStopUVE(val.gradient, 0.0F, Math::ColorUVE{1.0F, 1.0F, 1.0F}, 1.0F);
                                    AddVfxGradientStopUVE(val.gradient, 1.0F, Math::ColorUVE{1.0F, 1.0F, 1.0F}, 0.0F);
                                }
                            } else if (idx == 2) {
                                val.kind = Asset::ParticleValueKindUVE::RandomRange;
                            } else if (idx == 3) {
                                val.kind = Asset::ParticleValueKindUVE::UserParameter;
                                const auto params = MatchingUserParametersUVE(s, Asset::ParticleUserParameterTypeUVE::Color);
                                if (val.parameter.empty() && !params.empty()) {
                                    val.parameter = params.front();
                                }
                            } else {
                                val.kind = Asset::ParticleValueKindUVE::Constant;
                            }
                            m->colors[pinName] = std::move(val);
                        });
                        if (onAssetMutated) {
                            onAssetMutated();
                        }
                    };
                    pinBox->add(modeCombo);

                    if (cur.kind == Asset::ParticleValueKindUVE::Gradient) {
                        auto gradWidget = prism::make<prism::GradientEditor>();
                        SyncGradientEditorFromAssetUVE(cur.gradient, *gradWidget);
                        std::weak_ptr<prism::GradientEditor> weakGrad = gradWidget;
                        gradWidget->onChanged = [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, weakGrad, onAssetMutated] {
                            const auto gw = weakGrad.lock();
                            if (!gw) {
                                return;
                            }
                            Asset::ParticleGradientUVE synced;
                            SyncAssetFromGradientEditorUVE(*gw, synced);
                            editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                                if (selectedEmitterIndex < s.emitters.size()) {
                                    if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                        Asset::ParticleColorInputUVE val = m->colors.count(pinName) ? m->colors[pinName] : defVal;
                                        val.kind = Asset::ParticleValueKindUVE::Gradient;
                                        val.gradient = synced;
                                        m->colors[pinName] = std::move(val);
                                    }
                                }
                            });
                            if (onAssetMutated) {
                                onAssetMutated();
                            }
                        };
                        pinBox->add(gradWidget);
                    } else if (cur.kind == Asset::ParticleValueKindUVE::UserParameter) {
                        std::vector<std::string> choices = MatchingUserParametersUVE(sys, Asset::ParticleUserParameterTypeUVE::Color);
                        if (choices.empty()) {
                            choices.push_back("(none)");
                        }
                        int selIdx = 0;
                        for (std::size_t i = 0U; i < choices.size(); ++i) {
                            if (choices[i] == cur.parameter) {
                                selIdx = static_cast<int>(i);
                                break;
                            }
                        }
                        auto paramCombo = prism::make<prism::ComboBox>(choices, selIdx);
                        paramCombo->onChange = [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, choices, onAssetMutated](const int idx) {
                            if (idx < 0 || static_cast<std::size_t>(idx) >= choices.size()) {
                                return;
                            }
                            const std::string chosen = choices[static_cast<std::size_t>(idx)] == "(none)" ? "" : choices[static_cast<std::size_t>(idx)];
                            editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                                if (selectedEmitterIndex < s.emitters.size()) {
                                    if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                        Asset::ParticleColorInputUVE val = m->colors.count(pinName) ? m->colors[pinName] : defVal;
                                        val.parameter = chosen;
                                        m->colors[pinName] = std::move(val);
                                    }
                                }
                            });
                            if (onAssetMutated) {
                                onAssetMutated();
                            }
                        };
                        pinBox->add(paramCombo);
                    } else {
                        const prism::Color initCol(
                            static_cast<std::uint8_t>(std::clamp(cur.constant.r, 0.0F, 1.0F) * 255.0F + 0.5F),
                            static_cast<std::uint8_t>(std::clamp(cur.constant.g, 0.0F, 1.0F) * 255.0F + 0.5F),
                            static_cast<std::uint8_t>(std::clamp(cur.constant.b, 0.0F, 1.0F) * 255.0F + 0.5F),
                            static_cast<std::uint8_t>(std::clamp(cur.alpha, 0.0F, 1.0F) * 255.0F + 0.5F));
                        auto swatch = prism::make<prism::ColorField>(initCol);
                        swatch->onChange = [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const prism::Color c) {
                            editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                                if (selectedEmitterIndex < s.emitters.size()) {
                                    if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                        Asset::ParticleColorInputUVE val = m->colors.count(pinName) ? m->colors[pinName] : defVal;
                                        val.constant = Math::ColorUVE{
                                            static_cast<float>(c.r) / 255.0F,
                                            static_cast<float>(c.g) / 255.0F,
                                            static_cast<float>(c.b) / 255.0F};
                                        val.alpha = static_cast<float>(c.a) / 255.0F;
                                        m->colors[pinName] = std::move(val);
                                    }
                                }
                            });
                            if (onAssetMutated) {
                                onAssetMutated();
                            }
                        };
                        pinBox->add(swatch);
                    }
                    grid->addRow(modCat, pinName, pinBox);
                }

                // Int inputs.
                for (const auto& [pinName, defVal] : desc->defaultInts) {
                    const auto it = mod->ints.find(pinName);
                    const std::int32_t cur = (it != mod->ints.end()) ? it->second : defVal;
                    grid->addRow(modCat, pinName, MakeTypedNumberFieldUVE(static_cast<float>(cur), 1.0F, 0,
                        [&editor, selectedEmitterIndex, selectedModuleId, pinName, onAssetMutated](const float v) {
                            editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                                if (selectedEmitterIndex < s.emitters.size()) {
                                    if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                        m->ints[pinName] = static_cast<std::int32_t>( std::lround(v));
                                    }
                                }
                            });
                            if (onAssetMutated) {
                                onAssetMutated();
                            }
                        }));
                }

                // String inputs.
                for (const auto& [pinName, defVal] : desc->defaultStrings) {
                    const auto it = mod->strings.find(pinName);
                    const std::string cur = (it != mod->strings.end()) ? it->second : defVal;
                    auto textIn = prism::make<prism::TextInput>(cur, pinName);
                    textIn->onCommit = [&editor, selectedEmitterIndex, selectedModuleId, pinName, onAssetMutated](const std::string& v) {
                        editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                            if (selectedEmitterIndex < s.emitters.size()) {
                                if (Asset::ParticleModuleUVE* m = FindMutableModuleInEmitterUVE(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                    m->strings[pinName] = v;
                                }
                            }
                        });
                        if (onAssetMutated) {
                            onAssetMutated();
                        }
                    };
                    grid->addRow(modCat, pinName, textIn);
                }
            }
        }
    }

    // 2. Selected Emitter section.
    if (selectedEmitterIndex < sys.emitters.size()) {
        const Asset::ParticleEmitterAssetUVE& emitter = sys.emitters[selectedEmitterIndex];
        const int emCat = grid->addCategory("Emitter: " + emitter.name);

        auto nameInput = prism::make<prism::TextInput>(emitter.name, "Emitter Name");
        nameInput->onCommit = [&editor, selectedEmitterIndex, onAssetMutated](const std::string& v) {
            if (v.empty()) {
                return;
            }
            editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                if (selectedEmitterIndex < s.emitters.size()) {
                    s.emitters[selectedEmitterIndex].name = v;
                }
            });
            if (onAssetMutated) {
                onAssetMutated();
            }
        };
        grid->addRow(emCat, "Name", nameInput);

        auto emEnabled = prism::make<prism::CheckBox>("", emitter.enabled);
        emEnabled->onChange = [&editor, selectedEmitterIndex, onAssetMutated](const bool v) {
            editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                if (selectedEmitterIndex < s.emitters.size()) {
                    s.emitters[selectedEmitterIndex].enabled = v;
                }
            });
            if (onAssetMutated) {
                onAssetMutated();
            }
        };
        grid->addRow(emCat, "Enabled", emEnabled);

        auto spaceCombo = prism::make<prism::ComboBox>(
            std::vector<std::string>{"World", "Local"},
            emitter.space == Asset::ParticleSimSpaceUVE::Local ? 1 : 0);
        spaceCombo->onChange = [&editor, selectedEmitterIndex, onAssetMutated](const int idx) {
            editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                if (selectedEmitterIndex < s.emitters.size()) {
                    s.emitters[selectedEmitterIndex].space =
                        (idx == 1) ? Asset::ParticleSimSpaceUVE::Local : Asset::ParticleSimSpaceUVE::World;
                }
            });
            if (onAssetMutated) {
                onAssetMutated();
            }
        };
        grid->addRow(emCat, "Simulation Space", spaceCombo);

        auto targetCombo = prism::make<prism::ComboBox>(
            std::vector<std::string>{"CPU", "GPU", "Auto"},
            static_cast<int>(emitter.target));
        targetCombo->onChange = [&editor, selectedEmitterIndex, onAssetMutated](const int idx) {
            editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                if (selectedEmitterIndex < s.emitters.size()) {
                    s.emitters[selectedEmitterIndex].target =
                        static_cast<Asset::ParticleSimTargetUVE>(std::clamp(idx, 0, 2));
                }
            });
            if (onAssetMutated) {
                onAssetMutated();
            }
        };
        grid->addRow(emCat, "Simulation Target", targetCombo);

        grid->addRow(emCat, "Max Particles",
            MakeTypedNumberFieldUVE(static_cast<float>(emitter.maxParticles), 100.0F, 0,
                [&editor, selectedEmitterIndex, onAssetMutated](const float v) {
                    const auto clamped = static_cast<std::uint32_t>(
                        std::clamp(std::lround(v), 1L, static_cast<long>(Asset::kMaximumParticlesPerEmitterUVE)));
                    editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                        if (selectedEmitterIndex < s.emitters.size()) {
                            s.emitters[selectedEmitterIndex].maxParticles = clamped;
                        }
                    });
                    if (onAssetMutated) {
                        onAssetMutated();
                    }
                }));

        grid->addRow(emCat, "Seed",
            MakeTypedNumberFieldUVE(static_cast<float>(emitter.seed), 1.0F, 0,
                [&editor, selectedEmitterIndex, onAssetMutated](const float v) {
                    const auto seedVal = static_cast<std::uint32_t>(std::max(0L, std::lround(v)));
                    editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                        if (selectedEmitterIndex < s.emitters.size()) {
                            s.emitters[selectedEmitterIndex].seed = seedVal;
                        }
                    });
                    if (onAssetMutated) {
                        onAssetMutated();
                    }
                }));
    }

    // 3. Particle System settings section.
    const int sysCat = grid->addCategory("Particle System");
    grid->addRow(sysCat, "Duration (s)",
        MakeTypedNumberFieldUVE(sys.duration, 0.1F, 2, [&editor, onAssetMutated](const float v) {
            editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                s.duration = std::clamp(v, 0.05F, 3600.0F);
            });
            if (onAssetMutated) {
                onAssetMutated();
            }
        }));

    auto loopCheck = prism::make<prism::CheckBox>("", sys.looping);
    loopCheck->onChange = [&editor, onAssetMutated](const bool v) {
        editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) { s.looping = v; });
        if (onAssetMutated) {
            onAssetMutated();
        }
    };
    grid->addRow(sysCat, "Looping", loopCheck);

    grid->addRow(sysCat, "Warm-Up (s)",
        MakeTypedNumberFieldUVE(sys.warmUpSeconds, 0.1F, 2, [&editor, onAssetMutated](const float v) {
            editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                s.warmUpSeconds = std::clamp(v, 0.0F, 10.0F);
            });
            if (onAssetMutated) {
                onAssetMutated();
            }
        }));

    grid->addRow(sysCat, "Bounds Min",
        MakeTypedVec3FieldUVE(sys.fixedBounds.min, [&editor, onAssetMutated](const Math::Vector3UVE& v) {
            editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                s.fixedBounds.min = v;
                s.fixedBounds.max.x = std::max(s.fixedBounds.min.x + 0.1F, s.fixedBounds.max.x);
                s.fixedBounds.max.y = std::max(s.fixedBounds.min.y + 0.1F, s.fixedBounds.max.y);
                s.fixedBounds.max.z = std::max(s.fixedBounds.min.z + 0.1F, s.fixedBounds.max.z);
            });
            if (onAssetMutated) {
                onAssetMutated();
            }
        }));

    grid->addRow(sysCat, "Bounds Max",
        MakeTypedVec3FieldUVE(sys.fixedBounds.max, [&editor, onAssetMutated](const Math::Vector3UVE& v) {
            editor.EditVfxSystemUVE([&](Asset::ParticleSystemAssetUVE& s) {
                s.fixedBounds.max.x = std::max(s.fixedBounds.min.x + 0.1F, v.x);
                s.fixedBounds.max.y = std::max(s.fixedBounds.min.y + 0.1F, v.y);
                s.fixedBounds.max.z = std::max(s.fixedBounds.min.z + 0.1F, v.z);
            });
            if (onAssetMutated) {
                onAssetMutated();
            }
        }));

    return prism::make<FitWidthUVE>(grid);
}

} // namespace UVE::Editor
