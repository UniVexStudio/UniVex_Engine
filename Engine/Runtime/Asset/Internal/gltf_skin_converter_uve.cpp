// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/gltf_skin_converter_uve.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "uve/asset/gltf_metadata_uve.h"

#include "gltf_document_uve.h"

namespace UVE::Asset {
namespace {

[[nodiscard]] bool ReadSkinJointNodesUVE(const nlohmann::json& skin, std::vector<std::size_t>& outNodes) {
    const auto jointsIt = skin.find("joints");
    if (jointsIt == skin.end() || !jointsIt->is_array() || jointsIt->empty() ||
        jointsIt->size() > kMaximumSkeletonAssetJointsUVE) {
        return false;
    }
    outNodes.clear();
    outNodes.reserve(jointsIt->size());
    for (const nlohmann::json& entry : *jointsIt) {
        if (!entry.is_number_unsigned()) {
            return false;
        }
        outNodes.push_back(entry.get<std::size_t>());
    }
    return true;
}

[[nodiscard]] std::optional<std::size_t> FindAttributeIndexUVE(const nlohmann::json& attributes,
                                                               const char* name) {
    const auto it = attributes.find(name);
    if (it == attributes.end() || !it->is_number_unsigned()) {
        return std::nullopt;
    }
    return it->get<std::size_t>();
}

} // namespace

bool ConvertGltfSkinUVE(const GltfSkinSourceUVE& source, GltfSkinResultUVE& outSkin) {
    const std::size_t jointCount = source.skeleton.joints.size();
    if (jointCount == 0U || jointCount > kMaximumSkeletonAssetJointsUVE) {
        return false;
    }
    if (source.skinJointNodes.size() != jointCount) {
        return false;
    }
    if (source.vertexCount == 0U || source.vertexCount > kMaximumGltfAccessorElementsUVE) {
        return false;
    }

    // Document node index -> skeleton-order position, then the skin-order mapping. The values
    // must cover every joint exactly once: a duplicate skin node would leave one joint without
    // an inverse bind matrix and give another two.
    std::unordered_map<std::size_t, std::uint32_t> nodeToJoint;
    nodeToJoint.reserve(jointCount * 2U);
    for (std::size_t i = 0; i < jointCount; ++i) {
        nodeToJoint.emplace(source.skeleton.joints[i].sourceNodeIndex, static_cast<std::uint32_t>(i));
    }
    if (nodeToJoint.size() != jointCount) {
        return false;
    }
    std::vector<std::uint32_t> skinOrderToSkeleton;
    skinOrderToSkeleton.reserve(jointCount);
    std::vector<bool> jointSeen(jointCount, false);
    for (const std::size_t node : source.skinJointNodes) {
        const auto it = nodeToJoint.find(node);
        if (it == nodeToJoint.end() || jointSeen[it->second]) {
            return false;
        }
        jointSeen[it->second] = true;
        skinOrderToSkeleton.push_back(it->second);
    }

    if (source.joints0.componentType == GltfComponentTypeUVE::Float) {
        return false;
    }
    const std::uint64_t jointElementSize =
        Detail::GltfComponentSizeUVE(source.joints0.componentType) * kMaxJointInfluencesUVE;
    if (!Detail::ValidateGltfAccessorViewUVE(source.joints0, jointElementSize,
                                             kMaximumGltfAccessorElementsUVE) ||
        source.joints0.elementCount != source.vertexCount) {
        return false;
    }
    if (source.weights0.componentType != GltfComponentTypeUVE::Float ||
        !Detail::ValidateGltfAccessorViewUVE(source.weights0, 16U, kMaximumGltfAccessorElementsUVE) ||
        source.weights0.elementCount != source.vertexCount) {
        return false;
    }
    if (source.inverseBindMatrices.has_value()) {
        const GltfAccessorViewUVE& ibm = *source.inverseBindMatrices;
        if (ibm.componentType != GltfComponentTypeUVE::Float ||
            !Detail::ValidateGltfAccessorViewUVE(ibm, 64U, kMaximumSkeletonAssetJointsUVE) ||
            ibm.elementCount != static_cast<std::uint64_t>(jointCount)) {
            return false;
        }
    }

    std::vector<MeshSkinningInfluenceUVE> influences(static_cast<std::size_t>(source.vertexCount));
    for (std::uint64_t vertex = 0U; vertex < source.vertexCount; ++vertex) {
        const std::size_t index = static_cast<std::size_t>(vertex);
        const std::byte* const jointBytes =
            Detail::GltfAccessorElementUVE(source.joints0, index, jointElementSize);
        const std::byte* const weightBytes =
            Detail::GltfAccessorElementUVE(source.weights0, index, 16U);
        if (jointBytes == nullptr || weightBytes == nullptr) {
            return false;
        }
        MeshSkinningInfluenceUVE influence{};
        float weightSum = 0.0F;
        for (std::size_t k = 0U; k < kMaxJointInfluencesUVE; ++k) {
            std::uint32_t skinJoint = 0U;
            switch (source.joints0.componentType) {
            case GltfComponentTypeUVE::UnsignedByte:
                skinJoint = std::to_integer<std::uint8_t>(jointBytes[k]);
                break;
            case GltfComponentTypeUVE::UnsignedShort:
                skinJoint = Detail::ReadGltfU16LEUVE(jointBytes + k * 2U);
                break;
            case GltfComponentTypeUVE::UnsignedInt:
                skinJoint = Detail::ReadGltfU32LEUVE(jointBytes + k * 4U);
                break;
            case GltfComponentTypeUVE::Float:
                return false;
            }
            if (skinJoint >= jointCount) {
                return false;
            }
            const float weight = Detail::ReadGltfFloatLEUVE(weightBytes + k * 4U);
            if (!std::isfinite(weight) || weight < 0.0F) {
                return false;
            }
            influence.joints[k] = skinOrderToSkeleton[skinJoint];
            influence.weights[k] = weight;
            weightSum += weight;
        }
        if (!std::isfinite(weightSum) || weightSum <= 0.0F) {
            return false;
        }
        for (std::size_t k = 0U; k < kMaxJointInfluencesUVE; ++k) {
            influence.weights[k] /= weightSum;
        }
        influences[index] = influence;
    }

    SkeletonAssetUVE skeleton;
    skeleton.skeletonId = source.skeletonId;
    skeleton.joints.reserve(jointCount);
    std::vector<MeshJointUVE> joints;
    joints.reserve(jointCount);
    for (std::size_t i = 0U; i < jointCount; ++i) {
        const GltfJointUVE& gltfJoint = source.skeleton.joints[i];
        SkeletonJointUVE joint;
        joint.name = gltfJoint.name;
        joint.parent = gltfJoint.parentIndex;
        joint.position = gltfJoint.translation;
        joint.rotation = gltfJoint.rotation;
        joint.scale = gltfJoint.scale;
        skeleton.joints.push_back(joint);
        MeshJointUVE meshJoint;
        meshJoint.parentIndex = gltfJoint.parentIndex < 0 ? kInvalidJointParentUVE
                                                          : static_cast<std::uint32_t>(gltfJoint.parentIndex);
        // inverseBindMatrix defaults to identity, which is the spec reading of a skin without
        // inverseBindMatrices — only overwritten below when the accessor is present.
        meshJoint.name = gltfJoint.name;
        joints.push_back(std::move(meshJoint));
    }
    if (source.inverseBindMatrices.has_value()) {
        for (std::size_t s = 0U; s < jointCount; ++s) {
            const std::byte* const matrixBytes =
                Detail::GltfAccessorElementUVE(*source.inverseBindMatrices, s, 64U);
            if (matrixBytes == nullptr) {
                return false;
            }
            float columnMajor[16U];
            for (std::size_t f = 0U; f < 16U; ++f) {
                columnMajor[f] = Detail::ReadGltfFloatLEUVE(matrixBytes + f * 4U);
                if (!std::isfinite(columnMajor[f])) {
                    return false;
                }
            }
            Math::Matrix4x4UVE ibm{};
            for (std::size_t r = 0U; r < 4U; ++r) {
                for (std::size_t c = 0U; c < 4U; ++c) {
                    ibm.m[r][c] = columnMajor[c * 4U + r];
                }
            }
            joints[skinOrderToSkeleton[s]].inverseBindMatrix = ibm;
        }
    }

    if (!IsSkeletonAssetValidUVE(skeleton)) {
        return false;
    }
    outSkin.skeleton = std::move(skeleton);
    outSkin.joints = std::move(joints);
    outSkin.influences = std::move(influences);
    return true;
}

std::optional<GltfSkinSourceUVE> ParseGltfSkinSourceUVE(std::string_view json,
                                                        const std::vector<std::byte>& buffer) {
    nlohmann::json document;
    try {
        document = nlohmann::json::parse(json);
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
    if (!document.is_object()) {
        return std::nullopt;
    }
    const auto skinsIt = document.find("skins");
    if (skinsIt == document.end() || !skinsIt->is_array() || skinsIt->empty()) {
        return std::nullopt;
    }
    const nlohmann::json& skin = skinsIt->at(0U);
    if (!skin.is_object()) {
        return std::nullopt;
    }
    std::vector<std::size_t> skinJointNodes;
    if (!ReadSkinJointNodesUVE(skin, skinJointNodes)) {
        return std::nullopt;
    }
    std::optional<GltfSkeletonUVE> skeleton =
        ParseGltfSkeletonUVE(json, kMaximumSkeletonAssetJointsUVE);
    if (!skeleton.has_value() || skeleton->joints.size() != skinJointNodes.size()) {
        return std::nullopt;
    }

    std::optional<GltfAccessorViewUVE> inverseBindMatrices;
    const auto ibmIt = skin.find("inverseBindMatrices");
    if (ibmIt != skin.end()) {
        if (!ibmIt->is_number_unsigned()) {
            return std::nullopt;
        }
        inverseBindMatrices =
            Detail::BuildAccessorViewUVE(document, buffer, ibmIt->get<std::uint64_t>(), "MAT4", false);
        if (!inverseBindMatrices.has_value()) {
            return std::nullopt;
        }
    }

    const auto meshesIt = document.find("meshes");
    if (meshesIt == document.end() || !meshesIt->is_array() || meshesIt->size() != 1U) {
        return std::nullopt;
    }
    const nlohmann::json& mesh = meshesIt->at(0U);
    if (!mesh.is_object()) {
        return std::nullopt;
    }
    const auto primitivesIt = mesh.find("primitives");
    if (primitivesIt == mesh.end() || !primitivesIt->is_array() || primitivesIt->size() != 1U) {
        return std::nullopt;
    }
    const nlohmann::json& primitive = primitivesIt->at(0U);
    if (!primitive.is_object()) {
        return std::nullopt;
    }
    const auto attributesIt = primitive.find("attributes");
    if (attributesIt == primitive.end() || !attributesIt->is_object()) {
        return std::nullopt;
    }
    const std::optional<std::size_t> positionIndex =
        FindAttributeIndexUVE(*attributesIt, "POSITION");
    const std::optional<std::size_t> jointsIndex =
        FindAttributeIndexUVE(*attributesIt, "JOINTS_0");
    const std::optional<std::size_t> weightsIndex =
        FindAttributeIndexUVE(*attributesIt, "WEIGHTS_0");
    if (!positionIndex.has_value() || !jointsIndex.has_value() || !weightsIndex.has_value()) {
        return std::nullopt;
    }
    const std::optional<GltfAccessorViewUVE> positions = Detail::BuildAccessorViewUVE(
        document, buffer, static_cast<std::uint64_t>(*positionIndex), "VEC3", false);
    // JOINTS_0 accepts any unsigned component width; float joints fail inside the builder.
    const std::optional<GltfAccessorViewUVE> joints0 = Detail::BuildAccessorViewUVE(
        document, buffer, static_cast<std::uint64_t>(*jointsIndex), "VEC4", true);
    const std::optional<GltfAccessorViewUVE> weights0 = Detail::BuildAccessorViewUVE(
        document, buffer, static_cast<std::uint64_t>(*weightsIndex), "VEC4", false);
    if (!positions.has_value() || !joints0.has_value() || !weights0.has_value()) {
        return std::nullopt;
    }

    std::string skeletonId = "skin";
    const auto nameIt = skin.find("name");
    if (nameIt != skin.end()) {
        if (!nameIt->is_string()) {
            return std::nullopt;
        }
        const std::string name = nameIt->get<std::string>();
        if (!name.empty()) {
            skeletonId = name;
        }
    }
    return GltfSkinSourceUVE{std::move(skeletonId),
                             std::move(*skeleton),
                             std::move(skinJointNodes),
                             std::move(inverseBindMatrices),
                             *joints0,
                             *weights0,
                             positions->elementCount};
}

} // namespace UVE::Asset
