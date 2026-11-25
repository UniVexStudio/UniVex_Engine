// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_tween.h"

#include <algorithm>
#include <cmath>

#include "ui_button_component.h"
#include "ui_image_component.h"
#include "ui_layout_container_component.h"
#include "ui_progress_bar_component.h"
#include "ui_slider_component.h"
#include "ui_text_component.h"
#include "i_entity_manager.h"
#include "easing.h"
#include "rect.h"
#include "scalar.h"

namespace UVE::UI {

float EaseUITween(const Scene::UITweenEase ease, const float t) noexcept {
    // The tween's own ease enum stays the serialized vocabulary (append-only, stable on disk);
    // the formulas live in Math::Ease now, so this maps and delegates instead of owning them.
    const float clamped = std::clamp(t, 0.0F, 1.0F);
    Math::EaseKind kind = Math::EaseKind::Linear;
    switch (ease) {
        case Scene::UITweenEase::Linear:
            kind = Math::EaseKind::Linear;
            break;
        case Scene::UITweenEase::SineInOut:
            kind = Math::EaseKind::SineInOut;
            break;
        case Scene::UITweenEase::QuadIn:
            kind = Math::EaseKind::QuadIn;
            break;
        case Scene::UITweenEase::QuadOut:
            kind = Math::EaseKind::QuadOut;
            break;
        case Scene::UITweenEase::QuadInOut:
            kind = Math::EaseKind::QuadInOut;
            break;
        case Scene::UITweenEase::CubicIn:
            kind = Math::EaseKind::CubicIn;
            break;
        case Scene::UITweenEase::CubicOut:
            kind = Math::EaseKind::CubicOut;
            break;
        case Scene::UITweenEase::CubicInOut:
            kind = Math::EaseKind::CubicInOut;
            break;
        case Scene::UITweenEase::OutBack:
            kind = Math::EaseKind::BackOut;
            break;
    }
    return Math::Ease(kind, clamped);
}

namespace {

void ApplyTweenedRect(Scene::IEntityManager& entityManager, const Scene::Entity entity,
                         const Math::Rect& rect) {
    // Every positioned component moves together, mirroring the layout pass's own rule: a tweened
    // entity stays in one piece.
    if (entityManager.HasComponent<Scene::UIButtonComponent>(entity)) {
        entityManager.GetComponent<Scene::UIButtonComponent>(entity).rect = rect;
    }
    if (entityManager.HasComponent<Scene::UIImageComponent>(entity)) {
        entityManager.GetComponent<Scene::UIImageComponent>(entity).rect = rect;
    }
    if (entityManager.HasComponent<Scene::UISliderComponent>(entity)) {
        entityManager.GetComponent<Scene::UISliderComponent>(entity).rect = rect;
    }
    if (entityManager.HasComponent<Scene::UIProgressBarComponent>(entity)) {
        entityManager.GetComponent<Scene::UIProgressBarComponent>(entity).rect = rect;
    }
    if (entityManager.HasComponent<Scene::UILayoutContainerComponent>(entity)) {
        entityManager.GetComponent<Scene::UILayoutContainerComponent>(entity).rect = rect;
    }
    if (entityManager.HasComponent<Scene::UITextComponent>(entity)) {
        entityManager.GetComponent<Scene::UITextComponent>(entity).positionPixels = rect.position;
    }
}

} // namespace

void TickUITweens(Scene::IEntityManager& entityManager, const float dt) {
    const bool advance = dt > 0.0F && std::isfinite(dt);
    entityManager.ForEach<Scene::UITweenComponent>(
        [&entityManager, dt, advance](const Scene::Entity entity, Scene::UITweenComponent& tween) {
            if (!IsUITweenComponentValid(tween)) {
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
            if (tween.loop == Scene::UITweenLoop::Once) {
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
                    fraction = tween.loop == Scene::UITweenLoop::Loop
                                   ? cycleFraction
                                   : (static_cast<int>(cycle) % 2 == 0 ? cycleFraction : 1.0F - cycleFraction);
                    // A wrap is a completion: cycle 1+ reached past where the previous tick was.
                    // Delay phases (negative previousT) never count - ending a delay starts the
                    // tween, it doesn't complete anything.
                    tween.completedThisFrame = cycle >= 1.0F && cycle > std::floor(previousT);
                }
            }
            const float eased = EaseUITween(tween.ease, fraction);
            if (tween.target == Scene::UITweenTarget::Rect) {
                const Math::Rect easedRect{
                    Math::Vector2{Math::Lerp(tween.fromRect.position.x, tween.toRect.position.x, eased),
                                     Math::Lerp(tween.fromRect.position.y, tween.toRect.position.y, eased)},
                    Math::Vector2{Math::Lerp(tween.fromRect.size.x, tween.toRect.size.x, eased),
                                     Math::Lerp(tween.fromRect.size.y, tween.toRect.size.y, eased)}};
                ApplyTweenedRect(entityManager, entity, easedRect);
            } else {
                tween.currentAlpha =
                    std::clamp(Math::Lerp(tween.fromAlpha, tween.toAlpha, eased), 0.0F, 1.0F);
            }
        });
}

float TweenedAlpha(Scene::IEntityManager& entityManager, const Scene::Entity entity,
                      const float baseAlpha) {
    if (!entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<Scene::UITweenComponent>(entity)) {
        return baseAlpha;
    }
    const Scene::UITweenComponent& tween =
        entityManager.GetComponent<Scene::UITweenComponent>(entity);
    if (tween.target != Scene::UITweenTarget::Alpha || !IsUITweenComponentValid(tween)) {
        return baseAlpha;
    }
    return baseAlpha * tween.currentAlpha;
}

} // namespace UVE::UI
