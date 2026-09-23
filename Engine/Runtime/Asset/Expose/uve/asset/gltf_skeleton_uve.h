// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Asset {

/// One bone of a glTF skin, in its rest pose relative to its parent bone.
struct GltfJointUVE final {
    std::string name;
    /// Index into the same joint list, always lower than this joint's own index; -1 for a root.
    std::int32_t parentIndex = -1;
    Math::Vector3UVE translation{};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE scale{1.0F, 1.0F, 1.0F};
};

/// The skeleton of the first skin in a glTF 2.0 file - what a DCC tool such as Blender exports as
/// an armature. Joints are ordered so every parent precedes its children, which lets a consumer
/// build world poses in one forward pass. Names are made unique ("Bone", "Bone.001" style is
/// preserved; a clash gets a numeric suffix) because bones are looked up by name.
struct GltfSkeletonUVE final {
    std::vector<GltfJointUVE> joints;
    /// Number of skins in the file; only the first is read.
    std::size_t skinCount = 0U;
};

/// Reads the skeleton from glTF JSON. Only node names, hierarchy and TRS (or a decomposed
/// `matrix`) are used, so no binary buffer is needed. Returns std::nullopt for malformed JSON, a
/// file with no skin, a joint list over `maximumJoints`, or a joint referencing a missing node.
[[nodiscard]] std::optional<GltfSkeletonUVE> ParseGltfSkeletonUVE(std::string_view json, std::size_t maximumJoints);

/// Reads the skeleton from a .gltf or .glb file. For a .glb only the header and JSON chunk are
/// read, never the binary payload.
[[nodiscard]] std::optional<GltfSkeletonUVE> ReadGltfSkeletonUVE(const std::filesystem::path& path,
                                                                 std::size_t maximumJoints);

} // namespace UVE::Asset
