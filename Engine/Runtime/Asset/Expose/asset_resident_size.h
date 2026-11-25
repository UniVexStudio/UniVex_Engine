// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include "texture_asset.h"
#include "mesh_asset.h"
#include "shader_asset.h"
#include "material_asset.h"
#include "audio_asset.h"
#include "animation_clip_asset.h"
namespace UVE::Asset {
/// Count owned container capacities, including nested payloads. This is CPU asset residency,
/// not allocator metadata, GPU memory or buffers copied by downstream consumers.
template<class T> [[nodiscard]] std::size_t AssetCapacityBytes(const std::vector<T>& v) {
    return v.capacity() * sizeof(T);
}
inline std::size_t AssetResidentBytes(const TextureAsset& v) {
    auto n = sizeof(v) + AssetCapacityBytes(v.pixels) + AssetCapacityBytes(v.basisKtx2Data)
        + AssetCapacityBytes(v.mipLevels);
    for (const auto& m : v.mipLevels) n += AssetCapacityBytes(m.pixels);
    return n;
}
inline std::size_t AssetResidentBytes(const MeshAsset& v) {
    auto n = sizeof(v) + AssetCapacityBytes(v.vertices) + AssetCapacityBytes(v.indices)
        + AssetCapacityBytes(v.skinningInfluences) + AssetCapacityBytes(v.joints)
        + AssetCapacityBytes(v.materialGuids) + AssetCapacityBytes(v.surfaces)
        + AssetCapacityBytes(v.morphTargets) + AssetCapacityBytes(v.defaultMorphWeights);
    for (const auto& j : v.joints) n += j.name.capacity() + 1;
    for (const auto& t : v.morphTargets) n += t.name.capacity() + 1 + AssetCapacityBytes(t.deltas);
    return n;
}
inline std::size_t AssetResidentBytes(const ShaderAsset& v) {
    return sizeof(v) + v.sourceCode.capacity() + v.entryPointName.capacity() + 2;
}
inline std::size_t AssetResidentBytes(const MaterialAsset& v) { return sizeof(v); }
inline std::size_t AssetResidentBytes(const AudioAsset& v) {
    return sizeof(v) + AssetCapacityBytes(v.samples);
}
inline std::size_t AssetResidentBytes(const AnimationClipAsset& v) {
    auto n = sizeof(v) + v.clipId.capacity() + 1 + AssetCapacityBytes(v.samples)
        + AssetCapacityBytes(v.events) + AssetCapacityBytes(v.bones)
        + AssetCapacityBytes(v.rest) + AssetCapacityBytes(v.morphWeights);
    for (const auto& e : v.events) n += e.eventId.capacity() + 1;
    for (const auto& b : v.bones) n += b.bone.capacity() + 1 + AssetCapacityBytes(b.samples);
    for (const auto& b : v.rest) n += b.bone.capacity() + 1;
    for (const auto& t : v.morphWeights) {
        n += t.node.capacity() + 1 + AssetCapacityBytes(t.samples);
        for (const auto& s : t.samples) n += AssetCapacityBytes(s.weights);
    }
    return n;
}
} // namespace UVE::Asset
