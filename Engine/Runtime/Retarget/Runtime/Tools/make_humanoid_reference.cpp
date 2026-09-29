// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// Builds the humanoid reference (Data/humanoid_reference.json) from a source rig: reads the rig's
// skeleton from an FBX the same way the importer does, then BuildHumanoidReferenceUVE renames it,
// straightens it to the A-pose and fills in each bone's role. The source FBX stays outside the
// repository; only the JSON it produces is kept.
//
//   uve_retarget_make_reference <source.fbx> <out.json> [--report]
//
// --report prints every bone before and after (world position and rotation, and its length to
// its parent), so a new reference can be checked number by number before it is committed.

#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "uve/asset/fbx_mesh_converter_uve.h"
#include "uve/retarget/retarget_humanoid_uve.h"

namespace {

void PrintReportUVE(const UVE::Retarget::RetargetSkeletonUVE& source, const UVE::Retarget::HumanoidReferenceUVE& reference) {
    using namespace UVE;
    const Retarget::RetargetSkeletonUVE folded = Retarget::FoldScaleUVE(source);
    const std::vector<Retarget::WorldTransformUVE> before = Retarget::ComputeWorldTransformsUVE(folded);
    const std::vector<Retarget::WorldTransformUVE> after = Retarget::ComputeWorldTransformsUVE(reference.skeleton);
    std::printf("name\tsource\tkind\tparent\tlength_before\tlength_after\t"
                "before_px\tbefore_py\tbefore_pz\tbefore_qx\tbefore_qy\tbefore_qz\tbefore_qw\t"
                "after_px\tafter_py\tafter_pz\tafter_qx\tafter_qy\tafter_qz\tafter_qw\n");
    for (std::size_t index = 0U; index < reference.skeleton.bones.size(); ++index) {
        const std::int32_t parent = reference.skeleton.bones[index].parent;
        const auto lengthOf = [parent, index](const std::vector<Retarget::WorldTransformUVE>& world) {
            return parent < 0 ? 0.0F
                              : Math::LengthUVE(world[index].position - world[static_cast<std::size_t>(parent)].position);
        };
        const Retarget::WorldTransformUVE& a = before[index];
        const Retarget::WorldTransformUVE& b = after[index];
        std::printf("%s\t%s\t%s\t%s\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%.5f\t%."
                    "5f\t%.5f\n",
                    reference.skeleton.bones[index].name.c_str(), source.bones[index].name.c_str(),
                    Retarget::HumanoidBoneKindNameUVE(reference.info[index].kind),
                    parent < 0 ? "-" : reference.skeleton.bones[static_cast<std::size_t>(parent)].name.c_str(),
                    static_cast<double>(lengthOf(before)), static_cast<double>(lengthOf(after)),
                    static_cast<double>(a.position.x), static_cast<double>(a.position.y),
                    static_cast<double>(a.position.z), static_cast<double>(a.rotation.x),
                    static_cast<double>(a.rotation.y), static_cast<double>(a.rotation.z),
                    static_cast<double>(a.rotation.w), static_cast<double>(b.position.x),
                    static_cast<double>(b.position.y), static_cast<double>(b.position.z),
                    static_cast<double>(b.rotation.x), static_cast<double>(b.rotation.y),
                    static_cast<double>(b.rotation.z), static_cast<double>(b.rotation.w));
    }
}

} // namespace

int main(const int argc, char** const argv) {
    const bool report = argc == 4 && std::string_view{argv[3]} == "--report";
    if (argc != 3 && !report) {
        std::fprintf(stderr, "usage: uve_retarget_make_reference <source.fbx> <out.json> [--report]\n");
        return 2;
    }
    std::ifstream input(argv[1], std::ios::binary);
    const std::vector<char> raw{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (raw.empty()) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    const std::span<const std::byte> bytes{reinterpret_cast<const std::byte*>(raw.data()), raw.size()};
    const std::optional<UVE::Asset::GltfSkeletonUVE> skeleton = UVE::Asset::ReadFbxSkeletonUVE(bytes, 256U);
    if (!skeleton.has_value()) {
        std::fprintf(stderr, "%s has no skeleton the importer can read\n", argv[1]);
        return 1;
    }
    UVE::Retarget::RetargetSkeletonUVE source;
    for (const UVE::Asset::GltfJointUVE& joint : skeleton->joints) {
        UVE::Retarget::RetargetBoneUVE bone;
        bone.name = joint.name;
        bone.parent = joint.parentIndex;
        bone.position = joint.translation;
        bone.rotation = joint.rotation;
        bone.scale = joint.scale.x; // rigs scale uniformly (a unit change)
        source.bones.push_back(bone);
    }
    std::string error;
    const std::optional<UVE::Retarget::HumanoidReferenceUVE> reference =
        UVE::Retarget::BuildHumanoidReferenceUVE(source, &error);
    if (!reference.has_value()) {
        std::fprintf(stderr, "cannot build the reference: %s\n", error.c_str());
        return 1;
    }
    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    output << UVE::Retarget::WriteHumanoidReferenceUVE(*reference) << '\n';
    if (!output) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    if (report) {
        PrintReportUVE(source, *reference);
    }
    std::fprintf(stderr, "%zu bones, hips %.3f m above the ground -> %s\n", reference->skeleton.bones.size(),
                 static_cast<double>(reference->hipsHeight), argv[2]);
    return 0;
}
