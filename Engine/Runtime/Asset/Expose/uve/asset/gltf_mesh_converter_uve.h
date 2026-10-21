// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "uve/asset/gltf_metadata_uve.h"
#include "uve/asset/mesh_asset_uve.h"

namespace UVE::Asset {

enum class GltfComponentTypeUVE : std::uint32_t {
    UnsignedByte = 5121U,
    UnsignedShort = 5123U,
    UnsignedInt = 5125U,
    Float = 5126U,
};

struct GltfAccessorViewUVE final {
    std::span<const std::byte> buffer;
    std::uint64_t byteOffset = 0U;
    std::uint64_t elementCount = 0U;
    std::uint64_t byteStride = 0U;
    GltfComponentTypeUVE componentType = GltfComponentTypeUVE::Float;
};

/// One glTF morph target (blend shape): the per-vertex displacements that turn the base mesh into
/// the shaped one. `positions` is required by the glTF spec and is what makes the target meaningful;
/// `normals` and `tangents` are optional streams that most exporters omit, and omitting one means
/// "this shape does not bend normals", not "unknown" - so an absent stream becomes zero deltas
/// rather than a failed import.
struct GltfMorphTargetSourceUVE final {
    GltfAccessorViewUVE positions;
    std::optional<GltfAccessorViewUVE> normals;
    std::optional<GltfAccessorViewUVE> tangents;
};

/// A primitive's blend shapes, kept out of GltfPrimitiveSourceUVE because they are additive data
/// rather than part of the geometry contract: a caller with no shapes (which is nearly all of them)
/// passes nothing and never has to spell out three empty members.
struct GltfMorphTargetSetUVE final {
    /// The primitive's morph targets in declaration order, which is the order a `weights` array
    /// indexes into.
    std::vector<GltfMorphTargetSourceUVE> targets;
    /// Display names, parallel to `targets`. glTF carries these out-of-band in
    /// `mesh.extras.targetNames`; an empty entry (or a short array) leaves the converter to assign
    /// its deterministic `targetN` fallback rather than producing an unnamed, unaddressable shape.
    std::vector<std::string> names;
    /// `mesh.weights`: the rest-pose weight of each target. Empty means the source declared none,
    /// which is different from declaring all zeroes.
    std::vector<float> defaultWeights;

    [[nodiscard]] bool IsEmptyUVE() const noexcept { return targets.empty(); }
};

struct GltfPrimitiveSourceUVE final {
    GltfAccessorViewUVE positions;
    std::optional<GltfAccessorViewUVE> normals;
    std::optional<GltfAccessorViewUVE> texcoords0;
    std::optional<GltfAccessorViewUVE> indices;
    std::uint32_t mode = 4U; // glTF TRIANGLES.
};

/// Converts one bounded caller-owned glTF TRIANGLES primitive into a failure-atomic MeshAssetUVE.
/// Positions, normals, and texcoords0 use float32 accessor views; indices may use unsigned byte,
/// unsigned short, or unsigned int views. Accessor spans are checked through the existing overflow-safe
/// validator. Missing normals receive deterministic face-normal accumulation, missing indices produce
/// sequential triangle indices, and local bounds are copied. The converter performs no JSON parsing,
/// URI/file resolution, material/image decoding, filesystem I/O, or GPU allocation.
[[nodiscard]] bool ConvertGltfPrimitiveUVE(const GltfPrimitiveSourceUVE& source, MeshAssetUVE& outMesh);

/// The same conversion with blend shapes attached. `morphTargets` must describe the same vertices
/// as `source.positions`: a target whose streams disagree with the base mesh is rejected rather
/// than padded, because a shape that moves the wrong vertex is worse than no shape at all.
[[nodiscard]] bool ConvertGltfPrimitiveUVE(const GltfPrimitiveSourceUVE& source,
                                           const GltfMorphTargetSetUVE& morphTargets,
                                           MeshAssetUVE& outMesh);

} // namespace UVE::Asset
