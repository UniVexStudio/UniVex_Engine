// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/asset_content_fingerprint_uve.h"

#include "uve/utilities/hash_uve.h"

#include <array>
#include <cstddef>
#include <fstream>
#include <system_error>

namespace UVE::Asset {

std::optional<AssetContentFingerprintUVE>
ComputeAssetContentFingerprintUVE(const std::filesystem::path& path) {
    if (path.empty()) {
        return std::nullopt;
    }

    std::error_code errorCode;
    if (!std::filesystem::is_regular_file(path, errorCode) || errorCode) {
        return std::nullopt;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }

    AssetContentFingerprintUVE fingerprint;
    Utilities::Fnv1a64UVE hasher;
    std::array<char, 16U * 1024U> buffer{};
    while (file.good()) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize readCount = file.gcount();
        if (readCount <= 0) {
            break;
        }
        hasher.AppendBytes(buffer.data(), static_cast<std::size_t>(readCount));
        fingerprint.byteCount += static_cast<std::uint64_t>(readCount);
    }

    if (file.bad()) {
        return std::nullopt;
    }
    fingerprint.hash = hasher.Digest();
    return fingerprint;
}

} // namespace UVE::Asset
