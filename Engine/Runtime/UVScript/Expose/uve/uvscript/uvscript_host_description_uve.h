// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "uve/uvscript/uvscript_host_uve.h"

namespace UVE::UVScript {

/// A host that only describes: what an object offers, read from text, for compiling a script where no
/// object exists (uvsc at build time). It cannot run a script. One declaration per line, `#` comments:
///
///     property velocity vec3
///     property is_on_floor bool readonly
///     function input.axis(str, str) -> float
///     event tick(float)
///
/// Types are bool, int, float, str, vec3 and none; any other name is an object kind.
class DescribedHostUVE final : public UVScriptHostUVE {
public:
    /// Nothing, with `error` set ("line 3: ..."), when the text is not a valid description.
    [[nodiscard]] static std::optional<DescribedHostUVE> ParseUVE(std::string_view text, std::string& error);

    [[nodiscard]] std::optional<HostPropertyUVE> DescribePropertyUVE(std::string_view name) const override;
    [[nodiscard]] std::optional<HostFunctionUVE> DescribeFunctionUVE(std::string_view name) const override;
    [[nodiscard]] std::optional<std::vector<TypeUVE>> DescribeEventUVE(std::string_view event) const override;

    /// Run-time calls do nothing: a described host never runs a script.
    [[nodiscard]] ValueUVE GetPropertyUVE(std::string_view name) override;
    void SetPropertyUVE(std::string_view name, const ValueUVE& value) override;
    [[nodiscard]] ValueUVE CallFunctionUVE(std::string_view name, std::span<const ValueUVE> args) override;
    void PrintUVE(std::string_view text) override;

private:
    std::map<std::string, HostPropertyUVE, std::less<>> m_properties;
    std::map<std::string, HostFunctionUVE, std::less<>> m_functions;
    std::map<std::string, std::vector<TypeUVE>, std::less<>> m_events;
};

} // namespace UVE::UVScript
