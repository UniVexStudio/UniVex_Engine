// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hub_setup.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <zlib.h>

#include "Support/test_scratch.h"

#include "process_launcher.h"

namespace UVE::Online::Tests {
namespace {

/// A gzip-compressed tar holding `files` (name -> contents), for setup payloads.
void WritePayload(const std::filesystem::path& archive, const std::vector<std::pair<std::string, std::string>>& files) {
    std::string tar;
    for (const auto& [name, contents] : files) {
        std::array<char, 512U> header{};
        std::memcpy(header.data(), name.data(), name.size());
        std::snprintf(header.data() + 100, 8, "%07o", 0755U);
        std::snprintf(header.data() + 124, 12, "%011llo", static_cast<unsigned long long>(contents.size()));
        header[156] = '0';
        std::memcpy(header.data() + 257, "ustar", 6);
        tar.append(header.data(), header.size());
        tar.append(contents);
        tar.append((512U - (contents.size() % 512U)) % 512U, '\0');
    }
    tar.append(1024U, '\0');
    gzFile output = gzopen(archive.string().c_str(), "wb");
    ASSERT_NE(output, nullptr);
    gzwrite(output, tar.data(), static_cast<unsigned>(tar.size()));
    gzclose(output);
}

/// Appends `payload` and the footer to a copy of `program`, as make_hub_installer.py does.
void WriteSetupProgram(const std::filesystem::path& setup, const std::string& program,
                          const std::filesystem::path& payload) {
    std::ifstream input(payload, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const Sha256Digest digest = *HashFileSha256(payload);
    std::ofstream output(setup, std::ios::binary | std::ios::trunc);
    output << program << bytes;
    output.write(reinterpret_cast<const char*>(digest.data()), static_cast<std::streamsize>(digest.size()));
    std::uint64_t size = bytes.size();
    for (std::size_t index = 0U; index < kSetupPayloadSizeBytes; ++index) {
        output.put(static_cast<char>((size >> (8U * index)) & 0xFFU));
    }
    output << kSetupPayloadMagic;
}

[[nodiscard]] std::string HubFile() {
    return std::string(kHubExecutableStem) + std::string(Platform::kExecutableSuffix);
}

class HubSetupTest : public ::testing::Test {
protected:
    void SetUp() override { root = ::UVE::Tests::MakeTestCaseDirectory("setup"); }
    void TearDown() override { std::filesystem::remove_all(root); }
    std::filesystem::path root;
};

TEST(HubVersionTest, DisplayVersionHidesAZeroBuild) {
    EXPECT_EQ(FormatDisplayVersion(Core::Version{1U, 2U, 3U, 0U}), "1.2.3");
    EXPECT_EQ(FormatDisplayVersion(Core::Version{1U, 2U, 3U, 4U}), "1.2.3+build.4");
}

TEST_F(HubSetupTest, FindsAndVerifiesTheAppendedPayload) {
    WritePayload(root / "payload.tar.gz", {{HubFile(), "hub"}});
    WriteSetupProgram(root / "setup", "PROGRAM-BYTES", root / "payload.tar.gz");

    const std::optional<SetupPayloadLocation> location = LocateSetupPayload(root / "setup");
    ASSERT_TRUE(location.has_value());
    EXPECT_EQ(location->offset, std::string("PROGRAM-BYTES").size());
    ASSERT_TRUE(ExtractSetupPayload(root / "setup", *location, root / "copy.tar.gz"));
    EXPECT_EQ(*HashFileSha256(root / "copy.tar.gz"), *HashFileSha256(root / "payload.tar.gz"));

    SetupPayloadLocation tampered = *location;
    tampered.digest[0] ^= 0xFFU;
    EXPECT_FALSE(ExtractSetupPayload(root / "setup", tampered, root / "bad.tar.gz"));

    std::ofstream(root / "plain") << "no payload here";
    EXPECT_FALSE(LocateSetupPayload(root / "plain").has_value());
}

TEST_F(HubSetupTest, InstallsUpgradesRegistersAndUninstalls) {
    const std::string icon = (std::filesystem::path(kHubIconsDirectoryName) / kHubIconFileName).generic_string();
    WritePayload(root / "v1.tar.gz", {{HubFile(), "hub v1"}, {icon, "png"}});
    WritePayload(root / "v2.tar.gz", {{HubFile(), "hub v2"}, {icon, "png"}});
    std::ofstream(root / "setup-program") << "setup";
    const std::filesystem::path install = root / "Programs" / kHubInstallFolderName;
    const std::filesystem::path entries = root / "entries";

    ASSERT_TRUE(InstallHub(root / "v1.tar.gz", install, root / "setup-program").succeeded);
    EXPECT_TRUE(std::filesystem::is_regular_file(HubExecutableIn(install)));
    EXPECT_TRUE(std::filesystem::is_regular_file(
        install / (std::string(kSetupExecutableStem) + std::string(Platform::kExecutableSuffix))));
    std::ofstream(install / "user-left-file") << "x";

    const HubSetupResult upgraded = InstallHub(root / "v2.tar.gz", install, root / "setup-program");
    ASSERT_TRUE(upgraded.succeeded) << upgraded.message;
    std::ifstream hub(HubExecutableIn(install));
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(hub), std::istreambuf_iterator<char>()), "hub v2");
    std::size_t siblings = 0U;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(install.parent_path())) {
        ++siblings;
    }
    EXPECT_EQ(siblings, 1U); // no staging or previous folders left behind

    ASSERT_TRUE(RegisterHubWithDesktop(install, "1.2.3+build.4", entries).succeeded);
    const auto countFiles = [&entries] {
        std::size_t files = 0U;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(entries)) {
            files += entry.is_regular_file() ? 1U : 0U;
        }
        return files;
    };
#if !defined(_WIN32)
    // Desktop entry, media type for project files, and the two icons.
    EXPECT_EQ(countFiles(), 4U);
    std::ifstream desktop(entries / "applications" / "univex-hub.desktop");
    const std::string entryText{std::istreambuf_iterator<char>(desktop), std::istreambuf_iterator<char>()};
    EXPECT_NE(entryText.find(std::string("MimeType=") + std::string(kProjectMediaType)), std::string::npos);
    EXPECT_NE(entryText.find(" %f"), std::string::npos);
    EXPECT_NE(entryText.find("StartupWMClass="), std::string::npos);
    EXPECT_NE(entryText.find("[Desktop Action Uninstall]"), std::string::npos);
#else
    EXPECT_EQ(countFiles(), 1U);
#endif

    ASSERT_TRUE(UninstallHub(install, entries).succeeded);
    EXPECT_FALSE(std::filesystem::exists(install));
    EXPECT_EQ(countFiles(), 0U);
}

TEST_F(HubSetupTest, RefusesPayloadWithoutTheHubAndKeepsTheOldInstall) {
    WritePayload(root / "good.tar.gz", {{HubFile(), "hub"}});
    WritePayload(root / "nohub.tar.gz", {{"readme.txt", "x"}});
    const std::filesystem::path install = root / "Hub";
    ASSERT_TRUE(InstallHub(root / "good.tar.gz", install, {}).succeeded);
    EXPECT_FALSE(InstallHub(root / "nohub.tar.gz", install, {}).succeeded);
    EXPECT_TRUE(std::filesystem::is_regular_file(HubExecutableIn(install)));
    EXPECT_FALSE(UninstallHub(root / "not-a-hub", root / "entries").succeeded);
}

} // namespace
} // namespace UVE::Online::Tests
