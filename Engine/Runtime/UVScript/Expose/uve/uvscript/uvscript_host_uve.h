// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "uve/uvscript/uvscript_value_uve.h"

namespace UVE::UVScript {

struct HostPropertyUVE final {
    TypeUVE type;
    bool writable = true;
};

struct HostFunctionUVE final {
    std::vector<TypeUVE> params;
    TypeUVE result;
};

/// What a script can reach on the node it drives. The engine implements it per node kind; the
/// compiler asks the Describe* questions, a running script the others.
///
/// A name may be dotted ("input.axis") - `input.axis(...)` in a script is looked up whole.
class UVScriptHostUVE {
public:
    virtual ~UVScriptHostUVE() = default;

    // ---- compile time: what exists, and its type

    [[nodiscard]] virtual std::optional<HostPropertyUVE> DescribePropertyUVE(std::string_view name) const = 0;
    [[nodiscard]] virtual std::optional<HostFunctionUVE> DescribeFunctionUVE(std::string_view name) const = 0;
    /// The parameters of `on <event>`. Nothing when the node kind has no such event.
    [[nodiscard]] virtual std::optional<std::vector<TypeUVE>> DescribeEventUVE(std::string_view event) const = 0;

    // ---- run time. Only called with names and values the compiler accepted.

    [[nodiscard]] virtual ValueUVE GetPropertyUVE(std::string_view name) = 0;
    virtual void SetPropertyUVE(std::string_view name, const ValueUVE& value) = 0;
    [[nodiscard]] virtual ValueUVE CallFunctionUVE(std::string_view name, std::span<const ValueUVE> args) = 0;
    /// Where `print` goes.
    virtual void PrintUVE(std::string_view text) = 0;
};

} // namespace UVE::UVScript
