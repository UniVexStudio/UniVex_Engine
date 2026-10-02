// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Asset::Detail {

/// Initializes Basis exactly once across encoder and transcoder call sites. On desktop, the
/// encoder's initializer also initializes the transcoder; mobile builds initialize only the
/// smaller transcoder library.
[[nodiscard]] bool EnsureBasisUniversalInitializedUVE() noexcept;

} // namespace UVE::Asset::Detail
