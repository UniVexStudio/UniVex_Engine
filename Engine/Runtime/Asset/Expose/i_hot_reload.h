// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "asset_guid.h"
#include "i_asset_database.h"
#include "i_asset_manager.h"

namespace UVE::Asset {

/// IHotReload detects on-disk changes to currently-loaded assets and triggers
/// AssetManager::Reload() for them, without any OS-native file-watching backend (none
/// exists in engine/platform/ yet, and cross-platform native watching is its own future
/// increment) — instead, it polls tracked assets' mtimes on a configurable interval.
/// Thread-safety: not thread-safe — intended to be driven by a single caller (EngineCore::
/// Update(), on the main/engine thread) via Poll(); Track()/Untrack() are called by
/// AssetManager from inside its own thread-safe operations (LoadErased()/CollectGarbage()),
/// so implementations must guard their internal tracked-set with a mutex even though Poll()
/// itself is main-thread-only.
class IHotReload {
public:
    virtual ~IHotReload() = default;

    /// Starts tracking `guid` for on-disk changes, recording its current mtime as the baseline.
    /// Called by AssetManager once a load completes successfully.
    virtual void Track(AssetGuid guid) = 0;

    /// Stops tracking `guid`. Called by AssetManager when an asset is unloaded via
    /// CollectGarbage().
    virtual void Untrack(AssetGuid guid) = 0;

    /// Accumulates a finite, non-negative `deltaTimeSeconds`; once the configured poll interval has
    /// elapsed, checks every tracked GUID's resolved path (via `assetDatabase`) for an mtime change
    /// since it was last observed and calls `assetManager.Reload()` for each one that changed.
    /// Negative or non-finite deltas are ignored before accumulator or reload state mutation. A
    /// tracked file that no longer exists logs a warning and is skipped for this poll (never crashes,
    /// never untracks it — the file may reappear).
    virtual void Poll(IAssetManager& assetManager, IAssetDatabase& assetDatabase,
                          double deltaTimeSeconds) = 0;
};

} // namespace UVE::Asset
