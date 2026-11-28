// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "blueprint_value.h"

namespace UVE::Blueprint {

struct HostProperty final {
    Type type;
    bool writable = true;
};

struct HostFunction final {
    std::vector<Type> params;
    Type result;
};

/// What a script can reach on the object it drives. The engine implements it per object kind; the
/// compiler asks the Describe* questions, a running script the others.
///
/// A name may be dotted ("input.axis") - `input.axis(...)` in a script is looked up whole.
class BlueprintHost {
public:
    virtual ~BlueprintHost() = default;

    // ---- compile time: what exists, and its type

    [[nodiscard]] virtual std::optional<HostProperty> DescribeProperty(std::string_view name) const = 0;
    [[nodiscard]] virtual std::optional<HostFunction> DescribeFunction(std::string_view name) const = 0;
    /// The parameters of `on <event>`. Nothing when the object kind has no such event.
    [[nodiscard]] virtual std::optional<std::vector<Type>> DescribeEvent(std::string_view event) const = 0;

    // ---- run time. Only called with names and values the compiler accepted.

    [[nodiscard]] virtual Value GetProperty(std::string_view name) = 0;
    virtual void SetProperty(std::string_view name, const Value& value) = 0;
    [[nodiscard]] virtual Value CallFunction(std::string_view name, std::span<const Value> args) = 0;
    /// Calls `method(args)` on another object (a `ref` this host handed out). Fire-and-forget:
    /// the caller gets none either way, and a missing target or method fails closed as nothing.
    virtual void CallMethod(ObjectRef target, std::string_view method, std::span<const Value> args) = 0;
    /// Where `print` goes.
    virtual void Print(std::string_view text) = 0;
};

} // namespace UVE::Blueprint
