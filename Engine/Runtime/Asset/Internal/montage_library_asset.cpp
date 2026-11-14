// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "montage_library_asset.h"

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

using JsonUVE = nlohmann::json;
constexpr std::string_view kMontageLibrarySchemaUVE = "uve-montage-library-v1";

} // namespace

bool IsMontageLibraryAssetValidUVE(const MontageLibraryAssetUVE& library) noexcept {
    if (library.libraryId.empty() ||
        library.libraryId.size() > kMaximumMontageLibraryIdentifierBytesUVE ||
        library.libraryId.contains('\0') ||
        library.entries.size() > kMaximumMontageLibraryEntriesUVE) {
        return false;
    }
    for (std::size_t index = 0U; index < library.entries.size(); ++index) {
        const MontageLibraryEntryUVE& entry = library.entries[index];
        if (entry.montage == kInvalidAssetGuidUVE || entry.name.empty() ||
            entry.name.size() > kMaximumMontageLibraryIdentifierBytesUVE || entry.name.contains('\0')) {
            return false;
        }
        for (std::size_t other = 0U; other < index; ++other) {
            if (library.entries[other].montage == entry.montage) {
                return false;
            }
        }
    }
    return true;
}

bool SaveMontageLibraryAssetUVE(const MontageLibraryAssetUVE& library,
                                  const std::filesystem::path& path) {
    if (!IsMontageLibraryAssetValidUVE(library)) {
        UVE_ERROR("MontageLibraryAssetUVE: refusing to save invalid library to {}", path.string());
        return false;
    }
    JsonUVE document{{"schema", kMontageLibrarySchemaUVE},
                     {"libraryId", library.libraryId},
                     {"entries", JsonUVE::array()}};
    for (const MontageLibraryEntryUVE& entry : library.entries) {
        document["entries"].push_back({{"montage", entry.montage.value}, {"name", entry.name}});
    }
    const std::string serialized = document.dump();
    if (serialized.empty() || serialized.size() > kMaximumMontageLibraryPayloadBytesUVE) {
        UVE_ERROR("MontageLibraryAssetUVE: serialized payload is empty or oversized for {}", path.string());
        return false;
    }
    const auto* const bytes = reinterpret_cast<const std::byte*>(serialized.data());
    return WriteUveFileUVE(path, AssetKindUVE::MontageLibrary,
                            std::vector<std::byte>(bytes, bytes + serialized.size()));
}

bool LoadMontageLibraryAssetUVE(const std::filesystem::path& path,
                                  MontageLibraryAssetUVE& outLibrary) {
    const std::optional<std::pair<UveFileHeaderUVE, std::vector<std::byte>>> file = ReadUveFileUVE(path);
    if (!file.has_value() || file->first.assetType != AssetKindUVE::MontageLibrary || file->second.empty() ||
        file->second.size() > kMaximumMontageLibraryPayloadBytesUVE) {
        UVE_ERROR("MontageLibraryAssetUVE: invalid library envelope {}", path.string());
        return false;
    }
    try {
        const std::string serialized(reinterpret_cast<const char*>(file->second.data()), file->second.size());
        const JsonUVE document = JsonUVE::parse(serialized);
        const std::string schema = document.is_object() ? document.value("schema", std::string{}) : std::string{};
        if (!document.is_object() || schema != kMontageLibrarySchemaUVE || !document.contains("libraryId") ||
            !document.contains("entries")) {
            return false;
        }
        MontageLibraryAssetUVE candidate;
        candidate.libraryId = document.at("libraryId").get<std::string>();
        const JsonUVE& entries = document.at("entries");
        if (!entries.is_array() || entries.size() > kMaximumMontageLibraryEntriesUVE) {
            return false;
        }
        candidate.entries.reserve(entries.size());
        for (const JsonUVE& value : entries) {
            if (!value.is_object() || !value.contains("montage") || !value.contains("name")) {
                return false;
            }
            MontageLibraryEntryUVE entry;
            entry.montage = AssetGuidUVE{value.at("montage").get<std::uint64_t>()};
            entry.name = value.at("name").get<std::string>();
            candidate.entries.push_back(std::move(entry));
        }
        if (!IsMontageLibraryAssetValidUVE(candidate)) {
            return false;
        }
        outLibrary = std::move(candidate);
        return true;
    } catch (const std::exception& exception) {
        UVE_ERROR("MontageLibraryAssetUVE: failed to parse {}: {}", path.string(), exception.what());
        return false;
    } catch (...) {
        UVE_ERROR("MontageLibraryAssetUVE: failed to parse {} with an unknown exception", path.string());
        return false;
    }
}

} // namespace UVE::Asset
