// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "particle_draw_command.h"

#include <cmath>

namespace UVE::Render {

bool IsValidParticleDrawCommand(const ParticleDrawCommand& command) noexcept {
    return std::isfinite(command.position.x) && std::isfinite(command.position.y) &&
           std::isfinite(command.position.z) && std::isfinite(command.remainingLifetimeSeconds) &&
           command.remainingLifetimeSeconds >= 0.0F && std::isfinite(command.sortDepth) &&
           command.sequence != 0U;
}

ParticleDrawRecording ParticleDrawRecorder::Record(const RenderQueue& queue,
                                                             const std::size_t maximumCommands) {
    ParticleDrawRecording recording;
    RecordInto(queue, maximumCommands, recording);
    return recording;
}

void ParticleDrawRecorder::RecordInto(const RenderQueue& queue, const std::size_t maximumCommands,
                                            ParticleDrawRecording& outRecording) {
    outRecording.sourceItemCount = queue.particleItems.size();
    outRecording.truncated = false;
    outRecording.commands.clear();
    if (maximumCommands == 0U) {
        outRecording.truncated = outRecording.sourceItemCount != 0U;
        return;
    }

    outRecording.commands.reserve(std::min(queue.particleItems.size(), maximumCommands));
    for (const ParticleRenderItem& item : queue.particleItems) {
        if (outRecording.commands.size() >= maximumCommands) {
            outRecording.truncated = true;
            break;
        }
        const ParticleDrawCommand command{
            item.entity, item.position, item.remainingLifetimeSeconds, item.sortDepth, item.sequence};
        if (!IsValidParticleDrawCommand(command)) {
            // RenderQueue is intentionally mutable/public, so defend the GPU boundary even when
            // the normal ParticleRenderBridge path has already validated its source snapshot.
            outRecording.truncated = true;
            continue;
        }
        outRecording.commands.push_back(command);
    }
    outRecording.truncated = outRecording.truncated || queue.particleItemsTruncated ||
                             outRecording.commands.size() < outRecording.sourceItemCount;
}

} // namespace UVE::Render
