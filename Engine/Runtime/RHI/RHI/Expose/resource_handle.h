// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// Shared strong-handle template for the RHI's five GPU resource kinds
// (Buffer/Texture/Shader/Pipeline/Sampler). Until the 2026-09-17 audit, each kind had
// its own hand-copied ~37-line header implementing the same wrapper+equality+hash; they are
// now generated from this one template via per-kind tag types and `using` aliases in
// buffer_handle_uve.h / texture_handle_uve.h / shader_handle_uve.h / pipeline_handle_uve.h.
// Behavior is unchanged: the alias names are the same types consumers already used.
//
// Why a small wrapper struct rather than a bare std::uint32_t alias: a ResourceHandle<BufferTag>
// can never be silently passed where a ResourceHandle<TextureTag> was meant — matching
// AssetGuid's precedent for opaque ids that must not be confused with each other, which
// matters here since the RHI has five distinct resource kinds.
// Thread-safety: value type; safe to copy/compare/hash freely, no shared state.


#pragma once

#include <cstdint>
#include <functional>

namespace UVE::Render {

/// Opaque handle to a GPU resource created via IRenderDevice. The Tag parameter is a
/// per-kind empty struct; see the four sibling headers for the concrete aliases and their
/// kInvalid*Handle sentinels.
template <typename Tag>
struct ResourceHandle {
    std::uint32_t value = 0;
};

template <typename Tag>
[[nodiscard]] constexpr bool operator==(const ResourceHandle<Tag>& lhs,
                                        const ResourceHandle<Tag>& rhs) noexcept {
    return lhs.value == rhs.value;
}

template <typename Tag>
[[nodiscard]] constexpr bool operator!=(const ResourceHandle<Tag>& lhs,
                                        const ResourceHandle<Tag>& rhs) noexcept {
    return !(lhs == rhs);
}

} // namespace UVE::Render

template <typename Tag>
struct std::hash<UVE::Render::ResourceHandle<Tag>> {
    [[nodiscard]] std::size_t operator()(const UVE::Render::ResourceHandle<Tag>& handle) const noexcept {
        return std::hash<std::uint32_t>{}(handle.value);
    }
};
