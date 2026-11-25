// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "animation_library_asset.h"

#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "uve_file_envelope.h"
#include "logging_macros.h"

namespace UVE::Asset {
namespace {

using Json = nlohmann::json;
constexpr std::string_view kAnimationLibrarySchema = "uve-animation-library-v1";

} // namespace

bool IsAnimationLibraryAssetValid(const AnimationLibraryAsset& library) noexcept {
    if (library.libraryId.empty() ||
        library.libraryId.size() > kMaximumAnimationLibraryIdentifierBytes ||
        library.libraryId.contains('\0') ||
        library.entries.size() > kMaximumAnimationLibraryEntries) {
        return false;
    }
    for (std::size_t index = 0U; index < library.entries.size(); ++index) {
        const AnimationLibraryEntry& entry = library.entries[index];
        if (entry.clip == kInvalidAssetGuid || entry.name.empty() ||
            entry.name.size() > kMaximumAnimationLibraryIdentifierBytes || entry.name.contains('\0')) {
            return false;
        }
        for (std::size_t other = 0U; other < index; ++other) {
            if (library.entries[other].clip == entry.clip) {
                return false;
            }
        }
    }
    return true;
}

bool SaveAnimationLibraryAsset(const AnimationLibraryAsset& library,
                                  const std::filesystem::path& path) {
    if (!IsAnimationLibraryAssetValid(library)) {
        UVE_ERROR("AnimationLibraryAsset: refusing to save invalid library to {}", path.string());
        return false;
    }
    Json document{{"schema", kAnimationLibrarySchema},
                     {"libraryId", library.libraryId},
                     {"entries", Json::array()}};
    for (const AnimationLibraryEntry& entry : library.entries) {
        document["entries"].push_back({{"clip", entry.clip.value}, {"name", entry.name}});
    }
    const std::string serialized = document.dump();
    if (serialized.empty() || serialized.size() > kMaximumAnimationLibraryPayloadBytes) {
        UVE_ERROR("AnimationLibraryAsset: serialized payload is empty or oversized for {}", path.string());
        return false;
    }
    const auto* const bytes = reinterpret_cast<const std::byte*>(serialized.data());
    return WriteUveFile(path, AssetKind::AnimationLibrary,
                            std::vector<std::byte>(bytes, bytes + serialized.size()));
}

bool LoadAnimationLibraryAsset(const std::filesystem::path& path,
                                  AnimationLibraryAsset& outLibrary) {
    const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(path);
    if (!file.has_value() || file->first.assetType != AssetKind::AnimationLibrary || file->second.empty() ||
        file->second.size() > kMaximumAnimationLibraryPayloadBytes) {
        UVE_ERROR("AnimationLibraryAsset: invalid library envelope {}", path.string());
        return false;
    }
    try {
        const std::string serialized(reinterpret_cast<const char*>(file->second.data()), file->second.size());
        const Json document = Json::parse(serialized);
        const std::string schema = document.is_object() ? document.value("schema", std::string{}) : std::string{};
        if (!document.is_object() || schema != kAnimationLibrarySchema || !document.contains("libraryId") ||
            !document.contains("entries")) {
            return false;
        }
        AnimationLibraryAsset candidate;
        candidate.libraryId = document.at("libraryId").get<std::string>();
        const Json& entries = document.at("entries");
        if (!entries.is_array() || entries.size() > kMaximumAnimationLibraryEntries) {
            return false;
        }
        candidate.entries.reserve(entries.size());
        for (const Json& value : entries) {
            if (!value.is_object() || !value.contains("clip") || !value.contains("name")) {
                return false;
            }
            AnimationLibraryEntry entry;
            entry.clip = AssetGuid{value.at("clip").get<std::uint64_t>()};
            entry.name = value.at("name").get<std::string>();
            candidate.entries.push_back(std::move(entry));
        }
        if (!IsAnimationLibraryAssetValid(candidate)) {
            return false;
        }
        outLibrary = std::move(candidate);
        return true;
    } catch (const std::exception& exception) {
        UVE_ERROR("AnimationLibraryAsset: failed to parse {}: {}", path.string(), exception.what());
        return false;
    } catch (...) {
        UVE_ERROR("AnimationLibraryAsset: failed to parse {} with an unknown exception", path.string());
        return false;
    }
}

} // namespace UVE::Asset
