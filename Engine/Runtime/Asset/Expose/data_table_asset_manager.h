// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Asset {

class IAssetManager;

/// Registers the existing envelope-backed DataTable loader with the supplied asset manager.
/// Registration is synchronous; individual loads retain AssetManager's asynchronous and
/// reference-counted ownership semantics.
void RegisterDataTableAssetLoader(IAssetManager& assetManager);

} // namespace UVE::Asset
