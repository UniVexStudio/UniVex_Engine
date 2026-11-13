// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// An animation clip's icon: the skeleton the clip moves, caught half-way through, drawn from the
// front as one connected figure (the editor's bone look, flat); a clip that moves an object and
// no skeleton shows the path it takes. Drawn on the CPU with PRISM's canvas, kept per file and
// drawn again when the file changes.
#include "editor.h"

#include "animation_clip_asset.h"
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

namespace UVE::Editor {
namespace {

constexpr int kIconSizeUVE = 128;

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
    const float size = static_cast<float>(kIconSizeUVE);
    const float w = std::max(highX - lowX, 1e-5F);
    const float h = std::max(highY - lowY, 1e-5F);
    const float scale = std::min((size - 2.0F * margin) / w, (size - 2.0F * margin - bottomSpace) / h);
    const float cx = (lowX + highX) * 0.5F;
    const float cy = (lowY + highY) * 0.5F;
    std::vector<prism::Vec2> out;
    out.reserve(points.size());
    for (const Point2UVE& p : points) {
        out.push_back(prism::Vec2{size * 0.5F + (p.x - cx) * scale, (size - bottomSpace) * 0.5F - (p.y - cy) * scale});
    }
    return out;
}

/// Draws the icon of `clip` into `canvas` (cleared, kIconSizeUVE square).
void DrawClipIconUVE(prism::Canvas& canvas, const Asset::AnimationClipAssetUVE& clip) {
    canvas.resize(kIconSizeUVE, kIconSizeUVE);
    // A deep backdrop of its own, so the icon reads the same on any tile.
    canvas.clear(prism::Color(20, 22, 27));
    canvas.gradientV(prism::Rect{0.0F, 0.0F, static_cast<float>(kIconSizeUVE), static_cast<float>(kIconSizeUVE)}, prism::Color(38, 44, 54),
                     prism::Color(18, 20, 25));
    const prism::Color line(110, 236, 196);
    const prism::Color joint(222, 255, 244);
    const prism::Color shade(8, 10, 14, 200);
    const float size = static_cast<float>(kIconSizeUVE);
    // Along the bottom: the clip's length, with a mark where the pose is taken.
    constexpr float kBarUVE = 12.0F;
    canvas.fillRoundRect(prism::Rect{10.0F, size - kBarUVE, size - 20.0F, 4.0F}, 2.0F, prism::Color(255, 255, 255, 40));
    canvas.fillRoundRect(prism::Rect{10.0F, size - kBarUVE, (size - 20.0F) * 0.5F, 4.0F}, 2.0F, prism::Color(110, 236, 196, 150));
    canvas.fillCircle(prism::Vec2{size * 0.5F, size - kBarUVE + 2.0F}, 3.5F, joint);

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
            if (IsHelperUVE(clip.rest[bone].bone)) {
                continue;
            }
            lowY = std::min(lowY, at[bone].y);
            highY = std::max(highY, at[bone].y);
            lowZ = std::min(lowZ, at[bone].z);
            highZ = std::max(highZ, at[bone].z);
        }
        const bool zUp = highZ - lowZ > (highY - lowY) * 1.2F;
        std::vector<Point2UVE> flat;
        std::vector<std::size_t> drawn;
        for (std::size_t bone = 0U; bone < count; ++bone) {
            if (!IsHelperUVE(clip.rest[bone].bone)) {
                flat.push_back(Point2UVE{at[bone].x, zUp ? at[bone].z : at[bone].y});
                drawn.push_back(bone);
            }
        }
        if (!flat.empty()) {
            const std::vector<prism::Vec2> screen = FitUVE(flat, 12.0F, kBarUVE + 4.0F);
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
            // A dark stroke under every bone, then the bone: it reads on any tile.
            for (const int pass : {0, 1}) {
                for (const std::size_t bone : drawn) {
                    if (const std::optional<prism::Vec2> from = drawnParent(bone)) {
                        canvas.line(*from, where[bone], pass == 0 ? shade : line, pass == 0 ? 4.2F : 2.2F);
                    }
                }
            }
            for (const std::size_t bone : drawn) {
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
        std::vector<prism::Vec2> screen = FitUVE(path, 18.0F, kBarUVE + 4.0F);
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

const EditorUVE::AnimationIconUVE* EditorUVE::GetAnimationIconUVE(const std::filesystem::path& absolutePath) {
    std::error_code error;
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(absolutePath, error);
    if (error) {
        return nullptr;
    }
    const std::string key = absolutePath.lexically_normal().generic_string();
    if (const auto found = m_animationIcons.find(key); found != m_animationIcons.end() && found->second.written == written) {
        return found->second.pixels.empty() ? nullptr : &found->second;
    }
    AnimationIconUVE icon;
    icon.written = written;
    Asset::AnimationClipAssetUVE clip;
    if (Asset::LoadAnimationClipAssetUVE(absolutePath, clip)) {
        prism::Canvas canvas;
        DrawClipIconUVE(canvas, clip);
        icon.size = kIconSizeUVE;
        icon.pixels.assign(canvas.pixels(), canvas.pixels() + static_cast<std::size_t>(kIconSizeUVE) * static_cast<std::size_t>(kIconSizeUVE));
        icon.durationSeconds = clip.durationSeconds;
        std::size_t frames = 0U;
        for (const Asset::AnimationAssetBoneTrackUVE& track : clip.bones) {
            frames = std::max(frames, track.samples.size());
        }
        icon.frames = std::max(frames, clip.samples.size());
        icon.bones = clip.rest.empty() ? clip.bones.size() : clip.rest.size();
    }
    AnimationIconUVE& stored = m_animationIcons[key];
    const std::uint64_t serial = stored.serial + 1U;
    stored = std::move(icon);
    stored.serial = serial;
    return stored.pixels.empty() ? nullptr : &stored;
}

} // namespace UVE::Editor
