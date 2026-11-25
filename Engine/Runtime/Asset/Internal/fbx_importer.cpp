// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "fbx_importer.h"

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
#include "import_helpers.h"

#include "material_asset.h"
#include "mesh_asset.h"
#include "skeleton_asset.h"
#include "fbx_mesh_converter.h"
#include "logging_macros.h"

namespace UVE::Asset {
namespace {

constexpr const char* kFbxImporterName = "FbxImporter";
constexpr std::string_view kFbxTemporarySuffix = ".uve_fbx_tmp";

/// Import policy per slot kind. Colour maps are sRGB colour data; a normal map is linear and carries
/// Normal usage; occlusion is linear data. Getting this wrong is the classic "normal maps look
/// inverted and albedo looks washed out" import bug, so it is decided here once, by slot, rather
/// than left to whatever the default happens to be.
[[nodiscard]] TextureImportSettings ColorSlotSettings(const std::uint32_t maxResolutionClamp) {
    TextureImportSettings settings;
    settings.colorSpace = TextureAssetColorSpace::Srgb;
    settings.usage = TextureUsage::Color;
    settings.maxResolutionClamp = maxResolutionClamp;
    return settings;
}

[[nodiscard]] TextureImportSettings NormalSlotSettings(const std::uint32_t maxResolutionClamp) {
    TextureImportSettings settings;
    settings.colorSpace = TextureAssetColorSpace::Linear;
    settings.usage = TextureUsage::Normal;
    settings.maxResolutionClamp = maxResolutionClamp;
    return settings;
}

[[nodiscard]] TextureImportSettings DataSlotSettings(const std::uint32_t maxResolutionClamp) {
    TextureImportSettings settings;
    settings.colorSpace = TextureAssetColorSpace::Linear;
    settings.usage = TextureUsage::Data;
    settings.maxResolutionClamp = maxResolutionClamp;
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
/// the same path - and therefore, through Register, on the same GUID.
void PublishTextureSlot(const std::filesystem::path& destinationPath, const std::filesystem::path& sourceDirectory,
                           const std::string_view modelStem, FbxTextureSource& source,
                           const std::string& materialStem, const std::string_view materialName,
                           const std::string_view slotName, const TextureImportSettings& settings,
                           IAssetDatabase& assetDatabase, AssetGuid& slot) {
    const std::filesystem::path texturePath =
        destinationPath.parent_path() / (materialStem + "_" + std::string{slotName} + ".uvtex");
    const auto publish = [&](const std::vector<std::byte>& bytes, const std::string_view describe) {
        if (!Detail::PublishEmbeddedTexture(texturePath, bytes, settings, kFbxImporterName, describe)) {
            return false;
        }
        const AssetGuid guid = assetDatabase.Register(texturePath);
        if (guid == kInvalidAssetGuid) {
            UVE_ERROR("FbxImporter: asset database rejected texture \"{}\"", texturePath.string());
            return false;
        }
        slot = guid;
        return true;
    };

    if (source.HasImage()) {
        const bool published = publish(source.embedded, "the " + std::string{slotName} + " map embedded in the FBX");
        // Release raw embedded bytes immediately after decoding to keep peak RAM low on mobile.
        source.embedded.clear();
        source.embedded.shrink_to_fit();
        if (published) {
            return;
        }
    } else if (!source.file.empty()) {
        const std::optional<std::filesystem::path> imageFile =
            Detail::ResolveAuthoredTextureFile(sourceDirectory, modelStem, source.file);
        if (imageFile.has_value()) {
            std::vector<std::byte> bytes;
            if (Detail::ReadBoundedSourceBytes(*imageFile, kFbxImporterName, kMaximumFbxEmbeddedTextureBytes,
                                                  bytes) &&
                publish(bytes, imageFile->filename().string())) {
                return;
            }
        }
    }

    // Fall back to a `.uvtex` this project already holds, matched by stem. This is the path a
    // texture imported by hand through a material slot takes.
    slot = Detail::ResolveImportedSiblingTextureGuid(destinationPath, source.file, assetDatabase);
    if (slot != kInvalidAssetGuid) {
        return;
    }
    if (source.procedural) {
        UVE_WARNING("FbxImporter: material \"{}\" uses a layered/procedural/unnamed texture for its {} map that "
                    "cannot be extracted; that slot stays empty",
                    materialName, slotName);
    } else if (!source.file.empty()) {
        UVE_WARNING("FbxImporter: material \"{}\" references {} texture \"{}\", but the FBX does not embed it, no "
                    "file of that name sits beside \"{}\", and no .uvtex with that stem is registered; the slot stays "
                    "empty",
                    materialName, slotName, source.file, sourceDirectory.string());
    }
}

[[nodiscard]] bool ImportFbxSource(const std::filesystem::path& sourcePath,
                                      const std::filesystem::path& destinationPath,
                                      IAssetDatabase& assetDatabase,
                                      const AssetImportSettings& settings) {
    if (destinationPath.extension() != ".uvmodel") {
        UVE_ERROR("FbxImporter: destination \"{}\" must use the .uvmodel extension",
                  destinationPath.string());
        return false;
    }

    std::uint32_t maxTextureResolution = 2048U;
    if (const auto* const modelSettings = dynamic_cast<const ModelImportSettings*>(&settings);
        modelSettings != nullptr) {
        maxTextureResolution = modelSettings->maxEmbeddedTextureResolution;
    }
    const TextureImportSettings colorSettings = ColorSlotSettings(maxTextureResolution);
    const TextureImportSettings normalSettings = NormalSlotSettings(maxTextureResolution);
    const TextureImportSettings dataSettings = DataSlotSettings(maxTextureResolution);

    std::vector<std::byte> sourceBytes;
    if (!Detail::ReadBoundedSourceBytes(sourcePath, kFbxImporterName, kMaximumFbxMeshSourceBytes,
                                           sourceBytes)) {
        return false;
    }

    // Single-pass extraction: parse the FBX once with ufbx and pull mesh, materials, skeleton,
    // per-node parts, and scene hierarchy from the same in-memory scene.
    FbxSceneImportBundle bundle;
    if (!ExtractFbxImportBundle(sourceBytes, bundle, kMaximumAnimationAssetBones)) {
        UVE_ERROR("FbxImporter: source \"{}\" failed FBX conversion", sourcePath.string());
        return false;
    }
    // Release raw FBX file bytes before decoding embedded textures to avoid doubling peak RAM.
    sourceBytes.clear();
    sourceBytes.shrink_to_fit();

    MeshAsset& mesh = bundle.mergedMesh;
    const std::vector<FbxSurfaceRange>& surfaceRanges = bundle.surfaceRanges;

    // Foundation WP7: publish every material as its own .uvmat beside the model and reference
    // them from the mesh. Materials are published BEFORE the mesh so a failure leaves no
    // .uvmodel pointing at materials that do not exist.
    if (!bundle.materials.has_value()) {
        UVE_ERROR("FbxImporter: source \"{}\" failed FBX material conversion", sourcePath.string());
        return false;
    }
    std::vector<FbxMaterial>& materials = *bundle.materials;
    const std::string modelStem = destinationPath.stem().string();
    const std::filesystem::path directory = destinationPath.parent_path();
    std::vector<std::string> usedStems;
    std::vector<AssetGuid> materialGuids;
    materialGuids.reserve(materials.size());
    // File material position -> published slot. A material ReadFbxMaterials could not validate
    // is left out of the published list, so the range's file position cannot be used as a slot
    // directly; this is the one place both orders are known.
    std::unordered_map<std::uint32_t, std::uint32_t> slotOfSourceIndex;
    slotOfSourceIndex.reserve(materials.size());
    const std::filesystem::path sourceDirectory = sourcePath.parent_path();
    const std::string sourceStem = sourcePath.stem().string();
    for (std::size_t materialIndex = 0U; materialIndex < materials.size(); ++materialIndex) {
        FbxMaterial& source = materials[materialIndex];
        // The stem is needed before the materials are written now: the textures derived from this
        // material are published first and named after it, so a re-import finds them by the same
        // path and therefore the same GUID.
        const std::string stem = Detail::MakeMaterialFileStem(modelStem, source.name, materialIndex, "material", usedStems);
        MaterialAsset material = source.material;
        PublishTextureSlot(destinationPath, sourceDirectory, sourceStem, source.albedo, stem,
                              source.name, "albedo", colorSettings, assetDatabase, material.albedoTexture);
        PublishTextureSlot(destinationPath, sourceDirectory, sourceStem, source.normal, stem,
                              source.name, "normal", normalSettings, assetDatabase, material.normalTexture);
        PublishTextureSlot(destinationPath, sourceDirectory, sourceStem, source.emissive, stem,
                              source.name, "emissive", colorSettings, assetDatabase, material.emissiveTexture);
        PublishTextureSlot(destinationPath, sourceDirectory, sourceStem, source.ambientOcclusion, stem,
                              source.name, "ambientOcclusion", dataSettings, assetDatabase, material.aoTexture);
        const std::filesystem::path materialPath = directory / (stem + ".uvmat");
        if (!Detail::PublishAssetAtomically(materialPath, kFbxImporterName, kFbxTemporarySuffix,
                                               [&material](const std::filesystem::path& temporaryPath) {
                                                   return SaveMaterialAsset(material, temporaryPath);
                                               })) {
            return false;
        }
        const AssetGuid guid = assetDatabase.Register(materialPath);
        if (guid == kInvalidAssetGuid) {
            UVE_ERROR("FbxImporter: asset database rejected material \"{}\"", materialPath.string());
            return false;
        }
        slotOfSourceIndex.emplace(source.sourceIndex,
                                  static_cast<std::uint32_t>(materialGuids.size()));
        materialGuids.push_back(guid);
    }
    mesh.materialGuids = std::move(materialGuids);

    if (!surfaceRanges.empty() && !mesh.materialGuids.empty()) {
        mesh.surfaces.reserve(surfaceRanges.size());
        for (const FbxSurfaceRange& range : surfaceRanges) {
            MeshSurface surface{range.firstIndex, range.indexCount, kInvalidMeshSurfaceMaterialSlot};
            if (const auto found = slotOfSourceIndex.find(range.materialSourceIndex);
                found != slotOfSourceIndex.end()) {
                surface.materialSlot = found->second;
            }
            mesh.surfaces.push_back(surface);
        }
        if (!IsMeshSurfaceDataValid(mesh)) {
            // The ranges come from the same index buffer the mesh holds, so this cannot happen
            // unless one of the two changed; refusing them keeps a broken table out of the .uvmodel.
            UVE_WARNING("FbxImporter: the converted surface ranges do not describe the index buffer; "
                        "\"{}\" publishes without them",
                        destinationPath.string());
            mesh.surfaces.clear();
        }
    }

    if (!Detail::PublishAssetAtomically(
            destinationPath, kFbxImporterName, kFbxTemporarySuffix,
            [&mesh](const std::filesystem::path& temporaryPath) { return SaveMeshAsset(mesh, temporaryPath); })) {
        return false;
    }

    // A rigged model also publishes its rig as its own `.uvskel` beside it - the standard split of a
    // Skeletal Mesh from its Skeleton, and for the same reason: one rig is shared by every clip
    // made for it and by every mesh skinned to it, instead of each consumer re-parsing the model
    // (a 30 MB FBX) to rediscover bones that were already read once here.
    //
    // Only a model that actually has a skin gets one: a static mesh has no rig to share, and a file
    // that is bones-only is an animation, which the editor imports down a different path.
    //
    // Failure here does not fail the import. The mesh is already on disk and is what the scene
    // draws; losing the shared skeleton costs a re-read, not the character.
    if (mesh.IsSkinned() && !mesh.joints.empty()) {
        // Read the rig back with the same cap, and the same truncating policy, the mesh was
        // converted with: that is what makes the two bone walks agree on which bones exist. A
        // `.uvskel` holding bones the mesh dropped would be a rig whose tracks point at nothing.
        static_assert(kMaximumSkeletonAssetJoints >= kMaximumAnimationAssetBones,
                      "a published rig must be able to hold every bone a mesh can hold");
        const std::optional<GltfSkeleton>& skeleton = bundle.skeleton;
        if (skeleton.has_value() && bundle.droppedBones > 0U) {
            UVE_WARNING("FbxImporter: \"{}\" holds {} bones over the {} cap; the published .uvskel keeps the {} "
                        "the model kept",
                        sourcePath.string(), kMaximumAnimationAssetBones + bundle.droppedBones,
                        kMaximumAnimationAssetBones, skeleton->joints.size());
        }
        if (skeleton.has_value() && skeleton->joints.size() == mesh.joints.size()) {
            const std::filesystem::path skeletonPath = directory / (modelStem + ".uvskel");
            const SkeletonAsset asset = MakeSkeletonAsset(*skeleton, modelStem);
            if (Detail::PublishAssetAtomically(
                    skeletonPath, kFbxImporterName, kFbxTemporarySuffix,
                    [&asset](const std::filesystem::path& temporaryPath) {
                        return SaveSkeletonAsset(asset, temporaryPath);
                    })) {
                if (assetDatabase.Register(skeletonPath) == kInvalidAssetGuid) {
                    UVE_WARNING("FbxImporter: asset database rejected skeleton \"{}\"; the model keeps its own "
                                "joints and nothing else can share the rig",
                                skeletonPath.string());
                }
            }
        } else if (!skeleton.has_value()) {
            UVE_WARNING("FbxImporter: \"{}\" holds a skin whose bones could not be read back; no .uvskel is "
                        "published, so nothing else can share the rig",
                        destinationPath.string());
        } else {
            UVE_WARNING("FbxImporter: the rig read back for \"{}\" has {} bones but the model holds {} joints; "
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
    std::vector<FbxMeshPart>& parts = bundle.parts;
    const bool partsKnown = bundle.partsKnown;
    // The `.part-` file stem of each part, empty for a part that is not published on its own (the merged model
    // is then that part: a file with one mesh node has nothing to split).
    std::vector<std::string> publishedPartStems(parts.size());
    bool allPartsPublished = partsKnown;
    if (partsKnown && parts.size() >= 2U) {
        std::vector<std::string> usedPartStems;
        for (std::size_t partIndex = 0U; partIndex < parts.size(); ++partIndex) {
            FbxMeshPart& part = parts[partIndex];
            MeshAsset partMesh = std::move(part.mesh);
            if (!part.surfaces.empty() && !mesh.materialGuids.empty()) {
                partMesh.materialGuids = mesh.materialGuids;
                for (const FbxSurfaceRange& range : part.surfaces) {
                    MeshSurface surface{range.firstIndex, range.indexCount, kInvalidMeshSurfaceMaterialSlot};
                    if (const auto found = slotOfSourceIndex.find(range.materialSourceIndex);
                        found != slotOfSourceIndex.end()) {
                        surface.materialSlot = found->second;
                    }
                    partMesh.surfaces.push_back(surface);
                }
                if (!IsMeshSurfaceDataValid(partMesh)) {
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
            if (!Detail::PublishAssetAtomically(
                    partPath, kFbxImporterName, kFbxTemporarySuffix,
                    [&partMesh](const std::filesystem::path& temporaryPath) { return SaveMeshAsset(partMesh, temporaryPath); }) ||
                assetDatabase.Register(partPath) == kInvalidAssetGuid) {
                allPartsPublished = false;
                UVE_WARNING("FbxImporter: mesh node \"{}\" of \"{}\" could not be published on its own; the \
merged model still draws it",
                            part.name, sourcePath.string());
            } else {
                publishedPartStems[partIndex] = uniqueStem;
            }
        }
    }
    // The file's own object hierarchy (`Geo` > `Soldier_head`, `Soldier_body`), written beside the model so a scene is
    // built from what the DCC tool authored instead of from a shape the engine makes up. Written only when
    // every part it names exists; otherwise the stale one, if any, goes.
    const std::filesystem::path hierarchyPath = directory / (modelStem + ".uvhierarchy");
    const std::filesystem::path legacyNodesPath = directory / (modelStem + ".uvnodes");
    std::error_code cleanError;
    std::filesystem::remove(legacyNodesPath, cleanError);
    if (allPartsPublished && bundle.hierarchyKnown && !bundle.hierarchy.empty()) {
        nlohmann::json document;
        document["version"] = 1;
        nlohmann::json list = nlohmann::json::array();
        for (const FbxHierarchyEntry& item : bundle.hierarchy) {
            nlohmann::json entry;
            entry["name"] = item.name;
            entry["parent"] = item.parent;
            entry["part"] = (item.part >= 0 && static_cast<std::size_t>(item.part) < publishedPartStems.size())
                                ? publishedPartStems[static_cast<std::size_t>(item.part)]
                                : std::string{};
            entry["mesh"] = item.part >= 0;
            list.push_back(std::move(entry));
        }
        document["objects"] = std::move(list);
        const std::string text = document.dump(1);
        if (!Detail::PublishAssetAtomically(hierarchyPath, kFbxImporterName, kFbxTemporarySuffix,
                                               [&text](const std::filesystem::path& temporaryPath) {
                                                   std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
                                                   output.write(text.data(), static_cast<std::streamsize>(text.size()));
                                                   return output.good();
                                               })) {
            UVE_WARNING("FbxImporter: the object hierarchy of \"{}\" could not be written; scenes fall back to the mesh list",
                        sourcePath.string());
        }
    } else {
        std::error_code removeError;
        std::filesystem::remove(hierarchyPath, removeError);
    }
    return true;
}

} // namespace

void RegisterFbxImporter(IAssetImporter& importer) {
    importer.RegisterDatabaseAwareImporter("fbx", &ImportFbxSource);
}

} // namespace UVE::Asset
