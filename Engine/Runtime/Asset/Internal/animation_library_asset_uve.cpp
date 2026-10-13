// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/animation_library_asset_uve.h"

#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Asset {
namespace {

using JsonUVE = nlohmann::json;
constexpr std::string_view kAnimationLibrarySchemaUVE = "uve-animation-library-v1";

} // namespace

bool IsAnimationLibraryAssetValidUVE(const AnimationLibraryAssetUVE& library) noexcept {
    if (library.libraryId.empty() ||
        library.libraryId.size() > kMaximumAnimationLibraryIdentifierBytesUVE ||
        library.libraryId.contains('\0') ||
        library.entries.size() > kMaximumAnimationLibraryEntriesUVE) {
        return false;
    }
    for (std::size_t index = 0U; index < library.entries.size(); ++index) {
        const AnimationLibraryEntryUVE& entry = library.entries[index];
        if (entry.clip == kInvalidAssetGuidUVE || entry.name.empty() ||
            entry.name.size() > kMaximumAnimationLibraryIdentifierBytesUVE || entry.name.contains('\0')) {
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

bool SaveAnimationLibraryAssetUVE(const AnimationLibraryAssetUVE& library,
                                  const std::filesystem::path& path) {
    if (!IsAnimationLibraryAssetValidUVE(library)) {
        UVE_ERROR("AnimationLibraryAssetUVE: refusing to save invalid library to {}", path.string());
        return false;
    }
    JsonUVE document{{"schema", kAnimationLibrarySchemaUVE},
                     {"libraryId", library.libraryId},
                     {"entries", JsonUVE::array()}};
    for (const AnimationLibraryEntryUVE& entry : library.entries) {
        document["entries"].push_back({{"clip", entry.clip.value}, {"name", entry.name}});
    }
    const std::string serialized = document.dump();
    if (serialized.empty() || serialized.size() > kMaximumAnimationLibraryPayloadBytesUVE) {
        UVE_ERROR("AnimationLibraryAssetUVE: serialized payload is empty or oversized for {}", path.string());
        return false;
    }
    const auto* const bytes = reinterpret_cast<const std::byte*>(serialized.data());
    return WriteUveFileUVE(path, AssetKindUVE::AnimationLibrary,
                            std::vector<std::byte>(bytes, bytes + serialized.size()));
}

bool LoadAnimationLibraryAssetUVE(const std::filesystem::path& path,
                                  AnimationLibraryAssetUVE& outLibrary) {
    const std::optional<std::pair<UveFileHeaderUVE, std::vector<std::byte>>> file = ReadUveFileUVE(path);
    if (!file.has_value() || file->first.assetType != AssetKindUVE::AnimationLibrary || file->second.empty() ||
        file->second.size() > kMaximumAnimationLibraryPayloadBytesUVE) {
        UVE_ERROR("AnimationLibraryAssetUVE: invalid library envelope {}", path.string());
        return false;
    }
    try {
        const std::string serialized(reinterpret_cast<const char*>(file->second.data()), file->second.size());
        const JsonUVE document = JsonUVE::parse(serialized);
        const std::string schema = document.is_object() ? document.value("schema", std::string{}) : std::string{};
        if (!document.is_object() || schema != kAnimationLibrarySchemaUVE || !document.contains("libraryId") ||
            !document.contains("entries")) {
            return false;
        }
        AnimationLibraryAssetUVE candidate;
        candidate.libraryId = document.at("libraryId").get<std::string>();
        const JsonUVE& entries = document.at("entries");
        if (!entries.is_array() || entries.size() > kMaximumAnimationLibraryEntriesUVE) {
            return false;
        }
        candidate.entries.reserve(entries.size());
        for (const JsonUVE& value : entries) {
            if (!value.is_object() || !value.contains("clip") || !value.contains("name")) {
                return false;
            }
            AnimationLibraryEntryUVE entry;
            entry.clip = AssetGuidUVE{value.at("clip").get<std::uint64_t>()};
            entry.name = value.at("name").get<std::string>();
            candidate.entries.push_back(std::move(entry));
        }
        if (!IsAnimationLibraryAssetValidUVE(candidate)) {
            return false;
        }
        outLibrary = std::move(candidate);
        return true;
    } catch (const std::exception& exception) {
        UVE_ERROR("AnimationLibraryAssetUVE: failed to parse {}: {}", path.string(), exception.what());
        return false;
    } catch (...) {
        UVE_ERROR("AnimationLibraryAssetUVE: failed to parse {} with an unknown exception", path.string());
        return false;
    }
}

} // namespace UVE::Asset
