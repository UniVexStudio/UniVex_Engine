// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/entity_uve.h"
#include "uve/uvscript/uvscript_host_uve.h"

namespace UVE::Input {
class IInputSystemUVE;
} // namespace UVE::Input

namespace UVE::Scene {
class IEntityManagerUVE;
} // namespace UVE::Scene

namespace UVE::Core {

/// What a `.uvs` script can reach on the node it is attached to. The node's components decide:
/// - every node: `name` (read-only);
/// - a node with a transform: `position`, `scale` (local, metres);
/// - a character body: `velocity`, `is_on_floor` (read-only);
/// - always: `input.pressed/held/released(action)` and `input.axis(negative, positive)`, which
///   read the project's input map; events `ready` and `tick(dt)`.
class UVScriptNodeHostUVE final : public UVScript::UVScriptHostUVE {
public:
    /// `input` may be null (no input system): input calls then read as not pressed.
    UVScriptNodeHostUVE(Scene::IEntityManagerUVE& entityManager, const Input::IInputSystemUVE* input, Scene::EntityUVE entity) noexcept;

    [[nodiscard]] std::optional<UVScript::HostPropertyUVE> DescribePropertyUVE(std::string_view name) const override;
    [[nodiscard]] std::optional<UVScript::HostFunctionUVE> DescribeFunctionUVE(std::string_view name) const override;
    [[nodiscard]] std::optional<std::vector<UVScript::TypeUVE>> DescribeEventUVE(std::string_view event) const override;

    [[nodiscard]] UVScript::ValueUVE GetPropertyUVE(std::string_view name) override;
    void SetPropertyUVE(std::string_view name, const UVScript::ValueUVE& value) override;
    [[nodiscard]] UVScript::ValueUVE CallFunctionUVE(std::string_view name,
                                                      std::span<const UVScript::ValueUVE> args) override;
    void PrintUVE(std::string_view text) override;

private:
    Scene::IEntityManagerUVE& m_entityManager;
    const Input::IInputSystemUVE* m_input = nullptr;
    Scene::EntityUVE m_entity = Scene::kInvalidEntityUVE;
};

} // namespace UVE::Core
