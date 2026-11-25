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

TEST(LiveOpsPhaseJTest, TelemetryConsentGateAndJsonlExport) {
    const std::filesystem::path logFile =
        std::filesystem::temp_directory_path() / "uve_test_telemetry.jsonl";
    std::filesystem::remove(logFile);

    Core::TelemetryService telemetry("sess-42");
    telemetry.AddSink(std::make_shared<Core::JsonFileTelemetrySink>(logFile));

    // Ignored while consent is disabled.
    telemetry.RecordEvent("startup", {{"platform", "linux"}});
    EXPECT_EQ(telemetry.PendingEventCount(), 0U);

    telemetry.SetConsentEnabled(true);
    telemetry.RecordEvent("startup", {{"platform", "linux"}, {"tier", "Ultra"}});
    EXPECT_EQ(telemetry.PendingEventCount(), 1U);
    telemetry.Flush();
    EXPECT_EQ(telemetry.PendingEventCount(), 0U);
    ASSERT_TRUE(std::filesystem::exists(logFile));

    std::ifstream in(logFile);
    std::string line;
    std::getline(in, line);
    EXPECT_NE(line.find("\"event\":\"startup\""), std::string::npos);
    EXPECT_NE(line.find("\"tier\":\"Ultra\""), std::string::npos);
    std::filesystem::remove(logFile);
}

TEST(LiveOpsPhaseJTest, PatchManagerPriorityIntegrityCheckAndRollback) {
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
    ASSERT_TRUE(ComputeFileIntegrityHashHex(patch1Dir / "patch.manifest", p1Hash));

    PatchManager mgr(baseDir);
    EXPECT_EQ(mgr.ResolvePatchedAssetPath("Materials/Hero.uvmaterial"),
              baseDir / "Materials/Hero.uvmaterial");

    // Wrong digest rejected.
    EXPECT_FALSE(mgr.MountPatch({"hotfix-1", "1.0.1", 10, patch1Dir, "0000000000000000"}));
    // Valid digest accepted.
    ASSERT_TRUE(mgr.MountPatch({"hotfix-1", "1.0.1", 10, patch1Dir, p1Hash}));
    EXPECT_EQ(mgr.ResolvePatchedAssetPath("Materials/Hero.uvmaterial"),
              patch1Dir / "Materials/Hero.uvmaterial");

    // Higher-priority patch wins.
    ASSERT_TRUE(mgr.MountPatch({"dlc-2", "1.1.0", 20, patch2Dir, ""}));
    EXPECT_EQ(mgr.ResolvePatchedAssetPath("Materials/Hero.uvmaterial"),
              patch2Dir / "Materials/Hero.uvmaterial");

    // Rollback restores priority 10 patch cleanly.
    ASSERT_TRUE(mgr.RollbackPatch("dlc-2"));
    EXPECT_EQ(mgr.ResolvePatchedAssetPath("Materials/Hero.uvmaterial"),
              patch1Dir / "Materials/Hero.uvmaterial");

    std::filesystem::remove_all(root);
}

TEST(LiveOpsPhaseJTest, StorefrontPackagingAndMockXrRuntimeWork) {
    Platform::LocalMockPlatformStorefront store;
    EXPECT_FALSE(store.IsAchievementUnlocked("FIRST_BUILD"));
    ASSERT_TRUE(store.SetAchievementProgress("FIRST_BUILD", 0.5F));
    EXPECT_FALSE(store.IsAchievementUnlocked("FIRST_BUILD"));
    ASSERT_TRUE(store.SetAchievementProgress("FIRST_BUILD", 1.0F));
    EXPECT_TRUE(store.IsAchievementUnlocked("FIRST_BUILD"));
    ASSERT_TRUE(store.SetStatInt("matches_played", 12));
    EXPECT_EQ(store.GetStatInt("matches_played"), 12);
    store.GrantEntitlement("DLC_EXPANSION_1");
    EXPECT_TRUE(store.HasEntitlement("DLC_EXPANSION_1"));

    const std::vector<std::uint8_t> savePayload{1, 2, 3, 4};
    ASSERT_TRUE(store.WriteCloudFile("slot0.sav", savePayload));
    std::vector<std::uint8_t> loadedPayload;
    ASSERT_TRUE(store.ReadCloudFile("slot0.sav", loadedPayload));
    EXPECT_EQ(loadedPayload, savePayload);

    StorePackageSpec winSpec{};
    winSpec.appName = "DemoGame";
    winSpec.appVersion = "1.2.0";
    winSpec.executableName = "DemoGame.exe";
    winSpec.codeSigningIdentity = "UniVex Studio Cert";
    winSpec.stagedDirectory = "Build/Staged";
    winSpec.targetOs = StorePackageTargetOs::Windows;
    const std::string iss = GenerateWindowsInstallerScript(winSpec);
    EXPECT_NE(iss.find("AppName=DemoGame"), std::string::npos);
    EXPECT_NE(iss.find("SignTool=signtool"), std::string::npos);
    EXPECT_NE(BuildCodeSigningCommand(winSpec, "DemoGame.exe").find("signtool sign"),
              std::string::npos);

    Platform::MockXrRuntime xr(0.064F);
    ASSERT_TRUE(xr.BeginFrame());
    const auto leftEye = xr.GetEyeView(Platform::XrEye::Left);
    const auto rightEye = xr.GetEyeView(Platform::XrEye::Right);
    EXPECT_NEAR(rightEye.pose.position[0] - leftEye.pose.position[0], 0.064F, 1.0e-5F);
    EXPECT_TRUE(xr.GetControllerState(Platform::XrHand::Right).pose.tracked);
    xr.EndFrame();
}

} // namespace
} // namespace UVE::Pack
