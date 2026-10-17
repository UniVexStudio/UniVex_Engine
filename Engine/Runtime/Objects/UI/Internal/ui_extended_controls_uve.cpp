// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/canvas/ui_extended_controls_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/disabled_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

void ApplyUIExtendedButtonObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                              const EntityUVE entity,
                                              const UIExtendedButtonObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<UIExtendedButtonComponentUVE>(entity, value.button);
}

void ApplyUIDisplayWidgetObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                             const EntityUVE entity,
                                             const UIDisplayWidgetObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<UIDisplayWidgetComponentUVE>(entity, value.display);
}

void ApplyUIRangeControlObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                            const EntityUVE entity,
                                            const UIRangeControlObjectDefinitionUVE& value) {
    UIRangeControlComponentUVE out = value.range;
    if (out.maxValue < out.minValue) {
        out.maxValue = out.minValue;
    }
    out.value = std::clamp(out.value, out.minValue, out.maxValue);
    entityManager.AddComponentUVE<UIRangeControlComponentUVE>(entity, out);
}

void ApplyUISelectionWidgetObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                               const EntityUVE entity,
                                               const UISelectionWidgetObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<UISelectionWidgetComponentUVE>(entity, value.selection);
}

void ApplyUIDialogWindowObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                            const EntityUVE entity,
                                            const UIDialogWindowObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<UIDialogWindowComponentUVE>(entity, value.dialog);
}

void ApplyUISpecialInputObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                            const EntityUVE entity,
                                            const UISpecialInputObjectDefinitionUVE& value) {
    entityManager.AddComponentUVE<UISpecialInputComponentUVE>(entity, value.input);
}

std::size_t UpdateUIExtendedObjectsUVE(IEntityManagerUVE& entityManager,
                                       const float deltaTimeSeconds) {
    std::size_t updatedCount = 0U;
    const float dt =
        (std::isfinite(deltaTimeSeconds) && deltaTimeSeconds > 0.0F) ? deltaTimeSeconds : 0.0F;

    entityManager.ForEachUVE<UIRangeControlComponentUVE>(
        [&](const EntityUVE entity, UIRangeControlComponentUVE& r) {
            if (entityManager.HasComponentUVE<DisabledComponentUVE>(entity)) {
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

    entityManager.ForEachUVE<UIDisplayWidgetComponentUVE>(
        [&](const EntityUVE entity, UIDisplayWidgetComponentUVE& d) {
            if (entityManager.HasComponentUVE<DisabledComponentUVE>(entity)) {
                return;
            }
            if (d.kind == UIDisplayWidgetKindUVE::VideoStreamPlayer && d.playing && dt > 0.0F) {
                d.playbackPositionSeconds += dt;
                ++updatedCount;
            }
        });

    entityManager.ForEachUVE<UISpecialInputComponentUVE>(
        [&](const EntityUVE entity, UISpecialInputComponentUVE& inp) {
            if (entityManager.HasComponentUVE<DisabledComponentUVE>(entity)) {
                return;
            }
            if (inp.kind == UISpecialInputKindUVE::VirtualJoystick) {
                const float len = std::sqrt(inp.joystickAxis.x * inp.joystickAxis.x +
                                            inp.joystickAxis.y * inp.joystickAxis.y);
                if (!std::isfinite(len) || len < std::max(0.0F, inp.deadzone)) {
                    inp.joystickAxis = Math::Vector2UVE{0.0F, 0.0F};
                } else if (len > 1.0F) {
                    inp.joystickAxis = inp.joystickAxis * (1.0F / len);
                }
                ++updatedCount;
            }
        });

    return updatedCount;
}

} // namespace UVE::Scene
