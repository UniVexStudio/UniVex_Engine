// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "asset_guid.h"

namespace UVE::Asset {

/// Queued once HotReload detects an on-disk change to a tracked asset and dispatches a
/// reload for it. Signals only that a reload was triggered, not that it necessarily succeeded —
/// subscribe to AssetLoadCompletedEvent for the pass/fail outcome once the reload finishes.
struct AssetReloadedEvent final {
    AssetGuid guid;
};

} // namespace UVE::Asset
