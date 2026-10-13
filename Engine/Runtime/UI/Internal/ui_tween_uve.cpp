// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/ui_tween_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/ui_button_component_uve.h"
#include "uve/component/ui_image_component_uve.h"
#include "uve/component/ui_layout_container_component_uve.h"
#include "uve/component/ui_progress_bar_component_uve.h"
#include "uve/component/ui_slider_component_uve.h"
#include "uve/component/ui_text_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/easing_uve.h"
#include "uve/math/rect_uve.h"
#include "uve/math/scalar_uve.h"

namespace UVE::UI {

float EaseUITweenUVE(const Scene::UITweenEaseUVE ease, const float t) noexcept {
    // The tween's own ease enum stays the serialized vocabulary (append-only, stable on disk);
    // the formulas live in Math::EaseUVE now, so this maps and delegates instead of owning them.
    const float clamped = std::clamp(t, 0.0F, 1.0F);
    Math::EaseKindUVE kind = Math::EaseKindUVE::Linear;
    switch (ease) {
        case Scene::UITweenEaseUVE::Linear:
            kind = Math::EaseKindUVE::Linear;
            break;
        case Scene::UITweenEaseUVE::SineInOut:
            kind = Math::EaseKindUVE::SineInOut;
            break;
        case Scene::UITweenEaseUVE::QuadIn:
            kind = Math::EaseKindUVE::QuadIn;
            break;
        case Scene::UITweenEaseUVE::QuadOut:
            kind = Math::EaseKindUVE::QuadOut;
            break;
        case Scene::UITweenEaseUVE::QuadInOut:
            kind = Math::EaseKindUVE::QuadInOut;
            break;
        case Scene::UITweenEaseUVE::CubicIn:
            kind = Math::EaseKindUVE::CubicIn;
            break;
        case Scene::UITweenEaseUVE::CubicOut:
            kind = Math::EaseKindUVE::CubicOut;
            break;
        case Scene::UITweenEaseUVE::CubicInOut:
            kind = Math::EaseKindUVE::CubicInOut;
            break;
        case Scene::UITweenEaseUVE::OutBack:
            kind = Math::EaseKindUVE::BackOut;
            break;
    }
    return Math::EaseUVE(kind, clamped);
}

namespace {

void ApplyTweenedRectUVE(Scene::IEntityManagerUVE& entityManager, const Scene::EntityUVE entity,
                         const Math::RectUVE& rect) {
    // Every positioned component moves together, mirroring the layout pass's own rule: a tweened
    // entity stays in one piece.
    if (entityManager.HasComponentUVE<Scene::UIButtonComponentUVE>(entity)) {
        entityManager.GetComponentUVE<Scene::UIButtonComponentUVE>(entity).rect = rect;
    }
    if (entityManager.HasComponentUVE<Scene::UIImageComponentUVE>(entity)) {
        entityManager.GetComponentUVE<Scene::UIImageComponentUVE>(entity).rect = rect;
    }
    if (entityManager.HasComponentUVE<Scene::UISliderComponentUVE>(entity)) {
        entityManager.GetComponentUVE<Scene::UISliderComponentUVE>(entity).rect = rect;
    }
    if (entityManager.HasComponentUVE<Scene::UIProgressBarComponentUVE>(entity)) {
        entityManager.GetComponentUVE<Scene::UIProgressBarComponentUVE>(entity).rect = rect;
    }
    if (entityManager.HasComponentUVE<Scene::UILayoutContainerComponentUVE>(entity)) {
        entityManager.GetComponentUVE<Scene::UILayoutContainerComponentUVE>(entity).rect = rect;
    }
    if (entityManager.HasComponentUVE<Scene::UITextComponentUVE>(entity)) {
        entityManager.GetComponentUVE<Scene::UITextComponentUVE>(entity).positionPixels = rect.position;
    }
}

} // namespace

void TickUITweensUVE(Scene::IEntityManagerUVE& entityManager, const float dt) {
    const bool advance = dt > 0.0F && std::isfinite(dt);
    entityManager.ForEachUVE<Scene::UITweenComponentUVE>(
        [&entityManager, dt, advance](const Scene::EntityUVE entity, Scene::UITweenComponentUVE& tween) {
            if (!IsUITweenComponentValidUVE(tween)) {
                tween.elapsed = 0.0F;
                tween.playing = false;
                tween.completedThisFrame = false;
                tween.currentAlpha = 1.0F;
                return;
            }
            tween.completedThisFrame = false;
            if (!tween.playing || !advance) {
                return;
            }
            const float previousT = (tween.elapsed - tween.delay) / tween.duration;
            tween.elapsed += dt;
            const float t = (tween.elapsed - tween.delay) / tween.duration;
            float fraction = 0.0F;
            if (tween.loop == Scene::UITweenLoopUVE::Once) {
                if (t >= 1.0F) {
                    fraction = 1.0F;
                    tween.playing = false;
                    tween.completedThisFrame = previousT < 1.0F;
                } else {
                    fraction = std::max(0.0F, t);
                }
            } else {
                if (t >= 0.0F) {
                    const float cycle = std::floor(t);
                    const float cycleFraction = t - cycle;
                    fraction = tween.loop == Scene::UITweenLoopUVE::Loop
                                   ? cycleFraction
                                   : (static_cast<int>(cycle) % 2 == 0 ? cycleFraction : 1.0F - cycleFraction);
                    // A wrap is a completion: cycle 1+ reached past where the previous tick was.
                    // Delay phases (negative previousT) never count - ending a delay starts the
                    // tween, it doesn't complete anything.
                    tween.completedThisFrame = cycle >= 1.0F && cycle > std::floor(previousT);
                }
            }
            const float eased = EaseUITweenUVE(tween.ease, fraction);
            if (tween.target == Scene::UITweenTargetUVE::Rect) {
                const Math::RectUVE easedRect{
                    Math::Vector2UVE{Math::LerpUVE(tween.fromRect.position.x, tween.toRect.position.x, eased),
                                     Math::LerpUVE(tween.fromRect.position.y, tween.toRect.position.y, eased)},
                    Math::Vector2UVE{Math::LerpUVE(tween.fromRect.size.x, tween.toRect.size.x, eased),
                                     Math::LerpUVE(tween.fromRect.size.y, tween.toRect.size.y, eased)}};
                ApplyTweenedRectUVE(entityManager, entity, easedRect);
            } else {
                tween.currentAlpha =
                    std::clamp(Math::LerpUVE(tween.fromAlpha, tween.toAlpha, eased), 0.0F, 1.0F);
            }
        });
}

float TweenedAlphaUVE(Scene::IEntityManagerUVE& entityManager, const Scene::EntityUVE entity,
                      const float baseAlpha) {
    if (!entityManager.IsAliveUVE(entity) ||
        !entityManager.HasComponentUVE<Scene::UITweenComponentUVE>(entity)) {
        return baseAlpha;
    }
    const Scene::UITweenComponentUVE& tween =
        entityManager.GetComponentUVE<Scene::UITweenComponentUVE>(entity);
    if (tween.target != Scene::UITweenTargetUVE::Alpha || !IsUITweenComponentValidUVE(tween)) {
        return baseAlpha;
    }
    return baseAlpha * tween.currentAlpha;
}

} // namespace UVE::UI
