// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once
//
// Scope: the tap/inspector shared vocabulary for which way a datagram traveled. Split out
// of the inspector header so session taps can name the direction without including the
// inspector (which decodes sessions and would close an include cycle).
//

#include <cstdint>

namespace UVE::Network {

// Which way the datagram traveled from the watched endpoint's point of view.
enum class PacketDirectionUVE : std::uint8_t { Outbound, Inbound };

} // namespace UVE::Network
