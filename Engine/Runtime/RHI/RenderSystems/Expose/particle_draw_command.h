// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "render_queue.h"

namespace UVE::Render {

inline constexpr std::size_t kMaximumParticleDrawCommands = 1'000'000U;

struct ParticleDrawCommand final {
    Scene::Entity entity;
    Math::Vector3 position{};
    float remainingLifetimeSeconds = 0.0F;
    float sortDepth = 0.0F;
    std::uint64_t sequence = 0U;

    [[nodiscard]] bool operator==(const ParticleDrawCommand&) const = default;
};

[[nodiscard]] bool IsValidParticleDrawCommand(const ParticleDrawCommand& command) noexcept;

struct ParticleDrawRecording final {
    std::size_t sourceItemCount = 0U;
    bool truncated = false;
    std::vector<ParticleDrawCommand> commands;

    [[nodiscard]] bool operator==(const ParticleDrawRecording&) const = default;
};

struct ParticleGpuUploadPlan final {
    static constexpr std::size_t kParticleStrideBytes = sizeof(float) * 5U;
    std::size_t commandCount = 0U;
    std::size_t byteCount = 0U;
    bool truncated = false;
};

/// Plans a bounded fixed-stride particle upload from copied draw commands. It does not allocate
/// GPU buffers, resolve materials, bind pipelines, submit commands, or mutate the recording.
[[nodiscard]] inline bool PlanParticleGpuUpload(
    const ParticleDrawRecording& recording, const std::size_t maximumBytes,
    ParticleGpuUploadPlan& outPlan) noexcept {
    if (recording.commands.size() > kMaximumParticleDrawCommands ||
        recording.sourceItemCount < recording.commands.size() ||
        (!recording.truncated && recording.sourceItemCount != recording.commands.size()) ||
        maximumBytes < ParticleGpuUploadPlan::kParticleStrideBytes) {
        return false;
    }
    const std::size_t commandCount = recording.commands.size();
    if (commandCount > maximumBytes / ParticleGpuUploadPlan::kParticleStrideBytes) {
        return false;
    }
    for (const ParticleDrawCommand& command : recording.commands) {
        if (!IsValidParticleDrawCommand(command)) {
            return false;
        }
    }
    ParticleGpuUploadPlan plan;
    plan.commandCount = commandCount;
    plan.byteCount = commandCount * ParticleGpuUploadPlan::kParticleStrideBytes;
    plan.truncated = recording.truncated;
    outPlan = plan;
    return true;
}

/// Copies renderer-owned particle queue values into a bounded command description. This v1 does
/// not allocate GPU buffers, bind pipelines, submit OpenGL calls, resolve assets, or mutate the queue.
class ParticleDrawRecorder final {
public:
    [[nodiscard]] static ParticleDrawRecording Record(
        const RenderQueue& queue,
        std::size_t maximumCommands = kMaximumParticleDrawCommands);

    /// Fills caller-owned recording storage while preserving command-vector capacity between
    /// frames. The same hard cap and truncation rules as Record() apply.
    static void RecordInto(const RenderQueue& queue, std::size_t maximumCommands,
                              ParticleDrawRecording& outRecording);
};

} // namespace UVE::Render
