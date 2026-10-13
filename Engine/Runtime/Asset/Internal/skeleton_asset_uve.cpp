// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/skeleton_asset_uve.h"

#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Asset {
namespace {

using JsonUVE = nlohmann::json;
constexpr std::string_view kSkeletonSchemaUVE = "uve-skeleton-v1";

[[nodiscard]] JsonUVE ToVectorJsonUVE(const Math::Vector3UVE& value) {
    return JsonUVE::array({value.x, value.y, value.z});
}

[[nodiscard]] JsonUVE ToQuaternionJsonUVE(const Math::QuaternionUVE& value) {
    return JsonUVE::array({value.x, value.y, value.z, value.w});
}

[[nodiscard]] bool ReadVectorJsonUVE(const JsonUVE& value, Math::Vector3UVE& outVector) {
    if (!value.is_array() || value.size() != 3U) {
        return false;
    }
    const Math::Vector3UVE candidate{value.at(0).get<float>(), value.at(1).get<float>(), value.at(2).get<float>()};
    if (!Math::IsFiniteUVE(candidate)) {
        return false;
    }
    outVector = candidate;
    return true;
}

[[nodiscard]] bool ReadQuaternionJsonUVE(const JsonUVE& value, Math::QuaternionUVE& outRotation) {
    if (!value.is_array() || value.size() != 4U) {
        return false;
    }
    const Math::QuaternionUVE candidate{value.at(0).get<float>(), value.at(1).get<float>(),
                                       value.at(2).get<float>(), value.at(3).get<float>()};
    Math::QuaternionUVE normalized;
    if (!Math::TryNormalizeUVE(candidate, normalized)) {
        return false;
    }
    outRotation = normalized;
    return true;
}

} // namespace

bool IsSkeletonAssetValidUVE(const SkeletonAssetUVE& skeleton) noexcept {
    if (skeleton.skeletonId.empty() || skeleton.skeletonId.size() > kMaximumSkeletonAssetIdentifierBytesUVE ||
        skeleton.skeletonId.contains('\0') || skeleton.joints.empty() ||
        skeleton.joints.size() > kMaximumSkeletonAssetJointsUVE) {
        return false;
    }
    for (std::size_t index = 0U; index < skeleton.joints.size(); ++index) {
        const SkeletonJointUVE& joint = skeleton.joints[index];
        Math::QuaternionUVE normalized;
        if (joint.name.empty() || joint.name.size() > kMaximumSkeletonAssetIdentifierBytesUVE ||
            joint.name.contains('\0') || joint.parent < -1 || joint.parent >= static_cast<std::int32_t>(index) ||
            !Math::IsFiniteUVE(joint.position) || !Math::IsFiniteUVE(joint.scale) ||
            !Math::TryNormalizeUVE(joint.rotation, normalized)) {
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

bool SaveSkeletonAssetUVE(const SkeletonAssetUVE& skeleton, const std::filesystem::path& path) {
    if (!IsSkeletonAssetValidUVE(skeleton)) {
        UVE_ERROR("SkeletonAssetUVE: refusing to save invalid skeleton to {}", path.string());
        return false;
    }
    JsonUVE document{{"schema", kSkeletonSchemaUVE},
                     {"skeletonId", skeleton.skeletonId},
                     {"joints", JsonUVE::array()}};
    for (const SkeletonJointUVE& joint : skeleton.joints) {
        document["joints"].push_back({{"name", joint.name},
                                      {"parent", joint.parent},
                                      {"position", ToVectorJsonUVE(joint.position)},
                                      {"rotation", ToQuaternionJsonUVE(joint.rotation)},
                                      {"scale", ToVectorJsonUVE(joint.scale)}});
    }
    const std::string serialized = document.dump();
    if (serialized.empty() || serialized.size() > kMaximumSkeletonAssetPayloadBytesUVE) {
        UVE_ERROR("SkeletonAssetUVE: serialized payload is empty or oversized for {}", path.string());
        return false;
    }
    const auto* const bytes = reinterpret_cast<const std::byte*>(serialized.data());
    return WriteUveFileUVE(path, AssetKindUVE::Skeleton, std::vector<std::byte>(bytes, bytes + serialized.size()));
}

bool LoadSkeletonAssetUVE(const std::filesystem::path& path, SkeletonAssetUVE& outSkeleton) {
    const std::optional<std::pair<UveFileHeaderUVE, std::vector<std::byte>>> file = ReadUveFileUVE(path);
    if (!file.has_value() || file->first.assetType != AssetKindUVE::Skeleton || file->second.empty() ||
        file->second.size() > kMaximumSkeletonAssetPayloadBytesUVE) {
        UVE_ERROR("SkeletonAssetUVE: invalid skeleton envelope {}", path.string());
        return false;
    }
    try {
        const std::string serialized(reinterpret_cast<const char*>(file->second.data()), file->second.size());
        const JsonUVE document = JsonUVE::parse(serialized);
        const std::string schema = document.is_object() ? document.value("schema", std::string{}) : std::string{};
        if (!document.is_object() || schema != kSkeletonSchemaUVE || !document.contains("skeletonId") ||
            !document.contains("joints")) {
            return false;
        }
        SkeletonAssetUVE candidate;
        candidate.skeletonId = document.at("skeletonId").get<std::string>();
        const JsonUVE& joints = document.at("joints");
        if (!joints.is_array() || joints.size() > kMaximumSkeletonAssetJointsUVE) {
            return false;
        }
        candidate.joints.reserve(joints.size());
        for (const JsonUVE& value : joints) {
            if (!value.is_object() || !value.contains("name") || !value.contains("parent") ||
                !value.contains("position") || !value.contains("rotation") || !value.contains("scale")) {
                return false;
            }
            SkeletonJointUVE joint;
            joint.name = value.at("name").get<std::string>();
            joint.parent = value.at("parent").get<std::int32_t>();
            if (!ReadVectorJsonUVE(value.at("position"), joint.position) ||
                !ReadQuaternionJsonUVE(value.at("rotation"), joint.rotation) ||
                !ReadVectorJsonUVE(value.at("scale"), joint.scale)) {
                return false;
            }
            candidate.joints.push_back(std::move(joint));
        }
        if (!IsSkeletonAssetValidUVE(candidate)) {
            return false;
        }
        outSkeleton = std::move(candidate);
        return true;
    } catch (const std::exception& exception) {
        UVE_ERROR("SkeletonAssetUVE: failed to parse {}: {}", path.string(), exception.what());
        return false;
    } catch (...) {
        UVE_ERROR("SkeletonAssetUVE: failed to parse {} with an unknown exception", path.string());
        return false;
    }
}

} // namespace UVE::Asset
