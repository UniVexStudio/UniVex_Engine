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
class TarGzWriter final {
public:
    void AddFile(const std::string& name, const std::string& contents, const unsigned mode = 0644U,
                    const char type = '0') {
        Header(name, contents.size(), mode, type);
        m_tar.append(contents);
        m_tar.append((512U - (contents.size() % 512U)) % 512U, '\0');
    }
    void AddDirectory(const std::string& name) { Header(name, 0U, 0755U, '5'); }

    void Write(const std::filesystem::path& file) {
        m_tar.append(1024U, '\0');
        gzFile output = gzopen(file.string().c_str(), "wb");
        ASSERT_NE(output, nullptr);
        ASSERT_EQ(gzwrite(output, m_tar.data(), static_cast<unsigned>(m_tar.size())), static_cast<int>(m_tar.size()));
        gzclose(output);
    }

private:
    void Header(const std::string& name, const std::size_t size, const unsigned mode, const char type) {
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

[[nodiscard]] std::string ReadText(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

/// Serves fixed responses by URL, recording what it was asked.
class FakeHttpClient final : public IHttpClient {
public:
    std::map<std::string, HttpResponse> responses;
    std::map<std::string, std::filesystem::path> files;
    std::vector<HttpRequest> requests;

    HttpResponse Get(const HttpRequest& request) override {
        requests.push_back(request);
        const auto found = responses.find(request.url);
        return found == responses.end() ? HttpResponse{false, 0, {}, "unreachable"} : found->second;
    }
    HttpResponse Download(const HttpRequest& request, const std::filesystem::path& file,
                                const HttpProgress& progress) override {
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

inline constexpr Core::Version kPackagedVersion{2U, 1U, 0U, 3U};

/// Writes an engine package for kPackagedVersion targeting this platform.
[[nodiscard]] std::filesystem::path WriteEnginePackage(const std::filesystem::path& directory,
                                                          const Core::Version& version = kPackagedVersion) {
    TarGzWriter writer;
    const std::string manifest = std::string(R"({"format":"uvengine","schemaVersion":1,"version":")") +
                                 version.ToString() + R"(","channel":"beta","target":")" +
                                 std::string(Platform::GetApplicationTarget()) + "\"}";
    writer.AddFile(std::string(kEngineManifestFileName), manifest);
    writer.AddFile(std::string(Pack::kEditorExecutableStem) + std::string(Platform::kExecutableSuffix),
                      "editor", 0755U);
    writer.AddDirectory("project_templates/");
    writer.AddFile("project_templates/Blank/template.json", "{}");
    const std::filesystem::path package = directory / EnginePackageName(version, Platform::GetApplicationTarget());
    writer.Write(package);
    return package;
}

[[nodiscard]] InstalledEngine LocalBuild(const Core::Version& version) {
    InstalledEngine local;
    local.install.version = version;
    local.localBuild = true;
    return local;
}

// --- SHA-256 ------------------------------------------------------------------------------------

TEST(Sha256Test, MatchesPublishedVectors) {
    Sha256 empty;
    EXPECT_EQ(ToHex(empty.Finish()), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    Sha256 abc;
    const std::string_view text = "abc";
    abc.Update(std::as_bytes(std::span(text.data(), text.size())));
    EXPECT_EQ(ToHex(abc.Finish()), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Sha256 twoBlocks;
    const std::string_view longer = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    twoBlocks.Update(std::as_bytes(std::span(longer.data(), longer.size())));
    EXPECT_EQ(ToHex(twoBlocks.Finish()), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256Test, ParsesHexInEitherCaseAndRejectsTheRest) {
    const std::string hex = "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD";
    const std::optional<Sha256Digest> digest = ParseSha256Hex(hex);
    ASSERT_TRUE(digest.has_value());
    EXPECT_EQ(ToHex(*digest), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_FALSE(ParseSha256Hex("abc").has_value());
    EXPECT_FALSE(ParseSha256Hex(std::string(64U, 'z')).has_value());
}

// --- archives -----------------------------------------------------------------------------------

TEST(TarGzArchiveTest, ExtractsFilesDirectoriesAndExecutableBits) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("tar");
    TarGzWriter writer;
    writer.AddDirectory("bin/");
    writer.AddFile("bin/tool", "#!/bin/sh\n", 0755U);
    writer.AddFile("data/readme.txt", std::string(1500U, 'x'));
    writer.Write(root / "a.tar.gz");
    std::filesystem::create_directories(root / "out");

    const ArchiveExtractResult result = ExtractTarGz(root / "a.tar.gz", root / "out");

    ASSERT_TRUE(result.extracted) << result.message;
    EXPECT_EQ(ReadText(root / "out" / "bin" / "tool"), "#!/bin/sh\n");
    EXPECT_EQ(std::filesystem::file_size(root / "out" / "data" / "readme.txt"), 1500U);
#if !defined(_WIN32)
    EXPECT_NE(std::filesystem::status(root / "out" / "bin" / "tool").permissions() & std::filesystem::perms::owner_exec,
              std::filesystem::perms::none);
#endif
    std::filesystem::remove_all(root);
}

TEST(TarGzArchiveTest, ExtractsOnlyTheEntriesAFilterAccepts) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("tarfilter");
    TarGzWriter writer;
    writer.AddFile("keep/a.txt", "a");
    writer.AddFile("skip/b.txt", std::string(2000U, 'b'));
    writer.AddFile("keep/c.txt", "c");
    writer.Write(root / "f.tar.gz");
    std::filesystem::create_directories(root / "out");
    ArchiveLimits limits;
    limits.include = [](const std::filesystem::path& path) { return *path.begin() == "keep"; };

    ASSERT_TRUE(ExtractTarGz(root / "f.tar.gz", root / "out", limits).extracted);
    EXPECT_EQ(ReadText(root / "out" / "keep" / "c.txt"), "c");
    EXPECT_FALSE(std::filesystem::exists(root / "out" / "skip"));
    std::filesystem::remove_all(root);
}

TEST(TarGzArchiveTest, RefusesEscapesLinksAndOversizedArchives) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("tarbad");
    const auto attempt = [&root](TarGzWriter& writer, const ArchiveLimits& limits = {}) {
        writer.Write(root / "bad.tar.gz");
        std::filesystem::remove_all(root / "out");
        std::filesystem::create_directories(root / "out");
        return ExtractTarGz(root / "bad.tar.gz", root / "out", limits).extracted;
    };
    TarGzWriter escape;
    escape.AddFile("../outside.txt", "x");
    EXPECT_FALSE(attempt(escape));
    EXPECT_FALSE(std::filesystem::exists(root / "outside.txt"));

    TarGzWriter absolute;
    absolute.AddFile("/etc/evil", "x");
    EXPECT_FALSE(attempt(absolute));

    TarGzWriter link;
    link.AddFile("link", "", 0777U, '2');
    EXPECT_FALSE(attempt(link));

    TarGzWriter big;
    big.AddFile("big.bin", std::string(4096U, 'b'));
    ArchiveLimits small;
    small.maximumTotalBytes = 1024U;
    EXPECT_FALSE(attempt(big, small));

    std::ofstream(root / "garbage.tar.gz") << "not gzip";
    std::filesystem::remove_all(root / "out");
    std::filesystem::create_directories(root / "out");
    EXPECT_FALSE(ExtractTarGz(root / "garbage.tar.gz", root / "out").extracted);
    std::filesystem::remove_all(root);
}

// --- release feed -------------------------------------------------------------------------------

TEST(EngineReleaseFeedTest, ParsesVersionTags) {
    EXPECT_EQ(ParseEngineVersionTag("v1.2.3"), (Core::Version{1U, 2U, 3U, 0U}));
    EXPECT_EQ(ParseEngineVersionTag("1.2.3+build.4"), (Core::Version{1U, 2U, 3U, 4U}));
    EXPECT_EQ(ParseEngineVersionTag("v1.2.3.4"), (Core::Version{1U, 2U, 3U, 4U}));
    EXPECT_EQ(ParseEngineVersionTag(Core::Version{0U, 1U, 0U, 1U}.ToString()),
              (Core::Version{0U, 1U, 0U, 1U}));
    EXPECT_EQ(ParseEngineVersionTag("v1.3.0-beta"), (Core::Version{1U, 3U, 0U, 0U}));
    EXPECT_FALSE(ParseEngineVersionTag("v1.2").has_value());
    EXPECT_FALSE(ParseEngineVersionTag("release-7").has_value());
    EXPECT_FALSE(ParseEngineVersionTag("v1.2.3.4+build.5").has_value());
}

[[nodiscard]] std::string ReleaseJson(const std::string& tag, const bool prerelease, const bool draft,
                                         const std::string& target, const bool withChecksum = true) {
    const std::optional<Core::Version> version = ParseEngineVersionTag(tag);
    const std::string package = EnginePackageName(*version, target);
    std::string assets = R"({"name":")" + package + R"(","url":"https://api.example/assets/)" + tag +
                         R"(/pkg","size":1234})";
    if (withChecksum) {
        assets += R"(,{"name":")" + package + R"(.sha256","url":"https://api.example/assets/)" + tag + R"(/sum","size":64})";
    }
    return R"({"tag_name":")" + tag + R"(","name":"UniVex )" + tag + R"(","prerelease":)" +
           (prerelease ? "true" : "false") + R"(,"draft":)" + (draft ? "true" : "false") + R"(,"assets":[)" + assets +
           "]}";
}

TEST(EngineReleaseFeedTest, PackageNamesRoundTrip) {
    const Core::Version version{1U, 2U, 3U, 4U};
    EXPECT_EQ(ParseEnginePackageName(EnginePackageName(version, "linux"), "linux"), version);
    EXPECT_FALSE(ParseEnginePackageName(EnginePackageName(version, "linux"), "windows").has_value());
    EXPECT_FALSE(ParseEnginePackageName("something.uvpackage", "linux").has_value());
}

TEST(EngineReleaseFeedTest, ParsesReleasesForThisTargetNewestFirst) {
    const std::string target(Platform::GetApplicationTarget());
    const std::string json = "[" + ReleaseJson("v0.9.0", false, false, target) + "," +
                             ReleaseJson("v1.1.0", true, false, target) + "," +
                             ReleaseJson("v1.0.0", false, false, target) + "," +
                             ReleaseJson("v2.0.0", false, true, target) + "," +
                             ReleaseJson("v1.0.1", false, false, "other-target") + "," +
                             ReleaseJson("v1.0.2", false, false, target, false) + "]";
    const std::vector<EngineRelease> releases = ParseGitHubReleases(json, target);
    ASSERT_EQ(releases.size(), 3U);
    EXPECT_EQ(releases[0].version, (Core::Version{1U, 1U, 0U, 0U}));
    EXPECT_EQ(releases[0].channel, EngineChannel::Beta);
    EXPECT_EQ(releases[1].tag, "v1.0.0");
    EXPECT_EQ(releases[1].packageBytes, 1234U);
    EXPECT_EQ(releases[1].checksumUrl, "https://api.example/assets/v1.0.0/sum");

    const EngineReleaseGroups groups = GroupEngineReleases(releases);
    ASSERT_TRUE(groups.stable.has_value());
    EXPECT_EQ(groups.stable->tag, "v1.0.0");
    ASSERT_TRUE(groups.beta.has_value());
    EXPECT_EQ(groups.beta->tag, "v1.1.0");
    ASSERT_EQ(groups.previous.size(), 1U);
    EXPECT_EQ(groups.previous[0].tag, "v0.9.0");
}

TEST(EngineReleaseFeedTest, OlderBetaThanStableIsNotOffered) {
    const std::string target(Platform::GetApplicationTarget());
    const std::string json =
        "[" + ReleaseJson("v1.0.0", false, false, target) + "," + ReleaseJson("v0.9.0", true, false, target) + "]";
    EXPECT_FALSE(GroupEngineReleases(ParseGitHubReleases(json, target)).beta.has_value());
}

TEST(EngineReleaseFeedTest, FetchReportsOfflineNotFoundBusyAndOnline) {
    const EngineFeedSource source{"https://api.example", "studio", "engine"};
    const std::string url = "https://api.example/repos/studio/engine/releases?per_page=100";
    FakeHttpClient client;
    EXPECT_EQ(FetchEngineReleases(client, source, "linux").state, EngineFeedState::Offline);

    client.responses[url] = {true, 404, "{}", {}};
    EXPECT_EQ(FetchEngineReleases(client, source, "linux").state, EngineFeedState::NotFound);

    client.responses[url] = {true, 403, "{}", {}};
    EXPECT_EQ(FetchEngineReleases(client, source, "linux").state, EngineFeedState::Failed);

    client.responses[url] = {true, 200, "[" + ReleaseJson("v1.0.0", false, false, "linux") + "]", {}};
    const EngineFeedResult online = FetchEngineReleases(client, source, "linux");
    EXPECT_EQ(online.state, EngineFeedState::Online);
    EXPECT_EQ(online.releases.size(), 1U);
    // The feed is public: no request ever carries credentials.
    for (const HttpRequest& request : client.requests) {
        for (const HttpHeader& header : request.headers) {
            EXPECT_NE(header.name, "Authorization");
        }
    }
}

TEST(EngineReleaseFeedTest, LoadsFeedSourceAndRejectsInsecureOnes) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("feed");
    std::ofstream(root / "good.json")
        << R"({"format":"uvenginefeed","schemaVersion":1,"owner":"studio","repository":"engine"})";
    std::ofstream(root / "insecure.json")
        << R"({"format":"uvenginefeed","schemaVersion":1,"apiRoot":"http://x","owner":"a","repository":"b"})";
    const std::optional<EngineFeedSource> good = LoadEngineFeedSource(root / "good.json");
    ASSERT_TRUE(good.has_value());
    EXPECT_EQ(good->apiRoot, kDefaultFeedApiRoot);
    EXPECT_FALSE(LoadEngineFeedSource(root / "insecure.json").has_value());
    EXPECT_FALSE(LoadEngineFeedSource(root / "missing.json").has_value());
    std::filesystem::remove_all(root);
}

// --- engine library -----------------------------------------------------------------------------

TEST(EngineLibraryTest, InstallsVerifiedPackageAndListsIt) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("library");
    const std::filesystem::path package = WriteEnginePackage(root);
    const std::filesystem::path engines = root / "Engines";

    Sha256Digest wrong{};
    EXPECT_FALSE(InstallEnginePackage(package, wrong, kPackagedVersion, engines).installed);

    const EngineInstallOutcome installed =
        InstallEnginePackage(package, *HashFileSha256(package), kPackagedVersion, engines);
    ASSERT_TRUE(installed.installed) << installed.message;
    EXPECT_FALSE(InstallEnginePackage(package, *HashFileSha256(package), kPackagedVersion, engines).installed);

    const std::vector<InstalledEngine> listed =
        ListInstalledEngines(engines, LocalBuild(Core::Version{1U, 0U, 0U, 0U}));
    ASSERT_EQ(listed.size(), 2U);
    EXPECT_EQ(listed[0].install.version, kPackagedVersion);
    EXPECT_EQ(listed[0].channel, EngineChannel::Beta);
    EXPECT_FALSE(listed[0].localBuild);
    EXPECT_TRUE(listed[1].localBuild);

    EXPECT_EQ(ListInstalledEngines(engines).size(), 1U);
    EXPECT_TRUE(UninstallEngine(engines, kPackagedVersion));
    EXPECT_EQ(ListInstalledEngines(engines, LocalBuild(Core::Version{1U, 0U, 0U, 0U})).size(), 1U);
    std::filesystem::remove_all(root);
}

TEST(EngineLibraryTest, RefusesPackageForAnotherVersion) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("mismatch");
    const std::filesystem::path package = WriteEnginePackage(root);
    EXPECT_FALSE(InstallEnginePackage(package, *HashFileSha256(package), Core::Version{9U, 9U, 9U, 9U},
                                         root / "Engines")
                     .installed);
    EXPECT_FALSE(std::filesystem::exists(root / "Engines" / Core::Version{9U, 9U, 9U, 9U}.ToString()));
    std::filesystem::remove_all(root);
}

TEST(EngineLibraryTest, ImportsLocalPackageVerifiedByItsChecksumFile) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("import_verified");
    const std::filesystem::path package = WriteEnginePackage(root);
    std::filesystem::path checksum = package;
    checksum += kEngineChecksumSuffix;
    std::ofstream(checksum) << ToHex(*HashFileSha256(package)) << "  " << package.filename().string() << '\n';

    const EngineInstallOutcome outcome = InstallLocalEnginePackage(package, root / "Engines");
    ASSERT_TRUE(outcome.installed) << outcome.message;
    EXPECT_TRUE(outcome.verified);
    ASSERT_TRUE(outcome.version.has_value());
    EXPECT_EQ(*outcome.version, kPackagedVersion);
    EXPECT_FALSE(InstallLocalEnginePackage(package, root / "Engines", false).installed);
    std::filesystem::remove_all(root);
}

TEST(EngineLibraryTest, ReimportingLocalPackageAtomicallyReplacesExistingInstall) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("import_replace");
    const std::filesystem::path engines = root / "Engines";
    const std::string editorFile =
        std::string(Pack::kEditorExecutableStem) + std::string(Platform::kExecutableSuffix);

    const auto writeCustom = [&](const std::string& filename, const std::string& payload) {
        TarGzWriter writer;
        const std::string manifest = std::string(R"({"format":"uvengine","schemaVersion":1,"version":")") +
                                     kPackagedVersion.ToString() + R"(","channel":"beta","target":")" +
                                     std::string(Platform::GetEnginePackageTarget()) + "\"}";
        writer.AddFile(std::string(kEngineManifestFileName), manifest);
        writer.AddFile(editorFile, payload, 0755U);
        const std::filesystem::path path = root / filename;
        writer.Write(path);
        return path;
    };

    const std::filesystem::path v1 = writeCustom("dev-v1.uvpackage", "editor-v1");
    const std::filesystem::path v2 = writeCustom("dev-v2.uvpackage", "editor-v2");
    ASSERT_TRUE(InstallLocalEnginePackage(v1, engines).installed);
    const std::filesystem::path installedEditor = engines / kPackagedVersion.ToString() / editorFile;
    EXPECT_EQ(ReadText(installedEditor), "editor-v1");

    const EngineInstallOutcome replaced = InstallLocalEnginePackage(v2, engines);
    ASSERT_TRUE(replaced.installed) << replaced.message;
    EXPECT_EQ(ReadText(installedEditor), "editor-v2");
    EXPECT_EQ(ListInstalledEngines(engines).size(), 1U);
    std::filesystem::remove_all(root);
}

TEST(EngineLibraryTest, TargetCompatibilityMatchesCurrentPlatform) {
    EXPECT_TRUE(Platform::IsCompatibleEnginePackageTarget(Platform::GetEnginePackageTarget()));
    EXPECT_FALSE(Platform::IsCompatibleEnginePackageTarget("nonexistent-target"));
#if defined(__linux__) && !defined(__ANDROID__)
    EXPECT_FALSE(Platform::IsCompatibleEnginePackageTarget("windows"));
    EXPECT_FALSE(Platform::IsCompatibleEnginePackageTarget("android-arm64-v8a"));
#endif
}

TEST(EngineLibraryTest, ImportsRenamedPackageWithoutChecksumAsUnverified) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("import_unverified");
    const std::filesystem::path renamed = root / "my-engine-copy.uvpackage";
    std::filesystem::rename(WriteEnginePackage(root), renamed);

    const EngineInstallOutcome outcome = InstallLocalEnginePackage(renamed, root / "Engines");
    ASSERT_TRUE(outcome.installed) << outcome.message;
    EXPECT_FALSE(outcome.verified);
    EXPECT_NE(outcome.message.find("not verified"), std::string::npos);
    EXPECT_TRUE(std::filesystem::is_directory(root / "Engines" / kPackagedVersion.ToString()));
    std::filesystem::remove_all(root);
}

TEST(EngineLibraryTest, RefusesLocalPackageThatDoesNotMatchItsChecksum) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("import_tampered");
    const std::filesystem::path package = WriteEnginePackage(root);
    std::filesystem::path checksum = package;
    checksum += kEngineChecksumSuffix;
    std::ofstream(checksum) << ToHex(Sha256Digest{}) << '\n';

    EXPECT_FALSE(InstallLocalEnginePackage(package, root / "Engines").installed);
    EXPECT_FALSE(InstallLocalEnginePackage(root / "missing.tar.gz", root / "Engines").installed);
    EXPECT_FALSE(std::filesystem::exists(root / "Engines" / kPackagedVersion.ToString()));
    std::filesystem::remove_all(root);
}

TEST(EngineLibraryTest, ChoosesTheEngineForAProject) {
    std::vector<InstalledEngine> engines{LocalBuild({2U, 0U, 0U, 0U}), LocalBuild({1U, 4U, 0U, 0U}),
                                            LocalBuild({1U, 2U, 0U, 0U})};
    EXPECT_EQ(ChooseEngineForProject(engines, {1U, 2U, 0U, 0U}, "")->install.version, (Core::Version{1U, 2U, 0U, 0U}));
    EXPECT_EQ(ChooseEngineForProject(engines, {1U, 3U, 0U, 0U}, "")->install.version, (Core::Version{2U, 0U, 0U, 0U}));
    EXPECT_EQ(ChooseEngineForProject(engines, {1U, 2U, 0U, 0U}, Core::Version{1U, 4U, 0U, 0U}.ToString())
                  ->install.version,
              (Core::Version{1U, 4U, 0U, 0U}));
    EXPECT_EQ(ChooseEngineForProject(engines, {3U, 0U, 0U, 0U}, ""), nullptr);
}

TEST(EngineLibraryTest, DownloadJobFetchesVerifiesAndInstalls) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("download");
    const std::filesystem::path package = WriteEnginePackage(root);
    auto client = std::make_unique<FakeHttpClient>();
    EngineRelease release;
    release.version = kPackagedVersion;
    release.packageUrl = "https://api.example/pkg";
    release.checksumUrl = "https://api.example/sum";
    client->responses[release.checksumUrl] = {true, 200, ToHex(*HashFileSha256(package)) + "  pkg\n", {}};
    client->files[release.packageUrl] = package;

    EngineDownload job(std::move(client), release, root / "Engines");
    for (int attempt = 0; attempt < 500 && job.GetState() == EngineDownload::State::Running; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(job.GetState(), EngineDownload::State::Installed) << job.Message();
    EXPECT_TRUE(ReadEngineManifest(root / "Engines" / kPackagedVersion.ToString()).has_value());
    std::filesystem::remove_all(root);
}

TEST(EngineLibraryTest, DownloadJobReportsOffline) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("offline");
    EngineRelease release;
    release.version = kPackagedVersion;
    release.packageUrl = "https://api.example/pkg";
    release.checksumUrl = "https://api.example/sum";
    EngineDownload job(std::make_unique<FakeHttpClient>(), release, root / "Engines");
    for (int attempt = 0; attempt < 500 && job.GetState() == EngineDownload::State::Running; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(job.GetState(), EngineDownload::State::Failed);
    EXPECT_NE(job.Message().find("Offline"), std::string::npos);
    std::filesystem::remove_all(root);
}

} // namespace
} // namespace UVE::Online::Tests
