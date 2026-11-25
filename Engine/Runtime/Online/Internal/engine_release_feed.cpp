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

using Json = nlohmann::json;

inline constexpr std::string_view kStableName = "stable";
inline constexpr std::string_view kBetaName = "beta";
inline constexpr std::string_view kBuildSeparator = "+build.";
inline constexpr char kTagPrefix = 'v';
/// How many releases one feed request asks for (the API's maximum page).
inline constexpr int kReleasesPerPage = 100;
inline constexpr std::string_view kJsonMediaType = "application/vnd.github+json";
inline constexpr std::string_view kApiVersionHeader = "X-GitHub-Api-Version";
inline constexpr std::string_view kApiVersion = "2022-11-28";
inline constexpr std::string_view kUserAgent = "UniVex-Hub";
inline constexpr long kRateLimitedStatus = 403L;
inline constexpr long kNotFoundStatus = 404L;

[[nodiscard]] std::optional<std::uint32_t> ParseNumber(const std::string_view text) {
    std::uint32_t value = 0U;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] auto VersionKey(const Core::Version& version) {
    return std::tie(version.major, version.minor, version.patch, version.build);
}

} // namespace

std::string_view EngineChannelName(const EngineChannel channel) noexcept {
    return channel == EngineChannel::Beta ? kBetaName : kStableName;
}

std::optional<EngineChannel> ParseEngineChannel(const std::string_view name) noexcept {
    if (name == kStableName) {
        return EngineChannel::Stable;
    }
    if (name == kBetaName) {
        return EngineChannel::Beta;
    }
    return std::nullopt;
}

std::optional<EngineFeedSource> LoadEngineFeedSource(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input.is_open()) {
        return std::nullopt;
    }
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const Json json = Json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object() || json.value("format", std::string{}) != kEngineFeedFormat ||
        json.value("schemaVersion", 0U) != kCurrentEngineFeedSchemaVersion) {
        return std::nullopt;
    }
    EngineFeedSource source;
    source.apiRoot = json.value("apiRoot", std::string(kDefaultFeedApiRoot));
    source.owner = json.value("owner", std::string{});
    source.repository = json.value("repository", std::string{});
    if (source.owner.empty() || source.repository.empty() || !source.apiRoot.starts_with("https://")) {
        return std::nullopt;
    }
    return source;
}

std::optional<Core::Version> ParseEngineVersionTag(std::string_view tag) {
    if (!tag.empty() && tag.front() == kTagPrefix) {
        tag.remove_prefix(1U);
    }
    if (tag.ends_with(kBetaTagSuffix)) {
        tag.remove_suffix(kBetaTagSuffix.size());
    }
    Core::Version version{};
    std::string_view numbers = tag;
    std::optional<std::uint32_t> separateBuild;
    if (const std::size_t build = tag.find(kBuildSeparator); build != std::string_view::npos) {
        const std::optional<std::uint32_t> buildNumber = ParseNumber(tag.substr(build + kBuildSeparator.size()));
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
        const std::optional<std::uint32_t> value = ParseNumber(numbers.substr(0U, dot));
        if (!value.has_value() || count == parts.size()) {
            return std::nullopt;
        }
        *parts[count++] = *value;
        if (dot == std::string_view::npos) {
            break;
        }
        numbers.remove_prefix(dot + 1U);
    }
    constexpr std::size_t kMinimumParts = 3U;
    constexpr std::size_t kPartsBeforeBuild = 3U;
    if (count < kMinimumParts || (separateBuild.has_value() && count != kPartsBeforeBuild)) {
        return std::nullopt;
    }
    if (separateBuild.has_value()) {
        version.build = *separateBuild;
    }
    return version;
}

std::string EnginePackageName(const Core::Version& version, const std::string_view target) {
    return std::string(kEnginePackagePrefix) + version.ToString() + "-" + std::string(target) +
           std::string(kEnginePackageExtension);
}

std::optional<Core::Version> ParseEnginePackageName(std::string_view name, const std::string_view target) {
    const std::string suffix = "-" + std::string(target) + std::string(kEnginePackageExtension);
    if (!name.starts_with(kEnginePackagePrefix) || !name.ends_with(suffix)) {
        return std::nullopt;
    }
    name.remove_prefix(kEnginePackagePrefix.size());
    name.remove_suffix(suffix.size());
    return ParseEngineVersionTag(name);
}

std::vector<EngineRelease> ParseGitHubReleases(const std::string_view json, const std::string_view target,
                                                     std::string* const errorMessage) {
    std::vector<EngineRelease> releases;
    const Json document = Json::parse(json, nullptr, false);
    if (document.is_discarded() || !document.is_array()) {
        if (errorMessage != nullptr) {
            *errorMessage = "The release list is not a JSON array.";
        }
        return releases;
    }
    for (const Json& item : document) {
        if (!item.is_object() || item.value("draft", false)) {
            continue;
        }
        const std::string tag = item.value("tag_name", std::string{});
        const std::optional<Core::Version> version = ParseEngineVersionTag(tag);
        if (!version.has_value() || !item.contains("assets") || !item.at("assets").is_array()) {
            continue;
        }
        EngineRelease release;
        release.version = *version;
        release.channel = item.value("prerelease", false) ? EngineChannel::Beta : EngineChannel::Stable;
        release.tag = tag;
        release.title = item.value("name", tag);
        release.publishedAt = item.value("published_at", std::string{});
        release.pageUrl = item.value("html_url", std::string{});
        release.packageName = EnginePackageName(*version, target);
        const std::string checksumName = release.packageName + std::string(kEngineChecksumSuffix);
        for (const Json& asset : item.at("assets")) {
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
    std::sort(releases.begin(), releases.end(), [](const EngineRelease& lhs, const EngineRelease& rhs) {
        return VersionKey(lhs.version) > VersionKey(rhs.version);
    });
    return releases;
}

EngineReleaseGroups GroupEngineReleases(const std::vector<EngineRelease>& releases) {
    EngineReleaseGroups groups;
    for (const EngineRelease& release : releases) {
        if (release.channel == EngineChannel::Stable) {
            if (!groups.stable.has_value()) {
                groups.stable = release;
            } else {
                groups.previous.push_back(release);
            }
        }
    }
    for (const EngineRelease& release : releases) {
        if (release.channel == EngineChannel::Beta &&
            (!groups.stable.has_value() || VersionKey(release.version) > VersionKey(groups.stable->version))) {
            groups.beta = release;
            break;
        }
    }
    return groups;
}

std::vector<HttpHeader> MakeFeedHeaders(const std::string_view accept) {
    return {{"Accept", std::string(accept)},
            {std::string(kApiVersionHeader), std::string(kApiVersion)},
            {"User-Agent", std::string(kUserAgent)}};
}

EngineFeedResult FetchEngineReleases(IHttpClient& client, const EngineFeedSource& source,
                                           const std::string_view target) {
    HttpRequest request;
    request.url = source.apiRoot + "/repos/" + source.owner + "/" + source.repository +
                  "/releases?per_page=" + std::to_string(kReleasesPerPage);
    request.headers = MakeFeedHeaders(kJsonMediaType);
    const HttpResponse response = client.Get(request);
    EngineFeedResult result;
    if (!response.reached) {
        result.state = EngineFeedState::Offline;
        result.message = "Offline: " + response.error;
        return result;
    }
    if (response.status == kNotFoundStatus) {
        result.state = EngineFeedState::NotFound;
        result.message = "The engine release list could not be found; installed engines still work.";
        return result;
    }
    // Unauthenticated requests are rate limited per address; the limit lifts within the hour.
    if (response.status == kRateLimitedStatus) {
        result.state = EngineFeedState::Failed;
        result.message = "The engine release list is busy right now; check again later.";
        return result;
    }
    if (!response.IsSuccess()) {
        result.state = EngineFeedState::Failed;
        result.message = "The release list answered with HTTP " + std::to_string(response.status) + ".";
        return result;
    }
    std::string error;
    result.releases = ParseGitHubReleases(response.body, target, &error);
    if (!error.empty()) {
        result.state = EngineFeedState::Failed;
        result.message = error;
        return result;
    }
    result.state = EngineFeedState::Online;
    result.message = result.releases.empty()
                         ? "No engine versions have been published for this platform yet."
                         : std::to_string(result.releases.size()) + " engine versions published.";
    return result;
}

} // namespace UVE::Online
