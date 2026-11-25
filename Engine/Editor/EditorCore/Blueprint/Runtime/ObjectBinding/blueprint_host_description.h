// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "blueprint_host.h"

namespace UVE::Blueprint {

/// A host that only describes: what an object offers, read from text, for compiling a script where no
/// object exists (uvsc at build time). It cannot run a script. One declaration per line, `#` comments:
///
///     property velocity vec3
///     property grounded bool readonly
///     function input.axis(str, str) -> float
///     event tick(float)
///
/// Types are bool, int, float, str, vec3 and none; any other name is an object kind.
class DescribedHost final : public UVScriptHost {
public:
    /// Nothing, with `error` set ("line 3: ..."), when the text is not a valid description.
    [[nodiscard]] static std::optional<DescribedHost> Parse(std::string_view text, std::string& error);

    [[nodiscard]] std::optional<HostProperty> DescribeProperty(std::string_view name) const override;
    [[nodiscard]] std::optional<HostFunction> DescribeFunction(std::string_view name) const override;
    [[nodiscard]] std::optional<std::vector<Type>> DescribeEvent(std::string_view event) const override;

    /// Run-time calls do nothing: a described host never runs a script.
    [[nodiscard]] Value GetProperty(std::string_view name) override;
    void SetProperty(std::string_view name, const Value& value) override;
    [[nodiscard]] Value CallFunction(std::string_view name, std::span<const Value> args) override;
    void CallMethod(ObjectRef target, std::string_view method, std::span<const Value> args) override;
    void Print(std::string_view text) override;

private:
    std::map<std::string, HostProperty, std::less<>> m_properties;
    std::map<std::string, HostFunction, std::less<>> m_functions;
    std::map<std::string, std::vector<Type>, std::less<>> m_events;
};

} // namespace UVE::Blueprint
