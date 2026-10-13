// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/render_systems/render_graph_uve.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>
#include "uve/logging/logging_macros_uve.h"
#include "uve/rhi/i_command_buffer_uve.h"
#include "uve/rhi/i_render_device_uve.h"
#include "uve/utilities/scope_guard_uve.h"
#include "uve/utilities/topological_order_uve.h"

namespace UVE::Render {
namespace {
bool WritesUVE(RenderGraphResourceAccessUVE access) { return access != RenderGraphResourceAccessUVE::Read; }
bool ReadsUVE(RenderGraphResourceAccessUVE access) { return access != RenderGraphResourceAccessUVE::Write; }
bool SameShapeUVE(const TextureDescUVE &a, const TextureDescUVE &b) {
    return a.width == b.width && a.height == b.height && a.format == b.format && a.mipLevels == b.mipLevels &&
           a.colorSpace == b.colorSpace && a.type == b.type && a.arrayLayers == b.arrayLayers;
}
} // namespace
TextureHandleUVE
RenderGraphExecutionContextUVE::GetTextureUVE(RenderGraphResourceHandleUVE resource) const noexcept {
    if (!resource.IsValidUVE() || resource.owner != m_owner || resource.generation != m_generation ||
        resource.value >= m_textures.size())
        return {};
    return m_textures[resource.value];
}
RenderGraphUVE::RenderGraphUVE() {
    Core::ObjectHandleUVE identity;
    if (!m_identityRegistry.RegisterUVE(m_identity, identity))
        throw std::logic_error("render graph identity registration failed");
    m_owner = identity.registry;
}
bool RenderGraphUVE::ContainsUVE(RenderGraphResourceHandleUVE handle) const noexcept {
    return handle.IsValidUVE() && handle.owner == m_owner && handle.generation == m_generation &&
           handle.value < m_resourceCount;
}
bool RenderGraphUVE::ContainsUVE(RenderGraphPassHandleUVE handle) const noexcept {
    return handle.IsValidUVE() && handle.owner == m_owner && handle.generation == m_generation &&
           handle.value < m_passCount;
}
RenderGraphResourceHandleUVE RenderGraphUVE::ImportTextureUVE(TextureHandleUVE texture,
                                                              std::string_view name) {
    if (m_executing) {
        m_buildError = RenderGraphCompileCodeUVE::Busy;
        return {};
    }
    if (m_buildError != RenderGraphCompileCodeUVE::Valid)
        return {};
    if (texture == kInvalidTextureHandleUVE) {
        m_buildError = RenderGraphCompileCodeUVE::InvalidResource;
        return {};
    }
    for (std::size_t i = 0U; i < m_resourceCount; ++i)
        if (!m_resources[i].transient && m_resources[i].texture == texture)
            return {static_cast<std::uint32_t>(i), m_generation, m_owner};
    if (m_resourceCount >= kMaximumResourcesUVE) {
        m_buildError = RenderGraphCompileCodeUVE::CapacityExceeded;
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
RenderGraphResourceHandleUVE RenderGraphUVE::CreateTransientTextureUVE(const TextureDescUVE &desc,
                                                                       std::string_view name) {
    if (m_executing) {
        m_buildError = RenderGraphCompileCodeUVE::Busy;
        return {};
    }
    if (m_buildError != RenderGraphCompileCodeUVE::Valid)
        return {};
    if (!ValidateTextureUploadUVE(desc, {}) || desc.mipLevels != 1U) {
        m_buildError = RenderGraphCompileCodeUVE::InvalidResource;
        return {};
    }
    if (m_resourceCount >= kMaximumResourcesUVE) {
        m_buildError = RenderGraphCompileCodeUVE::CapacityExceeded;
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
bool RenderGraphUVE::MarkOutputUVE(RenderGraphResourceHandleUVE resource) {
    if (m_executing) {
        m_buildError = RenderGraphCompileCodeUVE::Busy;
        return false;
    }
    if (!ContainsUVE(resource)) {
        m_buildError = RenderGraphCompileCodeUVE::InvalidResource;
        return false;
    }
    m_resources[resource.value].output = true;
    return true;
}
RenderGraphPassHandleUVE RenderGraphUVE::AddPassUVE(RenderGraphPassDescUVE desc) {
    const auto handle = AddPassUVE(desc.debugNameUVE, desc.resources, std::move(desc.recordCallbackUVE));
    if (handle.IsValidUVE()) {
        auto &pass = m_passes[handle.value];
        try {
            pass.after.assign(desc.after.begin(), desc.after.end());
            pass.sideEffects = desc.sideEffects;
            pass.executeCallbackUVE = std::move(desc.executeCallbackUVE);
        } catch (...) {
            --m_passCount;
            m_buildError = RenderGraphCompileCodeUVE::InvalidPass;
            throw;
        }
    }
    return handle;
}
RenderGraphPassHandleUVE RenderGraphUVE::AddPassUVE(std::string_view name,
                                                    std::span<const RenderGraphResourceUseUVE> resources,
                                                    std::function<void(ICommandBufferUVE &)> callback) {
    if (m_executing) {
        m_buildError = RenderGraphCompileCodeUVE::Busy;
        return {};
    }
    if (m_buildError != RenderGraphCompileCodeUVE::Valid)
        return {};
    if (m_passCount >= kMaximumPassesUVE) {
        m_buildError = RenderGraphCompileCodeUVE::CapacityExceeded;
        return {};
    }
    if (m_passCount == m_passes.size())
        m_passes.emplace_back();
    auto &pass = m_passes[m_passCount];
    pass.debugNameUVE = name;
    pass.resources.assign(resources.begin(), resources.end());
    pass.after.clear();
    pass.sideEffects = true;
    pass.recordCallbackUVE = std::move(callback);
    pass.executeCallbackUVE = {};
    return {static_cast<std::uint32_t>(m_passCount++), m_generation, m_owner};
}
bool RenderGraphUVE::AddDependencyUVE(RenderGraphPassHandleUVE pass, RenderGraphPassHandleUVE prerequisite) {
    if (m_executing) {
        m_buildError = RenderGraphCompileCodeUVE::Busy;
        return false;
    }
    if (!ContainsUVE(pass) || !ContainsUVE(prerequisite)) {
        m_buildError = RenderGraphCompileCodeUVE::InvalidDependency;
        return false;
    }
    auto &after = m_passes[pass.value].after;
    if (std::find(after.begin(), after.end(), prerequisite) == after.end())
        after.push_back(prerequisite);
    return true;
}
void RenderGraphUVE::ReserveUVE(std::size_t resources, std::size_t passes) {
    if (m_executing) {
        m_buildError = RenderGraphCompileCodeUVE::Busy;
        return;
    }
    m_resources.reserve(std::min(resources, kMaximumResourcesUVE));
    m_passes.reserve(std::min(passes, kMaximumPassesUVE));
}
RenderGraphCompileCodeUVE RenderGraphUVE::CompileUVE(RenderGraphPlanUVE &out,
                                                     const RenderGraphOptionsUVE &options) const {
    if (m_executing)
        return RenderGraphCompileCodeUVE::Busy;
    if (m_buildError != RenderGraphCompileCodeUVE::Valid)
        return m_buildError;
    RenderGraphPlanUVE plan;
    plan.prerequisites.resize(m_passCount);
    std::vector<std::uint32_t> marks(m_resourceCount, UINT32_MAX);
    std::size_t uses = 0U;
    for (std::size_t p = 0U; p < m_passCount; ++p) {
        const auto &pass = m_passes[p];
        if (pass.debugNameUVE.empty() ||
            static_cast<bool>(pass.recordCallbackUVE) == static_cast<bool>(pass.executeCallbackUVE))
            return RenderGraphCompileCodeUVE::InvalidPass;
        if (pass.resources.size() > 65536U - uses)
            return RenderGraphCompileCodeUVE::CapacityExceeded;
        uses += pass.resources.size();
        for (auto dependency : pass.after) {
            if (!ContainsUVE(dependency))
                return RenderGraphCompileCodeUVE::InvalidDependency;
            plan.prerequisites[p].push_back(dependency.value);
        }
        for (const auto &use : pass.resources) {
            if (!ContainsUVE(use.resource) ||
                (use.access != RenderGraphResourceAccessUVE::Read &&
                 use.access != RenderGraphResourceAccessUVE::Write &&
                 use.access != RenderGraphResourceAccessUVE::ReadWrite) ||
                marks[use.resource.value] == p)
                return RenderGraphCompileCodeUVE::InvalidResource;
            marks[use.resource.value] = static_cast<std::uint32_t>(p);
        }
    }
    std::vector<std::uint32_t> order;
    if (Utilities::BuildTopologicalOrderUVE(plan.prerequisites, order) !=
        Utilities::TopologicalOrderCodeUVE::Ordered)
        return RenderGraphCompileCodeUVE::Cycle;
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
            if (ReadsUVE(use.access)) {
                if (writer[r] == UINT32_MAX && m_resources[r].transient)
                    return RenderGraphCompileCodeUVE::UninitializedRead;
                edge(p, writer[r]);
                readers[r].push_back(p);
            }
            if (WritesUVE(use.access)) {
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
                return RenderGraphCompileCodeUVE::UninitializedRead;
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
                SameShapeUVE(plan.transientTextures[slot], m_resources[r].desc))
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
            if (touched[key] && (written[key] || WritesUVE(use.access)))
                barrier = true;
            touched[key] = true;
            written[key] = WritesUVE(use.access);
        }
        plan.barrierBeforePass.push_back(barrier);
    }
    out = std::move(plan);
    return RenderGraphCompileCodeUVE::Valid;
}
bool RenderGraphUVE::ExecuteUVE(ICommandBufferUVE &commands) const {
    RenderGraphOptionsUVE options;
    options.emitBarriers = false;
    return ExecuteInternalUVE(nullptr, commands, options).IsCompletedUVE();
}
RenderGraphExecuteResultUVE RenderGraphUVE::ExecuteUVE(ICommandBufferUVE &commands,
                                                       const RenderGraphOptionsUVE &options) const {
    return ExecuteInternalUVE(nullptr, commands, options);
}
RenderGraphExecuteResultUVE RenderGraphUVE::ExecuteUVE(IRenderDeviceUVE &device, ICommandBufferUVE &commands,
                                                       const RenderGraphOptionsUVE &options) const {
    return ExecuteInternalUVE(&device, commands, options);
}
RenderGraphExecuteResultUVE RenderGraphUVE::ExecuteInternalUVE(IRenderDeviceUVE *device,
                                                               ICommandBufferUVE &commands,
                                                               const RenderGraphOptionsUVE &options) const {
    if (m_executing)
        return {RenderGraphExecuteCodeUVE::Busy, 0U};
    RenderGraphPlanUVE plan;
    if (CompileUVE(plan, options) != RenderGraphCompileCodeUVE::Valid)
        return {};
    if (device && !device->IsUsableUVE())
        return {RenderGraphExecuteCodeUVE::DeviceUnavailable, 0U};
    const bool needsBarriers = std::find(plan.barrierBeforePass.begin(), plan.barrierBeforePass.end(),
                                         true) != plan.barrierBeforePass.end();
    if ((options.emitBarriers && needsBarriers &&
         commands.GetMemoryBarrierSupportUVE() == RenderFeatureSupportUVE::Unsupported) ||
        (!plan.transientTextures.empty() &&
         (!device || !options.emitBarriers ||
          commands.GetMemoryBarrierSupportUVE() == RenderFeatureSupportUVE::Unsupported ||
          device->GetCapabilitiesUVE().retainedTextures == RenderFeatureSupportUVE::Unsupported)))
        return {RenderGraphExecuteCodeUVE::Unsupported, 0U};
    if (device)
        for (std::size_t r = 0U; r < m_resourceCount; ++r) {
            if (m_resources[r].transient || plan.lifetimes[r].firstUse == UINT32_MAX)
                continue;
            TextureDescUVE description;
            const auto described = device->GetTextureDescUVE(m_resources[r].texture, description);
            if (described == RenderOperationResultUVE::Unsupported)
                return {RenderGraphExecuteCodeUVE::Unsupported, 0U};
            if (described != RenderOperationResultUVE::Success)
                return {RenderGraphExecuteCodeUVE::InvalidGraph, 0U};
        }
    std::vector<TextureHandleUVE> textures(m_resourceCount), allocations;
    allocations.reserve(plan.transientTextures.size());
    Utilities::ScopeGuardUVE rollback{[&] {
        if (device)
            for (auto texture : allocations)
                device->DestroyTextureUVE(texture);
    }};
    for (const auto &desc : plan.transientTextures) {
        const auto texture = device->CreateTextureUVE(desc);
        if (texture == kInvalidTextureHandleUVE)
            return {RenderGraphExecuteCodeUVE::AllocationFailed, 0U};
        allocations.push_back(texture);
    }
    for (std::size_t r = 0U; r < m_resourceCount; ++r) {
        if (!m_resources[r].transient)
            textures[r] = m_resources[r].texture;
        else if (plan.lifetimes[r].physicalSlot != UINT32_MAX)
            textures[r] = allocations[plan.lifetimes[r].physicalSlot];
    }
    if (!allocations.empty()) {
        auto lease = device->AdoptTransientTexturesUVE(std::move(allocations));
        if (!lease)
            return {RenderGraphExecuteCodeUVE::AllocationFailed, 0U};
        commands.RetainResourcesUVE(std::move(lease));
    }
    RenderGraphExecutionContextUVE context;
    context.m_textures = textures;
    context.m_owner = m_owner;
    context.m_generation = m_generation;
    m_executing = true;
    Utilities::ScopeGuardUVE done{[&] { m_executing = false; }};
    std::size_t completed = 0U;
    try {
        for (std::size_t position = 0U; position < plan.passOrder.size(); ++position) {
            if (options.emitBarriers && plan.barrierBeforePass[position]) {
                const auto result = commands.MemoryBarrierUVE(MemoryBarrierMaskUVE::All);
                if (result != RenderOperationResultUVE::Success &&
                    result != RenderOperationResultUVE::Recorded)
                    return {RenderGraphExecuteCodeUVE::BarrierFailed, completed};
            }
            const auto &pass = m_passes[plan.passOrder[position]];
            if (pass.recordCallbackUVE)
                pass.recordCallbackUVE(commands);
            else
                pass.executeCallbackUVE(commands, context);
            ++completed;
            if (m_buildError != RenderGraphCompileCodeUVE::Valid)
                return {RenderGraphExecuteCodeUVE::CallbackFailed, completed};
        }
    } catch (...) {
        return {RenderGraphExecuteCodeUVE::CallbackFailed, completed};
    }
    return {RenderGraphExecuteCodeUVE::Completed, completed};
}
void RenderGraphUVE::ClearUVE() noexcept {
    if (m_executing) {
        m_buildError = RenderGraphCompileCodeUVE::Busy;
        return;
    }
    for (std::size_t p = 0U; p < m_passCount; ++p) {
        auto &pass = m_passes[p];
        pass.debugNameUVE.clear();
        pass.resources.clear();
        pass.after.clear();
        pass.recordCallbackUVE = nullptr;
        pass.executeCallbackUVE = nullptr;
    }
    for (std::size_t r = 0U; r < m_resourceCount; ++r)
        m_resources[r].name.clear();
    m_passCount = m_resourceCount = 0U;
    if (m_generation == std::numeric_limits<std::uint64_t>::max())
        m_buildError = RenderGraphCompileCodeUVE::CapacityExceeded;
    else {
        ++m_generation;
        m_buildError = RenderGraphCompileCodeUVE::Valid;
    }
}
} // namespace UVE::Render
