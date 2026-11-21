// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine_version.h"
#include "http_client.h"

namespace UVE::Online {

/// Where a launcher looks for engine releases. Shipped beside the launcher as
/// kEngineFeedFileNameUVE, so the source is configuration, not code.
inline constexpr std::string_view kEngineFeedFileNameUVE = "engine_feed.json";
inline constexpr std::string_view kEngineFeedFormatUVE = "uvenginefeed";
inline constexpr std::uint32_t kCurrentEngineFeedSchemaVersionUVE = 1U;
inline constexpr std::string_view kDefaultFeedApiRootUVE = "https://api.github.com";

/// Release assets are named <prefix><version>-<target><extension>, with a checksum file beside
/// each package named <package><checksum suffix> holding its SHA-256 in hexadecimal.
inline constexpr std::string_view kEnginePackagePrefixUVE = "univex-engine-";
/// A gzip-compressed tar archive under the engine's own extension, so it is recognisable as an engine.
inline constexpr std::string_view kEnginePackageExtensionUVE = ".uvpackage";
inline constexpr std::string_view kEngineChecksumSuffixUVE = ".sha256";

enum class EngineChannelUVE : std::uint8_t {
    Stable = 0,
    Beta,
};

[[nodiscard]] std::string_view EngineChannelNameUVE(EngineChannelUVE channel) noexcept;
[[nodiscard]] std::optional<EngineChannelUVE> ParseEngineChannelUVE(std::string_view name) noexcept;

struct EngineFeedSourceUVE final {
    std::string apiRoot{kDefaultFeedApiRootUVE};
    std::string owner;
    std::string repository;
};

/// Reads the feed configuration; nothing when the file is missing or malformed.
[[nodiscard]] std::optional<EngineFeedSourceUVE> LoadEngineFeedSourceUVE(const std::filesystem::path& file);

/// One published engine version that has a package for the running platform.
struct EngineReleaseUVE final {
    Core::VersionUVE version;
    EngineChannelUVE channel = EngineChannelUVE::Stable;
    std::string tag;
    std::string title;
    std::string publishedAt;
    std::string pageUrl;
    std::string packageName;
    std::string packageUrl;
    std::uint64_t packageBytes = 0U;
    std::string checksumUrl;
};

/// Suffix of a release tag published to the Beta channel, e.g. "v1.3.0-beta".
inline constexpr std::string_view kBetaTagSuffixUVE = "-beta";

/// The engine version a release tag names: "v1.2.3", "1.2.3", "v1.2.3+build.4" or "v1.2.3.4",
/// optionally followed by kBetaTagSuffixUVE.
[[nodiscard]] std::optional<Core::VersionUVE> ParseEngineVersionTagUVE(std::string_view tag);

/// The package asset name for `version` on `target`.
[[nodiscard]] std::string EnginePackageNameUVE(const Core::VersionUVE& version, std::string_view target);
/// The version a package file name carries for `target`, if it is one.
[[nodiscard]] std::optional<Core::VersionUVE> ParseEnginePackageNameUVE(std::string_view name, std::string_view target);

/// Releases from a GitHub "list releases" response that carry a package and a checksum for
/// `target`, newest version first. Drafts are skipped; prereleases are the Beta channel.
[[nodiscard]] std::vector<EngineReleaseUVE> ParseGitHubReleasesUVE(std::string_view json, std::string_view target,
                                                                   std::string* errorMessage = nullptr);

/// The releases a launcher shows: the newest Stable, the newest Beta when it is newer than that
/// Stable, and the older Stable versions.
struct EngineReleaseGroupsUVE final {
    std::optional<EngineReleaseUVE> stable;
    std::optional<EngineReleaseUVE> beta;
    std::vector<EngineReleaseUVE> previous;
};
[[nodiscard]] EngineReleaseGroupsUVE GroupEngineReleasesUVE(const std::vector<EngineReleaseUVE>& releases);

enum class EngineFeedStateUVE : std::uint8_t {
    Online = 0,
    /// No response at all: no network. The launcher shows installed engines only.
    Offline,
    /// The feed answered that the release list does not exist (a wrong or unpublished source).
    NotFound,
    /// The feed answered with something unusable.
    Failed,
};

struct EngineFeedResultUVE final {
    EngineFeedStateUVE state = EngineFeedStateUVE::Offline;
    std::string message;
    std::vector<EngineReleaseUVE> releases;
};

/// The request headers every feed call sends. The feed is public: no credentials are ever sent.
[[nodiscard]] std::vector<HttpHeaderUVE> MakeFeedHeadersUVE(std::string_view accept);

/// Lists the releases for `target`. Never throws; an unreachable feed is Offline.
[[nodiscard]] EngineFeedResultUVE FetchEngineReleasesUVE(IHttpClientUVE& client, const EngineFeedSourceUVE& source,
                                                         std::string_view target);

} // namespace UVE::Online
