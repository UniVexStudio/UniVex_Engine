// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "online_credentials.h"

#include <fstream>
#include <iterator>
#include <system_error>

#include <nlohmann/json.hpp>

namespace UVE::Online {
namespace {

using JsonUVE = nlohmann::json;

inline constexpr std::string_view kCredentialsFormatUVE = "uvcredentials";
inline constexpr std::uint32_t kCurrentCredentialsSchemaVersionUVE = 1U;
inline constexpr std::string_view kTemporaryFileSuffixUVE = ".tmp";
inline constexpr std::filesystem::perms kOwnerOnlyUVE =
    std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;

} // namespace

std::string LoadAccessTokenUVE(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const JsonUVE json = JsonUVE::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() || json.value("format", std::string{}) != kCredentialsFormatUVE ||
        json.value("schemaVersion", 0U) != kCurrentCredentialsSchemaVersionUVE) {
        return {};
    }
    return json.value("accessToken", std::string{});
}

bool SaveAccessTokenUVE(const std::filesystem::path& file, const std::string& token) {
    std::error_code error;
    if (token.empty()) {
        std::filesystem::remove(file, error);
        return !error;
    }
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
    }
    std::filesystem::path temporary = file;
    temporary += kTemporaryFileSuffixUVE;
    {
        // Created empty and restricted first, so the token is never readable by others.
        std::ofstream create(temporary, std::ios::binary | std::ios::trunc);
        if (!create.is_open()) {
            return false;
        }
    }
    std::filesystem::permissions(temporary, kOwnerOnlyUVE, std::filesystem::perm_options::replace, error);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << JsonUVE{{"format", kCredentialsFormatUVE},
                          {"schemaVersion", kCurrentCredentialsSchemaVersionUVE},
                          {"accessToken", token}}
                      .dump();
        if (!output.good()) {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    std::filesystem::rename(temporary, file, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    return true;
}

} // namespace UVE::Online
