// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "asset_guid.h"

namespace UVE::Asset {

/// Queued (via IEventSystem::QueueEvent(), safe to call from the IThreadPool worker that
/// ran the load) once AssetManager finishes loading `guid`, whether it succeeded or failed —
/// lets interested systems react without polling AssetHandle::IsReady() every frame.
struct AssetLoadCompletedEvent final {
    AssetGuid guid;
    bool success = false;
};

} // namespace UVE::Asset
