// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Pack {

/// Spaces per nesting level in the JSON files the launcher tools write (project registry, launcher
/// settings, starter scenes, engine manifests). These files are meant to be readable and diffable.
inline constexpr int kPackJsonIndentUVE = 2;

} // namespace UVE::Pack
