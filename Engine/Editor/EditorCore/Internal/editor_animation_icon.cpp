// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// An animation clip's icon: the skeleton the clip moves, caught half-way through, drawn from the
// front as one connected figure (the editor's bone look, flat); a clip that moves an object and
// no skeleton shows the path it takes. Drawn on the CPU with PRISM's canvas, kept per file and
// drawn again when the file changes.
#include "editor.h"

#include "animation_clip_asset.h"
#include "model_animations.h"
#include "matrix4x4.h"
#include "quaternion.h"
#include "retarget_humanoid.h"

#include "prism/Canvas.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>
#include <unordered_set>
#include <thread>
#include <mutex>
#include <fstream>
#include <cstdio>
#include <condition_variable>
#include <deque>

namespace UVE::Editor {
namespace {

/// Wide, the shape of a Content Browser tile's picture, so the figure fills it edge to edge.
constexpr int kIconWUVE = 152;
constexpr int kIconHUVE = 128;

/// The sample of `samples` nearest `time` (they are sorted by time).
[[nodiscard]] const Asset::AnimationAssetPoseUVE* PoseAtUVE(const std::vector<Asset::AnimationAssetSampleUVE>& samples, const double time) {
    if (samples.empty()) {
        return nullptr;
    }
    const auto after = std::lower_bound(samples.begin(), samples.end(), time,
                                        [](const Asset::AnimationAssetSampleUVE& s, const double t) { return s.timeSeconds < t; });
    if (after == samples.end()) {
        return &samples.back().pose;
    }
    if (after == samples.begin()) {
        return &after->pose;
    }
    const auto before = after - 1;
    return std::abs(before->timeSeconds - time) <= std::abs(after->timeSeconds - time) ? &before->pose : &after->pose;
}

/// A helper bone (root, IK target, socket): left out, so the figure reads as a body.
[[nodiscard]] bool IsHelperUVE(const std::string& name) {
    static const std::unordered_map<std::string, bool> helpers = [] {
        std::unordered_map<std::string, bool> out;
        const Retarget::HumanoidReferenceUVE& reference = Retarget::GetHumanoidReferenceUVE();
        for (std::size_t bone = 0U; bone < reference.info.size() && bone < reference.skeleton.bones.size(); ++bone) {
            const Retarget::HumanoidBoneKindUVE kind = reference.info[bone].kind;
            if (kind == Retarget::HumanoidBoneKindUVE::Root || kind == Retarget::HumanoidBoneKindUVE::IK ||
                kind == Retarget::HumanoidBoneKindUVE::Socket) {
                out.emplace(reference.skeleton.bones[bone].name, true);
            }
        }
        return out;
    }();
    return helpers.contains(name) || (name.size() > 2U && (name[0] == 'i' || name[0] == 'I') && (name[1] == 'k' || name[1] == 'K') &&
                                      (name[2] == '_' || std::isupper(static_cast<unsigned char>(name[2])) != 0));
}

struct Point2UVE {
    float x = 0.0F;
    float y = 0.0F;
};

/// Fits `points` into the icon (y up), leaving `margin`; returns the mapped points.
[[nodiscard]] std::vector<prism::Vec2> FitUVE(const std::vector<Point2UVE>& points, const float margin, const float bottomSpace) {
    float lowX = 1e30F, highX = -1e30F, lowY = 1e30F, highY = -1e30F;
    for (const Point2UVE& p : points) {
        lowX = std::min(lowX, p.x);
        highX = std::max(highX, p.x);
        lowY = std::min(lowY, p.y);
        highY = std::max(highY, p.y);
    }
    const float width = static_cast<float>(kIconWUVE);
    const float height = static_cast<float>(kIconHUVE);
    const float w = std::max(highX - lowX, 1e-5F);
    const float h = std::max(highY - lowY, 1e-5F);
    const float scale = std::min((width - 2.0F * margin) / w, (height - 2.0F * margin - bottomSpace) / h);
    const float cx = (lowX + highX) * 0.5F;
    const float cy = (lowY + highY) * 0.5F;
    std::vector<prism::Vec2> out;
    out.reserve(points.size());
    for (const Point2UVE& p : points) {
        out.push_back(prism::Vec2{width * 0.5F + (p.x - cx) * scale, (height - bottomSpace) * 0.5F - (p.y - cy) * scale});
    }
    return out;
}

/// Draws the icon of `clip` into `canvas` (cleared, kIconWUVE x kIconHUVE).
void DrawClipIconUVE(prism::Canvas& canvas, const Asset::AnimationClipAssetUVE& clip) {
    canvas.resize(kIconWUVE, kIconHUVE);
    // A deep backdrop of its own, so the icon reads the same on any tile.
    // One plain dark backdrop (no gradient): the figure reads the same on any tile.
    canvas.clear(prism::Color(18, 19, 23));
    const prism::Color line(110, 236, 196);
    const prism::Color joint(222, 255, 244);
    const prism::Color shade(8, 10, 14, 200);
    const prism::Color helper(96, 214, 255);
    constexpr float kBarUVE = -4.0F;   // no bar along the bottom: the figure takes the whole tile

    const double mid = clip.durationSeconds * 0.5;
    if (!clip.rest.empty()) {
        // The skeleton at mid-clip: each bone its track's pose there, else its rest.
        std::unordered_map<std::string, const Asset::AnimationAssetBoneTrackUVE*> tracks;
        for (const Asset::AnimationAssetBoneTrackUVE& track : clip.bones) {
            tracks.emplace(track.bone, &track);
        }
        const std::size_t count = clip.rest.size();
        std::vector<Math::Matrix4x4UVE> world(count, Math::Matrix4x4UVE::IdentityUVE());
        std::vector<Math::Vector3UVE> at(count);
        for (std::size_t bone = 0U; bone < count; ++bone) {
            const Asset::AnimationAssetRestBoneUVE& rest = clip.rest[bone];
            Math::Vector3UVE position = rest.position;
            Math::QuaternionUVE rotation = rest.rotation;
            Math::Vector3UVE scale = rest.scale;
            if (const auto found = tracks.find(rest.bone); found != tracks.end()) {
                if (const Asset::AnimationAssetPoseUVE* pose = PoseAtUVE(found->second->samples, mid)) {
                    position = pose->position;
                    rotation = pose->rotation;
                    scale = pose->scale;
                }
            }
            const Math::Matrix4x4UVE local = Math::Matrix4x4UVE::ComposeTrsUVE(position, rotation, scale);
            const std::int32_t parent = rest.parent;
            world[bone] = parent >= 0 && static_cast<std::size_t>(parent) < bone ? world[static_cast<std::size_t>(parent)] * local : local;
            at[bone] = Math::TransformPointUVE(world[bone], Math::Vector3UVE{});
        }
        // Seen from the front, standing up (a rig made standing on Z is stood up too).
        float lowY = 1e30F, highY = -1e30F, lowZ = 1e30F, highZ = -1e30F;
        for (std::size_t bone = 0U; bone < count; ++bone) {
            lowY = std::min(lowY, at[bone].y);
            highY = std::max(highY, at[bone].y);
            lowZ = std::min(lowZ, at[bone].z);
            highZ = std::max(highZ, at[bone].z);
        }
        const bool zUp = highZ - lowZ > (highY - lowY) * 1.2F;
        std::vector<Point2UVE> flat;
        std::vector<std::size_t> drawn;
        // Every bone, helpers and IK targets too (they are drawn their own way below).
        for (std::size_t bone = 0U; bone < count; ++bone) {
            flat.push_back(Point2UVE{at[bone].x, zUp ? at[bone].z : at[bone].y});
            drawn.push_back(bone);
        }
        if (!flat.empty()) {
            const std::vector<prism::Vec2> screen = FitUVE(flat, 6.0F, kBarUVE + 4.0F);
            static_cast<void>(joint);
            std::unordered_map<std::size_t, prism::Vec2> where;
            for (std::size_t i = 0U; i < drawn.size(); ++i) {
                where.emplace(drawn[i], screen[i]);
            }
            const auto drawnParent = [&](std::size_t bone) -> std::optional<prism::Vec2> {
                std::int32_t parent = clip.rest[bone].parent;
                while (parent >= 0) {
                    if (const auto found = where.find(static_cast<std::size_t>(parent)); found != where.end()) {
                        return found->second;
                    }
                    parent = clip.rest[static_cast<std::size_t>(parent)].parent;
                }
                return std::nullopt;
            };
            // A dark stroke under every bone, then the bone: it reads on any tile. Helpers and IK
            // targets hang off what they follow by a dotted line.
            for (const int pass : {0, 1}) {
                for (const std::size_t bone : drawn) {
                    const std::optional<prism::Vec2> from = drawnParent(bone);
                    if (!from.has_value()) {
                        continue;
                    }
                    if (IsHelperUVE(clip.rest[bone].bone)) {
                        if (pass == 1) {
                            const prism::Vec2 to = where[bone];
                            for (int dot = 1; dot < 8; dot += 2) {
                                const float a = static_cast<float>(dot) / 8.0F;
                                const float b = static_cast<float>(dot + 1) / 8.0F;
                                canvas.line(prism::Vec2{from->x + (to.x - from->x) * a, from->y + (to.y - from->y) * a},
                                            prism::Vec2{from->x + (to.x - from->x) * b, from->y + (to.y - from->y) * b}, helper, 1.0F);
                            }
                        }
                        continue;
                    }
                    canvas.line(*from, where[bone], pass == 0 ? shade : line, pass == 0 ? 4.2F : 2.2F);
                }
            }
            for (const std::size_t bone : drawn) {
                if (IsHelperUVE(clip.rest[bone].bone)) {
                    // A small ringed cross, as in the viewport.
                    const prism::Vec2 c = where[bone];
                    canvas.strokeCircle(c, 3.4F, shade, 2.4F);
                    canvas.strokeCircle(c, 3.4F, helper, 1.2F);
                    canvas.line(prism::Vec2{c.x - 5.0F, c.y}, prism::Vec2{c.x + 5.0F, c.y}, helper, 1.2F);
                    canvas.line(prism::Vec2{c.x, c.y - 5.0F}, prism::Vec2{c.x, c.y + 5.0F}, helper, 1.2F);
                    continue;
                }
                canvas.fillCircle(where[bone], 2.6F, shade);
                canvas.fillCircle(where[bone], 1.6F, joint);
            }
        }
        return;
    }
    // An object's motion: its path from above, start to end, with an arrowhead.
    if (clip.samples.size() >= 2U) {
        std::vector<Point2UVE> path;
        for (const Asset::AnimationAssetSampleUVE& sample : clip.samples) {
            path.push_back(Point2UVE{sample.pose.position.x, -sample.pose.position.z});
        }
        std::vector<prism::Vec2> screen = FitUVE(path, 10.0F, kBarUVE + 4.0F);
        for (std::size_t i = 1U; i < screen.size(); ++i) {
            canvas.line(screen[i - 1U], screen[i], shade, 4.2F);
        }
        for (std::size_t i = 1U; i < screen.size(); ++i) {
            canvas.line(screen[i - 1U], screen[i], line, 2.2F);
        }
        canvas.fillCircle(screen.front(), 3.5F, joint);
        canvas.fillCircle(screen.back(), 4.5F, line);
    }
}

} // namespace

namespace {

/// The icon of the clip at `path` as it is on disk now (empty pixels: not a clip).
[[nodiscard]] EditorUVE::AnimationIconUVE MakeIconUVE(const std::filesystem::path& path, const std::filesystem::file_time_type written) {
    EditorUVE::AnimationIconUVE icon;
    icon.written = written;
    Asset::AnimationClipAssetUVE clip;
    if (Asset::LoadAnimationClipAssetUVE(path, clip)) {
        prism::Canvas canvas;
        DrawClipIconUVE(canvas, clip);
        icon.width = kIconWUVE;
        icon.height = kIconHUVE;
        icon.pixels.assign(canvas.pixels(), canvas.pixels() + static_cast<std::size_t>(kIconWUVE) * static_cast<std::size_t>(kIconHUVE));
        icon.durationSeconds = clip.durationSeconds;
        std::size_t frames = 0U;
        for (const Asset::AnimationAssetBoneTrackUVE& track : clip.bones) {
            frames = std::max(frames, track.samples.size());
        }
        icon.frames = std::max(frames, clip.samples.size());
        icon.bones = clip.rest.empty() ? clip.bones.size() : clip.rest.size();
    }
    return icon;
}

// ---- the disk cache: <project>/.uvcache/animicons/<hash of the path>.bin
constexpr std::uint32_t kCacheMagicUVE = 0x55564149U;   // "UVAI"
constexpr std::uint32_t kCacheVersionUVE = 2U;

[[nodiscard]] std::filesystem::path CacheFileUVE(const std::filesystem::path& dir, const std::string& key) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const unsigned char c : key) {
        h = (h ^ c) * 1099511628211ULL;
    }
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.bin", static_cast<unsigned long long>(h));
    return dir / name;
}

[[nodiscard]] bool ReadCacheUVE(const std::filesystem::path& file, const std::filesystem::file_time_type written, EditorUVE::AnimationIconUVE& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    std::uint32_t magic = 0U, version = 0U;
    std::int64_t stamp = 0;
    std::int32_t w = 0, h = 0;
    double duration = 0.0;
    std::uint64_t frames = 0U, bones = 0U;
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    in.read(reinterpret_cast<char*>(&stamp), sizeof(stamp));
    in.read(reinterpret_cast<char*>(&w), sizeof(w));
    in.read(reinterpret_cast<char*>(&h), sizeof(h));
    in.read(reinterpret_cast<char*>(&duration), sizeof(duration));
    in.read(reinterpret_cast<char*>(&frames), sizeof(frames));
    in.read(reinterpret_cast<char*>(&bones), sizeof(bones));
    if (!in || magic != kCacheMagicUVE || version != kCacheVersionUVE || stamp != written.time_since_epoch().count() || w != kIconWUVE ||
        h != kIconHUVE) {
        return false;
    }
    out.pixels.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    in.read(reinterpret_cast<char*>(out.pixels.data()), static_cast<std::streamsize>(out.pixels.size() * sizeof(std::uint32_t)));
    if (!in) {
        out.pixels.clear();
        return false;
    }
    out.written = written;
    out.width = w;
    out.height = h;
    out.durationSeconds = duration;
    out.frames = static_cast<std::size_t>(frames);
    out.bones = static_cast<std::size_t>(bones);
    return true;
}

void WriteCacheUVE(const std::filesystem::path& file, const EditorUVE::AnimationIconUVE& icon) {
    if (icon.pixels.empty() || file.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    const std::filesystem::path temporary = file.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return;
        }
        const std::uint32_t magic = kCacheMagicUVE, version = kCacheVersionUVE;
        const std::int64_t stamp = icon.written.time_since_epoch().count();
        const std::int32_t w = icon.width, h = icon.height;
        const double duration = icon.durationSeconds;
        const std::uint64_t frames = icon.frames, bones = icon.bones;
        out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        out.write(reinterpret_cast<const char*>(&version), sizeof(version));
        out.write(reinterpret_cast<const char*>(&stamp), sizeof(stamp));
        out.write(reinterpret_cast<const char*>(&w), sizeof(w));
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));
        out.write(reinterpret_cast<const char*>(&duration), sizeof(duration));
        out.write(reinterpret_cast<const char*>(&frames), sizeof(frames));
        out.write(reinterpret_cast<const char*>(&bones), sizeof(bones));
        out.write(reinterpret_cast<const char*>(icon.pixels.data()), static_cast<std::streamsize>(icon.pixels.size() * sizeof(std::uint32_t)));
    }
    std::filesystem::rename(temporary, file, error);
}

} // namespace

/// One thread that reads and draws clips the views asked for, newest first, so the folder on
/// screen fills before one scrolled past; each icon also goes to the disk cache.
struct AnimationIconWorkerUVE final {
    struct JobUVE final {
        std::string key;
        std::filesystem::path path;
        std::filesystem::file_time_type written{};
        std::filesystem::path cacheFile;
    };
    std::mutex mutex;
    std::condition_variable wake;
    /// In the order they are drawn: a view's batch goes in ahead of what was asked before it, in the
    /// view's own order, so the folder on screen fills top-down first.
    std::deque<JobUVE> jobs;
    std::size_t insertAt = 0U;
    std::unordered_set<std::string> pending;
    std::vector<std::pair<std::string, EditorUVE::AnimationIconUVE>> done;
    bool stopping = false;
    std::thread thread;

    AnimationIconWorkerUVE() {
        thread = std::thread([this] { RunUVE(); });
    }
    ~AnimationIconWorkerUVE() {
        {
            const std::scoped_lock lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        if (thread.joinable()) {
            thread.join();
        }
    }
    AnimationIconWorkerUVE(const AnimationIconWorkerUVE&) = delete;
    AnimationIconWorkerUVE& operator=(const AnimationIconWorkerUVE&) = delete;

    void BeginBatchUVE() {
        const std::scoped_lock lock(mutex);
        insertAt = 0U;
    }

    void AskUVE(JobUVE job) {
        {
            const std::scoped_lock lock(mutex);
            if (!pending.insert(job.key).second) {
                // Asked again: moved up to this batch's place.
                const auto found = std::find_if(jobs.begin(), jobs.end(), [&job](const JobUVE& j) { return j.key == job.key; });
                if (found != jobs.end()) {
                    const auto index = static_cast<std::size_t>(found - jobs.begin());
                    if (index > insertAt) {
                        JobUVE moved = std::move(*found);
                        jobs.erase(found);
                        jobs.insert(jobs.begin() + static_cast<std::ptrdiff_t>(std::min(insertAt, jobs.size())), std::move(moved));
                    }
                    insertAt = std::min(insertAt + 1U, jobs.size());
                }
                return;
            }
            jobs.insert(jobs.begin() + static_cast<std::ptrdiff_t>(std::min(insertAt, jobs.size())), std::move(job));
            insertAt = std::min(insertAt + 1U, jobs.size());
        }
        wake.notify_one();
    }

    void RunUVE() {
        for (;;) {
            JobUVE job;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return stopping || !jobs.empty(); });
                if (stopping) {
                    return;
                }
                job = std::move(jobs.front());
                jobs.pop_front();
                insertAt = insertAt > 0U ? insertAt - 1U : 0U;
            }
            EditorUVE::AnimationIconUVE icon = MakeIconUVE(job.path, job.written);
            WriteCacheUVE(job.cacheFile, icon);
            const std::scoped_lock lock(mutex);
            pending.erase(job.key);
            done.emplace_back(job.key, std::move(icon));
        }
    }
};

namespace {
/// The file a clip's path is kept in: itself, or the model a take lives in.
[[nodiscard]] std::filesystem::path StoredInUVE(const std::filesystem::path& path) {
    std::filesystem::path model;
    std::string take;
    return Asset::SplitEmbeddedClipPathUVE(path, model, take) ? model : path;
}
} // namespace

const EditorUVE::AnimationIconUVE* EditorUVE::GetAnimationIconUVE(const std::filesystem::path& absolutePath) {
    std::error_code error;
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(StoredInUVE(absolutePath), error);
    if (error) {
        return nullptr;
    }
    if (!m_animationIconWorker) {
        m_animationIconWorker = std::make_shared<AnimationIconWorkerUVE>();
    }
    if (m_animationIconCacheDir.empty()) {
        m_animationIconCacheDir = m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot.parent_path() / ".uvcache" / "animicons";
    }
    // What the worker finished since last asked.
    {
        std::vector<std::pair<std::string, AnimationIconUVE>> finished;
        {
            const std::scoped_lock lock(m_animationIconWorker->mutex);
            finished.swap(m_animationIconWorker->done);
        }
        for (auto& [key, icon] : finished) {
            AnimationIconUVE& stored = m_animationIcons[key];
            const std::uint64_t serial = stored.serial + 1U;
            stored = std::move(icon);
            stored.serial = serial;
        }
        if (!finished.empty()) {
            ++m_animationIconGeneration;
        }
    }
    const std::string key = absolutePath.lexically_normal().generic_string();
    const auto found = m_animationIcons.find(key);
    if (found != m_animationIcons.end() && found->second.written == written) {
        return found->second.pixels.empty() ? nullptr : &found->second;
    }
    // From the disk cache, at once.
    const std::filesystem::path cacheFile = CacheFileUVE(m_animationIconCacheDir, key);
    AnimationIconUVE cached;
    if (ReadCacheUVE(cacheFile, written, cached)) {
        AnimationIconUVE& stored = m_animationIcons[key];
        const std::uint64_t serial = stored.serial + 1U;
        stored = std::move(cached);
        stored.serial = serial;
        return &stored;
    }
    m_animationIconWorker->AskUVE(AnimationIconWorkerUVE::JobUVE{key, absolutePath, written, cacheFile});
    // A changed file keeps its last picture until the new one is ready.
    return found != m_animationIcons.end() && !found->second.pixels.empty() ? &found->second : nullptr;
}

void EditorUVE::BeginAnimationIconBatchUVE() {
    if (m_animationIconWorker) {
        m_animationIconWorker->BeginBatchUVE();
    }
}

const EditorUVE::AnimationIconUVE* EditorUVE::GetAnimationIconNowUVE(const std::filesystem::path& absolutePath) {
    if (const AnimationIconUVE* icon = GetAnimationIconUVE(absolutePath); icon != nullptr) {
        std::error_code error;
        if (icon->written == std::filesystem::last_write_time(StoredInUVE(absolutePath), error)) {
            return icon;
        }
    }
    std::error_code error;
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(StoredInUVE(absolutePath), error);
    if (error) {
        return nullptr;
    }
    const std::string key = absolutePath.lexically_normal().generic_string();
    AnimationIconUVE& stored = m_animationIcons[key];
    const std::uint64_t serial = stored.serial + 1U;
    stored = MakeIconUVE(absolutePath, written);
    stored.serial = serial;
    WriteCacheUVE(CacheFileUVE(m_animationIconCacheDir, key), stored);
    return stored.pixels.empty() ? nullptr : &stored;
}

} // namespace UVE::Editor
