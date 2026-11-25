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

constexpr float kCurveTimeEpsilon = 1e-4F;

[[nodiscard]] prism::Ref<prism::NumberField> MakeTypedNumberField(
    const float value,
    const float step,
    const int decimals,
    std::function<void(float)> onCommit) {
    auto field = prism::make<prism::NumberField>(value, step, decimals);
    field->setScrubbable(false);
    field->onChange = std::move(onCommit);
    return field;
}

[[nodiscard]] prism::Ref<prism::Vec3Field> MakeTypedVec3Field(
    const Math::Vector3& v,
    std::function<void(const Math::Vector3&)> onCommit) {
    auto field = prism::make<prism::Vec3Field>(0.1F, 2);
    field->setScrubbable(false);
    field->setValue(v.x, v.y, v.z);
    prism::Vec3Field* const raw = field.get();
    field->onChange = [raw, onCommit = std::move(onCommit)](const int, const float) {
        if (onCommit) {
            onCommit(Math::Vector3{raw->get(0), raw->get(1), raw->get(2)});
        }
    };
    return field;
}

[[nodiscard]] Asset::ParticleModule* FindMutableModuleInEmitter(
    Asset::ParticleEmitterAsset& emitter,
    const std::uint32_t moduleId) noexcept {
    auto findIn = [moduleId](std::vector<Asset::ParticleModule>& list) -> Asset::ParticleModule* {
        for (Asset::ParticleModule& m : list) {
            if (m.id == moduleId) {
                return &m;
            }
        }
        return nullptr;
    };
    if (Asset::ParticleModule* m = findIn(emitter.emitterUpdate)) {
        return m;
    }
    if (Asset::ParticleModule* m = findIn(emitter.particleSpawn)) {
        return m;
    }
    if (Asset::ParticleModule* m = findIn(emitter.particleUpdate)) {
        return m;
    }
    if (Asset::ParticleModule* m = findIn(emitter.render)) {
        return m;
    }
    return findIn(emitter.detached);
}

[[nodiscard]] const Asset::ParticleModule* FindConstModuleInEmitter(
    const Asset::ParticleEmitterAsset& emitter,
    const std::uint32_t moduleId) noexcept {
    auto findIn = [moduleId](const std::vector<Asset::ParticleModule>& list) -> const Asset::ParticleModule* {
        for (const Asset::ParticleModule& m : list) {
            if (m.id == moduleId) {
                return &m;
            }
        }
        return nullptr;
    };
    if (const Asset::ParticleModule* m = findIn(emitter.emitterUpdate)) {
        return m;
    }
    if (const Asset::ParticleModule* m = findIn(emitter.particleSpawn)) {
        return m;
    }
    if (const Asset::ParticleModule* m = findIn(emitter.particleUpdate)) {
        return m;
    }
    if (const Asset::ParticleModule* m = findIn(emitter.render)) {
        return m;
    }
    return findIn(emitter.detached);
}

[[nodiscard]] std::vector<std::string> MatchingUserParameters(
    const Asset::ParticleSystemAsset& system,
    const Asset::ParticleUserParameterType desiredType) {
    std::vector<std::string> names;
    for (const Asset::ParticleUserParameter& param : system.parameters) {
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

std::size_t AddVfxCurveKey(Asset::ParticleCurve& curve,
                              const float time,
                              const float value,
                              const float inTangent,
                              const float outTangent) {
    const float clampedTime = std::clamp(std::isfinite(time) ? time : 0.0F, 0.0F, 1.0F);
    const float safeValue = std::isfinite(value) ? value : 0.0F;
    const float safeIn = std::isfinite(inTangent) ? inTangent : 0.0F;
    const float safeOut = std::isfinite(outTangent) ? outTangent : 0.0F;

    for (std::size_t i = 0U; i < curve.keys.size(); ++i) {
        if (std::fabs(curve.keys[i].time - clampedTime) <= kCurveTimeEpsilon) {
            curve.keys[i].time = clampedTime;
            curve.keys[i].value = safeValue;
            curve.keys[i].inTangent = safeIn;
            curve.keys[i].outTangent = safeOut;
            return i;
        }
    }

    if (curve.keys.size() >= Asset::kMaximumCurveKeys) {
        return curve.keys.size() - 1U;
    }

    Asset::ParticleCurveKey key{};
    key.time = clampedTime;
    key.value = safeValue;
    key.inTangent = safeIn;
    key.outTangent = safeOut;

    const auto insertPos = std::lower_bound(
        curve.keys.begin(), curve.keys.end(), clampedTime,
        [](const Asset::ParticleCurveKey& a, const float t) { return a.time < t; });
    const auto idx = static_cast<std::size_t>(std::distance(curve.keys.begin(), insertPos));
    curve.keys.insert(insertPos, key);
    return idx;
}

std::size_t MoveVfxCurveKey(Asset::ParticleCurve& curve,
                               const std::size_t keyIndex,
                               const float newTime,
                               const float newValue) {
    if (keyIndex >= curve.keys.size()) {
        return 0U;
    }
    Asset::ParticleCurveKey moved = curve.keys[keyIndex];
    moved.time = std::clamp(std::isfinite(newTime) ? newTime : moved.time, 0.0F, 1.0F);
    moved.value = std::isfinite(newValue) ? newValue : moved.value;
    curve.keys.erase(curve.keys.begin() + static_cast<std::ptrdiff_t>(keyIndex));

    const auto insertPos = std::lower_bound(
        curve.keys.begin(), curve.keys.end(), moved.time,
        [](const Asset::ParticleCurveKey& a, const float t) { return a.time < t; });
    const auto newIdx = static_cast<std::size_t>(std::distance(curve.keys.begin(), insertPos));
    curve.keys.insert(insertPos, moved);
    return newIdx;
}

bool DeleteVfxCurveKey(Asset::ParticleCurve& curve,
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

bool SetVfxCurveKeyTangents(Asset::ParticleCurve& curve,
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

void ApplyVfxCurvePreset(Asset::ParticleCurve& curve, const VfxCurvePreset preset) {
    curve.keys.clear();
    curve.smooth = true;
    switch (preset) {
        case VfxCurvePreset::Linear:
            curve.smooth = false;
            curve.keys.push_back(Asset::ParticleCurveKey{0.0F, 0.0F, 1.0F, 1.0F});
            curve.keys.push_back(Asset::ParticleCurveKey{1.0F, 1.0F, 1.0F, 1.0F});
            break;
        case VfxCurvePreset::EaseIn:
            curve.keys.push_back(Asset::ParticleCurveKey{0.0F, 0.0F, 0.0F, 0.0F});
            curve.keys.push_back(Asset::ParticleCurveKey{1.0F, 1.0F, 2.0F, 2.0F});
            break;
        case VfxCurvePreset::EaseOut:
            curve.keys.push_back(Asset::ParticleCurveKey{0.0F, 0.0F, 2.0F, 2.0F});
            curve.keys.push_back(Asset::ParticleCurveKey{1.0F, 1.0F, 0.0F, 0.0F});
            break;
        case VfxCurvePreset::Bell:
            curve.keys.push_back(Asset::ParticleCurveKey{0.0F, 0.0F, 0.0F, 2.5F});
            curve.keys.push_back(Asset::ParticleCurveKey{0.5F, 1.0F, 0.0F, 0.0F});
            curve.keys.push_back(Asset::ParticleCurveKey{1.0F, 0.0F, -2.5F, 0.0F});
            break;
        case VfxCurvePreset::FadeOut:
            curve.keys.push_back(Asset::ParticleCurveKey{0.0F, 1.0F, 0.0F, -0.5F});
            curve.keys.push_back(Asset::ParticleCurveKey{1.0F, 0.0F, -1.5F, 0.0F});
            break;
    }
}

std::size_t AddVfxGradientStop(Asset::ParticleGradient& gradient,
                                  const float time,
                                  const Math::Color& color,
                                  const float alpha) {
    const float clampedTime = std::clamp(std::isfinite(time) ? time : 0.0F, 0.0F, 1.0F);
    const float clampedAlpha = std::clamp(std::isfinite(alpha) ? alpha : 1.0F, 0.0F, 1.0F);
    Math::Color safeColor = color;
    if (!std::isfinite(safeColor.r) || safeColor.r < 0.0F) { safeColor.r = 0.0F; }
    if (!std::isfinite(safeColor.g) || safeColor.g < 0.0F) { safeColor.g = 0.0F; }
    if (!std::isfinite(safeColor.b) || safeColor.b < 0.0F) { safeColor.b = 0.0F; }

    for (std::size_t i = 0U; i < gradient.stops.size(); ++i) {
        if (std::fabs(gradient.stops[i].time - clampedTime) <= kCurveTimeEpsilon) {
            gradient.stops[i].time = clampedTime;
            gradient.stops[i].color = safeColor;
            gradient.stops[i].alpha = clampedAlpha;
            return i;
        }
    }

    if (gradient.stops.size() >= Asset::kMaximumGradientStops) {
        return gradient.stops.size() - 1U;
    }

    Asset::ParticleGradientStop stop{};
    stop.time = clampedTime;
    stop.color = safeColor;
    stop.alpha = clampedAlpha;

    const auto insertPos = std::lower_bound(
        gradient.stops.begin(), gradient.stops.end(), clampedTime,
        [](const Asset::ParticleGradientStop& a, const float t) { return a.time < t; });
    const auto idx = static_cast<std::size_t>(std::distance(gradient.stops.begin(), insertPos));
    gradient.stops.insert(insertPos, stop);
    return idx;
}

std::size_t MoveVfxGradientStop(Asset::ParticleGradient& gradient,
                                   const std::size_t stopIndex,
                                   const float newTime) {
    if (stopIndex >= gradient.stops.size()) {
        return 0U;
    }
    Asset::ParticleGradientStop moved = gradient.stops[stopIndex];
    moved.time = std::clamp(std::isfinite(newTime) ? newTime : moved.time, 0.0F, 1.0F);
    gradient.stops.erase(gradient.stops.begin() + static_cast<std::ptrdiff_t>(stopIndex));

    const auto insertPos = std::lower_bound(
        gradient.stops.begin(), gradient.stops.end(), moved.time,
        [](const Asset::ParticleGradientStop& a, const float t) { return a.time < t; });
    const auto newIdx = static_cast<std::size_t>(std::distance(gradient.stops.begin(), insertPos));
    gradient.stops.insert(insertPos, moved);
    return newIdx;
}

bool DeleteVfxGradientStop(Asset::ParticleGradient& gradient,
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

void SyncGradientEditorFromAsset(const Asset::ParticleGradient& assetGrad,
                                    prism::GradientEditor& widget) {
    std::vector<prism::GradientColorStop> cStops;
    std::vector<prism::GradientAlphaStop> aStops;
    if (assetGrad.stops.empty()) {
        cStops.push_back({0.0F, 1.0F, 1.0F, 1.0F});
        cStops.push_back({1.0F, 1.0F, 1.0F, 1.0F});
        aStops.push_back({0.0F, 1.0F});
        aStops.push_back({1.0F, 1.0F});
    } else {
        for (const Asset::ParticleGradientStop& s : assetGrad.stops) {
            cStops.push_back({s.time, s.color.r, s.color.g, s.color.b});
            aStops.push_back({s.time, s.alpha});
        }
    }
    widget.setStops(std::move(cStops), std::move(aStops));
}

void SyncAssetFromGradientEditor(const prism::GradientEditor& widget,
                                    Asset::ParticleGradient& outGrad) {
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
    while (uniqueTimes.size() > Asset::kMaximumGradientStops) {
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
        Asset::ParticleGradientStop stop{};
        stop.time = t;
        stop.color = Math::Color{r, g, b};
        stop.alpha = a;
        outGrad.stops.push_back(stop);
    }
}

// -----------------------------------------------------------------------------
// VfxCurveEditWidget
// -----------------------------------------------------------------------------

VfxCurveEditWidget::VfxCurveEditWidget(Asset::ParticleCurve curve)
    : m_curve(std::move(curve)) {
    if (m_curve.keys.empty()) {
        ApplyVfxCurvePreset(m_curve, VfxCurvePreset::Linear);
    }
    setMinSize(180.0F, 112.0F);
}

void VfxCurveEditWidget::setCurve(const Asset::ParticleCurve& curve) {
    m_curve = curve;
    if (m_curve.keys.empty()) {
        ApplyVfxCurvePreset(m_curve, VfxCurvePreset::Linear);
    }
    if (m_selectedKey >= static_cast<int>(m_curve.keys.size())) {
        m_selectedKey = static_cast<int>(m_curve.keys.size()) - 1;
    }
    invalidate();
}

void VfxCurveEditWidget::setSelectedKey(const int index) {
    const int clamped = (index >= 0 && static_cast<std::size_t>(index) < m_curve.keys.size()) ? index : -1;
    if (m_selectedKey != clamped) {
        m_selectedKey = clamped;
        invalidate();
    }
}

prism::Vec2 VfxCurveEditWidget::measure(const prism::Vec2 avail) {
    return m_desired = constrain({std::max(180.0F, avail.x), 112.0F});
}

VfxCurveEditWidget::ValueRange VfxCurveEditWidget::ComputeRange() const noexcept {
    if (m_draggingKey) {
        return m_dragRange;
    }
    float lo = 0.0F;
    float hi = 1.0F;
    for (const Asset::ParticleCurveKey& k : m_curve.keys) {
        lo = std::min(lo, k.value);
        hi = std::max(hi, k.value);
    }
    if (hi - lo < 1e-3F) {
        hi = lo + 1.0F;
    }
    return ValueRange{lo, hi};
}

prism::Rect VfxCurveEditWidget::PlotRect() const noexcept {
    return m_rect.deflated(8.0F);
}

prism::Vec2 VfxCurveEditWidget::KeyToScreen(
    const float time, const float value, const prism::Rect& plot, const ValueRange range) const noexcept {
    const float span = std::max(1e-5F, range.maxVal - range.minVal);
    const float x = plot.x + std::clamp(time, 0.0F, 1.0F) * plot.w;
    const float y = plot.bottom() - ((value - range.minVal) / span) * plot.h;
    return {x, y};
}

std::pair<float, float> VfxCurveEditWidget::ScreenToKey(
    const prism::Vec2 pt, const prism::Rect& plot, const ValueRange range) const noexcept {
    const float t = plot.w > 1.0F ? std::clamp((pt.x - plot.x) / plot.w, 0.0F, 1.0F) : 0.0F;
    const float normY = plot.h > 1.0F ? std::clamp((plot.bottom() - pt.y) / plot.h, 0.0F, 1.0F) : 0.0F;
    const float v = range.minVal + normY * (range.maxVal - range.minVal);
    return {t, v};
}

int VfxCurveEditWidget::HitTestKey(
    const prism::Vec2 pt, const prism::Rect& plot, const ValueRange range) const noexcept {
    for (int i = 0; i < static_cast<int>(m_curve.keys.size()); ++i) {
        const prism::Vec2 kp = KeyToScreen(
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

void VfxCurveEditWidget::NotifyChanged() {
    invalidate();
    if (onChanged) {
        onChanged(m_curve);
    }
}

void VfxCurveEditWidget::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    c.fillRoundRect(m_rect, 4.0F, t.field);
    c.strokeRoundRect(m_rect, 4.0F, focused() ? t.highlight : t.border, 1.0F);

    const prism::Rect plot = PlotRect();
    if (plot.w <= 4.0F || plot.h <= 4.0F) {
        return;
    }

    const ValueRange range = ComputeRange();
    // Grid lines (25%, 50%, 75%).
    for (int i = 1; i <= 3; ++i) {
        const float f = static_cast<float>(i) * 0.25F;
        const float gx = plot.x + f * plot.w;
        const float gy = plot.y + f * plot.h;
        c.line({gx, plot.y}, {gx, plot.bottom()}, t.borderSoft, 1.0F);
        c.line({plot.x, gy}, {plot.right(), gy}, t.borderSoft, 1.0F);
    }

    // Sample curve across 64 segments.
    c.pushClip(m_rect.deflated(1.0F));
    constexpr int kSamples = 64;
    prism::Vec2 prevPt{};
    for (int s = 0; s <= kSamples; ++s) {
        const float u = static_cast<float>(s) / static_cast<float>(kSamples);
        const float val = VFX::EvaluateVfxCurveExact(m_curve, u);
        const prism::Vec2 curPt = KeyToScreen(u, val, plot, range);
        if (s > 0) {
            c.line(prevPt, curPt, t.highlight, 2.0F);
        }
        prevPt = curPt;
    }

    // Draw keys and tangent handles for the selected key.
    for (int i = 0; i < static_cast<int>(m_curve.keys.size()); ++i) {
        const Asset::ParticleCurveKey& key = m_curve.keys[static_cast<std::size_t>(i)];
        const prism::Vec2 kp = KeyToScreen(key.time, key.value, plot, range);
        const bool sel = (i == m_selectedKey);
        if (sel && m_curve.smooth) {
            const float dt = 0.08F;
            const prism::Vec2 outPt = KeyToScreen(
                std::min(1.0F, key.time + dt), key.value + key.outTangent * dt, plot, range);
            const prism::Vec2 inPt = KeyToScreen(
                std::max(0.0F, key.time - dt), key.value - key.inTangent * dt, plot, range);
            c.line(kp, outPt, t.textDim, 1.0F);
            c.line(kp, inPt, t.textDim, 1.0F);
            c.fillCircle(outPt, 3.0F, t.textBright);
            c.fillCircle(inPt, 3.0F, t.textBright);
        }
        c.fillCircle(kp, sel ? 5.0F : 4.0F, sel ? t.highlight : t.textBright);
        c.strokeCircle(kp, sel ? 5.0F : 4.0F, t.window, 1.2F);
    }
    c.popClip();
}

bool VfxCurveEditWidget::onMouseDown(const prism::MouseEvent& e) {
    if (e.button != prism::MouseButton::Left) {
        return false;
    }
    if (prism::Application::exists()) {
        prism::Application::get().setFocus(this);
    }
    const prism::Rect plot = PlotRect();
    const ValueRange range = ComputeRange();
    const int hit = HitTestKey(e.pos, plot, range);

    if (e.clicks >= 2 && hit < 0) {
        const auto [t, v] = ScreenToKey(e.pos, plot, range);
        m_selectedKey = static_cast<int>(AddVfxCurveKey(m_curve, t, v, 0.0F, 0.0F));
        NotifyChanged();
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

bool VfxCurveEditWidget::onMouseMove(const prism::MouseEvent& e) {
    if (!m_draggingKey && !m_draggingTangent) {
        return false;
    }
    if (m_selectedKey < 0 || static_cast<std::size_t>(m_selectedKey) >= m_curve.keys.size()) {
        return false;
    }
    const prism::Rect plot = PlotRect();
    const auto [t, v] = ScreenToKey(e.pos, plot, m_dragRange);

    if (m_draggingTangent) {
        const Asset::ParticleCurveKey& key = m_curve.keys[static_cast<std::size_t>(m_selectedKey)];
        const float dt = t - key.time;
        const float dv = v - key.value;
        const float slope = std::fabs(dt) > 1e-3F ? std::clamp(dv / dt, -20.0F, 20.0F) : 0.0F;
        SetVfxCurveKeyTangents(m_curve, static_cast<std::size_t>(m_selectedKey), slope, slope);
        m_curve.smooth = true;
        NotifyChanged();
        return true;
    }

    m_selectedKey = static_cast<int>(
        MoveVfxCurveKey(m_curve, static_cast<std::size_t>(m_selectedKey), t, v));
    NotifyChanged();
    return true;
}

bool VfxCurveEditWidget::onMouseUp(const prism::MouseEvent&) {
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

bool VfxCurveEditWidget::onKeyDown(const prism::KeyEvent& e) {
    if ((e.key == prism::Key::Delete || e.key == prism::Key::Backspace) && m_selectedKey >= 0) {
        if (DeleteVfxCurveKey(m_curve, static_cast<std::size_t>(m_selectedKey), true)) {
            if (m_selectedKey >= static_cast<int>(m_curve.keys.size())) {
                m_selectedKey = static_cast<int>(m_curve.keys.size()) - 1;
            }
            NotifyChanged();
            return true;
        }
    }
    return false;
}

// -----------------------------------------------------------------------------
// BuildVfxInspectorContent
// -----------------------------------------------------------------------------

prism::Ref<prism::Widget> BuildVfxInspectorContent(
    Editor& editor,
    const std::size_t selectedEmitterIndex,
    const std::uint32_t selectedModuleId,
    const std::function<void()>& onAssetMutated) {
    Editor::VfxEditSession* const session = editor.VfxEditSessionPtr();
    if (session == nullptr) {
        return prism::make<prism::Spacer>();
    }

    const Asset::ParticleSystemAsset& sys = session->system;
    auto grid = prism::make<prism::PropertyGrid>();

    // 1. Selected Module section (if any valid module is selected).
    if (selectedEmitterIndex < sys.emitters.size() && selectedModuleId != 0U) {
        const Asset::ParticleEmitterAsset& emitter = sys.emitters[selectedEmitterIndex];
        if (const Asset::ParticleModule* const mod = FindConstModuleInEmitter(emitter, selectedModuleId)) {
            const Asset::ParticleModuleDescriptor* const desc =
                Asset::FindParticleModuleDescriptor(mod->type);
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
                editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                    if (selectedEmitterIndex < s.emitters.size()) {
                        if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
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
                    const Asset::ParticleFloatInput cur = (it != mod->floats.end()) ? it->second : defVal;

                    auto pinBox = prism::vbox(4.0F);
                    const int modeIdx = (cur.kind == Asset::ParticleValueKind::RandomRange)     ? 1
                                        : (cur.kind == Asset::ParticleValueKind::Curve)         ? 2
                                        : (cur.kind == Asset::ParticleValueKind::UserParameter) ? 3
                                                                                                   : 0;
                    auto modeCombo = prism::make<prism::ComboBox>(
                        std::vector<std::string>{"Constant", "Random Range", "Curve", "User Parameter"}, modeIdx);
                    modeCombo->onChange = [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const int idx) {
                        editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                            if (selectedEmitterIndex >= s.emitters.size()) {
                                return;
                            }
                            Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId);
                            if (m == nullptr) {
                                return;
                            }
                            Asset::ParticleFloatInput val = m->floats.count(pinName) ? m->floats[pinName] : defVal;
                            if (idx == 1) {
                                val.kind = Asset::ParticleValueKind::RandomRange;
                                if (val.max < val.min) {
                                    val.max = val.min + 1.0F;
                                }
                            } else if (idx == 2) {
                                val.kind = Asset::ParticleValueKind::Curve;
                                if (val.curve.keys.empty()) {
                                    ApplyVfxCurvePreset(val.curve, VfxCurvePreset::Linear);
                                }
                            } else if (idx == 3) {
                                val.kind = Asset::ParticleValueKind::UserParameter;
                                const auto params = MatchingUserParameters(s, Asset::ParticleUserParameterType::Float);
                                if (val.parameter.empty() && !params.empty()) {
                                    val.parameter = params.front();
                                }
                            } else {
                                val.kind = Asset::ParticleValueKind::Constant;
                            }
                            m->floats[pinName] = std::move(val);
                        });
                        if (onAssetMutated) {
                            onAssetMutated();
                        }
                    };
                    pinBox->add(modeCombo);

                    if (cur.kind == Asset::ParticleValueKind::Constant || cur.kind == Asset::ParticleValueKind::Wire) {
                        pinBox->add(MakeTypedNumberField(cur.constant, 0.1F, 2,
                            [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const float v) {
                                editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                                    if (selectedEmitterIndex < s.emitters.size()) {
                                        if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                            Asset::ParticleFloatInput val = m->floats.count(pinName) ? m->floats[pinName] : defVal;
                                            val.constant = v;
                                            m->floats[pinName] = std::move(val);
                                        }
                                    }
                                });
                                if (onAssetMutated) {
                                    onAssetMutated();
                                }
                            }));
                    } else if (cur.kind == Asset::ParticleValueKind::RandomRange) {
                        auto minMaxRow = prism::hbox(4.0F);
                        minMaxRow->add(MakeTypedNumberField(cur.min, 0.1F, 2,
                            [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const float v) {
                                editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                                    if (selectedEmitterIndex < s.emitters.size()) {
                                        if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                            Asset::ParticleFloatInput val = m->floats.count(pinName) ? m->floats[pinName] : defVal;
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
                        minMaxRow->add(MakeTypedNumberField(cur.max, 0.1F, 2,
                            [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const float v) {
                                editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                                    if (selectedEmitterIndex < s.emitters.size()) {
                                        if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                            Asset::ParticleFloatInput val = m->floats.count(pinName) ? m->floats[pinName] : defVal;
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
                    } else if (cur.kind == Asset::ParticleValueKind::Curve) {
                        auto curveEditor = prism::make<VfxCurveEditWidget>(cur.curve);
                        curveEditor->onChanged = [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](
                                                     const Asset::ParticleCurve& updatedCurve) {
                            editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                                if (selectedEmitterIndex < s.emitters.size()) {
                                    if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                        Asset::ParticleFloatInput val = m->floats.count(pinName) ? m->floats[pinName] : defVal;
                                        val.kind = Asset::ParticleValueKind::Curve;
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
                        const std::array<std::pair<const char*, VfxCurvePreset>, 5> kPresets{{
                            {"Linear", VfxCurvePreset::Linear},
                            {"Ease In", VfxCurvePreset::EaseIn},
                            {"Ease Out", VfxCurvePreset::EaseOut},
                            {"Bell", VfxCurvePreset::Bell},
                            {"Fade Out", VfxCurvePreset::FadeOut},
                        }};
                        for (const auto& [label, preset] : kPresets) {
                            auto btn = prism::make<prism::Button>(label, prism::Icon::None, prism::ButtonStyle::Flat);
                            btn->onClick = [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, preset, onAssetMutated] {
                                editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                                    if (selectedEmitterIndex < s.emitters.size()) {
                                        if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                            Asset::ParticleFloatInput val = m->floats.count(pinName) ? m->floats[pinName] : defVal;
                                            val.kind = Asset::ParticleValueKind::Curve;
                                            ApplyVfxCurvePreset(val.curve, preset);
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
                        scaleRow->add(MakeTypedNumberField(cur.scale, 0.1F, 2,
                            [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const float v) {
                                editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                                    if (selectedEmitterIndex < s.emitters.size()) {
                                        if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                            Asset::ParticleFloatInput val = m->floats.count(pinName) ? m->floats[pinName] : defVal;
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
                    } else if (cur.kind == Asset::ParticleValueKind::UserParameter) {
                        std::vector<std::string> choices = MatchingUserParameters(sys, Asset::ParticleUserParameterType::Float);
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
                            editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                                if (selectedEmitterIndex < s.emitters.size()) {
                                    if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                        Asset::ParticleFloatInput val = m->floats.count(pinName) ? m->floats[pinName] : defVal;
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
                    const Asset::ParticleVectorInput cur = (it != mod->vectors.end()) ? it->second : defVal;

                    auto pinBox = prism::vbox(4.0F);
                    const int modeIdx = (cur.kind == Asset::ParticleValueKind::RandomRange)     ? 1
                                        : (cur.kind == Asset::ParticleValueKind::UserParameter) ? 2
                                                                                                   : 0;
                    auto modeCombo = prism::make<prism::ComboBox>(
                        std::vector<std::string>{"Constant", "Random Range", "User Parameter"}, modeIdx);
                    modeCombo->onChange = [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const int idx) {
                        editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                            if (selectedEmitterIndex >= s.emitters.size()) {
                                return;
                            }
                            Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId);
                            if (m == nullptr) {
                                return;
                            }
                            Asset::ParticleVectorInput val = m->vectors.count(pinName) ? m->vectors[pinName] : defVal;
                            if (idx == 1) {
                                val.kind = Asset::ParticleValueKind::RandomRange;
                            } else if (idx == 2) {
                                val.kind = Asset::ParticleValueKind::UserParameter;
                                const auto params = MatchingUserParameters(s, Asset::ParticleUserParameterType::Vector);
                                if (val.parameter.empty() && !params.empty()) {
                                    val.parameter = params.front();
                                }
                            } else {
                                val.kind = Asset::ParticleValueKind::Constant;
                            }
                            m->vectors[pinName] = std::move(val);
                        });
                        if (onAssetMutated) {
                            onAssetMutated();
                        }
                    };
                    pinBox->add(modeCombo);

                    if (cur.kind == Asset::ParticleValueKind::RandomRange) {
                        pinBox->add(MakeTypedVec3Field(cur.min,
                            [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const Math::Vector3& v) {
                                editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                                    if (selectedEmitterIndex < s.emitters.size()) {
                                        if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                            Asset::ParticleVectorInput val = m->vectors.count(pinName) ? m->vectors[pinName] : defVal;
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
                        pinBox->add(MakeTypedVec3Field(cur.max,
                            [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const Math::Vector3& v) {
                                editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                                    if (selectedEmitterIndex < s.emitters.size()) {
                                        if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                            Asset::ParticleVectorInput val = m->vectors.count(pinName) ? m->vectors[pinName] : defVal;
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
                    } else if (cur.kind == Asset::ParticleValueKind::UserParameter) {
                        std::vector<std::string> choices = MatchingUserParameters(sys, Asset::ParticleUserParameterType::Vector);
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
                            editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                                if (selectedEmitterIndex < s.emitters.size()) {
                                    if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                        Asset::ParticleVectorInput val = m->vectors.count(pinName) ? m->vectors[pinName] : defVal;
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
                        pinBox->add(MakeTypedVec3Field(cur.constant,
                            [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const Math::Vector3& v) {
                                editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                                    if (selectedEmitterIndex < s.emitters.size()) {
                                        if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                            Asset::ParticleVectorInput val = m->vectors.count(pinName) ? m->vectors[pinName] : defVal;
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
                    const Asset::ParticleColorInput cur = (it != mod->colors.end()) ? it->second : defVal;

                    auto pinBox = prism::vbox(4.0F);
                    const int modeIdx = (cur.kind == Asset::ParticleValueKind::Gradient)      ? 1
                                        : (cur.kind == Asset::ParticleValueKind::RandomRange) ? 2
                                        : (cur.kind == Asset::ParticleValueKind::UserParameter) ? 3
                                                                                                   : 0;
                    auto modeCombo = prism::make<prism::ComboBox>(
                        std::vector<std::string>{"Constant", "Gradient", "Random Range", "User Parameter"}, modeIdx);
                    modeCombo->onChange = [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, onAssetMutated](const int idx) {
                        editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                            if (selectedEmitterIndex >= s.emitters.size()) {
                                return;
                            }
                            Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId);
                            if (m == nullptr) {
                                return;
                            }
                            Asset::ParticleColorInput val = m->colors.count(pinName) ? m->colors[pinName] : defVal;
                            if (idx == 1) {
                                val.kind = Asset::ParticleValueKind::Gradient;
                                if (val.gradient.stops.empty()) {
                                    AddVfxGradientStop(val.gradient, 0.0F, Math::Color{1.0F, 1.0F, 1.0F}, 1.0F);
                                    AddVfxGradientStop(val.gradient, 1.0F, Math::Color{1.0F, 1.0F, 1.0F}, 0.0F);
                                }
                            } else if (idx == 2) {
                                val.kind = Asset::ParticleValueKind::RandomRange;
                            } else if (idx == 3) {
                                val.kind = Asset::ParticleValueKind::UserParameter;
                                const auto params = MatchingUserParameters(s, Asset::ParticleUserParameterType::Color);
                                if (val.parameter.empty() && !params.empty()) {
                                    val.parameter = params.front();
                                }
                            } else {
                                val.kind = Asset::ParticleValueKind::Constant;
                            }
                            m->colors[pinName] = std::move(val);
                        });
                        if (onAssetMutated) {
                            onAssetMutated();
                        }
                    };
                    pinBox->add(modeCombo);

                    if (cur.kind == Asset::ParticleValueKind::Gradient) {
                        auto gradWidget = prism::make<prism::GradientEditor>();
                        SyncGradientEditorFromAsset(cur.gradient, *gradWidget);
                        std::weak_ptr<prism::GradientEditor> weakGrad = gradWidget;
                        gradWidget->onChanged = [&editor, selectedEmitterIndex, selectedModuleId, pinName, defVal, weakGrad, onAssetMutated] {
                            const auto gw = weakGrad.lock();
                            if (!gw) {
                                return;
                            }
                            Asset::ParticleGradient synced;
                            SyncAssetFromGradientEditor(*gw, synced);
                            editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                                if (selectedEmitterIndex < s.emitters.size()) {
                                    if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                        Asset::ParticleColorInput val = m->colors.count(pinName) ? m->colors[pinName] : defVal;
                                        val.kind = Asset::ParticleValueKind::Gradient;
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
                    } else if (cur.kind == Asset::ParticleValueKind::UserParameter) {
                        std::vector<std::string> choices = MatchingUserParameters(sys, Asset::ParticleUserParameterType::Color);
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
                            editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                                if (selectedEmitterIndex < s.emitters.size()) {
                                    if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                        Asset::ParticleColorInput val = m->colors.count(pinName) ? m->colors[pinName] : defVal;
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
                            editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                                if (selectedEmitterIndex < s.emitters.size()) {
                                    if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
                                        Asset::ParticleColorInput val = m->colors.count(pinName) ? m->colors[pinName] : defVal;
                                        val.constant = Math::Color{
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
                    grid->addRow(modCat, pinName, MakeTypedNumberField(static_cast<float>(cur), 1.0F, 0,
                        [&editor, selectedEmitterIndex, selectedModuleId, pinName, onAssetMutated](const float v) {
                            editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                                if (selectedEmitterIndex < s.emitters.size()) {
                                    if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
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
                        editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                            if (selectedEmitterIndex < s.emitters.size()) {
                                if (Asset::ParticleModule* m = FindMutableModuleInEmitter(s.emitters[selectedEmitterIndex], selectedModuleId)) {
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
        const Asset::ParticleEmitterAsset& emitter = sys.emitters[selectedEmitterIndex];
        const int emCat = grid->addCategory("Emitter: " + emitter.name);

        auto nameInput = prism::make<prism::TextInput>(emitter.name, "Emitter Name");
        nameInput->onCommit = [&editor, selectedEmitterIndex, onAssetMutated](const std::string& v) {
            if (v.empty()) {
                return;
            }
            editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
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
            editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
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
            emitter.space == Asset::ParticleSimSpace::Local ? 1 : 0);
        spaceCombo->onChange = [&editor, selectedEmitterIndex, onAssetMutated](const int idx) {
            editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                if (selectedEmitterIndex < s.emitters.size()) {
                    s.emitters[selectedEmitterIndex].space =
                        (idx == 1) ? Asset::ParticleSimSpace::Local : Asset::ParticleSimSpace::World;
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
            editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                if (selectedEmitterIndex < s.emitters.size()) {
                    s.emitters[selectedEmitterIndex].target =
                        static_cast<Asset::ParticleSimTarget>(std::clamp(idx, 0, 2));
                }
            });
            if (onAssetMutated) {
                onAssetMutated();
            }
        };
        grid->addRow(emCat, "Simulation Target", targetCombo);

        grid->addRow(emCat, "Max Particles",
            MakeTypedNumberField(static_cast<float>(emitter.maxParticles), 100.0F, 0,
                [&editor, selectedEmitterIndex, onAssetMutated](const float v) {
                    const auto clamped = static_cast<std::uint32_t>(
                        std::clamp(std::lround(v), 1L, static_cast<long>(Asset::kMaximumParticlesPerEmitter)));
                    editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                        if (selectedEmitterIndex < s.emitters.size()) {
                            s.emitters[selectedEmitterIndex].maxParticles = clamped;
                        }
                    });
                    if (onAssetMutated) {
                        onAssetMutated();
                    }
                }));

        grid->addRow(emCat, "Seed",
            MakeTypedNumberField(static_cast<float>(emitter.seed), 1.0F, 0,
                [&editor, selectedEmitterIndex, onAssetMutated](const float v) {
                    const auto seedVal = static_cast<std::uint32_t>(std::max(0L, std::lround(v)));
                    editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
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
        MakeTypedNumberField(sys.duration, 0.1F, 2, [&editor, onAssetMutated](const float v) {
            editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                s.duration = std::clamp(v, 0.05F, 3600.0F);
            });
            if (onAssetMutated) {
                onAssetMutated();
            }
        }));

    auto loopCheck = prism::make<prism::CheckBox>("", sys.looping);
    loopCheck->onChange = [&editor, onAssetMutated](const bool v) {
        editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) { s.looping = v; });
        if (onAssetMutated) {
            onAssetMutated();
        }
    };
    grid->addRow(sysCat, "Looping", loopCheck);

    grid->addRow(sysCat, "Warm-Up (s)",
        MakeTypedNumberField(sys.warmUpSeconds, 0.1F, 2, [&editor, onAssetMutated](const float v) {
            editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                s.warmUpSeconds = std::clamp(v, 0.0F, 10.0F);
            });
            if (onAssetMutated) {
                onAssetMutated();
            }
        }));

    grid->addRow(sysCat, "Bounds Min",
        MakeTypedVec3Field(sys.fixedBounds.min, [&editor, onAssetMutated](const Math::Vector3& v) {
            editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
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
        MakeTypedVec3Field(sys.fixedBounds.max, [&editor, onAssetMutated](const Math::Vector3& v) {
            editor.EditVfxSystem([&](Asset::ParticleSystemAsset& s) {
                s.fixedBounds.max.x = std::max(s.fixedBounds.min.x + 0.1F, v.x);
                s.fixedBounds.max.y = std::max(s.fixedBounds.min.y + 0.1F, v.y);
                s.fixedBounds.max.z = std::max(s.fixedBounds.min.z + 0.1F, v.z);
            });
            if (onAssetMutated) {
                onAssetMutated();
            }
        }));

    return prism::make<FitWidth>(grid);
}

} // namespace UVE::Editor
