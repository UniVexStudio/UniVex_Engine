// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "engine_release_feed.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iterator>
#include <tuple>

#include <nlohmann/json.hpp>

namespace UVE::Online {
namespace {

using JsonUVE = nlohmann::json;

inline constexpr std::string_view kStableNameUVE = "stable";
inline constexpr std::string_view kBetaNameUVE = "beta";
inline constexpr std::string_view kBuildSeparatorUVE = "+build.";
inline constexpr char kTagPrefixUVE = 'v';
/// How many releases one feed request asks for (the API's maximum page).
inline constexpr int kReleasesPerPageUVE = 100;
inline constexpr std::string_view kJsonMediaTypeUVE = "application/vnd.github+json";
inline constexpr std::string_view kApiVersionHeaderUVE = "X-GitHub-Api-Version";
inline constexpr std::string_view kApiVersionUVE = "2022-11-28";
inline constexpr std::string_view kUserAgentUVE = "UniVex-Hub";
inline constexpr long kRateLimitedStatusUVE = 403L;
inline constexpr long kNotFoundStatusUVE = 404L;

[[nodiscard]] std::optional<std::uint32_t> ParseNumberUVE(const std::string_view text) {
    std::uint32_t value = 0U;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] auto VersionKeyUVE(const Core::VersionUVE& version) {
    return std::tie(version.major, version.minor, version.patch, version.build);
}

} // namespace

std::string_view EngineChannelNameUVE(const EngineChannelUVE channel) noexcept {
    return channel == EngineChannelUVE::Beta ? kBetaNameUVE : kStableNameUVE;
}

std::optional<EngineChannelUVE> ParseEngineChannelUVE(const std::string_view name) noexcept {
    if (name == kStableNameUVE) {
        return EngineChannelUVE::Stable;
    }
    if (name == kBetaNameUVE) {
        return EngineChannelUVE::Beta;
    }
    return std::nullopt;
}

std::optional<EngineFeedSourceUVE> LoadEngineFeedSourceUVE(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const JsonUVE json = JsonUVE::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() || json.value("format", std::string{}) != kEngineFeedFormatUVE ||
        json.value("schemaVersion", 0U) != kCurrentEngineFeedSchemaVersionUVE) {
        return std::nullopt;
    }
    EngineFeedSourceUVE source;
    source.apiRoot = json.value("apiRoot", std::string(kDefaultFeedApiRootUVE));
    source.owner = json.value("owner", std::string{});
    source.repository = json.value("repository", std::string{});
    if (source.owner.empty() || source.repository.empty() || !source.apiRoot.starts_with("https://")) {
        return std::nullopt;
    }
    return source;
}

std::optional<Core::VersionUVE> ParseEngineVersionTagUVE(std::string_view tag) {
    if (!tag.empty() && tag.front() == kTagPrefixUVE) {
        tag.remove_prefix(1U);
    }
    if (tag.ends_with(kBetaTagSuffixUVE)) {
        tag.remove_suffix(kBetaTagSuffixUVE.size());
    }
    Core::VersionUVE version{};
    std::string_view numbers = tag;
    std::optional<std::uint32_t> separateBuild;
    if (const std::size_t build = tag.find(kBuildSeparatorUVE); build != std::string_view::npos) {
        const std::optional<std::uint32_t> buildNumber = ParseNumberUVE(tag.substr(build + kBuildSeparatorUVE.size()));
        if (!buildNumber.has_value()) {
            return std::nullopt;
        }
        separateBuild = buildNumber;
        numbers = tag.substr(0U, build);
    }
    std::array<std::uint32_t*, 4U> parts{&version.major, &version.minor, &version.patch, &version.build};
    std::size_t count = 0U;
    while (!numbers.empty()) {
        const std::size_t dot = numbers.find('.');
        const std::optional<std::uint32_t> value = ParseNumberUVE(numbers.substr(0U, dot));
        if (!value.has_value() || count == parts.size()) {
            return std::nullopt;
        }
        *parts[count++] = *value;
        if (dot == std::string_view::npos) {
            break;
        }
        numbers.remove_prefix(dot + 1U);
    }
    constexpr std::size_t kMinimumPartsUVE = 3U;
    constexpr std::size_t kPartsBeforeBuildUVE = 3U;
    if (count < kMinimumPartsUVE || (separateBuild.has_value() && count != kPartsBeforeBuildUVE)) {
        return std::nullopt;
    }
    if (separateBuild.has_value()) {
        version.build = *separateBuild;
    }
    return version;
}

std::string EnginePackageNameUVE(const Core::VersionUVE& version, const std::string_view target) {
    return std::string(kEnginePackagePrefixUVE) + version.ToStringUVE() + "-" + std::string(target) +
           std::string(kEnginePackageExtensionUVE);
}

std::optional<Core::VersionUVE> ParseEnginePackageNameUVE(std::string_view name, const std::string_view target) {
    const std::string suffix = "-" + std::string(target) + std::string(kEnginePackageExtensionUVE);
    if (!name.starts_with(kEnginePackagePrefixUVE) || !name.ends_with(suffix)) {
        return std::nullopt;
    }
    name.remove_prefix(kEnginePackagePrefixUVE.size());
    name.remove_suffix(suffix.size());
    return ParseEngineVersionTagUVE(name);
}

std::vector<EngineReleaseUVE> ParseGitHubReleasesUVE(const std::string_view json, const std::string_view target,
                                                     std::string* const errorMessage) {
    std::vector<EngineReleaseUVE> releases;
    const JsonUVE document = JsonUVE::parse(json, nullptr, false);
    if (document.is_discarded() || !document.is_array()) {
        if (errorMessage != nullptr) {
            *errorMessage = "The release list is not a JSON array.";
        }
        return releases;
    }
    for (const JsonUVE& item : document) {
        if (!item.is_object() || item.value("draft", false)) {
            continue;
        }
        const std::string tag = item.value("tag_name", std::string{});
        const std::optional<Core::VersionUVE> version = ParseEngineVersionTagUVE(tag);
        if (!version.has_value() || !item.contains("assets") || !item.at("assets").is_array()) {
            continue;
        }
        EngineReleaseUVE release;
        release.version = *version;
        release.channel = item.value("prerelease", false) ? EngineChannelUVE::Beta : EngineChannelUVE::Stable;
        release.tag = tag;
        release.title = item.value("name", tag);
        release.publishedAt = item.value("published_at", std::string{});
        release.pageUrl = item.value("html_url", std::string{});
        release.packageName = EnginePackageNameUVE(*version, target);
        const std::string checksumName = release.packageName + std::string(kEngineChecksumSuffixUVE);
        for (const JsonUVE& asset : item.at("assets")) {
            if (!asset.is_object()) {
                continue;
            }
            const std::string name = asset.value("name", std::string{});
            // The API address serves the file itself when asked for octet-stream.
            const std::string url = asset.value("url", std::string{});
            if (name == release.packageName) {
                release.packageUrl = url;
                release.packageBytes = asset.value("size", std::uint64_t{0});
            } else if (name == checksumName) {
                release.checksumUrl = url;
            }
        }
        if (!release.packageUrl.empty() && !release.checksumUrl.empty()) {
            releases.push_back(std::move(release));
        }
    }
    std::sort(releases.begin(), releases.end(), [](const EngineReleaseUVE& lhs, const EngineReleaseUVE& rhs) {
        return VersionKeyUVE(lhs.version) > VersionKeyUVE(rhs.version);
    });
    return releases;
}

EngineReleaseGroupsUVE GroupEngineReleasesUVE(const std::vector<EngineReleaseUVE>& releases) {
    EngineReleaseGroupsUVE groups;
    for (const EngineReleaseUVE& release : releases) {
        if (release.channel == EngineChannelUVE::Stable) {
            if (!groups.stable.has_value()) {
                groups.stable = release;
            } else {
                groups.previous.push_back(release);
            }
        }
    }
    for (const EngineReleaseUVE& release : releases) {
        if (release.channel == EngineChannelUVE::Beta &&
            (!groups.stable.has_value() || VersionKeyUVE(release.version) > VersionKeyUVE(groups.stable->version))) {
            groups.beta = release;
            break;
        }
    }
    return groups;
}

std::vector<HttpHeaderUVE> MakeFeedHeadersUVE(const std::string_view accept) {
    return {{"Accept", std::string(accept)},
            {std::string(kApiVersionHeaderUVE), std::string(kApiVersionUVE)},
            {"User-Agent", std::string(kUserAgentUVE)}};
}

EngineFeedResultUVE FetchEngineReleasesUVE(IHttpClientUVE& client, const EngineFeedSourceUVE& source,
                                           const std::string_view target) {
    HttpRequestUVE request;
    request.url = source.apiRoot + "/repos/" + source.owner + "/" + source.repository +
                  "/releases?per_page=" + std::to_string(kReleasesPerPageUVE);
    request.headers = MakeFeedHeadersUVE(kJsonMediaTypeUVE);
    const HttpResponseUVE response = client.GetUVE(request);
    EngineFeedResultUVE result;
    if (!response.reached) {
        result.state = EngineFeedStateUVE::Offline;
        result.message = "Offline: " + response.error;
        return result;
    }
    if (response.status == kNotFoundStatusUVE) {
        result.state = EngineFeedStateUVE::NotFound;
        result.message = "The engine release list could not be found; installed engines still work.";
        return result;
    }
    // Unauthenticated requests are rate limited per address; the limit lifts within the hour.
    if (response.status == kRateLimitedStatusUVE) {
        result.state = EngineFeedStateUVE::Failed;
        result.message = "The engine release list is busy right now; check again later.";
        return result;
    }
    if (!response.IsSuccessUVE()) {
        result.state = EngineFeedStateUVE::Failed;
        result.message = "The release list answered with HTTP " + std::to_string(response.status) + ".";
        return result;
    }
    std::string error;
    result.releases = ParseGitHubReleasesUVE(response.body, target, &error);
    if (!error.empty()) {
        result.state = EngineFeedStateUVE::Failed;
        result.message = error;
        return result;
    }
    result.state = EngineFeedStateUVE::Online;
    result.message = result.releases.empty()
                         ? "No engine versions have been published for this platform yet."
                         : std::to_string(result.releases.size()) + " engine versions published.";
    return result;
}

} // namespace UVE::Online
