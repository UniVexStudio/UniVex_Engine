// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "object_registry.h"
#include "render_resource_descs.h"
#include "texture_handle.h"

namespace UVE::Render {
class ICommandBuffer;
class IRenderDevice;
struct RenderGraphResourceHandle {
    std::uint32_t value = UINT32_MAX;
    std::uint64_t generation = 0U;
    std::uint64_t owner = 0U;
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != UINT32_MAX && owner != 0U; }
    friend constexpr bool operator==(RenderGraphResourceHandle, RenderGraphResourceHandle) = default;
};
struct RenderGraphPassHandle {
    std::uint32_t value = UINT32_MAX;
    std::uint64_t generation = 0U;
    std::uint64_t owner = 0U;
    [[nodiscard]] constexpr bool IsValid() const noexcept { return value != UINT32_MAX && owner != 0U; }
    friend constexpr bool operator==(RenderGraphPassHandle, RenderGraphPassHandle) = default;
};
enum class RenderGraphResourceAccess : std::uint8_t { Read, Write, ReadWrite };
struct RenderGraphResourceUse {
    RenderGraphResourceHandle resource;
    RenderGraphResourceAccess access = RenderGraphResourceAccess::Read;
};
/// Borrowed for the callback only. Native transient handles must not escape their recording:
/// their command-buffer lease, not the graph's lifetime, owns their eventual GPU retirement.
class RenderGraphExecutionContext final {
  public:
    [[nodiscard]] TextureHandle GetTexture(RenderGraphResourceHandle resource) const noexcept;

  private:
    friend class RenderGraph;
    std::span<const TextureHandle> m_textures;
    std::uint64_t m_owner = 0U, m_generation = 0U;
};
struct RenderGraphPassDesc {
    std::string debugName;
    std::vector<RenderGraphResourceUse> resources;
    std::function<void(ICommandBuffer &)> recordCallback;
    // Appended: existing aggregate calls remain source compatible.
    std::vector<RenderGraphPassHandle> after{};
    bool sideEffects = true; // legacy passes are never implicitly culled
    std::function<void(ICommandBuffer &, const RenderGraphExecutionContext &)> executeCallback{};
};
struct RenderGraphOptions final {
    bool cullUnusedPasses = true;
    bool aliasTransientTextures = true;
    bool emitBarriers = true;
};
enum class RenderGraphCompileCode : std::uint8_t {
    Valid = 0,
    InvalidPass,
    InvalidResource,
    InvalidDependency,
    Cycle,
    UninitializedRead,
    CapacityExceeded,
    Busy
};
struct RenderGraphResourceLifetime final {
    std::uint32_t firstUse = UINT32_MAX, lastUse = UINT32_MAX;
    std::uint32_t physicalSlot = UINT32_MAX; // imported/unused resources have no transient allocation
};
/// An inspectable planning result, not an externally mutable execution authority. Execute always
/// compiles current declarations, preventing stale/tampered plans from bypassing validation.
struct RenderGraphPlan final {
    std::vector<std::uint32_t> passOrder;
    std::vector<std::vector<std::uint32_t>> prerequisites;
    std::vector<RenderGraphResourceLifetime> lifetimes;
    std::vector<TextureDesc> transientTextures;
    std::vector<bool> barrierBeforePass;
};
enum class RenderGraphExecuteCode : std::uint8_t {
    Completed = 0,
    InvalidGraph,
    Unsupported,
    DeviceUnavailable,
    AllocationFailed,
    BarrierFailed,
    CallbackFailed,
    Busy
};
struct RenderGraphExecuteResult final {
    RenderGraphExecuteCode code = RenderGraphExecuteCode::InvalidGraph;
    std::size_t completedPasses = 0U;
    [[nodiscard]] bool IsCompleted() const noexcept {
        return code == RenderGraphExecuteCode::Completed;
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
class RenderGraph final {
  public:
    static constexpr std::size_t kMaximumPasses = 512U;
    static constexpr std::size_t kMaximumResources = 4096U;
    RenderGraph();
    RenderGraph(const RenderGraph &) = delete;
    RenderGraph &operator=(const RenderGraph &) = delete;
    [[nodiscard]] RenderGraphResourceHandle ImportTexture(TextureHandle texture,
                                                                std::string_view name);
    [[nodiscard]] RenderGraphResourceHandle CreateTransientTexture(const TextureDesc &desc,
                                                                         std::string_view name);
    [[nodiscard]] bool MarkOutput(RenderGraphResourceHandle resource);
    RenderGraphPassHandle AddPass(RenderGraphPassDesc desc);
    RenderGraphPassHandle AddPass(std::string_view name,
                                        std::span<const RenderGraphResourceUse> resources,
                                        std::function<void(ICommandBuffer &)> callback);
    [[nodiscard]] bool AddDependency(RenderGraphPassHandle pass, RenderGraphPassHandle prerequisite);
    void Reserve(std::size_t resourceCapacity, std::size_t passCapacity);
    [[nodiscard]] RenderGraphCompileCode Compile(RenderGraphPlan &outPlan,
                                                       const RenderGraphOptions &options = {}) const;
    /// Legacy imported-only route preserves implicit backend transitions and command sequences.
    [[nodiscard]] bool Execute(ICommandBuffer &commands) const;
    /// Imported-only route with explicit planning/barrier options; imports remain caller owned.
    [[nodiscard]] RenderGraphExecuteResult Execute(ICommandBuffer &commands,
                                                         const RenderGraphOptions &options) const;
    /// Explicit route: unsupported barriers/retirement fail before any callback or allocation.
    /// Partial callback/barrier failure reports completed count; GL work cannot be rolled back.
    [[nodiscard]] RenderGraphExecuteResult Execute(IRenderDevice &device,
                                                         ICommandBuffer &commands,
                                                         const RenderGraphOptions &options = {}) const;
    void Clear() noexcept;
    [[nodiscard]] std::size_t GetPassCount() const noexcept { return m_passCount; }

  private:
    struct Resource {
        TextureHandle texture;
        std::string name;
        TextureDesc desc;
        bool transient = false, output = false;
    };
    [[nodiscard]] bool Contains(RenderGraphResourceHandle handle) const noexcept;
    [[nodiscard]] bool Contains(RenderGraphPassHandle handle) const noexcept;
    RenderGraphExecuteResult ExecuteInternal(IRenderDevice *device, ICommandBuffer &commands,
                                                   const RenderGraphOptions &options) const;
    Core::ObjectRegistry m_identityRegistry;
    Core::Object m_identity;
    std::uint64_t m_owner = 0U, m_generation = 1U;
    std::vector<Resource> m_resources;
    std::vector<RenderGraphPassDesc> m_passes;
    std::size_t m_resourceCount = 0U, m_passCount = 0U;
    mutable bool m_executing = false;
    mutable RenderGraphCompileCode m_buildError = RenderGraphCompileCode::Valid;
};
} // namespace UVE::Render
