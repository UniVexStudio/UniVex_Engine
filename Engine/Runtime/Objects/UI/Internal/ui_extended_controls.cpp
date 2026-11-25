// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ui_extended_controls.h"

#include <algorithm>
#include <cmath>

#include "disabled_component.h"
#include "i_entity_manager.h"

namespace UVE::Scene {

void ApplyUIExtendedButtonObjectDefinition(IEntityManager& entityManager,
                                              const Entity entity,
                                              const UIExtendedButtonObjectDefinition& value) {
    entityManager.AddComponent<UIExtendedButtonComponent>(entity, value.button);
}

void ApplyUIDisplayWidgetObjectDefinition(IEntityManager& entityManager,
                                             const Entity entity,
                                             const UIDisplayWidgetObjectDefinition& value) {
    entityManager.AddComponent<UIDisplayWidgetComponent>(entity, value.display);
}

void ApplyUIRangeControlObjectDefinition(IEntityManager& entityManager,
                                            const Entity entity,
                                            const UIRangeControlObjectDefinition& value) {
    UIRangeControlComponent out = value.range;
    if (out.maxValue < out.minValue) {
        out.maxValue = out.minValue;
    }
    out.value = std::clamp(out.value, out.minValue, out.maxValue);
    entityManager.AddComponent<UIRangeControlComponent>(entity, out);
}

void ApplyUISelectionWidgetObjectDefinition(IEntityManager& entityManager,
                                               const Entity entity,
                                               const UISelectionWidgetObjectDefinition& value) {
    entityManager.AddComponent<UISelectionWidgetComponent>(entity, value.selection);
}

void ApplyUIDialogWindowObjectDefinition(IEntityManager& entityManager,
                                            const Entity entity,
                                            const UIDialogWindowObjectDefinition& value) {
    entityManager.AddComponent<UIDialogWindowComponent>(entity, value.dialog);
}

void ApplyUISpecialInputObjectDefinition(IEntityManager& entityManager,
                                            const Entity entity,
                                            const UISpecialInputObjectDefinition& value) {
    entityManager.AddComponent<UISpecialInputComponent>(entity, value.input);
}

std::size_t UpdateUIExtendedObjects(IEntityManager& entityManager,
                                       const float deltaTimeSeconds) {
    std::size_t updatedCount = 0U;
    const float dt =
        (std::isfinite(deltaTimeSeconds) && deltaTimeSeconds > 0.0F) ? deltaTimeSeconds : 0.0F;

    entityManager.ForEach<UIRangeControlComponent>(
        [&](const Entity entity, UIRangeControlComponent& r) {
            if (entityManager.HasComponent<DisabledComponent>(entity)) {
                return;
            }
            if (r.maxValue < r.minValue) {
                r.maxValue = r.minValue;
            }
            if (std::isfinite(r.stepValue) && r.stepValue > 0.0F) {
                const float steps = std::round((r.value - r.minValue) / r.stepValue);
                r.value = r.minValue + steps * r.stepValue;
            }
            r.value = std::clamp(r.value, r.minValue, r.maxValue);
            ++updatedCount;
        });

    entityManager.ForEach<UIDisplayWidgetComponent>(
        [&](const Entity entity, UIDisplayWidgetComponent& d) {
            if (entityManager.HasComponent<DisabledComponent>(entity)) {
                return;
            }
            if (d.kind == UIDisplayWidgetKind::VideoStreamPlayer && d.playing && dt > 0.0F) {
                d.playbackPositionSeconds += dt;
                ++updatedCount;
            }
        });

    entityManager.ForEach<UISpecialInputComponent>(
        [&](const Entity entity, UISpecialInputComponent& inp) {
            if (entityManager.HasComponent<DisabledComponent>(entity)) {
                return;
            }
            if (inp.kind == UISpecialInputKind::VirtualJoystick) {
                const float len = std::sqrt(inp.joystickAxis.x * inp.joystickAxis.x +
                                            inp.joystickAxis.y * inp.joystickAxis.y);
                if (!std::isfinite(len) || len < std::max(0.0F, inp.deadzone)) {
                    inp.joystickAxis = Math::Vector2{0.0F, 0.0F};
                } else if (len > 1.0F) {
                    inp.joystickAxis = inp.joystickAxis * (1.0F / len);
                }
                ++updatedCount;
            }
        });

    return updatedCount;
}

} // namespace UVE::Scene
