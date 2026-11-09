// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Utilities {

/// `NonCopyableUVE` is the inherit-to-disable-copying base (`JobCounterUVE`,
/// `ThreadPoolUVE`-adjacent utilities, every lock): deriving makes the derived type neither
/// copyable nor movable, exactly like spelling the four deletes by hand. New code inherits this
/// instead of retyping them; existing hand-deleted types migrate opportunistically.
class NonCopyableUVE {
protected:
    NonCopyableUVE() = default;
    ~NonCopyableUVE() = default;

private:
    NonCopyableUVE(const NonCopyableUVE&) = delete;
    NonCopyableUVE& operator=(const NonCopyableUVE&) = delete;
};

} // namespace UVE::Utilities
