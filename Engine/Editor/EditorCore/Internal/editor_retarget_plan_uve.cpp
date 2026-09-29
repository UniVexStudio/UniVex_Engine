// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_retarget_plan_uve.h"

#include <algorithm>

#include "uve/asset/mesh_asset_uve.h"
#include "uve/retarget/retarget_conform_uve.h"
#include "uve/retarget/retarget_humanoid_uve.h"

namespace UVE::Editor {

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

RetargetPlanUVE PlanRetargetUVE(const std::filesystem::path& modelFile, const std::span<const std::filesystem::path> animations) {
    RetargetPlanUVE plan;
    plan.model = modelFile;
    const Retarget::HumanoidReferenceUVE& reference = Retarget::GetHumanoidReferenceUVE();

    Asset::MeshAssetUVE mesh;
    std::error_code exists;
    if (!std::filesystem::is_regular_file(modelFile, exists) || !Asset::LoadMeshAssetUVE(modelFile, mesh)) {
        plan.modelError = "cannot read " + modelFile.filename().string();
    } else if (std::string error; !Retarget::RigFromMeshUVE(mesh, &error).has_value()) {
        plan.modelError = error;
    } else {
        const Retarget::RetargetSkeletonUVE rig = *Retarget::RigFromMeshUVE(mesh);
        const Retarget::HumanoidMatchUVE match = Retarget::MatchHumanoidUVE(rig, reference);
        std::vector<std::string> names;
        for (const Retarget::RetargetBoneUVE& bone : rig.bones) {
            names.push_back(bone.name);
        }
        plan.modelReadable = true;
        plan.modelAlreadyConformed = Retarget::AreHumanoidNamesUVE(names, reference);
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
    }

    for (const std::filesystem::path& file : animations) {
        const Retarget::RetargetAnimationCheckUVE check = Retarget::CheckAnimationForRetargetUVE(file);
        plan.animations.push_back(RetargetAnimationRowUVE{file, check.state, check.note});
    }
    return plan;
}

} // namespace UVE::Editor
