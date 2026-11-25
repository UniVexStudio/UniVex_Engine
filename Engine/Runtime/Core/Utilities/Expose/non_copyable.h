// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Utilities {

/// `NonCopyable` is the inherit-to-disable-copying base (`JobCounter`,
/// `ThreadPool`-adjacent utilities, every lock): deriving makes the derived type neither
/// copyable nor movable, exactly like spelling the four deletes by hand. New code inherits this
/// instead of retyping them; existing hand-deleted types migrate opportunistically.
class NonCopyable {
protected:
    NonCopyable() = default;
    ~NonCopyable() = default;

private:
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};

} // namespace UVE::Utilities
