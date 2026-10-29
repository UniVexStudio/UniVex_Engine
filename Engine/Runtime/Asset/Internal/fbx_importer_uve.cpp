// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/fbx_importer_uve.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include "import_helpers_uve.h"

#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/skeleton_asset_uve.h"
#include "uve/asset/fbx_mesh_converter_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset {
namespace {

constexpr const char* kFbxImporterNameUVE = "FbxImporterUVE";
constexpr std::string_view kFbxTemporarySuffixUVE = ".uve_fbx_tmp";

/// Import policy per slot kind. Colour maps are sRGB colour data; a normal map is linear and carries
/// Normal usage; occlusion is linear data. Getting this wrong is the classic "normal maps look
/// inverted and albedo looks washed out" import bug, so it is decided here once, by slot, rather
/// than left to whatever the default happens to be.
[[nodiscard]] TextureImportSettingsUVE ColorSlotSettingsUVE() {
    TextureImportSettingsUVE settings;
    settings.colorSpace = TextureAssetColorSpaceUVE::Srgb;
    settings.usage = TextureUsageUVE::Color;
    return settings;
}

[[nodiscard]] TextureImportSettingsUVE NormalSlotSettingsUVE() {
    TextureImportSettingsUVE settings;
    settings.colorSpace = TextureAssetColorSpaceUVE::Linear;
    settings.usage = TextureUsageUVE::Normal;
    return settings;
}

[[nodiscard]] TextureImportSettingsUVE DataSlotSettingsUVE() {
    TextureImportSettingsUVE settings;
    settings.colorSpace = TextureAssetColorSpaceUVE::Linear;
    settings.usage = TextureUsageUVE::Data;
    return settings;
}

/// Turns one material slot into an asset identity, in the order of how much the file itself knows.
///
/// 1. The image the FBX **embeds**. This is the file's own copy of its texture and the only source
///    that cannot be missing, which is what makes a Mixamo "With Skin" download arrive coloured.
/// 2. An image **beside the model** matching the authored name, which is how every DCC tool that
///    references external maps exports.
/// 3. A `.uvtex` the project **already has** by stem, so a texture the author imported by hand is
///    not duplicated.
///
/// Nothing is invented: a slot that has none of the three stays empty and says exactly why. The
/// published `.uvtex` is named after the material's own stem plus the slot, so a re-import lands on
/// the same path - and therefore, through RegisterUVE, on the same GUID.
void PublishTextureSlotUVE(const std::filesystem::path& destinationPath, const std::filesystem::path& sourceDirectory,
                           const std::string_view modelStem, const FbxTextureSourceUVE& source,
                           const std::string& materialStem, const std::string_view materialName,
                           const std::string_view slotName, const TextureImportSettingsUVE& settings,
                           IAssetDatabaseUVE& assetDatabase, AssetGuidUVE& slot) {
    const std::filesystem::path texturePath =
        destinationPath.parent_path() / (materialStem + "_" + std::string{slotName} + ".uvtex");
    const auto publish = [&](const std::vector<std::byte>& bytes, const std::string_view describe) {
        if (!Detail::PublishEmbeddedTextureUVE(texturePath, bytes, settings, kFbxImporterNameUVE, describe)) {
            return false;
        }
        const AssetGuidUVE guid = assetDatabase.RegisterUVE(texturePath);
        if (guid == kInvalidAssetGuidUVE) {
            UVE_ERROR("FbxImporterUVE: asset database rejected texture \"{}\"", texturePath.string());
            return false;
        }
        slot = guid;
        return true;
    };

    if (source.HasImageUVE()) {
        if (publish(source.embedded, "the " + std::string{slotName} + " map embedded in the FBX")) {
            return;
        }
    } else if (!source.file.empty()) {
        const std::optional<std::filesystem::path> imageFile =
            Detail::ResolveAuthoredTextureFileUVE(sourceDirectory, modelStem, source.file);
        if (imageFile.has_value()) {
            std::vector<std::byte> bytes;
            if (Detail::ReadBoundedSourceBytesUVE(*imageFile, kFbxImporterNameUVE, kMaximumFbxEmbeddedTextureBytesUVE,
                                                  bytes) &&
                publish(bytes, imageFile->filename().string())) {
                return;
            }
        }
    }

    // Fall back to a `.uvtex` this project already holds, matched by stem. This is the path a
    // texture imported by hand through a material slot takes.
    slot = Detail::ResolveImportedSiblingTextureGuidUVE(destinationPath, source.file, assetDatabase);
    if (slot != kInvalidAssetGuidUVE) {
        return;
    }
    if (source.procedural) {
        UVE_WARNING("FbxImporterUVE: material \"{}\" uses a layered/procedural/unnamed texture for its {} map that "
                    "cannot be extracted; that slot stays empty",
                    materialName, slotName);
    } else if (!source.file.empty()) {
        UVE_WARNING("FbxImporterUVE: material \"{}\" references {} texture \"{}\", but the FBX does not embed it, no "
                    "file of that name sits beside \"{}\", and no .uvtex with that stem is registered; the slot stays "
                    "empty",
                    materialName, slotName, source.file, sourceDirectory.string());
    }
}

[[nodiscard]] bool ImportFbxSourceUVE(const std::filesystem::path& sourcePath,
                                      const std::filesystem::path& destinationPath,
                                      IAssetDatabaseUVE& assetDatabase,
                                      const AssetImportSettingsUVE& /*settings*/) {
    if (destinationPath.extension() != ".uvmodel") {
        UVE_ERROR("FbxImporterUVE: destination \"{}\" must use the .uvmodel extension",
                  destinationPath.string());
        return false;
    }

    std::vector<std::byte> sourceBytes;
    if (!Detail::ReadBoundedSourceBytesUVE(sourcePath, kFbxImporterNameUVE, kMaximumFbxMeshSourceBytesUVE,
                                           sourceBytes)) {
        return false;
    }

    MeshAssetUVE mesh;
    // The converter reports one range per material its faces use; which published slot each range
    // belongs to is known here, because only this importer knows which materials were publishable.
    std::vector<FbxSurfaceRangeUVE> surfaceRanges;
    if (!ConvertFbxMeshUVE(sourceBytes, mesh, &surfaceRanges)) {
        UVE_ERROR("FbxImporterUVE: source \"{}\" failed FBX mesh conversion", sourcePath.string());
        return false;
    }

    // Foundation WP7: publish every material as its own .uvmat beside the model and reference
    // them from the mesh. Materials are published BEFORE the mesh so a failure leaves no
    // .uvmodel pointing at materials that do not exist.
    const std::optional<std::vector<FbxMaterialUVE>> materials = ReadFbxMaterialsUVE(sourceBytes);
    if (!materials.has_value()) {
        UVE_ERROR("FbxImporterUVE: source \"{}\" failed FBX material conversion", sourcePath.string());
        return false;
    }
    const std::string modelStem = destinationPath.stem().string();
    const std::filesystem::path directory = destinationPath.parent_path();
    std::vector<std::string> usedStems;
    std::vector<AssetGuidUVE> materialGuids;
    materialGuids.reserve(materials->size());
    // File material position -> published slot. A material ReadFbxMaterialsUVE could not validate
    // is left out of the published list, so the range's file position cannot be used as a slot
    // directly; this is the one place both orders are known.
    std::unordered_map<std::uint32_t, std::uint32_t> slotOfSourceIndex;
    slotOfSourceIndex.reserve(materials->size());
    for (std::size_t materialIndex = 0U; materialIndex < materials->size(); ++materialIndex) {
        const FbxMaterialUVE& source = (*materials)[materialIndex];
        // The stem is needed before the materials are written now: the textures derived from this
        // material are published first and named after it, so a re-import finds them by the same
        // path and therefore the same GUID.
        const std::string stem = Detail::MakeMaterialFileStemUVE(modelStem, source.name, materialIndex, "material", usedStems);
        MaterialAssetUVE material = source.material;
        const std::filesystem::path sourceDirectory = sourcePath.parent_path();
        PublishTextureSlotUVE(destinationPath, sourceDirectory, sourcePath.stem().string(), source.albedo, stem,
                              source.name, "albedo", ColorSlotSettingsUVE(), assetDatabase, material.albedoTexture);
        PublishTextureSlotUVE(destinationPath, sourceDirectory, sourcePath.stem().string(), source.normal, stem,
                              source.name, "normal", NormalSlotSettingsUVE(), assetDatabase, material.normalTexture);
        PublishTextureSlotUVE(destinationPath, sourceDirectory, sourcePath.stem().string(), source.emissive, stem,
                              source.name, "emissive", ColorSlotSettingsUVE(), assetDatabase, material.emissiveTexture);
        PublishTextureSlotUVE(destinationPath, sourceDirectory, sourcePath.stem().string(), source.ambientOcclusion, stem,
                              source.name, "ambientOcclusion", DataSlotSettingsUVE(), assetDatabase, material.aoTexture);
        const std::filesystem::path materialPath = directory / (stem + ".uvmat");
        if (!Detail::PublishAssetAtomicallyUVE(materialPath, kFbxImporterNameUVE, kFbxTemporarySuffixUVE,
                                               [&material](const std::filesystem::path& temporaryPath) {
                                                   return SaveMaterialAssetUVE(material, temporaryPath);
                                               })) {
            return false;
        }
        const AssetGuidUVE guid = assetDatabase.RegisterUVE(materialPath);
        if (guid == kInvalidAssetGuidUVE) {
            UVE_ERROR("FbxImporterUVE: asset database rejected material \"{}\"", materialPath.string());
            return false;
        }
        slotOfSourceIndex.emplace(source.sourceIndex,
                                  static_cast<std::uint32_t>(materialGuids.size()));
        materialGuids.push_back(guid);
    }
    mesh.materialGuids = std::move(materialGuids);

    if (!surfaceRanges.empty() && !mesh.materialGuids.empty()) {
        mesh.surfaces.reserve(surfaceRanges.size());
        for (const FbxSurfaceRangeUVE& range : surfaceRanges) {
            MeshSurfaceUVE surface{range.firstIndex, range.indexCount, kInvalidMeshSurfaceMaterialSlotUVE};
            if (const auto found = slotOfSourceIndex.find(range.materialSourceIndex);
                found != slotOfSourceIndex.end()) {
                surface.materialSlot = found->second;
            }
            mesh.surfaces.push_back(surface);
        }
        if (!IsMeshSurfaceDataValidUVE(mesh)) {
            // The ranges come from the same index buffer the mesh holds, so this cannot happen
            // unless one of the two changed; refusing them keeps a broken table out of the .uvmodel.
            UVE_WARNING("FbxImporterUVE: the converted surface ranges do not describe the index buffer; "
                        "\"{}\" publishes without them",
                        destinationPath.string());
            mesh.surfaces.clear();
        }
    }

    if (!Detail::PublishAssetAtomicallyUVE(
            destinationPath, kFbxImporterNameUVE, kFbxTemporarySuffixUVE,
            [&mesh](const std::filesystem::path& temporaryPath) { return SaveMeshAssetUVE(mesh, temporaryPath); })) {
        return false;
    }

    // A rigged model also publishes its rig as its own `.uvskel` beside it - Unreal's split of a
    // Skeletal Mesh from its Skeleton, and for the same reason: one rig is shared by every clip
    // made for it and by every mesh skinned to it, instead of each consumer re-parsing the model
    // (a 30 MB FBX) to rediscover bones that were already read once here.
    //
    // Only a model that actually has a skin gets one: a static mesh has no rig to share, and a file
    // that is bones-only is an animation, which the editor imports down a different path.
    //
    // Failure here does not fail the import. The mesh is already on disk and is what the scene
    // draws; losing the shared skeleton costs a re-read, not the character.
    if (mesh.IsSkinnedUVE() && !mesh.joints.empty()) {
        // Read the rig back with the same cap, and the same truncating policy, the mesh was
        // converted with: that is what makes the two bone walks agree on which bones exist. A
        // `.uvskel` holding bones the mesh dropped would be a rig whose tracks point at nothing.
        static_assert(kMaximumSkeletonAssetJointsUVE >= kMaximumAnimationAssetBonesUVE,
                      "a published rig must be able to hold every bone a mesh can hold");
        std::size_t droppedBones = 0U;
        const std::optional<GltfSkeletonUVE> skeleton =
            ReadFbxSkeletonUVE(sourceBytes, kMaximumAnimationAssetBonesUVE, FbxBoneOverflowUVE::Truncate, &droppedBones);
        if (skeleton.has_value() && droppedBones > 0U) {
            UVE_WARNING("FbxImporterUVE: \"{}\" holds {} bones over the {} cap; the published .uvskel keeps the {} "
                        "the model kept",
                        sourcePath.string(), kMaximumAnimationAssetBonesUVE + droppedBones,
                        kMaximumAnimationAssetBonesUVE, skeleton->joints.size());
        }
        if (skeleton.has_value() && skeleton->joints.size() == mesh.joints.size()) {
            const std::filesystem::path skeletonPath = directory / (modelStem + ".uvskel");
            const SkeletonAssetUVE asset = MakeSkeletonAssetUVE(*skeleton, modelStem);
            if (Detail::PublishAssetAtomicallyUVE(
                    skeletonPath, kFbxImporterNameUVE, kFbxTemporarySuffixUVE,
                    [&asset](const std::filesystem::path& temporaryPath) {
                        return SaveSkeletonAssetUVE(asset, temporaryPath);
                    })) {
                if (assetDatabase.RegisterUVE(skeletonPath) == kInvalidAssetGuidUVE) {
                    UVE_WARNING("FbxImporterUVE: asset database rejected skeleton \"{}\"; the model keeps its own "
                                "joints and nothing else can share the rig",
                                skeletonPath.string());
                }
            }
        } else if (!skeleton.has_value()) {
            UVE_WARNING("FbxImporterUVE: \"{}\" holds a skin whose bones could not be read back; no .uvskel is "
                        "published, so nothing else can share the rig",
                        destinationPath.string());
        } else {
            UVE_WARNING("FbxImporterUVE: the rig read back for \"{}\" has {} bones but the model holds {} joints; "
                        "no .uvskel is published",
                        destinationPath.string(), skeleton->joints.size(), mesh.joints.size());
        }
    }

    // A file with several mesh nodes (a body and its joints, a head and its eyes) also publishes each one as
    // a model of its own, `<model>.part-<node>.uvmodel`, so the scene can place one mesh instance per node
    // the way Godot and Unity do, each with the node's own name and materials. The merged model above stays:
    // it is what the Retarget window and every older scene refer to. A part shares the merged model's joints
    // and bind poses, so it binds to the same skeleton by name. Parts never fail the import - without them
    // the merged model still draws the whole character.
    std::vector<FbxMeshPartUVE> parts;
    const bool partsKnown = ConvertFbxMeshPartsUVE(sourceBytes, parts);
    // The `.part-` file stem of each part, empty for a part that is not published on its own (the merged model
    // is then that part: a file with one mesh node has nothing to split).
    std::vector<std::string> publishedPartStems(parts.size());
    bool allPartsPublished = partsKnown;
    if (partsKnown && parts.size() >= 2U) {
        std::vector<std::string> usedPartStems;
        for (std::size_t partIndex = 0U; partIndex < parts.size(); ++partIndex) {
            FbxMeshPartUVE& part = parts[partIndex];
            MeshAssetUVE partMesh = std::move(part.mesh);
            if (!part.surfaces.empty() && !mesh.materialGuids.empty()) {
                partMesh.materialGuids = mesh.materialGuids;
                for (const FbxSurfaceRangeUVE& range : part.surfaces) {
                    MeshSurfaceUVE surface{range.firstIndex, range.indexCount, kInvalidMeshSurfaceMaterialSlotUVE};
                    if (const auto found = slotOfSourceIndex.find(range.materialSourceIndex);
                        found != slotOfSourceIndex.end()) {
                        surface.materialSlot = found->second;
                    }
                    partMesh.surfaces.push_back(surface);
                }
                if (!IsMeshSurfaceDataValidUVE(partMesh)) {
                    partMesh.surfaces.clear();
                }
            } else if (const auto found = slotOfSourceIndex.find(part.onlyMaterialSourceIndex);
                       found != slotOfSourceIndex.end() && found->second < mesh.materialGuids.size()) {
                // The one material the whole part draws with is its slot 0, where a mesh without a surface
                // table looks for it.
                partMesh.materialGuids = {mesh.materialGuids[found->second]};
            }
            std::string partStem;
            for (const char c : part.name) {
                partStem += (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-') ? c : '_';
            }
            if (partStem.empty()) {
                partStem = "Mesh";
            }
            const std::string uniqueStem = std::ranges::find(usedPartStems, partStem) == usedPartStems.end()
                                               ? partStem
                                               : partStem + "_" + std::to_string(partIndex);
            usedPartStems.push_back(uniqueStem);
            const std::filesystem::path partPath = directory / (modelStem + ".part-" + uniqueStem + ".uvmodel");
            if (!Detail::PublishAssetAtomicallyUVE(
                    partPath, kFbxImporterNameUVE, kFbxTemporarySuffixUVE,
                    [&partMesh](const std::filesystem::path& temporaryPath) { return SaveMeshAssetUVE(partMesh, temporaryPath); }) ||
                assetDatabase.RegisterUVE(partPath) == kInvalidAssetGuidUVE) {
                allPartsPublished = false;
                UVE_WARNING("FbxImporterUVE: mesh node \"{}\" of \"{}\" could not be published on its own; the \
merged model still draws it",
                            part.name, sourcePath.string());
            } else {
                publishedPartStems[partIndex] = uniqueStem;
            }
        }
    }
    // The file's own node tree (`Geo` > `Soldier_head`, `Soldier_body`), written beside the model so a scene is
    // built from what the DCC tool authored instead of from a shape the engine makes up. Written only when
    // every part it names exists; otherwise the stale one, if any, goes.
    const std::filesystem::path nodesPath = directory / (modelStem + ".uvnodes");
    std::vector<FbxNodeEntryUVE> nodes;
    if (allPartsPublished && ReadFbxNodeTreeUVE(sourceBytes, nodes) && !nodes.empty()) {
        nlohmann::json document;
        document["version"] = 1;
        nlohmann::json list = nlohmann::json::array();
        for (const FbxNodeEntryUVE& node : nodes) {
            nlohmann::json entry;
            entry["name"] = node.name;
            entry["parent"] = node.parent;
            entry["part"] = (node.part >= 0 && static_cast<std::size_t>(node.part) < publishedPartStems.size())
                                ? publishedPartStems[static_cast<std::size_t>(node.part)]
                                : std::string{};
            entry["mesh"] = node.part >= 0;
            list.push_back(std::move(entry));
        }
        document["nodes"] = std::move(list);
        const std::string text = document.dump(1);
        if (!Detail::PublishAssetAtomicallyUVE(nodesPath, kFbxImporterNameUVE, kFbxTemporarySuffixUVE,
                                               [&text](const std::filesystem::path& temporaryPath) {
                                                   std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
                                                   output.write(text.data(), static_cast<std::streamsize>(text.size()));
                                                   return output.good();
                                               })) {
            UVE_WARNING("FbxImporterUVE: the node tree of \"{}\" could not be written; scenes fall back to the mesh list",
                        sourcePath.string());
        }
    } else {
        std::error_code removeError;
        std::filesystem::remove(nodesPath, removeError);
    }
    return true;
}

} // namespace

void RegisterFbxImporterUVE(IAssetImporterUVE& importer) {
    importer.RegisterDatabaseAwareImporterUVE("fbx", &ImportFbxSourceUVE);
}

} // namespace UVE::Asset
