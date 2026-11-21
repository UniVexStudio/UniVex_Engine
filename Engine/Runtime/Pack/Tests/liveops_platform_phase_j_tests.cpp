// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>

#include "i_platform_storefront.h"
#include "i_xr_runtime.h"
#include "patch_manager.h"
#include "store_packaging.h"
#include "telemetry_service.h"

namespace UVE::Pack {
namespace {

TEST(LiveOpsPhaseJUVETest, TelemetryConsentGateAndJsonlExport) {
    const std::filesystem::path logFile =
        std::filesystem::temp_directory_path() / "uve_test_telemetry.jsonl";
    std::filesystem::remove(logFile);

    Core::TelemetryServiceUVE telemetry("sess-42");
    telemetry.AddSinkUVE(std::make_shared<Core::JsonFileTelemetrySinkUVE>(logFile));

    // Ignored while consent is disabled.
    telemetry.RecordEventUVE("startup", {{"platform", "linux"}});
    EXPECT_EQ(telemetry.PendingEventCountUVE(), 0U);

    telemetry.SetConsentEnabledUVE(true);
    telemetry.RecordEventUVE("startup", {{"platform", "linux"}, {"tier", "Ultra"}});
    EXPECT_EQ(telemetry.PendingEventCountUVE(), 1U);
    telemetry.FlushUVE();
    EXPECT_EQ(telemetry.PendingEventCountUVE(), 0U);
    ASSERT_TRUE(std::filesystem::exists(logFile));

    std::ifstream in(logFile);
    std::string line;
    std::getline(in, line);
    EXPECT_NE(line.find("\"event\":\"startup\""), std::string::npos);
    EXPECT_NE(line.find("\"tier\":\"Ultra\""), std::string::npos);
    std::filesystem::remove(logFile);
}

TEST(LiveOpsPhaseJUVETest, PatchManagerPriorityIntegrityCheckAndRollback) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "uve_test_patch_mgr";
    std::filesystem::remove_all(root);
    const std::filesystem::path baseDir = root / "base";
    const std::filesystem::path patch1Dir = root / "patch1";
    const std::filesystem::path patch2Dir = root / "patch2";
    std::filesystem::create_directories(baseDir / "Materials");
    std::filesystem::create_directories(patch1Dir / "Materials");
    std::filesystem::create_directories(patch2Dir / "Materials");
    {
        std::ofstream(baseDir / "Materials/Hero.uvmaterial") << "BASE";
        std::ofstream(patch1Dir / "Materials/Hero.uvmaterial") << "PATCH1";
        std::ofstream(patch1Dir / "patch.manifest") << "v1.0.1";
        std::ofstream(patch2Dir / "Materials/Hero.uvmaterial") << "PATCH2";
    }

    std::string p1Hash;
    ASSERT_TRUE(ComputeFileIntegrityHashHexUVE(patch1Dir / "patch.manifest", p1Hash));

    PatchManagerUVE mgr(baseDir);
    EXPECT_EQ(mgr.ResolvePatchedAssetPathUVE("Materials/Hero.uvmaterial"),
              baseDir / "Materials/Hero.uvmaterial");

    // Wrong digest rejected.
    EXPECT_FALSE(mgr.MountPatchUVE({"hotfix-1", "1.0.1", 10, patch1Dir, "0000000000000000"}));
    // Valid digest accepted.
    ASSERT_TRUE(mgr.MountPatchUVE({"hotfix-1", "1.0.1", 10, patch1Dir, p1Hash}));
    EXPECT_EQ(mgr.ResolvePatchedAssetPathUVE("Materials/Hero.uvmaterial"),
              patch1Dir / "Materials/Hero.uvmaterial");

    // Higher-priority patch wins.
    ASSERT_TRUE(mgr.MountPatchUVE({"dlc-2", "1.1.0", 20, patch2Dir, ""}));
    EXPECT_EQ(mgr.ResolvePatchedAssetPathUVE("Materials/Hero.uvmaterial"),
              patch2Dir / "Materials/Hero.uvmaterial");

    // Rollback restores priority 10 patch cleanly.
    ASSERT_TRUE(mgr.RollbackPatchUVE("dlc-2"));
    EXPECT_EQ(mgr.ResolvePatchedAssetPathUVE("Materials/Hero.uvmaterial"),
              patch1Dir / "Materials/Hero.uvmaterial");

    std::filesystem::remove_all(root);
}

TEST(LiveOpsPhaseJUVETest, StorefrontPackagingAndMockXrRuntimeWork) {
    Platform::LocalMockPlatformStorefrontUVE store;
    EXPECT_FALSE(store.IsAchievementUnlockedUVE("FIRST_BUILD"));
    ASSERT_TRUE(store.SetAchievementProgressUVE("FIRST_BUILD", 0.5F));
    EXPECT_FALSE(store.IsAchievementUnlockedUVE("FIRST_BUILD"));
    ASSERT_TRUE(store.SetAchievementProgressUVE("FIRST_BUILD", 1.0F));
    EXPECT_TRUE(store.IsAchievementUnlockedUVE("FIRST_BUILD"));
    ASSERT_TRUE(store.SetStatIntUVE("matches_played", 12));
    EXPECT_EQ(store.GetStatIntUVE("matches_played"), 12);
    store.GrantEntitlementUVE("DLC_EXPANSION_1");
    EXPECT_TRUE(store.HasEntitlementUVE("DLC_EXPANSION_1"));

    const std::vector<std::uint8_t> savePayload{1, 2, 3, 4};
    ASSERT_TRUE(store.WriteCloudFileUVE("slot0.sav", savePayload));
    std::vector<std::uint8_t> loadedPayload;
    ASSERT_TRUE(store.ReadCloudFileUVE("slot0.sav", loadedPayload));
    EXPECT_EQ(loadedPayload, savePayload);

    StorePackageSpecUVE winSpec{};
    winSpec.appName = "DemoGame";
    winSpec.appVersion = "1.2.0";
    winSpec.executableName = "DemoGame.exe";
    winSpec.codeSigningIdentity = "UniVex Studio Cert";
    winSpec.stagedDirectory = "Build/Staged";
    winSpec.targetOs = StorePackageTargetOsUVE::Windows;
    const std::string iss = GenerateWindowsInstallerScriptUVE(winSpec);
    EXPECT_NE(iss.find("AppName=DemoGame"), std::string::npos);
    EXPECT_NE(iss.find("SignTool=signtool"), std::string::npos);
    EXPECT_NE(BuildCodeSigningCommandUVE(winSpec, "DemoGame.exe").find("signtool sign"),
              std::string::npos);

    Platform::MockXrRuntimeUVE xr(0.064F);
    ASSERT_TRUE(xr.BeginFrameUVE());
    const auto leftEye = xr.GetEyeViewUVE(Platform::XrEyeUVE::Left);
    const auto rightEye = xr.GetEyeViewUVE(Platform::XrEyeUVE::Right);
    EXPECT_NEAR(rightEye.pose.position[0] - leftEye.pose.position[0], 0.064F, 1.0e-5F);
    EXPECT_TRUE(xr.GetControllerStateUVE(Platform::XrHandUVE::Right).pose.tracked);
    xr.EndFrameUVE();
}

} // namespace
} // namespace UVE::Pack
