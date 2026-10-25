// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once
#include "uve/asset/texture_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/shader_asset_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/audio_asset_uve.h"
#include "uve/asset/animation_clip_asset_uve.h"
namespace UVE::Asset {
/// Count owned container capacities, including nested payloads. This is CPU asset residency,
/// not allocator metadata, GPU memory or buffers copied by downstream consumers.
template<class T> [[nodiscard]] std::size_t AssetCapacityBytesUVE(const std::vector<T>& v) {
    return v.capacity() * sizeof(T);
}
inline std::size_t AssetResidentBytesUVE(const TextureAssetUVE& v) {
    auto n = sizeof(v) + AssetCapacityBytesUVE(v.pixels) + AssetCapacityBytesUVE(v.basisKtx2Data)
        + AssetCapacityBytesUVE(v.mipLevels);
    for (const auto& m : v.mipLevels) n += AssetCapacityBytesUVE(m.pixels);
    return n;
}
inline std::size_t AssetResidentBytesUVE(const MeshAssetUVE& v) {
    auto n = sizeof(v) + AssetCapacityBytesUVE(v.vertices) + AssetCapacityBytesUVE(v.indices)
        + AssetCapacityBytesUVE(v.skinningInfluences) + AssetCapacityBytesUVE(v.joints)
        + AssetCapacityBytesUVE(v.materialGuids) + AssetCapacityBytesUVE(v.surfaces)
        + AssetCapacityBytesUVE(v.morphTargets) + AssetCapacityBytesUVE(v.defaultMorphWeights);
    for (const auto& j : v.joints) n += j.name.capacity() + 1;
    for (const auto& t : v.morphTargets) n += t.name.capacity() + 1 + AssetCapacityBytesUVE(t.deltas);
    return n;
}
inline std::size_t AssetResidentBytesUVE(const ShaderAssetUVE& v) {
    return sizeof(v) + v.sourceCode.capacity() + v.entryPointName.capacity() + 2;
}
inline std::size_t AssetResidentBytesUVE(const MaterialAssetUVE& v) { return sizeof(v); }
inline std::size_t AssetResidentBytesUVE(const AudioAssetUVE& v) {
    return sizeof(v) + AssetCapacityBytesUVE(v.samples);
}
inline std::size_t AssetResidentBytesUVE(const AnimationClipAssetUVE& v) {
    auto n = sizeof(v) + v.clipId.capacity() + 1 + AssetCapacityBytesUVE(v.samples)
        + AssetCapacityBytesUVE(v.events) + AssetCapacityBytesUVE(v.bones)
        + AssetCapacityBytesUVE(v.rest) + AssetCapacityBytesUVE(v.morphWeights);
    for (const auto& e : v.events) n += e.eventId.capacity() + 1;
    for (const auto& b : v.bones) n += b.bone.capacity() + 1 + AssetCapacityBytesUVE(b.samples);
    for (const auto& b : v.rest) n += b.bone.capacity() + 1;
    for (const auto& t : v.morphWeights) {
        n += t.node.capacity() + 1 + AssetCapacityBytesUVE(t.samples);
        for (const auto& s : t.samples) n += AssetCapacityBytesUVE(s.weights);
    }
    return n;
}
} // namespace UVE::Asset
