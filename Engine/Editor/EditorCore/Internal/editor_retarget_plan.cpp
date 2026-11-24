// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_retarget_plan.h"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "animation_clip_asset.h"
#include "mesh_asset.h"
#include "model_animations.h"
#include "retarget_brain.h"
#include "retarget_conform.h"
#include "retarget_humanoid.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] Retarget::RetargetAnimationCheckUVE CheckFromSummaryUVE(const Asset::AnimationClipSummaryUVE& summary) {
    if (!summary.valid) {
        return {Retarget::RetargetAnimationStateUVE::Unreadable, "cannot read it"};
    }
    if (summary.conformed) {
        return {Retarget::RetargetAnimationStateUVE::AlreadyConformed, "already conformed"};
    }
    if (!summary.hasRest) {
        return {Retarget::RetargetAnimationStateUVE::NoSkeleton, "it has no skeleton, import the FBX again"};
    }
    if (!summary.hasBones) {
        return {Retarget::RetargetAnimationStateUVE::NoBones, "it moves no bones"};
    }
    return {Retarget::RetargetAnimationStateUVE::Ready, {}};
}

} // namespace

std::size_t RetargetPlanUVE::ReadyAnimationsUVE() const noexcept {
    return static_cast<std::size_t>(std::ranges::count(animations, Retarget::RetargetAnimationStateUVE::Ready,
                                                       &RetargetAnimationRowUVE::state));
}

const char* GetRetargetStatusLabelUVE(const Retarget::JointStatusUVE status) noexcept {
    return status == Retarget::JointStatusUVE::Missing ? "missing" : Retarget::JointStatusNameUVE(status);
}

std::array<float, 3> GetRetargetStatusColourUVE(const Retarget::JointStatusUVE status) noexcept {
    switch (status) {
        case Retarget::JointStatusUVE::Good: return {0.36F, 0.78F, 0.47F};
        case Retarget::JointStatusUVE::Warning: return {0.95F, 0.76F, 0.26F};
        case Retarget::JointStatusUVE::Broken: return {0.90F, 0.36F, 0.34F};
        case Retarget::JointStatusUVE::Missing: return {0.50F, 0.52F, 0.56F};
    }
    return {0.50F, 0.52F, 0.56F};
}

RetargetPlanUVE PlanRetargetUVE(const std::filesystem::path& modelFile,
                                const std::span<const std::filesystem::path> animations,
                                const RetargetPlanProgressUVE& progress,
                                const std::atomic<bool>* const cancel) {
    RetargetPlanUVE plan;
    plan.model = modelFile;
    const Retarget::HumanoidReferenceUVE& reference = Retarget::GetHumanoidReferenceUVE();
    const auto cancelled = [cancel]() noexcept {
        return cancel != nullptr && cancel->load(std::memory_order_relaxed);
    };

    // Single-read metadata cache per backing .uvmodel so planning 50+ embedded takes reads disk once.
    std::unordered_map<std::string, std::unordered_map<std::string, Asset::AnimationClipSummaryUVE>> modelSummaryCache;
    const auto loadModelSummaries = [&](const std::filesystem::path& model)
        -> const std::unordered_map<std::string, Asset::AnimationClipSummaryUVE>& {
        const std::string key = model.lexically_normal().generic_string();
        auto it = modelSummaryCache.find(key);
        if (it != modelSummaryCache.end()) {
            return it->second;
        }
        std::unordered_map<std::string, Asset::AnimationClipSummaryUVE> map;
        std::vector<std::pair<std::string, Asset::AnimationClipSummaryUVE>> summaries;
        if (Asset::InspectModelAnimationsSummaryUVE(model, summaries)) {
            map.reserve(summaries.size());
            for (auto& [takeName, summary] : summaries) {
                map.emplace(std::move(takeName), summary);
            }
        }
        return modelSummaryCache.emplace(key, std::move(map)).first->second;
    };

    // Gather requested animations + all takes embedded inside `modelFile`, deduplicated in O(N).
    std::vector<std::filesystem::path> allAnimations;
    allAnimations.reserve(animations.size());
    std::unordered_set<std::string> seenAnimations;
    seenAnimations.reserve(animations.size() + 16U);
    const auto addAnimationOnce = [&](const std::filesystem::path& path) {
        if (path.empty()) {
            return;
        }
        std::filesystem::path embeddedModel;
        std::string embeddedTake;
        const std::string key = Asset::SplitEmbeddedClipPathUVE(path, embeddedModel, embeddedTake)
                                    ? (embeddedModel.lexically_normal().generic_string() + "#" + embeddedTake)
                                    : path.lexically_normal().generic_string();
        if (seenAnimations.insert(key).second) {
            allAnimations.push_back(path);
        }
    };
    for (const std::filesystem::path& file : animations) {
        addAnimationOnce(file);
    }
    if (!modelFile.empty()) {
        std::vector<std::pair<std::string, Asset::AnimationClipSummaryUVE>> embedded;
        if (Asset::InspectModelAnimationsSummaryUVE(modelFile, embedded)) {
            const std::string key = modelFile.lexically_normal().generic_string();
            auto& map = modelSummaryCache[key];
            map.reserve(embedded.size());
            for (const auto& [takeName, summary] : embedded) {
                map.emplace(takeName, summary);
                addAnimationOnce(Asset::MakeEmbeddedClipPathUVE(modelFile, takeName));
            }
        }
    }

    const auto summaryOf = [&](const std::filesystem::path& file) -> Asset::AnimationClipSummaryUVE {
        std::filesystem::path embeddedModel;
        std::string embeddedTake;
        if (Asset::SplitEmbeddedClipPathUVE(file, embeddedModel, embeddedTake)) {
            const auto& map = loadModelSummaries(embeddedModel);
            const auto found = map.find(embeddedTake);
            return found == map.end() ? Asset::AnimationClipSummaryUVE{} : found->second;
        }
        return Asset::InspectAnimationClipFileSummaryUVE(file);
    };

    // Inspect animations via zero-DOM SAX summary so scanning 1,000 files skips keyframe parsing.
    plan.animations.reserve(allAnimations.size());
    std::filesystem::path firstRestClip;
    for (std::size_t i = 0U; i < allAnimations.size(); ++i) {
        if (cancelled()) {
            return plan;
        }
        const std::filesystem::path& file = allAnimations[i];
        if (progress && (i == 0U || (i & 15U) == 0U || i + 1U == allAnimations.size())) {
            progress(i, allAnimations.size(), Asset::AssetDisplayNameUVE(file));
        }
        const Asset::AnimationClipSummaryUVE summary = summaryOf(file);
        if (firstRestClip.empty() && summary.valid && summary.hasRest) {
            firstRestClip = file;
        }
        const Retarget::RetargetAnimationCheckUVE check = CheckFromSummaryUVE(summary);
        plan.animations.push_back(RetargetAnimationRowUVE{file, check.state, check.note});
    }
    if (cancelled()) {
        return plan;
    }

    std::optional<Retarget::RetargetSkeletonUVE> resolvedRig;
    if (!modelFile.empty()) {
        Asset::MeshAssetUVE mesh;
        std::error_code exists;
        if (!std::filesystem::is_regular_file(modelFile, exists) || !Asset::LoadMeshAssetUVE(modelFile, mesh)) {
            plan.modelError = "cannot read " + modelFile.filename().string();
        } else {
            std::string error;
            resolvedRig = Retarget::RigFromMeshUVE(mesh, &error);
            if (!resolvedRig.has_value()) {
                plan.modelError = std::move(error);
            }
        }
    } else {
        // Animation-only batch: read the full rest skeleton of ONLY the first clip that carries one.
        if (!firstRestClip.empty()) {
            Asset::AnimationClipAssetUVE clip;
            if (Asset::LoadAnimationClipAssetUVE(firstRestClip, clip) && !clip.rest.empty()) {
                Retarget::RetargetSkeletonUVE rig;
                rig.bones.reserve(clip.rest.size());
                for (const Asset::AnimationAssetRestBoneUVE& rest : clip.rest) {
                    Retarget::RetargetBoneUVE bone;
                    bone.name = rest.bone;
                    bone.parent = rest.parent;
                    bone.position = rest.position;
                    bone.rotation = rest.rotation;
                    bone.scale = rest.scale.x;
                    rig.bones.push_back(std::move(bone));
                }
                resolvedRig = std::move(rig);
            }
        }
        if (!resolvedRig.has_value()) {
            plan.modelError = "cannot read a skeleton from the selected files";
        }
    }

    if (resolvedRig.has_value()) {
        const Retarget::RetargetSkeletonUVE& rig = *resolvedRig;
        // Names first, and the Rig Brain once it is trained (it trains in the background).
        const Retarget::HumanoidMatchUVE match = Retarget::IsDefaultRigBrainReadyUVE()
                                                     ? Retarget::MatchHumanoidWithBrainUVE(rig, reference, Retarget::GetDefaultRigBrainUVE())
                                                     : Retarget::MatchHumanoidUVE(rig, reference);
        plan.usedBrain = Retarget::IsDefaultRigBrainReadyUVE();
        std::vector<std::string> names;
        names.reserve(rig.bones.size());
        for (const Retarget::RetargetBoneUVE& bone : rig.bones) {
            names.push_back(bone.name);
        }
        plan.modelReadable = true;
        plan.modelAlreadyConformed = !modelFile.empty() && Retarget::AreHumanoidNamesUVE(names, reference);
        plan.heightScale = match.heightScale;
        plan.counts = match.counts;
        std::vector<std::int32_t> depth(reference.skeleton.bones.size(), 0);
        for (std::size_t ref = 0U; ref < reference.skeleton.bones.size(); ++ref) {
            const std::int32_t parent = reference.skeleton.bones[ref].parent;
            depth[ref] = parent < 0 ? 0 : depth[static_cast<std::size_t>(parent)] + 1;
            RetargetJointRowUVE row;
            row.referenceBone = static_cast<std::int32_t>(ref);
            row.depth = depth[ref];
            row.status = match.joints[ref].status;
            row.reason = match.joints[ref].reason;
            row.optional = reference.info[ref].optional;
            row.byBrain = match.joints[ref].byBrain;
            row.confidence = match.joints[ref].confidence;
            if (row.status == Retarget::JointStatusUVE::Missing && !row.optional) {
                ++plan.missingCore;
            }
            if (match.joints[ref].bone >= 0) {
                row.characterName = rig.bones[static_cast<std::size_t>(match.joints[ref].bone)].name;
            }
            plan.joints.push_back(std::move(row));
        }
        for (std::size_t bone = 0U; bone < rig.bones.size(); ++bone) {
            if (match.referenceOfBone[bone] < 0) {
                plan.keptBones.push_back(rig.bones[bone].name);
            }
        }
        // What Generate will add: helpers it lacks, and body bones only when it lacks many.
        const Retarget::BodyBoneCountUVE body = Retarget::CountBodyBonesUVE(match, reference);
        plan.bonesToAdd = body.missingHelpers + (Retarget::IsRigVeryLackingUVE(match, reference) ? body.missing : 0U);
        plan.renameOnly = plan.bonesToAdd == 0U;
        const bool lacking = Retarget::IsRigVeryLackingUVE(match, reference);
        for (RetargetJointRowUVE& row : plan.joints) {
            if (row.status != Retarget::JointStatusUVE::Missing || row.optional || row.referenceBone < 0) {
                continue;
            }
            const Retarget::HumanoidBoneKindUVE kind = reference.info[static_cast<std::size_t>(row.referenceBone)].kind;
            const bool helper = kind == Retarget::HumanoidBoneKindUVE::Root || kind == Retarget::HumanoidBoneKindUVE::IK ||
                                kind == Retarget::HumanoidBoneKindUVE::Socket;
            row.willMake = helper || lacking;
        }
    }

    if (progress && !allAnimations.empty()) {
        progress(allAnimations.size(), allAnimations.size(), {});
    }
    return plan;
}

} // namespace UVE::Editor
