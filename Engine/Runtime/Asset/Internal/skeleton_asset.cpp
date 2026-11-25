// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "skeleton_asset.h"

#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "uve_file_envelope.h"
#include "logging_macros.h"
#include "vector3.h"

namespace UVE::Asset {
namespace {

using Json = nlohmann::json;
constexpr std::string_view kSkeletonSchema = "uve-skeleton-v1";

[[nodiscard]] Json ToVectorJson(const Math::Vector3& value) {
    return Json::array({value.x, value.y, value.z});
}

[[nodiscard]] Json ToQuaternionJson(const Math::Quaternion& value) {
    return Json::array({value.x, value.y, value.z, value.w});
}

[[nodiscard]] bool ReadVectorJson(const Json& value, Math::Vector3& outVector) {
    if (!value.is_array() || value.size() != 3U) {
        return false;
    }
    const Math::Vector3 candidate{value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
    if (!Math::IsFinite(candidate)) {
        return false;
    }
    outVector = candidate;
    return true;
}

[[nodiscard]] bool ReadQuaternionJson(const Json& value, Math::Quaternion& outRotation) {
    if (!value.is_array() || value.size() != 4U) {
        return false;
    }
    const Math::Quaternion candidate{value.at(0).get<float>(), value.at(1).get<float>(),
                                       value.at(2).get<float>(), value.at(3).get<float>()};
    Math::Quaternion normalized;
    if (!Math::TryNormalize(candidate, normalized)) {
        return false;
    }
    outRotation = normalized;
    return true;
}

} // namespace

SkeletonAsset MakeSkeletonAsset(const GltfSkeleton& source, std::string skeletonId) {
    SkeletonAsset asset;
    asset.skeletonId = std::move(skeletonId);
    asset.joints.reserve(source.joints.size());
    for (const GltfJoint& joint : source.joints) {
        SkeletonJoint converted;
        converted.name = joint.name;
        converted.parent = joint.parentIndex;
        converted.position = joint.translation;
        converted.rotation = joint.rotation;
        converted.scale = joint.scale;
        asset.joints.push_back(std::move(converted));
    }
    return asset;
}

bool IsSkeletonAssetValid(const SkeletonAsset& skeleton) noexcept {
    if (skeleton.skeletonId.empty() || skeleton.skeletonId.size() > kMaximumSkeletonAssetIdentifierBytes ||
        skeleton.skeletonId.contains('\0') || skeleton.joints.empty() ||
        skeleton.joints.size() > kMaximumSkeletonAssetJoints) {
        return false;
    }
    for (std::size_t index = 0U; index < skeleton.joints.size(); ++index) {
        const SkeletonJoint& joint = skeleton.joints[index];
        Math::Quaternion normalized;
        if (joint.name.empty() || joint.name.size() > kMaximumSkeletonAssetIdentifierBytes ||
            joint.name.contains('\0') || joint.parent < -1 || joint.parent >= static_cast<std::int32_t>(index) ||
            !Math::IsFinite(joint.position) || !Math::IsFinite(joint.scale) ||
            !Math::TryNormalize(joint.rotation, normalized)) {
            return false;
        }
        for (std::size_t other = 0U; other < index; ++other) {
            if (skeleton.joints[other].name == joint.name) {
                return false;
            }
        }
    }
    return true;
}

bool SaveSkeletonAsset(const SkeletonAsset& skeleton, const std::filesystem::path& path) {
    if (!IsSkeletonAssetValid(skeleton)) {
        UVE_ERROR("SkeletonAsset: refusing to save invalid skeleton to {}", path.string());
        return false;
    }
    Json document{{"schema", kSkeletonSchema},
                     {"skeletonId", skeleton.skeletonId},
                     {"joints", Json::array()}};
    for (const SkeletonJoint& joint : skeleton.joints) {
        document["joints"].push_back({{"name", joint.name},
                                      {"parent", joint.parent},
                                      {"position", ToVectorJson(joint.position)},
                                      {"rotation", ToQuaternionJson(joint.rotation)},
                                      {"scale", ToVectorJson(joint.scale)}});
    }
    const std::string serialized = document.dump();
    if (serialized.empty() || serialized.size() > kMaximumSkeletonAssetPayloadBytes) {
        UVE_ERROR("SkeletonAsset: serialized payload is empty or oversized for {}", path.string());
        return false;
    }
    const auto* const bytes = reinterpret_cast<const std::byte*>(serialized.data());
    return WriteUveFile(path, AssetKind::Skeleton, std::vector<std::byte>(bytes, bytes + serialized.size()));
}

bool LoadSkeletonAsset(const std::filesystem::path& path, SkeletonAsset& outSkeleton) {
    const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(path);
    if (!file.has_value() || file->first.assetType != AssetKind::Skeleton || file->second.empty() ||
        file->second.size() > kMaximumSkeletonAssetPayloadBytes) {
        UVE_ERROR("SkeletonAsset: invalid skeleton envelope {}", path.string());
        return false;
    }
    try {
        const std::string serialized(reinterpret_cast<const char*>(file->second.data()), file->second.size());
        const Json document = Json::parse(serialized);
        const std::string schema = document.is_object() ? document.value("schema", std::string{}) : std::string{};
        if (!document.is_object() || schema != kSkeletonSchema || !document.contains("skeletonId") ||
            !document.contains("joints")) {
            return false;
        }
        SkeletonAsset candidate;
        candidate.skeletonId = document.at("skeletonId").get<std::string>();
        const Json& joints = document.at("joints");
        if (!joints.is_array() || joints.size() > kMaximumSkeletonAssetJoints) {
            return false;
        }
        candidate.joints.reserve(joints.size());
        for (const Json& value : joints) {
            if (!value.is_object() || !value.contains("name") || !value.contains("parent") ||
                !value.contains("position") || !value.contains("rotation") || !value.contains("scale")) {
                return false;
            }
            SkeletonJoint joint;
            joint.name = value.at("name").get<std::string>();
            joint.parent = value.at("parent").get<std::int32_t>();
            if (!ReadVectorJson(value.at("position"), joint.position) ||
                !ReadQuaternionJson(value.at("rotation"), joint.rotation) ||
                !ReadVectorJson(value.at("scale"), joint.scale)) {
                return false;
            }
            candidate.joints.push_back(std::move(joint));
        }
        if (!IsSkeletonAssetValid(candidate)) {
            return false;
        }
        outSkeleton = std::move(candidate);
        return true;
    } catch (const std::exception& exception) {
        UVE_ERROR("SkeletonAsset: failed to parse {}: {}", path.string(), exception.what());
        return false;
    } catch (...) {
        UVE_ERROR("SkeletonAsset: failed to parse {} with an unknown exception", path.string());
        return false;
    }
}

} // namespace UVE::Asset
