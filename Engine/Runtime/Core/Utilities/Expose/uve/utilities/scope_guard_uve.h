// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <type_traits>
#include <utility>

namespace UVE::Utilities {

/// `ScopeGuardUVE` runs a callable at scope exit unless dismissed: the C++ spelling of
/// try/finally for the dozen early-return cleanups (unlock, rollback, restore) that otherwise
/// get retyped before every return. Move-only; moving dismisses the source, so exactly one
/// guard ever fires.
///
/// The callable runs from the destructor: it must not throw (a throwing destructor during stack
/// unwinding terminates). Prefer the `UVE_DEFER` macro, which names the guard for you.
template <typename TFunc>
class ScopeGuardUVE final {
public:
    explicit ScopeGuardUVE(TFunc onExit) noexcept(std::is_nothrow_move_constructible_v<TFunc>)
        : m_onExit(std::move(onExit)) {}

    ~ScopeGuardUVE() noexcept {
        if (m_armed) {
            m_onExit();
        }
    }

    ScopeGuardUVE(const ScopeGuardUVE&) = delete;
    ScopeGuardUVE& operator=(const ScopeGuardUVE&) = delete;

    ScopeGuardUVE(ScopeGuardUVE&& other) noexcept(std::is_nothrow_move_constructible_v<TFunc>)
        : m_onExit(std::move(other.m_onExit)), m_armed(other.m_armed) {
        other.m_armed = false;
    }

    ScopeGuardUVE& operator=(ScopeGuardUVE&&) = delete;

    /// Disarms the guard: scope exit runs nothing.
    void DismissUVE() noexcept { m_armed = false; }

private:
    TFunc m_onExit;
    bool m_armed = true;
};

} // namespace UVE::Utilities

#define UVE_SCOPE_GUARD_CONCATUVE(a, b) a##b
#define UVE_SCOPE_GUARD_NAMEUVE(a, b) UVE_SCOPE_GUARD_CONCATUVE(a, b)

/// `UVE_DEFER(statement...)` — runs the statements at scope exit:
/// `UVE_DEFER( المللی... )`. The capture is by reference; names stay hygienic via `__LINE__`.
#define UVE_DEFER(...)                                                                                \
    ::UVE::Utilities::ScopeGuardUVE UVE_SCOPE_GUARD_NAMEUVE(scopeGuardLineUVE, __LINE__) { [&] { \
        __VA_ARGS__;                                                                               \
    } }
