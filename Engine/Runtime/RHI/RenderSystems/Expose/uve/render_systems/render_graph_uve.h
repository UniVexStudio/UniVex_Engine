// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "uve/object/object_registry_uve.h"
#include "uve/rhi/render_resource_descs_uve.h"
#include "uve/rhi/texture_handle_uve.h"

namespace UVE::Render {
class ICommandBufferUVE;
class IRenderDeviceUVE;
struct RenderGraphResourceHandleUVE {
    std::uint32_t value = UINT32_MAX;
    std::uint64_t generation = 0U;
    std::uint64_t owner = 0U;
    [[nodiscard]] constexpr bool IsValidUVE() const noexcept { return value != UINT32_MAX && owner != 0U; }
    friend constexpr bool operator==(RenderGraphResourceHandleUVE, RenderGraphResourceHandleUVE) = default;
};
struct RenderGraphPassHandleUVE {
    std::uint32_t value = UINT32_MAX;
    std::uint64_t generation = 0U;
    std::uint64_t owner = 0U;
    [[nodiscard]] constexpr bool IsValidUVE() const noexcept { return value != UINT32_MAX && owner != 0U; }
    friend constexpr bool operator==(RenderGraphPassHandleUVE, RenderGraphPassHandleUVE) = default;
};
enum class RenderGraphResourceAccessUVE : std::uint8_t { Read, Write, ReadWrite };
struct RenderGraphResourceUseUVE {
    RenderGraphResourceHandleUVE resource;
    RenderGraphResourceAccessUVE access = RenderGraphResourceAccessUVE::Read;
};
/// Borrowed for the callback only. Native transient handles must not escape their recording:
/// their command-buffer lease, not the graph's lifetime, owns their eventual GPU retirement.
class RenderGraphExecutionContextUVE final {
  public:
    [[nodiscard]] TextureHandleUVE GetTextureUVE(RenderGraphResourceHandleUVE resource) const noexcept;

  private:
    friend class RenderGraphUVE;
    std::span<const TextureHandleUVE> m_textures;
    std::uint64_t m_owner = 0U, m_generation = 0U;
};
struct RenderGraphPassDescUVE {
    std::string debugNameUVE;
    std::vector<RenderGraphResourceUseUVE> resources;
    std::function<void(ICommandBufferUVE &)> recordCallbackUVE;
    // Appended: existing aggregate calls remain source compatible.
    std::vector<RenderGraphPassHandleUVE> after{};
    bool sideEffects = true; // legacy passes are never implicitly culled
    std::function<void(ICommandBufferUVE &, const RenderGraphExecutionContextUVE &)> executeCallbackUVE{};
};
struct RenderGraphOptionsUVE final {
    bool cullUnusedPasses = true;
    bool aliasTransientTextures = true;
    bool emitBarriers = true;
};
enum class RenderGraphCompileCodeUVE : std::uint8_t {
    Valid = 0,
    InvalidPass,
    InvalidResource,
    InvalidDependency,
    Cycle,
    UninitializedRead,
    CapacityExceeded,
    Busy
};
struct RenderGraphResourceLifetimeUVE final {
    std::uint32_t firstUse = UINT32_MAX, lastUse = UINT32_MAX;
    std::uint32_t physicalSlot = UINT32_MAX; // imported/unused resources have no transient allocation
};
/// An inspectable planning result, not an externally mutable execution authority. Execute always
/// compiles current declarations, preventing stale/tampered plans from bypassing validation.
struct RenderGraphPlanUVE final {
    std::vector<std::uint32_t> passOrder;
    std::vector<std::vector<std::uint32_t>> prerequisites;
    std::vector<RenderGraphResourceLifetimeUVE> lifetimes;
    std::vector<TextureDescUVE> transientTextures;
    std::vector<bool> barrierBeforePass;
};
enum class RenderGraphExecuteCodeUVE : std::uint8_t {
    Completed = 0,
    InvalidGraph,
    Unsupported,
    DeviceUnavailable,
    AllocationFailed,
    BarrierFailed,
    CallbackFailed,
    Busy
};
struct RenderGraphExecuteResultUVE final {
    RenderGraphExecuteCodeUVE code = RenderGraphExecuteCodeUVE::InvalidGraph;
    std::size_t completedPasses = 0U;
    [[nodiscard]] bool IsCompletedUVE() const noexcept {
        return code == RenderGraphExecuteCodeUVE::Completed;
    }
};

/// Single-queue texture graph: explicit dependencies, RAW/WAR/WAW ordering, stable topological
/// planning, opt-in pure-pass culling, lifetime-based compatible transient reuse and visibility
/// barriers. Subresources/parallel queues are not silently inferred. Imported textures remain
/// caller owned and duplicate imports coalesce. Transients are one-level render targets; first
/// use must write their required contents. Marked outputs remain live through frame completion.
/// Callbacks must close every render pass they open. No graph mutation during execution.
/// Clear invalidates graph-local handles but cannot destroy resources retained by recorded work.
/// Owner-thread only; returned plan/context storage is not a concurrent mutation API.
class RenderGraphUVE final {
  public:
    static constexpr std::size_t kMaximumPassesUVE = 512U;
    static constexpr std::size_t kMaximumResourcesUVE = 4096U;
    RenderGraphUVE();
    RenderGraphUVE(const RenderGraphUVE &) = delete;
    RenderGraphUVE &operator=(const RenderGraphUVE &) = delete;
    [[nodiscard]] RenderGraphResourceHandleUVE ImportTextureUVE(TextureHandleUVE texture,
                                                                std::string_view name);
    [[nodiscard]] RenderGraphResourceHandleUVE CreateTransientTextureUVE(const TextureDescUVE &desc,
                                                                         std::string_view name);
    [[nodiscard]] bool MarkOutputUVE(RenderGraphResourceHandleUVE resource);
    RenderGraphPassHandleUVE AddPassUVE(RenderGraphPassDescUVE desc);
    RenderGraphPassHandleUVE AddPassUVE(std::string_view name,
                                        std::span<const RenderGraphResourceUseUVE> resources,
                                        std::function<void(ICommandBufferUVE &)> callback);
    [[nodiscard]] bool AddDependencyUVE(RenderGraphPassHandleUVE pass, RenderGraphPassHandleUVE prerequisite);
    void ReserveUVE(std::size_t resourceCapacity, std::size_t passCapacity);
    [[nodiscard]] RenderGraphCompileCodeUVE CompileUVE(RenderGraphPlanUVE &outPlan,
                                                       const RenderGraphOptionsUVE &options = {}) const;
    /// Legacy imported-only route preserves implicit backend transitions and command sequences.
    [[nodiscard]] bool ExecuteUVE(ICommandBufferUVE &commands) const;
    /// Imported-only route with explicit planning/barrier options; imports remain caller owned.
    [[nodiscard]] RenderGraphExecuteResultUVE ExecuteUVE(ICommandBufferUVE &commands,
                                                         const RenderGraphOptionsUVE &options) const;
    /// Explicit route: unsupported barriers/retirement fail before any callback or allocation.
    /// Partial callback/barrier failure reports completed count; GL work cannot be rolled back.
    [[nodiscard]] RenderGraphExecuteResultUVE ExecuteUVE(IRenderDeviceUVE &device,
                                                         ICommandBufferUVE &commands,
                                                         const RenderGraphOptionsUVE &options = {}) const;
    void ClearUVE() noexcept;
    [[nodiscard]] std::size_t GetPassCountUVE() const noexcept { return m_passCount; }

  private:
    struct ResourceUVE {
        TextureHandleUVE texture;
        std::string name;
        TextureDescUVE desc;
        bool transient = false, output = false;
    };
    [[nodiscard]] bool ContainsUVE(RenderGraphResourceHandleUVE handle) const noexcept;
    [[nodiscard]] bool ContainsUVE(RenderGraphPassHandleUVE handle) const noexcept;
    RenderGraphExecuteResultUVE ExecuteInternalUVE(IRenderDeviceUVE *device, ICommandBufferUVE &commands,
                                                   const RenderGraphOptionsUVE &options) const;
    Core::ObjectRegistryUVE m_identityRegistry;
    Core::ObjectUVE m_identity;
    std::uint64_t m_owner = 0U, m_generation = 1U;
    std::vector<ResourceUVE> m_resources;
    std::vector<RenderGraphPassDescUVE> m_passes;
    std::size_t m_resourceCount = 0U, m_passCount = 0U;
    mutable bool m_executing = false;
    mutable RenderGraphCompileCodeUVE m_buildError = RenderGraphCompileCodeUVE::Valid;
};
} // namespace UVE::Render
