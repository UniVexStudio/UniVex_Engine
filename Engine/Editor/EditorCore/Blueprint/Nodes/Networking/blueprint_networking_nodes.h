// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace UVE::Blueprint::Nodes::Networking {

/// Pin data type tag for a Blueprint Networking node descriptor.
enum class NetworkingPinTypeUVE : std::uint8_t {
    Bool = 0,
    Int,
    Float,
    String,
    Vector,
};

/// One input or output pin of a Blueprint Networking node.
struct NetworkingPinSpecUVE final {
    std::string_view name;
    NetworkingPinTypeUVE type = NetworkingPinTypeUVE::Int;
    std::string_view defaultText;
};

/// Execution role of a Blueprint Networking node.
enum class NetworkingNodeKindUVE : std::uint8_t {
    Action = 0, ///< Exec-driven call into Engine/Runtime/Networking/ host bindings.
    Pure,       ///< Pure query reading Engine/Runtime/Networking/ host state.
};

/// Descriptor for one Blueprint Networking node registered into the Blueprint node catalog.
/// Every node delegates exclusively to public Engine/Runtime/Networking/ APIs bound on the
/// Blueprint host; no socket, transport, or protocol logic lives in Blueprint.
struct NetworkingNodeSpecUVE final {
    std::string_view key;
    std::string_view title;
    std::string_view category;
    std::string_view keywords;
    std::string_view tooltip;
    NetworkingNodeKindUVE kind = NetworkingNodeKindUVE::Pure;
    std::vector<NetworkingPinSpecUVE> inputs;
    std::vector<NetworkingPinSpecUVE> outputs;
    /// Action statement or pure output expression (using `{n}` input placeholders).
    std::string_view expression;
};

/// Returns all Blueprint Networking nodes backed by Engine/Runtime/Networking/ host bindings.
[[nodiscard]] std::span<const NetworkingNodeSpecUVE> GetNetworkingBlueprintNodesUVE() noexcept;

} // namespace UVE::Blueprint::Nodes::Networking
