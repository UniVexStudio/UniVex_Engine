// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <memory>
#include <span>
#include <thread>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <zlib.h>

#include "Support/test_scratch.h"

#include "application_runtime.h"
#include "engine_library.h"
#include "engine_release_feed.h"
#include "process_launcher.h"
#include "sha256.h"
#include "tar_gz_archive.h"

namespace UVE::Online::Tests {
namespace {

// --- helpers ------------------------------------------------------------------------------------

/// Builds a gzip-compressed ustar archive in memory, entry by entry, for the extractor tests.
class TarGzWriterUVE final {
public:
    void AddFileUVE(const std::string& name, const std::string& contents, const unsigned mode = 0644U,
                    const char type = '0') {
        HeaderUVE(name, contents.size(), mode, type);
        m_tar.append(contents);
        m_tar.append((512U - (contents.size() % 512U)) % 512U, '\0');
    }
    void AddDirectoryUVE(const std::string& name) { HeaderUVE(name, 0U, 0755U, '5'); }

    void WriteUVE(const std::filesystem::path& file) {
        m_tar.append(1024U, '\0');
        gzFile output = gzopen(file.string().c_str(), "wb");
        ASSERT_NE(output, nullptr);
        ASSERT_EQ(gzwrite(output, m_tar.data(), static_cast<unsigned>(m_tar.size())), static_cast<int>(m_tar.size()));
        gzclose(output);
    }

private:
    void HeaderUVE(const std::string& name, const std::size_t size, const unsigned mode, const char type) {
        std::array<char, 512U> header{};
        std::memcpy(header.data(), name.data(), std::min<std::size_t>(name.size(), 100U));
        std::snprintf(header.data() + 100, 8, "%07o", mode);
        std::snprintf(header.data() + 108, 8, "%07o", 0U);
        std::snprintf(header.data() + 116, 8, "%07o", 0U);
        std::snprintf(header.data() + 124, 12, "%011llo", static_cast<unsigned long long>(size));
        std::snprintf(header.data() + 136, 12, "%011o", 0U);
        header[156] = type;
        std::memcpy(header.data() + 257, "ustar", 6);
        std::memcpy(header.data() + 263, "00", 2);
        std::memset(header.data() + 148, ' ', 8);
        unsigned checksum = 0U;
        for (const char value : header) {
            checksum += static_cast<unsigned char>(value);
        }
        std::snprintf(header.data() + 148, 8, "%06o", checksum);
        m_tar.append(header.data(), header.size());
    }

    std::string m_tar;
};

[[nodiscard]] std::string ReadTextUVE(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

/// Serves fixed responses by URL, recording what it was asked.
class FakeHttpClientUVE final : public IHttpClientUVE {
public:
    std::map<std::string, HttpResponseUVE> responses;
    std::map<std::string, std::filesystem::path> files;
    std::vector<HttpRequestUVE> requests;

    HttpResponseUVE GetUVE(const HttpRequestUVE& request) override {
        requests.push_back(request);
        const auto found = responses.find(request.url);
        return found == responses.end() ? HttpResponseUVE{false, 0, {}, "unreachable"} : found->second;
    }
    HttpResponseUVE DownloadUVE(const HttpRequestUVE& request, const std::filesystem::path& file,
                                const HttpProgressUVE& progress) override {
        requests.push_back(request);
        const auto found = files.find(request.url);
        if (found == files.end()) {
            return {false, 0, {}, "unreachable"};
        }
        std::filesystem::copy_file(found->second, file, std::filesystem::copy_options::overwrite_existing);
        const auto size = std::filesystem::file_size(file);
        if (progress && !progress(size, size)) {
            return {false, 0, {}, "cancelled"};
        }
        return {true, 200, {}, {}};
    }
};

inline constexpr Core::VersionUVE kPackagedVersionUVE{2U, 1U, 0U, 3U};

/// Writes an engine package for kPackagedVersionUVE targeting this platform.
[[nodiscard]] std::filesystem::path WriteEnginePackageUVE(const std::filesystem::path& directory,
                                                          const Core::VersionUVE& version = kPackagedVersionUVE) {
    TarGzWriterUVE writer;
    const std::string manifest = std::string(R"({"format":"uvengine","schemaVersion":1,"version":")") +
                                 version.ToStringUVE() + R"(","channel":"beta","target":")" +
                                 std::string(Platform::GetApplicationTargetUVE()) + "\"}";
    writer.AddFileUVE(std::string(kEngineManifestFileNameUVE), manifest);
    writer.AddFileUVE(std::string(Pack::kEditorExecutableStemUVE) + std::string(Platform::kExecutableSuffixUVE),
                      "editor", 0755U);
    writer.AddDirectoryUVE("project_templates/");
    writer.AddFileUVE("project_templates/Blank/template.json", "{}");
    const std::filesystem::path package = directory / EnginePackageNameUVE(version, Platform::GetApplicationTargetUVE());
    writer.WriteUVE(package);
    return package;
}

[[nodiscard]] InstalledEngineUVE LocalBuildUVE(const Core::VersionUVE& version) {
    InstalledEngineUVE local;
    local.install.version = version;
    local.localBuild = true;
    return local;
}

// --- SHA-256 ------------------------------------------------------------------------------------

TEST(Sha256UVETest, MatchesPublishedVectors) {
    Sha256UVE empty;
    EXPECT_EQ(ToHexUVE(empty.FinishUVE()), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    Sha256UVE abc;
    const std::string_view text = "abc";
    abc.UpdateUVE(std::as_bytes(std::span(text.data(), text.size())));
    EXPECT_EQ(ToHexUVE(abc.FinishUVE()), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Sha256UVE twoBlocks;
    const std::string_view longer = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    twoBlocks.UpdateUVE(std::as_bytes(std::span(longer.data(), longer.size())));
    EXPECT_EQ(ToHexUVE(twoBlocks.FinishUVE()), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256UVETest, ParsesHexInEitherCaseAndRejectsTheRest) {
    const std::string hex = "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD";
    const std::optional<Sha256DigestUVE> digest = ParseSha256HexUVE(hex);
    ASSERT_TRUE(digest.has_value());
    EXPECT_EQ(ToHexUVE(*digest), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_FALSE(ParseSha256HexUVE("abc").has_value());
    EXPECT_FALSE(ParseSha256HexUVE(std::string(64U, 'z')).has_value());
}

// --- archives -----------------------------------------------------------------------------------

TEST(TarGzArchiveUVETest, ExtractsFilesDirectoriesAndExecutableBits) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("tar");
    TarGzWriterUVE writer;
    writer.AddDirectoryUVE("bin/");
    writer.AddFileUVE("bin/tool", "#!/bin/sh\n", 0755U);
    writer.AddFileUVE("data/readme.txt", std::string(1500U, 'x'));
    writer.WriteUVE(root / "a.tar.gz");
    std::filesystem::create_directories(root / "out");

    const ArchiveExtractResultUVE result = ExtractTarGzUVE(root / "a.tar.gz", root / "out");

    ASSERT_TRUE(result.extracted) << result.message;
    EXPECT_EQ(ReadTextUVE(root / "out" / "bin" / "tool"), "#!/bin/sh\n");
    EXPECT_EQ(std::filesystem::file_size(root / "out" / "data" / "readme.txt"), 1500U);
#if !defined(_WIN32)
    EXPECT_NE(std::filesystem::status(root / "out" / "bin" / "tool").permissions() & std::filesystem::perms::owner_exec,
              std::filesystem::perms::none);
#endif
    std::filesystem::remove_all(root);
}

TEST(TarGzArchiveUVETest, ExtractsOnlyTheEntriesAFilterAccepts) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("tarfilter");
    TarGzWriterUVE writer;
    writer.AddFileUVE("keep/a.txt", "a");
    writer.AddFileUVE("skip/b.txt", std::string(2000U, 'b'));
    writer.AddFileUVE("keep/c.txt", "c");
    writer.WriteUVE(root / "f.tar.gz");
    std::filesystem::create_directories(root / "out");
    ArchiveLimitsUVE limits;
    limits.include = [](const std::filesystem::path& path) { return *path.begin() == "keep"; };

    ASSERT_TRUE(ExtractTarGzUVE(root / "f.tar.gz", root / "out", limits).extracted);
    EXPECT_EQ(ReadTextUVE(root / "out" / "keep" / "c.txt"), "c");
    EXPECT_FALSE(std::filesystem::exists(root / "out" / "skip"));
    std::filesystem::remove_all(root);
}

TEST(TarGzArchiveUVETest, RefusesEscapesLinksAndOversizedArchives) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("tarbad");
    const auto attempt = [&root](TarGzWriterUVE& writer, const ArchiveLimitsUVE& limits = {}) {
        writer.WriteUVE(root / "bad.tar.gz");
        std::filesystem::remove_all(root / "out");
        std::filesystem::create_directories(root / "out");
        return ExtractTarGzUVE(root / "bad.tar.gz", root / "out", limits).extracted;
    };
    TarGzWriterUVE escape;
    escape.AddFileUVE("../outside.txt", "x");
    EXPECT_FALSE(attempt(escape));
    EXPECT_FALSE(std::filesystem::exists(root / "outside.txt"));

    TarGzWriterUVE absolute;
    absolute.AddFileUVE("/etc/evil", "x");
    EXPECT_FALSE(attempt(absolute));

    TarGzWriterUVE link;
    link.AddFileUVE("link", "", 0777U, '2');
    EXPECT_FALSE(attempt(link));

    TarGzWriterUVE big;
    big.AddFileUVE("big.bin", std::string(4096U, 'b'));
    ArchiveLimitsUVE small;
    small.maximumTotalBytes = 1024U;
    EXPECT_FALSE(attempt(big, small));

    std::ofstream(root / "garbage.tar.gz") << "not gzip";
    std::filesystem::remove_all(root / "out");
    std::filesystem::create_directories(root / "out");
    EXPECT_FALSE(ExtractTarGzUVE(root / "garbage.tar.gz", root / "out").extracted);
    std::filesystem::remove_all(root);
}

// --- release feed -------------------------------------------------------------------------------

TEST(EngineReleaseFeedUVETest, ParsesVersionTags) {
    EXPECT_EQ(ParseEngineVersionTagUVE("v1.2.3"), (Core::VersionUVE{1U, 2U, 3U, 0U}));
    EXPECT_EQ(ParseEngineVersionTagUVE("1.2.3+build.4"), (Core::VersionUVE{1U, 2U, 3U, 4U}));
    EXPECT_EQ(ParseEngineVersionTagUVE("v1.2.3.4"), (Core::VersionUVE{1U, 2U, 3U, 4U}));
    EXPECT_EQ(ParseEngineVersionTagUVE(Core::VersionUVE{0U, 1U, 0U, 1U}.ToStringUVE()),
              (Core::VersionUVE{0U, 1U, 0U, 1U}));
    EXPECT_EQ(ParseEngineVersionTagUVE("v1.3.0-beta"), (Core::VersionUVE{1U, 3U, 0U, 0U}));
    EXPECT_FALSE(ParseEngineVersionTagUVE("v1.2").has_value());
    EXPECT_FALSE(ParseEngineVersionTagUVE("release-7").has_value());
    EXPECT_FALSE(ParseEngineVersionTagUVE("v1.2.3.4+build.5").has_value());
}

[[nodiscard]] std::string ReleaseJsonUVE(const std::string& tag, const bool prerelease, const bool draft,
                                         const std::string& target, const bool withChecksum = true) {
    const std::optional<Core::VersionUVE> version = ParseEngineVersionTagUVE(tag);
    const std::string package = EnginePackageNameUVE(*version, target);
    std::string assets = R"({"name":")" + package + R"(","url":"https://api.example/assets/)" + tag +
                         R"(/pkg","size":1234})";
    if (withChecksum) {
        assets += R"(,{"name":")" + package + R"(.sha256","url":"https://api.example/assets/)" + tag + R"(/sum","size":64})";
    }
    return R"({"tag_name":")" + tag + R"(","name":"UniVex )" + tag + R"(","prerelease":)" +
           (prerelease ? "true" : "false") + R"(,"draft":)" + (draft ? "true" : "false") + R"(,"assets":[)" + assets +
           "]}";
}

TEST(EngineReleaseFeedUVETest, PackageNamesRoundTrip) {
    const Core::VersionUVE version{1U, 2U, 3U, 4U};
    EXPECT_EQ(ParseEnginePackageNameUVE(EnginePackageNameUVE(version, "linux"), "linux"), version);
    EXPECT_FALSE(ParseEnginePackageNameUVE(EnginePackageNameUVE(version, "linux"), "windows").has_value());
    EXPECT_FALSE(ParseEnginePackageNameUVE("something.uvpackage", "linux").has_value());
}

TEST(EngineReleaseFeedUVETest, ParsesReleasesForThisTargetNewestFirst) {
    const std::string target(Platform::GetApplicationTargetUVE());
    const std::string json = "[" + ReleaseJsonUVE("v0.9.0", false, false, target) + "," +
                             ReleaseJsonUVE("v1.1.0", true, false, target) + "," +
                             ReleaseJsonUVE("v1.0.0", false, false, target) + "," +
                             ReleaseJsonUVE("v2.0.0", false, true, target) + "," +
                             ReleaseJsonUVE("v1.0.1", false, false, "other-target") + "," +
                             ReleaseJsonUVE("v1.0.2", false, false, target, false) + "]";
    const std::vector<EngineReleaseUVE> releases = ParseGitHubReleasesUVE(json, target);
    ASSERT_EQ(releases.size(), 3U);
    EXPECT_EQ(releases[0].version, (Core::VersionUVE{1U, 1U, 0U, 0U}));
    EXPECT_EQ(releases[0].channel, EngineChannelUVE::Beta);
    EXPECT_EQ(releases[1].tag, "v1.0.0");
    EXPECT_EQ(releases[1].packageBytes, 1234U);
    EXPECT_EQ(releases[1].checksumUrl, "https://api.example/assets/v1.0.0/sum");

    const EngineReleaseGroupsUVE groups = GroupEngineReleasesUVE(releases);
    ASSERT_TRUE(groups.stable.has_value());
    EXPECT_EQ(groups.stable->tag, "v1.0.0");
    ASSERT_TRUE(groups.beta.has_value());
    EXPECT_EQ(groups.beta->tag, "v1.1.0");
    ASSERT_EQ(groups.previous.size(), 1U);
    EXPECT_EQ(groups.previous[0].tag, "v0.9.0");
}

TEST(EngineReleaseFeedUVETest, OlderBetaThanStableIsNotOffered) {
    const std::string target(Platform::GetApplicationTargetUVE());
    const std::string json =
        "[" + ReleaseJsonUVE("v1.0.0", false, false, target) + "," + ReleaseJsonUVE("v0.9.0", true, false, target) + "]";
    EXPECT_FALSE(GroupEngineReleasesUVE(ParseGitHubReleasesUVE(json, target)).beta.has_value());
}

TEST(EngineReleaseFeedUVETest, FetchReportsOfflineNotFoundBusyAndOnline) {
    const EngineFeedSourceUVE source{"https://api.example", "studio", "engine"};
    const std::string url = "https://api.example/repos/studio/engine/releases?per_page=100";
    FakeHttpClientUVE client;
    EXPECT_EQ(FetchEngineReleasesUVE(client, source, "linux").state, EngineFeedStateUVE::Offline);

    client.responses[url] = {true, 404, "{}", {}};
    EXPECT_EQ(FetchEngineReleasesUVE(client, source, "linux").state, EngineFeedStateUVE::NotFound);

    client.responses[url] = {true, 403, "{}", {}};
    EXPECT_EQ(FetchEngineReleasesUVE(client, source, "linux").state, EngineFeedStateUVE::Failed);

    client.responses[url] = {true, 200, "[" + ReleaseJsonUVE("v1.0.0", false, false, "linux") + "]", {}};
    const EngineFeedResultUVE online = FetchEngineReleasesUVE(client, source, "linux");
    EXPECT_EQ(online.state, EngineFeedStateUVE::Online);
    EXPECT_EQ(online.releases.size(), 1U);
    // The feed is public: no request ever carries credentials.
    for (const HttpRequestUVE& request : client.requests) {
        for (const HttpHeaderUVE& header : request.headers) {
            EXPECT_NE(header.name, "Authorization");
        }
    }
}

TEST(EngineReleaseFeedUVETest, LoadsFeedSourceAndRejectsInsecureOnes) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("feed");
    std::ofstream(root / "good.json")
        << R"({"format":"uvenginefeed","schemaVersion":1,"owner":"studio","repository":"engine"})";
    std::ofstream(root / "insecure.json")
        << R"({"format":"uvenginefeed","schemaVersion":1,"apiRoot":"http://x","owner":"a","repository":"b"})";
    const std::optional<EngineFeedSourceUVE> good = LoadEngineFeedSourceUVE(root / "good.json");
    ASSERT_TRUE(good.has_value());
    EXPECT_EQ(good->apiRoot, kDefaultFeedApiRootUVE);
    EXPECT_FALSE(LoadEngineFeedSourceUVE(root / "insecure.json").has_value());
    EXPECT_FALSE(LoadEngineFeedSourceUVE(root / "missing.json").has_value());
    std::filesystem::remove_all(root);
}

// --- engine library -----------------------------------------------------------------------------

TEST(EngineLibraryUVETest, InstallsVerifiedPackageAndListsIt) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("library");
    const std::filesystem::path package = WriteEnginePackageUVE(root);
    const std::filesystem::path engines = root / "Engines";

    Sha256DigestUVE wrong{};
    EXPECT_FALSE(InstallEnginePackageUVE(package, wrong, kPackagedVersionUVE, engines).installed);

    const EngineInstallOutcomeUVE installed =
        InstallEnginePackageUVE(package, *HashFileSha256UVE(package), kPackagedVersionUVE, engines);
    ASSERT_TRUE(installed.installed) << installed.message;
    EXPECT_FALSE(InstallEnginePackageUVE(package, *HashFileSha256UVE(package), kPackagedVersionUVE, engines).installed);

    const std::vector<InstalledEngineUVE> listed =
        ListInstalledEnginesUVE(engines, LocalBuildUVE(Core::VersionUVE{1U, 0U, 0U, 0U}));
    ASSERT_EQ(listed.size(), 2U);
    EXPECT_EQ(listed[0].install.version, kPackagedVersionUVE);
    EXPECT_EQ(listed[0].channel, EngineChannelUVE::Beta);
    EXPECT_FALSE(listed[0].localBuild);
    EXPECT_TRUE(listed[1].localBuild);

    EXPECT_EQ(ListInstalledEnginesUVE(engines).size(), 1U);
    EXPECT_TRUE(UninstallEngineUVE(engines, kPackagedVersionUVE));
    EXPECT_EQ(ListInstalledEnginesUVE(engines, LocalBuildUVE(Core::VersionUVE{1U, 0U, 0U, 0U})).size(), 1U);
    std::filesystem::remove_all(root);
}

TEST(EngineLibraryUVETest, RefusesPackageForAnotherVersion) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("mismatch");
    const std::filesystem::path package = WriteEnginePackageUVE(root);
    EXPECT_FALSE(InstallEnginePackageUVE(package, *HashFileSha256UVE(package), Core::VersionUVE{9U, 9U, 9U, 9U},
                                         root / "Engines")
                     .installed);
    EXPECT_FALSE(std::filesystem::exists(root / "Engines" / Core::VersionUVE{9U, 9U, 9U, 9U}.ToStringUVE()));
    std::filesystem::remove_all(root);
}

TEST(EngineLibraryUVETest, ImportsLocalPackageVerifiedByItsChecksumFile) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("import_verified");
    const std::filesystem::path package = WriteEnginePackageUVE(root);
    std::filesystem::path checksum = package;
    checksum += kEngineChecksumSuffixUVE;
    std::ofstream(checksum) << ToHexUVE(*HashFileSha256UVE(package)) << "  " << package.filename().string() << '\n';

    const EngineInstallOutcomeUVE outcome = InstallLocalEnginePackageUVE(package, root / "Engines");
    ASSERT_TRUE(outcome.installed) << outcome.message;
    EXPECT_TRUE(outcome.verified);
    ASSERT_TRUE(outcome.version.has_value());
    EXPECT_EQ(*outcome.version, kPackagedVersionUVE);
    EXPECT_FALSE(InstallLocalEnginePackageUVE(package, root / "Engines", false).installed);
    std::filesystem::remove_all(root);
}

TEST(EngineLibraryUVETest, ReimportingLocalPackageAtomicallyReplacesExistingInstall) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("import_replace");
    const std::filesystem::path engines = root / "Engines";
    const std::string editorFile =
        std::string(Pack::kEditorExecutableStemUVE) + std::string(Platform::kExecutableSuffixUVE);

    const auto writeCustom = [&](const std::string& filename, const std::string& payload) {
        TarGzWriterUVE writer;
        const std::string manifest = std::string(R"({"format":"uvengine","schemaVersion":1,"version":")") +
                                     kPackagedVersionUVE.ToStringUVE() + R"(","channel":"beta","target":")" +
                                     std::string(Platform::GetEnginePackageTargetUVE()) + "\"}";
        writer.AddFileUVE(std::string(kEngineManifestFileNameUVE), manifest);
        writer.AddFileUVE(editorFile, payload, 0755U);
        const std::filesystem::path path = root / filename;
        writer.WriteUVE(path);
        return path;
    };

    const std::filesystem::path v1 = writeCustom("dev-v1.uvpackage", "editor-v1");
    const std::filesystem::path v2 = writeCustom("dev-v2.uvpackage", "editor-v2");
    ASSERT_TRUE(InstallLocalEnginePackageUVE(v1, engines).installed);
    const std::filesystem::path installedEditor = engines / kPackagedVersionUVE.ToStringUVE() / editorFile;
    EXPECT_EQ(ReadTextUVE(installedEditor), "editor-v1");

    const EngineInstallOutcomeUVE replaced = InstallLocalEnginePackageUVE(v2, engines);
    ASSERT_TRUE(replaced.installed) << replaced.message;
    EXPECT_EQ(ReadTextUVE(installedEditor), "editor-v2");
    EXPECT_EQ(ListInstalledEnginesUVE(engines).size(), 1U);
    std::filesystem::remove_all(root);
}

TEST(EngineLibraryUVETest, TargetCompatibilityMatchesCurrentPlatform) {
    EXPECT_TRUE(Platform::IsCompatibleEnginePackageTargetUVE(Platform::GetEnginePackageTargetUVE()));
    EXPECT_FALSE(Platform::IsCompatibleEnginePackageTargetUVE("nonexistent-target"));
#if defined(__linux__) && !defined(__ANDROID__)
    EXPECT_FALSE(Platform::IsCompatibleEnginePackageTargetUVE("windows"));
    EXPECT_FALSE(Platform::IsCompatibleEnginePackageTargetUVE("android-arm64-v8a"));
#endif
}

TEST(EngineLibraryUVETest, ImportsRenamedPackageWithoutChecksumAsUnverified) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("import_unverified");
    const std::filesystem::path renamed = root / "my-engine-copy.uvpackage";
    std::filesystem::rename(WriteEnginePackageUVE(root), renamed);

    const EngineInstallOutcomeUVE outcome = InstallLocalEnginePackageUVE(renamed, root / "Engines");
    ASSERT_TRUE(outcome.installed) << outcome.message;
    EXPECT_FALSE(outcome.verified);
    EXPECT_NE(outcome.message.find("not verified"), std::string::npos);
    EXPECT_TRUE(std::filesystem::is_directory(root / "Engines" / kPackagedVersionUVE.ToStringUVE()));
    std::filesystem::remove_all(root);
}

TEST(EngineLibraryUVETest, RefusesLocalPackageThatDoesNotMatchItsChecksum) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("import_tampered");
    const std::filesystem::path package = WriteEnginePackageUVE(root);
    std::filesystem::path checksum = package;
    checksum += kEngineChecksumSuffixUVE;
    std::ofstream(checksum) << ToHexUVE(Sha256DigestUVE{}) << '\n';

    EXPECT_FALSE(InstallLocalEnginePackageUVE(package, root / "Engines").installed);
    EXPECT_FALSE(InstallLocalEnginePackageUVE(root / "missing.tar.gz", root / "Engines").installed);
    EXPECT_FALSE(std::filesystem::exists(root / "Engines" / kPackagedVersionUVE.ToStringUVE()));
    std::filesystem::remove_all(root);
}

TEST(EngineLibraryUVETest, ChoosesTheEngineForAProject) {
    std::vector<InstalledEngineUVE> engines{LocalBuildUVE({2U, 0U, 0U, 0U}), LocalBuildUVE({1U, 4U, 0U, 0U}),
                                            LocalBuildUVE({1U, 2U, 0U, 0U})};
    EXPECT_EQ(ChooseEngineForProjectUVE(engines, {1U, 2U, 0U, 0U}, "")->install.version, (Core::VersionUVE{1U, 2U, 0U, 0U}));
    EXPECT_EQ(ChooseEngineForProjectUVE(engines, {1U, 3U, 0U, 0U}, "")->install.version, (Core::VersionUVE{2U, 0U, 0U, 0U}));
    EXPECT_EQ(ChooseEngineForProjectUVE(engines, {1U, 2U, 0U, 0U}, Core::VersionUVE{1U, 4U, 0U, 0U}.ToStringUVE())
                  ->install.version,
              (Core::VersionUVE{1U, 4U, 0U, 0U}));
    EXPECT_EQ(ChooseEngineForProjectUVE(engines, {3U, 0U, 0U, 0U}, ""), nullptr);
}

TEST(EngineLibraryUVETest, DownloadJobFetchesVerifiesAndInstalls) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("download");
    const std::filesystem::path package = WriteEnginePackageUVE(root);
    auto client = std::make_unique<FakeHttpClientUVE>();
    EngineReleaseUVE release;
    release.version = kPackagedVersionUVE;
    release.packageUrl = "https://api.example/pkg";
    release.checksumUrl = "https://api.example/sum";
    client->responses[release.checksumUrl] = {true, 200, ToHexUVE(*HashFileSha256UVE(package)) + "  pkg\n", {}};
    client->files[release.packageUrl] = package;

    EngineDownloadUVE job(std::move(client), release, root / "Engines");
    for (int attempt = 0; attempt < 500 && job.GetStateUVE() == EngineDownloadUVE::StateUVE::Running; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(job.GetStateUVE(), EngineDownloadUVE::StateUVE::Installed) << job.MessageUVE();
    EXPECT_TRUE(ReadEngineManifestUVE(root / "Engines" / kPackagedVersionUVE.ToStringUVE()).has_value());
    std::filesystem::remove_all(root);
}

TEST(EngineLibraryUVETest, DownloadJobReportsOffline) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("offline");
    EngineReleaseUVE release;
    release.version = kPackagedVersionUVE;
    release.packageUrl = "https://api.example/pkg";
    release.checksumUrl = "https://api.example/sum";
    EngineDownloadUVE job(std::make_unique<FakeHttpClientUVE>(), release, root / "Engines");
    for (int attempt = 0; attempt < 500 && job.GetStateUVE() == EngineDownloadUVE::StateUVE::Running; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(job.GetStateUVE(), EngineDownloadUVE::StateUVE::Failed);
    EXPECT_NE(job.MessageUVE().find("Offline"), std::string::npos);
    std::filesystem::remove_all(root);
}

} // namespace
} // namespace UVE::Online::Tests
