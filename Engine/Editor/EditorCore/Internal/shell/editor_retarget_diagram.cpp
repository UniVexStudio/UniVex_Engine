// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_retarget_diagram.h"

#include "prism/Canvas.h"
#include "prism/Font.h"
#include "prism/Theme.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace UVE::Editor {
namespace {

constexpr float kLegendHeightUVE = 26.0F;
constexpr float kMarginUVE = 22.0F;

[[nodiscard]] prism::Color ColourUVE(const std::array<float, 3>& c, const float alpha = 1.0F) {
    const auto byte = [](const float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
    return prism::Color(byte(c[0]), byte(c[1]), byte(c[2]), byte(alpha));
}

[[nodiscard]] prism::Color ShadeUVE(const prism::Color c, const float k) {
    const auto f = [k](const std::uint8_t v) { return static_cast<std::uint8_t>(std::lround(std::clamp(static_cast<float>(v) * k, 0.0F, 255.0F))); };
    return prism::Color(f(c.r), f(c.g), f(c.b), c.a);
}

} // namespace

RetargetSkeletonDiagramUVE::RetargetSkeletonDiagramUVE() {
    setTooltip("The source skeleton. Wheel: zoom   Right drag: pan   Double click: fit   Click a joint to find it");
}

void RetargetSkeletonDiagramUVE::SetJointsUVE(std::vector<JointUVE> joints, const bool refit) {
    m_joints = std::move(joints);
    m_minX = m_minY = 1e30F;
    m_maxX = m_maxY = -1e30F;
    for (const JointUVE& joint : m_joints) {
        if (joint.hidden) {
            continue;
        }
        m_minX = std::min(m_minX, joint.x);
        m_maxX = std::max(m_maxX, joint.x);
        m_minY = std::min(m_minY, joint.y);
        m_maxY = std::max(m_maxY, joint.y);
    }
    if (m_minX > m_maxX) {
        m_minX = m_minY = 0.0F;
        m_maxX = m_maxY = 1.0F;
    }
    if (refit) {
        m_zoom = 1.0F;
        m_pan = prism::Vec2{0.0F, 0.0F};
    }
    if (m_selected >= static_cast<int>(m_joints.size())) {
        m_selected = -1;
    }
    m_hover = -1;
    invalidate();
}

void RetargetSkeletonDiagramUVE::SetSelectedUVE(const int joint) {
    if (joint != m_selected) {
        m_selected = joint;
        invalidate();
    }
}

void RetargetSkeletonDiagramUVE::SetEmptyTextUVE(std::string text) {
    m_empty = std::move(text);
    invalidate();
}

void RetargetSkeletonDiagramUVE::SetLegendUVE(std::vector<std::pair<std::array<float, 3>, std::string>> legend) {
    m_legend = std::move(legend);
    invalidate();
}

prism::Vec2 RetargetSkeletonDiagramUVE::measure(const prism::Vec2 available) {
    return constrain(prism::Vec2{std::max(available.x, 160.0F), std::max(available.y, 200.0F)});
}

prism::Rect RetargetSkeletonDiagramUVE::FigureRectUVE() const {
    const prism::Rect r = rect();
    return prism::Rect{r.x + kMarginUVE, r.y + kMarginUVE, std::max(1.0F, r.w - 2.0F * kMarginUVE),
                       std::max(1.0F, r.h - 2.0F * kMarginUVE - kLegendHeightUVE)};
}

float RetargetSkeletonDiagramUVE::FitScaleUVE() const {
    const prism::Rect f = FigureRectUVE();
    const float w = std::max(m_maxX - m_minX, 1e-4F);
    const float h = std::max(m_maxY - m_minY, 1e-4F);
    return std::min(f.w / w, f.h / h);
}

prism::Vec2 RetargetSkeletonDiagramUVE::ToViewUVE(const std::size_t index) const {
    const prism::Rect f = FigureRectUVE();
    const float s = FitScaleUVE() * m_zoom;
    const prism::Vec2 centre = f.center() + m_pan;
    const float cx = (m_minX + m_maxX) * 0.5F;
    const float cy = (m_minY + m_maxY) * 0.5F;
    // Seen from the front (the figure faces +Z): its left, +X, is on the right of the screen.
    return prism::Vec2{centre.x + (m_joints[index].x - cx) * s, centre.y - (m_joints[index].y - cy) * s};
}

int RetargetSkeletonDiagramUVE::DrawnParentUVE(const std::size_t index) const {
    int parent = m_joints[index].parent;
    for (std::size_t guard = 0U; parent >= 0 && guard < m_joints.size(); ++guard) {
        if (!m_joints[static_cast<std::size_t>(parent)].hidden) {
            return parent;
        }
        parent = m_joints[static_cast<std::size_t>(parent)].parent;
    }
    return -1;
}

int RetargetSkeletonDiagramUVE::HitUVE(const prism::Vec2 p) const {
    int best = -1;
    float bestDistance = 9.0F;
    for (std::size_t i = 0U; i < m_joints.size(); ++i) {
        if (m_joints[i].hidden) {
            continue;
        }
        const float d = (ToViewUVE(i) - p).length();
        if (d < bestDistance) {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void RetargetSkeletonDiagramUVE::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    const prism::Rect r = rect();
    c.pushClip(r);
    // A deep, quiet backdrop so the colours of the joints carry the picture.
    c.gradientV(r, prism::Color(30, 33, 40), prism::Color(20, 22, 27));
    const prism::Rect f = FigureRectUVE();
    {
        // Faint guides: the figure's middle and the ground under it.
        const prism::Color guide(255, 255, 255, 14);
        const float s = FitScaleUVE() * m_zoom;
        const float groundY = f.center().y + m_pan.y + ((m_minY + m_maxY) * 0.5F - m_minY) * s;
        c.line(prism::Vec2{r.x + 8.0F, groundY}, prism::Vec2{r.right() - 8.0F, groundY}, guide, 1.0F);
        c.line(prism::Vec2{f.center().x + m_pan.x, r.y + 8.0F}, prism::Vec2{f.center().x + m_pan.x, r.bottom() - kLegendHeightUVE - 4.0F}, guide, 1.0F);
    }
    if (m_joints.empty()) {
        c.textIn(*t.ui, prism::Rect{r.x + 12.0F, r.y + r.h * 0.5F - 10.0F, r.w - 24.0F, 20.0F}, m_empty, t.textDim, prism::HAlign::Center, true);
        c.popClip();
        return;
    }

    std::vector<prism::Vec2> at(m_joints.size());
    for (std::size_t i = 0U; i < m_joints.size(); ++i) {
        at[i] = ToViewUVE(i);
    }
    // How long a bone is on screen decides how wide its blade and how big its joint: the hand's
    // small bones stay fine, the long bones of the body read bold.
    std::vector<float> lengthTo(m_joints.size(), 0.0F);
    for (std::size_t i = 0U; i < m_joints.size(); ++i) {
        if (const int p = DrawnParentUVE(i); p >= 0 && !m_joints[i].hidden) {
            lengthTo[i] = (at[i] - at[static_cast<std::size_t>(p)]).length();
        }
    }

    // ---- The bones: a blade from each joint to the joint that hangs from it ----------------------
    for (std::size_t i = 0U; i < m_joints.size(); ++i) {
        const JointUVE& joint = m_joints[i];
        const int p = DrawnParentUVE(i);
        if (joint.hidden || joint.helper || p < 0 || m_joints[static_cast<std::size_t>(p)].helper) {
            continue;
        }
        const prism::Vec2 a = at[static_cast<std::size_t>(p)];
        const prism::Vec2 b = at[i];
        const prism::Vec2 d = b - a;
        const float length = d.length();
        if (length < 1.5F) {
            continue;
        }
        const prism::Vec2 n{-d.y / length, d.x / length};
        const float half = std::clamp(length * 0.11F, 1.2F, 6.5F);
        const prism::Vec2 waist = a + d * 0.2F;
        const std::array<prism::Vec2, 4> blade{a, waist + n * half, b, waist - n * half};
        const JointUVE& from = m_joints[static_cast<std::size_t>(p)];
        const bool faint = joint.faint || from.faint;
        const prism::Color base = ColourUVE(from.colour, faint ? 0.45F : 1.0F);
        const bool lit = static_cast<int>(i) == m_selected || p == m_selected;
        c.fillPolygon(blade, lit ? ShadeUVE(base, 1.15F) : ShadeUVE(base, 0.62F), true);
        // The upper face catches the light: a brighter half from the waist to the tip.
        const std::array<prism::Vec2, 3> face{waist + n * half, b, waist};
        c.fillPolygon(face, ShadeUVE(base, lit ? 1.3F : 0.82F).alpha(0.9F), true);
        c.polyline(blade, ShadeUVE(base, 0.35F).alpha(0.9F), 1.0F, true);
    }

    // ---- The joints: a dark ring and the joint's colour inside -------------------------------------
    for (std::size_t i = 0U; i < m_joints.size(); ++i) {
        const JointUVE& joint = m_joints[i];
        if (joint.hidden) {
            continue;
        }
        float radius = 4.6F;
        if (lengthTo[i] > 0.0F && !(joint.parent >= 0 && m_joints[static_cast<std::size_t>(joint.parent)].helper)) {
            radius = std::clamp(lengthTo[i] * 0.32F, 2.0F, 5.2F);
        }
        const bool picked = static_cast<int>(i) == m_selected;
        const bool hovered = static_cast<int>(i) == m_hover;
        if (joint.helper) {
            const prism::Color ring = ColourUVE(joint.colour, joint.faint ? 0.5F : 0.9F);
            c.strokeCircle(at[i], 3.2F, ring, 1.4F);
            if (picked || hovered) {
                c.strokeCircle(at[i], 6.0F, picked ? t.accent : prism::Color(255, 255, 255, 200), 1.4F);
            }
            continue;
        }
        const prism::Color colour = ColourUVE(joint.colour, joint.faint ? 0.55F : 1.0F);
        if (picked) {
            c.fillCircle(at[i], radius + 6.0F, t.accent.alpha(0.25F));
        }
        c.fillCircle(at[i], radius + 1.6F, prism::Color(10, 11, 14, 230));
        c.fillCircle(at[i], radius, colour);
        c.fillCircle(at[i] - prism::Vec2{radius * 0.3F, radius * 0.3F}, radius * 0.35F, prism::Color(255, 255, 255, joint.faint ? 30 : 70));
        if (picked || hovered) {
            c.strokeCircle(at[i], radius + 2.6F, picked ? t.accent : prism::Color(255, 255, 255, 200), 1.6F);
        }
    }

    // ---- The joint under the pointer and the picked one, named -------------------------------------
    for (const int which : {m_selected, m_hover}) {
        if (which < 0 || static_cast<std::size_t>(which) >= m_joints.size() || m_joints[static_cast<std::size_t>(which)].hidden) {
            continue;
        }
        if (which == m_selected && m_hover >= 0 && m_hover != m_selected) {
            continue;   // the hovered one is named; the picked one keeps its ring
        }
        const JointUVE& joint = m_joints[static_cast<std::size_t>(which)];
        const float w = std::max(t.semibold->width(joint.name), joint.detail.empty() ? 0.0F : t.small->width(joint.detail)) + 18.0F;
        const float h = joint.detail.empty() ? 22.0F : 38.0F;
        const prism::Vec2 p = at[static_cast<std::size_t>(which)];
        prism::Rect box{p.x + 12.0F, p.y - h - 6.0F, w, h};
        box.x = std::clamp(box.x, r.x + 4.0F, std::max(r.x + 4.0F, r.right() - w - 4.0F));
        box.y = std::clamp(box.y, r.y + 4.0F, std::max(r.y + 4.0F, r.bottom() - h - 4.0F));
        c.shadow(box, 6.0F, 6, prism::Color(0, 0, 0, 120));
        c.fillRoundRect(box, 5.0F, prism::Color(14, 15, 19, 240));
        c.strokeRoundRect(box, 5.0F, ColourUVE(joint.colour, 0.9F), 1);
        c.textIn(*t.semibold, prism::Rect{box.x + 9.0F, box.y + 3.0F, w - 14.0F, 17.0F}, joint.name, t.textBright, prism::HAlign::Left, true);
        if (!joint.detail.empty()) {
            c.textIn(*t.small, prism::Rect{box.x + 9.0F, box.y + 19.0F, w - 14.0F, 15.0F}, joint.detail, t.textDim, prism::HAlign::Left, true);
        }
    }

    // ---- The key ------------------------------------------------------------------------------------
    {
        const prism::Rect key{r.x, r.bottom() - kLegendHeightUVE, r.w, kLegendHeightUVE};
        c.fillRect(key, prism::Color(0, 0, 0, 70));
        c.hline(key.x, key.right(), key.y, prism::Color(255, 255, 255, 18));
        float x = key.x + 10.0F;
        for (const auto& [colour, label] : m_legend) {
            const float w = t.small->width(label);
            if (x + 14.0F + w > key.right() - 6.0F) {
                break;
            }
            c.fillCircle(prism::Vec2{x + 4.0F, key.y + key.h * 0.5F}, 4.0F, ColourUVE(colour));
            c.textIn(*t.small, prism::Rect{x + 12.0F, key.y, w + 4.0F, key.h}, label, t.textDim, prism::HAlign::Left, false);
            x += 12.0F + w + 14.0F;
        }
    }
    c.popClip();
}

bool RetargetSkeletonDiagramUVE::onMouseDown(const prism::MouseEvent& e) {
    if (e.button == prism::MouseButton::Right || e.button == prism::MouseButton::Middle) {
        m_panning = true;
        m_panLast = e.pos;
        return true;
    }
    if (e.button != prism::MouseButton::Left) {
        return false;
    }
    if (e.clicks >= 2 && HitUVE(e.pos) < 0) {
        m_zoom = 1.0F;
        m_pan = prism::Vec2{0.0F, 0.0F};
        invalidate();
        return true;
    }
    const int hit = HitUVE(e.pos);
    m_selected = hit;
    if (onPicked) {
        onPicked(hit);
    }
    invalidate();
    return true;
}

bool RetargetSkeletonDiagramUVE::onMouseMove(const prism::MouseEvent& e) {
    if (m_panning) {
        m_pan += e.pos - m_panLast;
        m_panLast = e.pos;
        invalidate();
        return true;
    }
    const int hover = HitUVE(e.pos);
    if (hover != m_hover) {
        m_hover = hover;
        invalidate();
    }
    return false;
}

bool RetargetSkeletonDiagramUVE::onMouseUp(const prism::MouseEvent& e) {
    if (m_panning && (e.button == prism::MouseButton::Right || e.button == prism::MouseButton::Middle)) {
        m_panning = false;
        return true;
    }
    return false;
}

bool RetargetSkeletonDiagramUVE::onMouseWheel(const prism::MouseEvent& e) {
    // Zoom about the pointer: the point under it stays under it.
    const float before = m_zoom;
    m_zoom = std::clamp(m_zoom * (e.wheel.y > 0.0F ? 1.18F : 1.0F / 1.18F), 0.5F, 12.0F);
    const prism::Vec2 centre = FigureRectUVE().center() + m_pan;
    const prism::Vec2 offset = e.pos - centre;
    m_pan += offset - offset * (m_zoom / before);
    invalidate();
    return true;
}

void RetargetSkeletonDiagramUVE::onMouseLeave() {
    if (m_hover >= 0) {
        m_hover = -1;
        invalidate();
    }
}

prism::Cursor RetargetSkeletonDiagramUVE::cursor() const {
    return m_panning ? prism::Cursor::SizeAll : (m_hover >= 0 ? prism::Cursor::Hand : prism::Cursor::Arrow);
}

} // namespace UVE::Editor
