// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <string_view>

namespace UVE::Audio {

/// Result of resolving an authored audio clip identity before voice creation. A resolver may
/// canonicalize virtual paths or reject them; it never decodes PCM or owns a device voice.
struct AudioClipResolution final {
    bool accepted = false;
    std::string resolvedPath;
    std::string diagnostic;
};

/// Optional borrowed clip identity resolver. Implementations own path/database policy; callers
/// retain ownership and lifetime, and AudioSystem copies only the accepted path into its source
/// state and backend descriptor.
class IAudioClipResolver {
public:
    virtual ~IAudioClipResolver() = default;

    [[nodiscard]] virtual AudioClipResolution ResolveAudioClip(std::string_view authoredPath) const = 0;
};

} // namespace UVE::Audio
