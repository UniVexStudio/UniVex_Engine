// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// Optimize Animations: the project's clips rewritten with as few keys as keep every joint within a
// small error of the original, on a worker thread, each backed up first.
#include "editor.h"

#include "animation_clip_asset.h"
#include "animation_optimizer.h"
#include "i_project_file_index.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace UVE::Editor {

struct AnimationOptimizeJobUVE final {
    std::mutex mutex;
    std::deque<std::filesystem::path> queue;
    std::filesystem::path contentRoot;
    std::filesystem::path backupRoot;
    std::atomic<std::size_t> done{0U};
    std::atomic<std::size_t> total{0U};
    std::atomic<std::size_t> changed{0U};
    std::atomic<std::uint64_t> bytesBefore{0U};
    std::atomic<std::uint64_t> bytesAfter{0U};
    std::atomic<std::uint64_t> keysBefore{0U};
    std::atomic<std::uint64_t> keysAfter{0U};
    std::atomic<bool> cancel{false};
    std::atomic<bool> finished{false};
    std::thread thread;

    ~AnimationOptimizeJobUVE() {
        cancel = true;
        if (thread.joinable()) {
            thread.join();
        }
    }

    void RunUVE() {
        for (;;) {
            std::filesystem::path file;
            {
                const std::scoped_lock lock(mutex);
                if (queue.empty() || cancel) {
                    finished = true;
                    return;
                }
                file = std::move(queue.front());
                queue.pop_front();
            }
            OptimizeOneUVE(file);
            ++done;
        }
    }

    void OptimizeOneUVE(const std::filesystem::path& file) {
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(file, error);
        Asset::AnimationClipAssetUVE clip;
        if (error || !Asset::LoadAnimationClipAssetUVE(file, clip)) {
            return;
        }
        const Asset::AnimationOptimizeResultUVE result = Asset::OptimizeAnimationClipUVE(clip);
        if (!result.changed) {
            return;
        }
        // The original first, under the same place it had in Content.
        std::filesystem::path relative = std::filesystem::relative(file, contentRoot, error);
        if (error || relative.empty()) {
            relative = file.filename();
            error.clear();
        }
        const std::filesystem::path backup = backupRoot / relative;
        std::filesystem::create_directories(backup.parent_path(), error);
        if (!std::filesystem::exists(backup, error)) {
            std::filesystem::copy_file(file, backup, std::filesystem::copy_options::overwrite_existing, error);
        }
        if (error || !Asset::SaveAnimationClipAssetUVE(clip, file)) {
            return;
        }
        ++changed;
        bytesBefore += size;
        bytesAfter += std::filesystem::file_size(file, error);
        keysBefore += result.samplesBefore;
        keysAfter += result.samplesAfter;
    }
};

std::vector<std::filesystem::path> EditorUVE::CollectAnimationClipsUVE(const std::filesystem::path& relativeFolder) const {
    const Asset::ProjectFileSnapshotUVE snapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    std::vector<std::filesystem::path> clips;
    const std::string folder = relativeFolder.generic_string();
    for (const Asset::ProjectFileEntryUVE& entry : snapshot.entries) {
        if (entry.kind != Asset::ProjectFileEntryKindUVE::File || entry.relativePath.extension() != ".uvanim") {
            continue;
        }
        const std::string path = entry.relativePath.generic_string();
        if (folder.empty() || path.rfind(folder + "/", 0U) == 0U) {
            clips.push_back(snapshot.contentRoot / entry.relativePath);
        }
    }
    return clips;
}

void EditorUVE::StartOptimizeAnimationsUVE(std::vector<std::filesystem::path> absoluteClips) {
    if (absoluteClips.empty()) {
        return;
    }
    if (m_animationOptimizeJob && !m_animationOptimizeJob->finished) {
        // One is running: these join it.
        const std::scoped_lock lock(m_animationOptimizeJob->mutex);
        for (std::filesystem::path& clip : absoluteClips) {
            m_animationOptimizeJob->queue.push_back(std::move(clip));
        }
        m_animationOptimizeJob->total += absoluteClips.size();
        return;
    }
    auto job = std::make_shared<AnimationOptimizeJobUVE>();
    const Asset::ProjectFileSnapshotUVE snapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    job->contentRoot = snapshot.contentRoot;
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%lld",
                  static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()));
    job->backupRoot = snapshot.contentRoot.parent_path() / ".uvcache" / "animation-backup" / stamp;
    job->total = absoluteClips.size();
    job->queue.assign(absoluteClips.begin(), absoluteClips.end());
    job->thread = std::thread([raw = job.get()] { raw->RunUVE(); });
    m_animationOptimizeJob = std::move(job);
    m_contentStatusMessage = "Optimizing " + std::to_string(absoluteClips.size()) + " animations...";
}

void EditorUVE::CancelOptimizeAnimationsUVE() {
    if (m_animationOptimizeJob) {
        m_animationOptimizeJob->cancel = true;
    }
}

bool EditorUVE::IsOptimizingAnimationsUVE() const {
    return m_animationOptimizeJob && !m_animationOptimizeJob->finished;
}

void EditorUVE::PollOptimizeAnimationsUVE() {
    if (!m_animationOptimizeJob) {
        return;
    }
    AnimationOptimizeJobUVE& job = *m_animationOptimizeJob;
    char line[256];
    if (!job.finished) {
        std::snprintf(line, sizeof(line), "Optimizing animations: %zu / %zu (%zu made smaller)", job.done.load(), job.total.load(),
                      job.changed.load());
        m_contentStatusMessage = line;
        return;
    }
    if (job.thread.joinable()) {
        job.thread.join();
    }
    const double before = static_cast<double>(job.bytesBefore.load()) / (1024.0 * 1024.0);
    const double after = static_cast<double>(job.bytesAfter.load()) / (1024.0 * 1024.0);
    const double keys = job.keysBefore.load() > 0U ? 100.0 * (1.0 - static_cast<double>(job.keysAfter.load()) / static_cast<double>(job.keysBefore.load())) : 0.0;
    std::snprintf(line, sizeof(line), "%s: %zu of %zu animations made smaller, %.1f MB -> %.1f MB (%.0f%% fewer keys). Originals in .uvcache/animation-backup.",
                  job.cancel ? "Optimize stopped" : "Animations optimized", job.changed.load(), job.total.load(), before, after, keys);
    m_contentStatusMessage = line;
    m_animationOptimizeJob.reset();
}

} // namespace UVE::Editor
