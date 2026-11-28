// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "legacy_extension_migration.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "logging_macros.h"

namespace UVE::Asset {
namespace {

// Longest names first, so ".uveshadercache" is not read as ".uveshader" + "cache" in text.
constexpr std::array<std::pair<std::string_view, std::string_view>, 24> kRenamedExtensions{{
    {".uveshadercache", ".uvshadercache"},
    {".uveimportcache", ".uvimportcache"},
    {".uvesettings", ".uvsettings"},
    {".uveassetdb", ".uvassetdb"},
    {".uveditor", ".uvproject"},
    {".uveprefab", ".uvprefab"},
    {".uveentity", ".uventity"},
    {".uveshader", ".uvshader"},
    {".uvebundle", ".uvbundle"},
    {".uvescene", ".uvscene"},
    {".uvemodel", ".uvmodel"},
    {".uvetable", ".uvtable"},
    {".uveaudio", ".uvaudio"},
    {".uveinput", ".uvinput"},
    {".uveasset", ".uvasset"},
    {".uveblob", ".uvblob"},
    {".uveanim", ".uvanim"},
    {".uveskel", ".uvskel"},
    {".uveclip", ".uvclip"},
    {".uvesave", ".uvsave"},
    {".uvetex", ".uvtex"},
    {".uvemat", ".uvmat"},
    {".uvenav", ".uvnav"},
    {".uvesky", ".uvsky"},
}};

[[nodiscard]] std::string Lower(std::string_view text) {
    std::string lower{text};
    std::ranges::transform(lower, lower.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

[[nodiscard]] std::string_view LegacyExtensionFor(const std::string_view currentExtension) noexcept {
    for (const auto& [legacy, current] : kRenamedExtensions) {
        if (current == currentExtension) {
            return legacy;
        }
    }
    return {};
}

[[nodiscard]] bool IsNameCharacter(const char c) noexcept {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

} // namespace

std::string_view GetRenamedExtension(const std::string_view extension) noexcept {
    try {
        const std::string lower = Lower(extension);
        for (const auto& [legacy, current] : kRenamedExtensions) {
            if (legacy == lower) {
                return current;
            }
        }
    } catch (...) {
    }
    return {};
}

bool MigrateLegacyFile(const std::filesystem::path& currentPath) {
    std::error_code error;
    const std::string_view legacy = LegacyExtensionFor(Lower(currentPath.extension().string()));
    if (legacy.empty() || std::filesystem::exists(currentPath, error)) {
        return false;
    }
    std::filesystem::path legacyPath = currentPath;
    legacyPath.replace_extension(std::string{legacy});
    if (!std::filesystem::is_regular_file(legacyPath, error)) {
        return false;
    }
    std::filesystem::rename(legacyPath, currentPath, error);
    if (error) {
        UVE_WARNING("LegacyExtensionMigration: could not rename \"{}\": {}", legacyPath.string(), error.message());
        return false;
    }
    UVE_INFO("LegacyExtensionMigration: renamed \"{}\" to \"{}\"", legacyPath.string(), currentPath.string());
    return true;
}

LegacyMigrationReport MigrateLegacyContent(const std::filesystem::path& root) {
    LegacyMigrationReport report;
    std::error_code error;
    if (!std::filesystem::is_directory(root, error)) {
        return report;
    }
    // Collected first: renaming while iterating would disturb the walk.
    std::vector<std::filesystem::path> legacyFiles;
    for (auto it = std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (it->is_regular_file(error) && !GetRenamedExtension(it->path().extension().string()).empty()) {
            legacyFiles.push_back(it->path());
        }
    }
    for (const std::filesystem::path& legacy : legacyFiles) {
        std::filesystem::path current = legacy;
        current.replace_extension(std::string{GetRenamedExtension(legacy.extension().string())});
        if (std::filesystem::exists(current, error)) {
            ++report.skipped;
            continue;
        }
        std::filesystem::rename(legacy, current, error);
        if (error) {
            ++report.skipped;
            continue;
        }
        ++report.renamed;
    }
    if (report.renamed + report.skipped > 0U) {
        UVE_INFO("LegacyExtensionMigration: renamed {} file(s) under \"{}\" to the .uv* names ({} left: new name taken)",
                 report.renamed, root.string(), report.skipped);
    }
    return report;
}

bool RewriteLegacyExtensionsInText(std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool changed = false;
    for (std::size_t i = 0U; i < text.size();) {
        bool replaced = false;
        if (text[i] == '.') {
            for (const auto& [legacy, current] : kRenamedExtensions) {
                const std::size_t end = i + legacy.size();
                if (end <= text.size() && Lower(std::string_view{text}.substr(i, legacy.size())) == legacy &&
                    (end == text.size() || !IsNameCharacter(text[end]))) {
                    out += current;
                    i = end;
                    replaced = changed = true;
                    break;
                }
            }
        }
        if (!replaced) {
            out += text[i++];
        }
    }
    if (changed) {
        text = std::move(out);
    }
    return changed;
}

bool RewriteLegacyExtensionsInTextFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    in.close();
    if (!RewriteLegacyExtensionsInText(text)) {
        return false;
    }
    std::ofstream save(file, std::ios::binary | std::ios::trunc);
    save << text;
    return static_cast<bool>(save);
}

} // namespace UVE::Asset
