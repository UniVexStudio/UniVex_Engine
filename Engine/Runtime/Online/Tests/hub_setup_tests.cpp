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
void WritePayloadUVE(const std::filesystem::path& archive, const std::vector<std::pair<std::string, std::string>>& files) {
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
void WriteSetupProgramUVE(const std::filesystem::path& setup, const std::string& program,
                          const std::filesystem::path& payload) {
    std::ifstream input(payload, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const Sha256DigestUVE digest = *HashFileSha256UVE(payload);
    std::ofstream output(setup, std::ios::binary | std::ios::trunc);
    output << program << bytes;
    output.write(reinterpret_cast<const char*>(digest.data()), static_cast<std::streamsize>(digest.size()));
    std::uint64_t size = bytes.size();
    for (std::size_t index = 0U; index < kSetupPayloadSizeBytesUVE; ++index) {
        output.put(static_cast<char>((size >> (8U * index)) & 0xFFU));
    }
    output << kSetupPayloadMagicUVE;
}

[[nodiscard]] std::string HubFileUVE() {
    return std::string(kHubExecutableStemUVE) + std::string(Platform::kExecutableSuffixUVE);
}

class HubSetupUVETest : public ::testing::Test {
protected:
    void SetUp() override { root = ::UVE::Tests::MakeTestCaseDirectoryUVE("setup"); }
    void TearDown() override { std::filesystem::remove_all(root); }
    std::filesystem::path root;
};

TEST(HubVersionUVETest, DisplayVersionHidesAZeroBuild) {
    EXPECT_EQ(FormatDisplayVersionUVE(Core::VersionUVE{1U, 2U, 3U, 0U}), "1.2.3");
    EXPECT_EQ(FormatDisplayVersionUVE(Core::VersionUVE{1U, 2U, 3U, 4U}), "1.2.3+build.4");
}

TEST_F(HubSetupUVETest, FindsAndVerifiesTheAppendedPayload) {
    WritePayloadUVE(root / "payload.tar.gz", {{HubFileUVE(), "hub"}});
    WriteSetupProgramUVE(root / "setup", "PROGRAM-BYTES", root / "payload.tar.gz");

    const std::optional<SetupPayloadLocationUVE> location = LocateSetupPayloadUVE(root / "setup");
    ASSERT_TRUE(location.has_value());
    EXPECT_EQ(location->offset, std::string("PROGRAM-BYTES").size());
    ASSERT_TRUE(ExtractSetupPayloadUVE(root / "setup", *location, root / "copy.tar.gz"));
    EXPECT_EQ(*HashFileSha256UVE(root / "copy.tar.gz"), *HashFileSha256UVE(root / "payload.tar.gz"));

    SetupPayloadLocationUVE tampered = *location;
    tampered.digest[0] ^= 0xFFU;
    EXPECT_FALSE(ExtractSetupPayloadUVE(root / "setup", tampered, root / "bad.tar.gz"));

    std::ofstream(root / "plain") << "no payload here";
    EXPECT_FALSE(LocateSetupPayloadUVE(root / "plain").has_value());
}

TEST_F(HubSetupUVETest, InstallsUpgradesRegistersAndUninstalls) {
    const std::string icon = (std::filesystem::path(kHubIconsDirectoryNameUVE) / kHubIconFileNameUVE).generic_string();
    WritePayloadUVE(root / "v1.tar.gz", {{HubFileUVE(), "hub v1"}, {icon, "png"}});
    WritePayloadUVE(root / "v2.tar.gz", {{HubFileUVE(), "hub v2"}, {icon, "png"}});
    std::ofstream(root / "setup-program") << "setup";
    const std::filesystem::path install = root / "Programs" / kHubInstallFolderNameUVE;
    const std::filesystem::path entries = root / "entries";

    ASSERT_TRUE(InstallHubUVE(root / "v1.tar.gz", install, root / "setup-program").succeeded);
    EXPECT_TRUE(std::filesystem::is_regular_file(HubExecutableInUVE(install)));
    EXPECT_TRUE(std::filesystem::is_regular_file(
        install / (std::string(kSetupExecutableStemUVE) + std::string(Platform::kExecutableSuffixUVE))));
    std::ofstream(install / "user-left-file") << "x";

    const HubSetupResultUVE upgraded = InstallHubUVE(root / "v2.tar.gz", install, root / "setup-program");
    ASSERT_TRUE(upgraded.succeeded) << upgraded.message;
    std::ifstream hub(HubExecutableInUVE(install));
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(hub), std::istreambuf_iterator<char>()), "hub v2");
    std::size_t siblings = 0U;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(install.parent_path())) {
        ++siblings;
    }
    EXPECT_EQ(siblings, 1U); // no staging or previous folders left behind

    ASSERT_TRUE(RegisterHubWithDesktopUVE(install, "1.2.3+build.4", entries).succeeded);
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
    EXPECT_NE(entryText.find(std::string("MimeType=") + std::string(kProjectMediaTypeUVE)), std::string::npos);
    EXPECT_NE(entryText.find(" %f"), std::string::npos);
    EXPECT_NE(entryText.find("StartupWMClass="), std::string::npos);
    EXPECT_NE(entryText.find("[Desktop Action Uninstall]"), std::string::npos);
#else
    EXPECT_EQ(countFiles(), 1U);
#endif

    ASSERT_TRUE(UninstallHubUVE(install, entries).succeeded);
    EXPECT_FALSE(std::filesystem::exists(install));
    EXPECT_EQ(countFiles(), 0U);
}

TEST_F(HubSetupUVETest, RefusesPayloadWithoutTheHubAndKeepsTheOldInstall) {
    WritePayloadUVE(root / "good.tar.gz", {{HubFileUVE(), "hub"}});
    WritePayloadUVE(root / "nohub.tar.gz", {{"readme.txt", "x"}});
    const std::filesystem::path install = root / "Hub";
    ASSERT_TRUE(InstallHubUVE(root / "good.tar.gz", install, {}).succeeded);
    EXPECT_FALSE(InstallHubUVE(root / "nohub.tar.gz", install, {}).succeeded);
    EXPECT_TRUE(std::filesystem::is_regular_file(HubExecutableInUVE(install)));
    EXPECT_FALSE(UninstallHubUVE(root / "not-a-hub", root / "entries").succeeded);
}

} // namespace
} // namespace UVE::Online::Tests
