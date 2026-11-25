// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "render_graph.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include "logging_macros.h"
#include "i_command_buffer.h"
#include "i_render_device.h"
#include "scope_guard.h"
#include "topological_order.h"

namespace UVE::Render {
namespace {
bool Writes(RenderGraphResourceAccess access) { return access != RenderGraphResourceAccess::Read; }
bool Reads(RenderGraphResourceAccess access) { return access != RenderGraphResourceAccess::Write; }
bool SameShape(const TextureDesc &a, const TextureDesc &b) {
    return a.width == b.width && a.height == b.height && a.format == b.format && a.mipLevels == b.mipLevels &&
           a.colorSpace == b.colorSpace && a.type == b.type && a.arrayLayers == b.arrayLayers;
}
} // namespace
TextureHandle
RenderGraphExecutionContext::GetTexture(RenderGraphResourceHandle resource) const noexcept {
    if (!resource.IsValid() || resource.owner != m_owner || resource.generation != m_generation ||
        resource.value >= m_textures.size())
        return {};
    return m_textures[resource.value];
}
RenderGraph::RenderGraph() {
    Core::ObjectHandle identity;
    if (!m_identityRegistry.Register(m_identity, identity))
        throw std::logic_error("render graph identity registration failed");
    m_owner = identity.registry;
}
bool RenderGraph::Contains(RenderGraphResourceHandle handle) const noexcept {
    return handle.IsValid() && handle.owner == m_owner && handle.generation == m_generation &&
           handle.value < m_resourceCount;
}
bool RenderGraph::Contains(RenderGraphPassHandle handle) const noexcept {
    return handle.IsValid() && handle.owner == m_owner && handle.generation == m_generation &&
           handle.value < m_passCount;
}
RenderGraphResourceHandle RenderGraph::ImportTexture(TextureHandle texture,
                                                              std::string_view name) {
    if (m_executing) {
        m_buildError = RenderGraphCompileCode::Busy;
        return {};
    }
    if (m_buildError != RenderGraphCompileCode::Valid)
        return {};
    if (texture == kInvalidTextureHandle) {
        m_buildError = RenderGraphCompileCode::InvalidResource;
        return {};
    }
    for (std::size_t i = 0U; i < m_resourceCount; ++i)
        if (!m_resources[i].transient && m_resources[i].texture == texture)
            return {static_cast<std::uint32_t>(i), m_generation, m_owner};
    if (m_resourceCount >= kMaximumResources) {
        m_buildError = RenderGraphCompileCode::CapacityExceeded;
        return {};
    }
    if (m_resourceCount == m_resources.size())
        m_resources.emplace_back();
    auto &resource = m_resources[m_resourceCount];
    resource.texture = texture;
    resource.name = name;
    resource.desc = {};
    resource.transient = resource.output = false;
    return {static_cast<std::uint32_t>(m_resourceCount++), m_generation, m_owner};
}
RenderGraphResourceHandle RenderGraph::CreateTransientTexture(const TextureDesc &desc,
                                                                       std::string_view name) {
    if (m_executing) {
        m_buildError = RenderGraphCompileCode::Busy;
        return {};
    }
    if (m_buildError != RenderGraphCompileCode::Valid)
        return {};
    if (!ValidateTextureUpload(desc, {}) || desc.mipLevels != 1U) {
        m_buildError = RenderGraphCompileCode::InvalidResource;
        return {};
    }
    if (m_resourceCount >= kMaximumResources) {
        m_buildError = RenderGraphCompileCode::CapacityExceeded;
        return {};
    }
    if (m_resourceCount == m_resources.size())
        m_resources.emplace_back();
    auto &resource = m_resources[m_resourceCount];
    resource.texture = {};
    resource.name = name;
    resource.desc = desc;
    resource.transient = true;
    resource.output = false;
    return {static_cast<std::uint32_t>(m_resourceCount++), m_generation, m_owner};
}
bool RenderGraph::MarkOutput(RenderGraphResourceHandle resource) {
    if (m_executing) {
        m_buildError = RenderGraphCompileCode::Busy;
        return false;
    }
    if (!Contains(resource)) {
        m_buildError = RenderGraphCompileCode::InvalidResource;
        return false;
    }
    m_resources[resource.value].output = true;
    return true;
}
RenderGraphPassHandle RenderGraph::AddPass(RenderGraphPassDesc desc) {
    const auto handle = AddPass(desc.debugName, desc.resources, std::move(desc.recordCallback));
    if (handle.IsValid()) {
        auto &pass = m_passes[handle.value];
        try {
            pass.after.assign(desc.after.begin(), desc.after.end());
            pass.sideEffects = desc.sideEffects;
            pass.executeCallback = std::move(desc.executeCallback);
        } catch (...) {
            --m_passCount;
            m_buildError = RenderGraphCompileCode::InvalidPass;
            throw;
        }
    }
    return handle;
}
RenderGraphPassHandle RenderGraph::AddPass(std::string_view name,
                                                    std::span<const RenderGraphResourceUse> resources,
                                                    std::function<void(ICommandBuffer &)> callback) {
    if (m_executing) {
        m_buildError = RenderGraphCompileCode::Busy;
        return {};
    }
    if (m_buildError != RenderGraphCompileCode::Valid)
        return {};
    if (m_passCount >= kMaximumPasses) {
        m_buildError = RenderGraphCompileCode::CapacityExceeded;
        return {};
    }
    if (m_passCount == m_passes.size())
        m_passes.emplace_back();
    auto &pass = m_passes[m_passCount];
    pass.debugName = name;
    pass.resources.assign(resources.begin(), resources.end());
    pass.after.clear();
    pass.sideEffects = true;
    pass.recordCallback = std::move(callback);
    pass.executeCallback = {};
    return {static_cast<std::uint32_t>(m_passCount++), m_generation, m_owner};
}
bool RenderGraph::AddDependency(RenderGraphPassHandle pass, RenderGraphPassHandle prerequisite) {
    if (m_executing) {
        m_buildError = RenderGraphCompileCode::Busy;
        return false;
    }
    if (!Contains(pass) || !Contains(prerequisite)) {
        m_buildError = RenderGraphCompileCode::InvalidDependency;
        return false;
    }
    auto &after = m_passes[pass.value].after;
    if (std::find(after.begin(), after.end(), prerequisite) == after.end())
        after.push_back(prerequisite);
    return true;
}
void RenderGraph::Reserve(std::size_t resources, std::size_t passes) {
    if (m_executing) {
        m_buildError = RenderGraphCompileCode::Busy;
        return;
    }
    m_resources.reserve(std::min(resources, kMaximumResources));
    m_passes.reserve(std::min(passes, kMaximumPasses));
}
RenderGraphCompileCode RenderGraph::Compile(RenderGraphPlan &out,
                                                     const RenderGraphOptions &options) const {
    if (m_executing)
        return RenderGraphCompileCode::Busy;
    if (m_buildError != RenderGraphCompileCode::Valid)
        return m_buildError;
    RenderGraphPlan plan;
    plan.prerequisites.resize(m_passCount);
    std::vector<std::uint32_t> marks(m_resourceCount, UINT32_MAX);
    std::size_t uses = 0U;
    for (std::size_t p = 0U; p < m_passCount; ++p) {
        const auto &pass = m_passes[p];
        if (pass.debugName.empty() ||
            static_cast<bool>(pass.recordCallback) == static_cast<bool>(pass.executeCallback))
            return RenderGraphCompileCode::InvalidPass;
        if (pass.resources.size() > 65536U - uses)
            return RenderGraphCompileCode::CapacityExceeded;
        uses += pass.resources.size();
        for (auto dependency : pass.after) {
            if (!Contains(dependency))
                return RenderGraphCompileCode::InvalidDependency;
            plan.prerequisites[p].push_back(dependency.value);
        }
        for (const auto &use : pass.resources) {
            if (!Contains(use.resource) ||
                (use.access != RenderGraphResourceAccess::Read &&
                 use.access != RenderGraphResourceAccess::Write &&
                 use.access != RenderGraphResourceAccess::ReadWrite) ||
                marks[use.resource.value] == p)
                return RenderGraphCompileCode::InvalidResource;
            marks[use.resource.value] = static_cast<std::uint32_t>(p);
        }
    }
    std::vector<std::uint32_t> order;
    if (Utilities::BuildTopologicalOrder(plan.prerequisites, order) !=
        Utilities::TopologicalOrderCode::Ordered)
        return RenderGraphCompileCode::Cycle;
    std::vector<std::uint32_t> writer(m_resourceCount, UINT32_MAX);
    std::vector<std::vector<std::uint32_t>> readers(m_resourceCount);
    const auto edge = [&](std::uint32_t pass, std::uint32_t prerequisite) {
        if (prerequisite != UINT32_MAX && prerequisite != pass)
            plan.prerequisites[pass].push_back(prerequisite);
    };
    // Explicit order defines versions of each mutable logical texture. Hazard edges preserve
    // RAW/WAR/WAW behavior, including the conservative dependency between consecutive writes.
    for (auto p : order)
        for (const auto &use : m_passes[p].resources) {
            const auto r = use.resource.value;
            if (Reads(use.access)) {
                if (writer[r] == UINT32_MAX && m_resources[r].transient)
                    return RenderGraphCompileCode::UninitializedRead;
                edge(p, writer[r]);
                readers[r].push_back(p);
            }
            if (Writes(use.access)) {
                edge(p, writer[r]);
                for (auto reader : readers[r])
                    edge(p, reader);
                readers[r].clear();
                writer[r] = p;
            }
        }
    for (auto &dependencies : plan.prerequisites) {
        std::sort(dependencies.begin(), dependencies.end());
        dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
    }
    std::vector<bool> needed(m_passCount, !options.cullUnusedPasses);
    std::vector<std::uint32_t> pending;
    for (std::size_t p = 0U; p < m_passCount; ++p)
        if (m_passes[p].sideEffects)
            pending.push_back(static_cast<std::uint32_t>(p));
    for (std::size_t r = 0U; r < m_resourceCount; ++r)
        if (m_resources[r].output) {
            if (writer[r] == UINT32_MAX && m_resources[r].transient)
                return RenderGraphCompileCode::UninitializedRead;
            if (writer[r] != UINT32_MAX)
                pending.push_back(writer[r]);
        }
    while (!pending.empty()) {
        const auto p = pending.back();
        pending.pop_back();
        if (needed[p])
            continue;
        needed[p] = true;
        pending.insert(pending.end(), plan.prerequisites[p].begin(), plan.prerequisites[p].end());
    }
    for (auto p : order)
        if (needed[p])
            plan.passOrder.push_back(p);
    plan.lifetimes.resize(m_resourceCount);
    for (std::size_t position = 0U; position < plan.passOrder.size(); ++position) {
        for (const auto &use : m_passes[plan.passOrder[position]].resources) {
            auto &life = plan.lifetimes[use.resource.value];
            if (life.firstUse == UINT32_MAX)
                life.firstUse = static_cast<std::uint32_t>(position);
            life.lastUse = static_cast<std::uint32_t>(position);
        }
    }
    std::vector<std::uint32_t> allocationOrder;
    for (std::size_t r = 0U; r < m_resourceCount; ++r) {
        if (m_resources[r].output && plan.lifetimes[r].firstUse != UINT32_MAX)
            plan.lifetimes[r].lastUse = static_cast<std::uint32_t>(plan.passOrder.size());
        if (m_resources[r].transient && plan.lifetimes[r].firstUse != UINT32_MAX)
            allocationOrder.push_back(static_cast<std::uint32_t>(r));
    }
    std::stable_sort(allocationOrder.begin(), allocationOrder.end(),
                     [&](auto a, auto b) { return plan.lifetimes[a].firstUse < plan.lifetimes[b].firstUse; });
    std::vector<std::uint32_t> slotEnd;
    for (auto r : allocationOrder) {
        auto &life = plan.lifetimes[r];
        std::size_t slot = 0U;
        for (; slot < plan.transientTextures.size(); ++slot) {
            if (options.aliasTransientTextures && slotEnd[slot] < life.firstUse &&
                SameShape(plan.transientTextures[slot], m_resources[r].desc))
                break;
        }
        if (slot == plan.transientTextures.size()) {
            plan.transientTextures.push_back(m_resources[r].desc);
            slotEnd.push_back(0U);
        }
        slotEnd[slot] = life.lastUse;
        life.physicalSlot = static_cast<std::uint32_t>(slot);
    }
    std::vector<bool> touched(plan.transientTextures.size() + m_resourceCount, false),
        written(touched.size(), false);
    for (auto p : plan.passOrder) {
        bool barrier = false;
        for (const auto &use : m_passes[p].resources) {
            const auto r = use.resource.value;
            const auto key =
                m_resources[r].transient ? plan.lifetimes[r].physicalSlot : plan.transientTextures.size() + r;
            if (touched[key] && (written[key] || Writes(use.access)))
                barrier = true;
            touched[key] = true;
            written[key] = Writes(use.access);
        }
        plan.barrierBeforePass.push_back(barrier);
    }
    out = std::move(plan);
    return RenderGraphCompileCode::Valid;
}
bool RenderGraph::Execute(ICommandBuffer &commands) const {
    RenderGraphOptions options;
    options.emitBarriers = false;
    return ExecuteInternal(nullptr, commands, options).IsCompleted();
}
RenderGraphExecuteResult RenderGraph::Execute(ICommandBuffer &commands,
                                                       const RenderGraphOptions &options) const {
    return ExecuteInternal(nullptr, commands, options);
}
RenderGraphExecuteResult RenderGraph::Execute(IRenderDevice &device, ICommandBuffer &commands,
                                                       const RenderGraphOptions &options) const {
    return ExecuteInternal(&device, commands, options);
}
RenderGraphExecuteResult RenderGraph::ExecuteInternal(IRenderDevice *device,
                                                               ICommandBuffer &commands,
                                                               const RenderGraphOptions &options) const {
    if (m_executing)
        return {RenderGraphExecuteCode::Busy, 0U};
    RenderGraphPlan plan;
    if (Compile(plan, options) != RenderGraphCompileCode::Valid)
        return {};
    if (device && !device->IsUsable())
        return {RenderGraphExecuteCode::DeviceUnavailable, 0U};
    const bool needsBarriers = std::find(plan.barrierBeforePass.begin(), plan.barrierBeforePass.end(),
                                         true) != plan.barrierBeforePass.end();
    if ((options.emitBarriers && needsBarriers &&
         commands.GetMemoryBarrierSupport() == RenderFeatureSupport::Unsupported) ||
        (!plan.transientTextures.empty() &&
         (!device || !options.emitBarriers ||
          commands.GetMemoryBarrierSupport() == RenderFeatureSupport::Unsupported ||
          device->GetCapabilities().retainedTextures == RenderFeatureSupport::Unsupported)))
        return {RenderGraphExecuteCode::Unsupported, 0U};
    if (device)
        for (std::size_t r = 0U; r < m_resourceCount; ++r) {
            if (m_resources[r].transient || plan.lifetimes[r].firstUse == UINT32_MAX)
                continue;
            TextureDesc description;
            const auto described = device->GetTextureDesc(m_resources[r].texture, description);
            if (described == RenderOperationResult::Unsupported)
                return {RenderGraphExecuteCode::Unsupported, 0U};
            if (described != RenderOperationResult::Success)
                return {RenderGraphExecuteCode::InvalidGraph, 0U};
        }
    std::vector<TextureHandle> textures(m_resourceCount), allocations;
    allocations.reserve(plan.transientTextures.size());
    Utilities::ScopeGuard rollback{[&] {
        if (device)
            for (auto texture : allocations)
                device->DestroyTexture(texture);
    }};
    for (const auto &desc : plan.transientTextures) {
        const auto texture = device->CreateTexture(desc);
        if (texture == kInvalidTextureHandle)
            return {RenderGraphExecuteCode::AllocationFailed, 0U};
        allocations.push_back(texture);
    }
    for (std::size_t r = 0U; r < m_resourceCount; ++r) {
        if (!m_resources[r].transient)
            textures[r] = m_resources[r].texture;
        else if (plan.lifetimes[r].physicalSlot != UINT32_MAX)
            textures[r] = allocations[plan.lifetimes[r].physicalSlot];
    }
    if (!allocations.empty()) {
        auto lease = device->AdoptTransientTextures(std::move(allocations));
        if (!lease)
            return {RenderGraphExecuteCode::AllocationFailed, 0U};
        commands.RetainResources(std::move(lease));
    }
    RenderGraphExecutionContext context;
    context.m_textures = textures;
    context.m_owner = m_owner;
    context.m_generation = m_generation;
    m_executing = true;
    Utilities::ScopeGuard done{[&] { m_executing = false; }};
    std::size_t completed = 0U;
    try {
        for (std::size_t position = 0U; position < plan.passOrder.size(); ++position) {
            if (options.emitBarriers && plan.barrierBeforePass[position]) {
                const auto result = commands.MemoryBarrier(MemoryBarrierMask::All);
                if (result != RenderOperationResult::Success &&
                    result != RenderOperationResult::Recorded)
                    return {RenderGraphExecuteCode::BarrierFailed, completed};
            }
            const auto &pass = m_passes[plan.passOrder[position]];
            if (pass.recordCallback)
                pass.recordCallback(commands);
            else
                pass.executeCallback(commands, context);
            ++completed;
            if (m_buildError != RenderGraphCompileCode::Valid)
                return {RenderGraphExecuteCode::CallbackFailed, completed};
        }
    } catch (...) {
        return {RenderGraphExecuteCode::CallbackFailed, completed};
    }
    return {RenderGraphExecuteCode::Completed, completed};
}
void RenderGraph::Clear() noexcept {
    if (m_executing) {
        m_buildError = RenderGraphCompileCode::Busy;
        return;
    }
    for (std::size_t p = 0U; p < m_passCount; ++p) {
        auto &pass = m_passes[p];
        pass.debugName.clear();
        pass.resources.clear();
        pass.after.clear();
        pass.recordCallback = nullptr;
        pass.executeCallback = nullptr;
    }
    for (std::size_t r = 0U; r < m_resourceCount; ++r)
        m_resources[r].name.clear();
    m_passCount = m_resourceCount = 0U;
    if (m_generation == std::numeric_limits<std::uint64_t>::max())
        m_buildError = RenderGraphCompileCode::CapacityExceeded;
    else {
        ++m_generation;
        m_buildError = RenderGraphCompileCode::Valid;
    }
}
} // namespace UVE::Render
