// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/core/uvscript_object_host_uve.h"

#include <string>

#include "uve/component/character_controller_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/input/i_input_system_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Core {
namespace {

using UVScript::HostFunctionUVE;
using UVScript::HostPropertyUVE;
using UVScript::TypeUVE;
using UVScript::ValueUVE;
using UVScript::Vec3ValueUVE;

[[nodiscard]] Vec3ValueUVE ToScriptUVE(const Math::Vector3UVE& v) noexcept {
    return {static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z)};
}

[[nodiscard]] Math::Vector3UVE ToEngineUVE(const ValueUVE& value) {
    const Vec3ValueUVE& v = std::get<Vec3ValueUVE>(value);
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

} // namespace

UVScriptObjectHostUVE::UVScriptObjectHostUVE(Scene::IEntityManagerUVE& entityManager, const Input::IInputSystemUVE* const input,
                                         const Scene::EntityUVE entity) noexcept
    : m_entityManager(entityManager), m_input(input), m_entity(entity) {}

std::optional<HostPropertyUVE> UVScriptObjectHostUVE::DescribePropertyUVE(const std::string_view name) const {
    if (name == "name") {
        return HostPropertyUVE{TypeUVE::StrUVE(), false};
    }
    if ((name == "position" || name == "scale") && m_entityManager.HasComponentUVE<Scene::TransformComponentUVE>(m_entity)) {
        return HostPropertyUVE{TypeUVE::Vec3UVE(), true};
    }
    if (m_entityManager.HasComponentUVE<Scene::CharacterControllerComponentUVE>(m_entity)) {
        if (name == "velocity") {
            return HostPropertyUVE{TypeUVE::Vec3UVE(), true};
        }
        if (name == "is_on_floor") {
            return HostPropertyUVE{TypeUVE::BoolUVE(), false};
        }
    }
    return std::nullopt;
}

std::optional<HostFunctionUVE> UVScriptObjectHostUVE::DescribeFunctionUVE(const std::string_view name) const {
    if (name == "input.pressed" || name == "input.held" || name == "input.released") {
        return HostFunctionUVE{{TypeUVE::StrUVE()}, TypeUVE::BoolUVE()};
    }
    if (name == "input.axis") {
        return HostFunctionUVE{{TypeUVE::StrUVE(), TypeUVE::StrUVE()}, TypeUVE::FloatUVE()};
    }
    return std::nullopt;
}

std::optional<std::vector<TypeUVE>> UVScriptObjectHostUVE::DescribeEventUVE(const std::string_view event) const {
    if (event == "ready") {
        return std::vector<TypeUVE>{};
    }
    if (event == "tick") {
        return std::vector<TypeUVE>{TypeUVE::FloatUVE()};
    }
    // An AnimationSequencer's clip passed one of its events: sent to the player's own script and to
    // the script of the object it animates (a character's script hears its footsteps).
    if (event == "animation_event") {
        return std::vector<TypeUVE>{TypeUVE::StrUVE()};
    }
    return std::nullopt;
}

ValueUVE UVScriptObjectHostUVE::GetPropertyUVE(const std::string_view name) {
    if (name == "name") {
        return m_entityManager.HasComponentUVE<Scene::NameComponentUVE>(m_entity)
                   ? ValueUVE{m_entityManager.GetComponentUVE<Scene::NameComponentUVE>(m_entity).name}
                   : ValueUVE{std::string{}};
    }
    if (name == "position" || name == "scale") {
        const Scene::TransformComponentUVE& transform = m_entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_entity);
        return ToScriptUVE(name == "position" ? transform.localPosition : transform.localScale);
    }
    const Scene::CharacterControllerComponentUVE& body =
        m_entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(m_entity);
    if (name == "velocity") {
        return ToScriptUVE(body.velocity);
    }
    return body.isOnFloor;
}

void UVScriptObjectHostUVE::SetPropertyUVE(const std::string_view name, const ValueUVE& value) {
    if (name == "position" || name == "scale") {
        Scene::TransformComponentUVE& transform = m_entityManager.GetComponentUVE<Scene::TransformComponentUVE>(m_entity);
        (name == "position" ? transform.localPosition : transform.localScale) = ToEngineUVE(value);
        // Marked stale so the scene graph recomputes the world transform. Not through
        // SetLocalTransformUVE: that ignores a object that has no world transform yet.
        if (m_entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(m_entity)) {
            m_entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(m_entity).dirty = true;
        }
    } else if (name == "velocity") {
        m_entityManager.GetComponentUVE<Scene::CharacterControllerComponentUVE>(m_entity).velocity = ToEngineUVE(value);
    }
}

ValueUVE UVScriptObjectHostUVE::CallFunctionUVE(const std::string_view name, const std::span<const ValueUVE> args) {
    if (m_input == nullptr) {
        return name == "input.axis" ? ValueUVE{0.0} : ValueUVE{false};
    }
    if (name == "input.axis") {
        const bool negative = m_input->IsActionHeldUVE(std::get<std::string>(args[0]));
        const bool positive = m_input->IsActionHeldUVE(std::get<std::string>(args[1]));
        return (positive ? 1.0 : 0.0) - (negative ? 1.0 : 0.0);
    }
    const std::string& action = std::get<std::string>(args[0]);
    if (name == "input.pressed") {
        return m_input->IsActionTriggeredUVE(action);
    }
    if (name == "input.released") {
        return m_input->IsActionReleasedUVE(action);
    }
    return m_input->IsActionHeldUVE(action);
}

void UVScriptObjectHostUVE::PrintUVE(const std::string_view text) {
    const std::string object = m_entityManager.HasComponentUVE<Scene::NameComponentUVE>(m_entity)
                                 ? m_entityManager.GetComponentUVE<Scene::NameComponentUVE>(m_entity).name
                                 : std::string{"object"};
    UVE_INFO("[{}] {}", object, text);
}

} // namespace UVE::Core
