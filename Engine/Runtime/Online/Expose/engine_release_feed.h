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
/// kEngineFeedFileName, so the source is configuration, not code.
inline constexpr std::string_view kEngineFeedFileName = "engine_feed.json";
inline constexpr std::string_view kEngineFeedFormat = "uvenginefeed";
inline constexpr std::uint32_t kCurrentEngineFeedSchemaVersion = 1U;
inline constexpr std::string_view kDefaultFeedApiRoot = "https://api.github.com";

/// Release assets are named <prefix><version>-<target><extension>, with a checksum file beside
/// each package named <package><checksum suffix> holding its SHA-256 in hexadecimal.
inline constexpr std::string_view kEnginePackagePrefix = "univex-engine-";
/// A gzip-compressed tar archive under the engine's own extension, so it is recognisable as an engine.
inline constexpr std::string_view kEnginePackageExtension = ".uvpackage";
inline constexpr std::string_view kEngineChecksumSuffix = ".sha256";

enum class EngineChannel : std::uint8_t {
    Stable = 0,
    Beta,
};

[[nodiscard]] std::string_view EngineChannelName(EngineChannel channel) noexcept;
[[nodiscard]] std::optional<EngineChannel> ParseEngineChannel(std::string_view name) noexcept;

struct EngineFeedSource final {
    std::string apiRoot{kDefaultFeedApiRoot};
    std::string owner;
    std::string repository;
};

/// Reads the feed configuration; nothing when the file is missing or malformed.
[[nodiscard]] std::optional<EngineFeedSource> LoadEngineFeedSource(const std::filesystem::path& file);

/// One published engine version that has a package for the running platform.
struct EngineRelease final {
    Core::Version version;
    EngineChannel channel = EngineChannel::Stable;
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
inline constexpr std::string_view kBetaTagSuffix = "-beta";

/// The engine version a release tag names: "v1.2.3", "1.2.3", "v1.2.3+build.4" or "v1.2.3.4",
/// optionally followed by kBetaTagSuffix.
[[nodiscard]] std::optional<Core::Version> ParseEngineVersionTag(std::string_view tag);

/// The package asset name for `version` on `target`.
[[nodiscard]] std::string EnginePackageName(const Core::Version& version, std::string_view target);
/// The version a package file name carries for `target`, if it is one.
[[nodiscard]] std::optional<Core::Version> ParseEnginePackageName(std::string_view name, std::string_view target);

/// Releases from a GitHub "list releases" response that carry a package and a checksum for
/// `target`, newest version first. Drafts are skipped; prereleases are the Beta channel.
[[nodiscard]] std::vector<EngineRelease> ParseGitHubReleases(std::string_view json, std::string_view target,
                                                                   std::string* errorMessage = nullptr);

/// The releases a launcher shows: the newest Stable, the newest Beta when it is newer than that
/// Stable, and the older Stable versions.
struct EngineReleaseGroups final {
    std::optional<EngineRelease> stable;
    std::optional<EngineRelease> beta;
    std::vector<EngineRelease> previous;
};
[[nodiscard]] EngineReleaseGroups GroupEngineReleases(const std::vector<EngineRelease>& releases);

enum class EngineFeedState : std::uint8_t {
    Online = 0,
    /// No response at all: no network. The launcher shows installed engines only.
    Offline,
    /// The feed answered that the release list does not exist (a wrong or unpublished source).
    NotFound,
    /// The feed answered with something unusable.
    Failed,
};

struct EngineFeedResult final {
    EngineFeedState state = EngineFeedState::Offline;
    std::string message;
    std::vector<EngineRelease> releases;
};

/// The request headers every feed call sends. The feed is public: no credentials are ever sent.
[[nodiscard]] std::vector<HttpHeader> MakeFeedHeaders(std::string_view accept);

/// Lists the releases for `target`. Never throws; an unreachable feed is Offline.
[[nodiscard]] EngineFeedResult FetchEngineReleases(IHttpClient& client, const EngineFeedSource& source,
                                                         std::string_view target);

} // namespace UVE::Online
