// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include <vector>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/asset/gltf_skeleton_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"

namespace UVE::Asset {

inline constexpr std::size_t kMaximumFbxMeshSourceBytesUVE = 256U * 1024U * 1024U;
inline constexpr std::uint32_t kMaximumFbxMeshVerticesUVE = 1'000'000U;

/// Largest single image this reader will hand out of an FBX's own payload. The same 64 MiB ceiling
/// the PNG importer applies to a file on disk - a texture no importer would accept from disk must
/// not become acceptable just because it arrived inside a model.
inline constexpr std::size_t kMaximumFbxEmbeddedTextureBytesUVE = 64U * 1024U * 1024U;

/// What a reader does when a file holds more bones than its caller can take.
enum class FbxBoneOverflowUVE : std::uint8_t {
    /// Refuse: the caller cannot represent a partial rig, so a partial answer would be a lie.
    Fail = 0,
    /// Keep the first `maximumBones` and report how many were left out. The mesh converter's
    /// default, because a character that bends one finger wrong beats a character that does not
    /// import at all.
    Truncate,
};

/// One run of a converted FBX's index buffer that shares a material, in index-buffer order.
/// `materialSourceIndex` is the material's position in the file's own material list - the order
/// ReadFbxMaterialsUVE returns - not yet a slot in the mesh's `materialGuids`. The importer owns that
/// remap, because only it knows which materials were publishable.
struct FbxSurfaceRangeUVE final {
    std::uint32_t firstIndex = 0U;
    std::uint32_t indexCount = 0U;
    std::uint32_t materialSourceIndex = kInvalidMeshSurfaceMaterialSlotUVE;
};

/// Converts an FBX file (binary or ASCII, any version the parser reads) into one failure-atomic
/// static MeshAssetUVE.
///
/// Every mesh instance in the scene is merged, each placed where its object puts it, so a model
/// authored as several parts imports as the whole it looks like. The result is in the engine's
/// space: metres, +Y up, right-handed - an FBX from a centimetre, Z-up tool comes in at its real
/// size and standing up. Faces are triangulated, missing normals are generated, the first UV set
/// is kept, identical corners are shared and tangents are derived.
///
/// A rig with more bones than `maximumJoints` still converts: the first `maximumJoints` bones are
/// kept (depth-first order guarantees no kept bone's parent was left out), the rest are dropped with
/// one warning, weights that referred to them are skipped and the remaining weights renormalized,
/// and a rigid part that rode a dropped bone falls back to its nearest kept ancestor. A character
/// that bends one finger wrong beats a character that does not import.
///
/// A file with a skin keeps it: every bone becomes a joint (the bones, order and names
/// ReadFbxSkeletonUVE gives, so a skeleton's pose and a clip's tracks line up by name), each
/// joint's inverse bind comes from its cluster, and every vertex keeps its four strongest weights,
/// normalized. Skinned vertices sit where the skin puts them in bind pose; a part with no skin
/// rides the nearest bone above it. Materials, textures, cameras, lights and animation are not
/// converted here (ReadFbxAnimationsUVE reads the takes). Embedded and external files are never
/// read. Returns false and leaves `outMesh` untouched when the bytes do not parse,
/// hold no triangles, exceed kMaximumFbxMeshVerticesUVE or produce a non-finite value.
///
/// `outSurfaces`, when non-null, receives one range per material the file's faces actually use,
/// with the triangles reordered so each material is one contiguous run. A file whose faces all use
/// the same material - or none at all - gets an EMPTY list: that is what having no surface table
/// already means, and the bytes stay identical to what earlier versions wrote. A file with more
/// distinct materials than kMaximumMeshSurfacesUVE also gets an empty list.
[[nodiscard]] bool ConvertFbxMeshUVE(std::span<const std::byte> source, MeshAssetUVE& outMesh,
                                     std::vector<FbxSurfaceRangeUVE>* outSurfaces = nullptr,
                                     std::size_t maximumJoints = kMaximumAnimationAssetBonesUVE);

/// One mesh node of an FBX, converted on its own: the unit a DCC tool's outliner shows and another engine
/// imports as one mesh instance (Godot's MeshInstance3D, Unity's SkinnedMeshRenderer). `mesh` carries the
/// same joint list and bind poses as ConvertFbxMeshUVE's merged mesh, so it binds to the same skeleton by
/// name; `surfaces` are its material runs, in the form ConvertFbxMeshUVE reports them.
struct FbxMeshPartUVE final {
    std::string name;
    MeshAssetUVE mesh;
    std::vector<FbxSurfaceRangeUVE> surfaces;
    /// The file material the whole part draws with, when `surfaces` is empty because it uses just one;
    /// kInvalidMeshSurfaceMaterialSlotUVE when it uses none, or several (then `surfaces` names them).
    std::uint32_t onlyMaterialSourceIndex = kInvalidMeshSurfaceMaterialSlotUVE;
};

/// Converts every mesh of the file into a part of its own, in the scene's order, instead of merging them.
/// False (and `outParts` empty) when the file cannot be read or any part cannot be converted.
[[nodiscard]] bool ConvertFbxMeshPartsUVE(std::span<const std::byte> source, std::vector<FbxMeshPartUVE>& outParts,
                                          std::size_t maximumJoints = kMaximumAnimationAssetBonesUVE);

/// What an FBX holds, read without its geometry or animation curves - enough to tell a model from
/// an animation-only file (a skeleton and its curves, the common shape of exported motion) before
/// anything is imported.
struct FbxSourceSummaryUVE final {
    std::size_t meshCount = 0U;
    std::size_t boneCount = 0U;
    std::size_t animationCount = 0U;
    /// The longest animation's length; 0 when there are none.
    double longestAnimationSeconds = 0.0;
    /// A mesh is skinned to bones, which is what makes a model a rigged Model rather than a Mesh.
    bool hasSkin = false;

    /// No mesh to show, but motion to play: the file is an animation, not a model.
    [[nodiscard]] bool IsAnimationOnlyUVE() const noexcept { return meshCount == 0U && animationCount > 0U; }
};

/// Summarizes the FBX in `source`; empty when the bytes do not parse as FBX.
[[nodiscard]] std::optional<FbxSourceSummaryUVE> DescribeFbxSourceUVE(std::span<const std::byte> source);

/// Reads an FBX's bones - names, hierarchy and rest pose - into the same joint list a glTF skin
/// gives (GltfSkeletonUVE is the engine's skeleton-source shape, whatever file it came from).
///
/// Every bone object counts, skinned or not, so an animation-only file (bones and takes, no mesh)
/// has a skeleton too. Poses are in the engine's space, metres and +Y up; parents precede their
/// children; a bone whose nearest bone ancestor is none is a root. Names are made unique the way
/// the glTF reader does it. Returns std::nullopt when the bytes do not parse or hold no bones, and,
/// when `overflow` is Fail, when the file holds more than `maximumJoints` bones.
///
/// `outDroppedBones`, when non-null, receives how many bones the cap excluded - zero for a rig that
/// fit. Pass `FbxBoneOverflowUVE::Truncate` to read the same joint set ConvertFbxMeshUVE keeps for a
/// wide rig, which is what a skeleton published beside a model has to be: a shared rig holding
/// bones the mesh dropped would be a rig whose tracks point at nothing.
[[nodiscard]] std::optional<GltfSkeletonUVE> ReadFbxSkeletonUVE(
    std::span<const std::byte> source, std::size_t maximumJoints, FbxBoneOverflowUVE overflow = FbxBoneOverflowUVE::Fail,
    std::size_t* outDroppedBones = nullptr);

/// Reads every take (animation stack) as one skeletal clip: a track per bone of ReadFbxSkeletonUVE's
/// skeleton - same names, same local space (metres, +Y up, relative to the parent bone) - sampled
/// once a frame at the file's frame rate (30 when it has none), fewer times when a take is longer
/// than kMaximumAnimationAssetSamplesUVE frames. A bone that never moves keeps a single sample.
/// The clip id is the take's name ("Armature|Run" becomes "Run"), made unique. Takes that are
/// empty or produce a non-finite pose are skipped; no bones, or bytes that do not parse, give none.
[[nodiscard]] std::vector<AnimationClipAssetUVE> ReadFbxAnimationsUVE(std::span<const std::byte> source,
                                                                      std::size_t maximumBones);

/// One FBX material's slot texture, wherever the file keeps it.
///
/// An FBX can supply a map three ways, and which one it is decides what the importer can do with
/// it: the image can be *inside* the file (a Mixamo "With Skin" download embeds its diffuse map
/// exactly this way), the file can merely name a path on the machine that exported it, or the map
/// can be a layered / procedural / shader graph with no image behind it at all. This reader opens
/// nothing and decodes nothing: it reports which of the three it found and hands over the bytes when
/// there are any, so the import pipeline - which owns the asset database - can publish them.
struct FbxTextureSourceUVE final {
    /// The file the FBX names, as authored. Empty when the map names no file.
    std::string file;

    /// The image itself, byte for byte as the file carries it (PNG or JPEG). Empty when the FBX does
    /// not embed it. Capped at kMaximumFbxEmbeddedTextureBytesUVE; a larger payload is reported and
    /// dropped rather than copied into memory, because one broken texture must not decide whether a
    /// character imports.
    std::vector<std::byte> embedded;

    /// The filename the embedded image had inside the file, when it had one. Only used to name the
    /// derived texture when the material itself gives nothing to build a name from.
    std::string embeddedName;

    /// The map is a layered / procedural / shader texture, or a file texture the FBX does not name
    /// and does not embed. Nothing can be extracted from it, so its slot stays empty.
    bool procedural = false;

    /// The image bytes are present, which is the only case the importer can turn into a texture
    /// without touching the filesystem.
    [[nodiscard]] bool HasImageUVE() const noexcept { return !embedded.empty(); }
};

/// One FBX material: the engine's PBR material type plus where each of its maps' images live.

struct FbxMaterialUVE final {
    std::string name;

    /// This material's position in the file's own material list. ReadFbxMaterialsUVE skips a
    /// material it cannot validate, so this is what ties a surface range back to the slot the
    /// material was published in - the position in THIS vector would shift after a skip.
    std::uint32_t sourceIndex = 0U;

    /// Albedo, emission, metalness, roughness and transparency mapped from FBX's PBR values
    /// (ufbx derives them from the classic properties when a material is not authored as PBR).
    /// Texture GUID slots stay invalid on purpose: this reader never opens an external file, and
    /// turning bytes or a path into an asset identity belongs to the import pipeline, which owns
    /// the asset database - not to a converter that has to stay a pure function of its input.
    MaterialAssetUVE material;

    /// Where each map's image comes from. Empty when the map is unconnected or disabled.
    FbxTextureSourceUVE albedo;
    FbxTextureSourceUVE normal;
    FbxTextureSourceUVE emissive;
    FbxTextureSourceUVE ambientOcclusion;

    /// `albedoClamped` records that a channel had to be clamped into `MaterialAssetUVE`'s validated
    /// [0,1] range rather than hiding it.
    bool albedoClamped = false;
};

/// Reads every material in the file, in file order. `std::nullopt` when the bytes do not parse or
/// exceed kMaximumFbxMeshSourceBytesUVE; an empty vector means the file has no materials at all.
[[nodiscard]] std::optional<std::vector<FbxMaterialUVE>> ReadFbxMaterialsUVE(std::span<const std::byte> source);

} // namespace UVE::Asset
