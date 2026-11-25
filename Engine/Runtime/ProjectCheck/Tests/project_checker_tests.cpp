// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "uve_file_envelope.h"
#include "project_checker.h"

namespace UVE::ProjectCheck::Tests {
namespace {

class ProjectCheckerTest : public ::testing::Test {
protected:
    void SetUp() override { m_root = ::UVE::Tests::MakeTestCaseDirectory(); }

    void TearDown() override { std::filesystem::remove_all(m_root); }

    [[nodiscard]] std::filesystem::path WriteEnvelope(const std::string& name, const Asset::AssetKind kind) const {
        const std::filesystem::path path = m_root / name;
        EXPECT_TRUE(Asset::WriteUveFile(path, kind, {}));
        return path;
    }

    std::filesystem::path m_root;
};

TEST_F(ProjectCheckerTest, Run_IsReadOnlyAndRendersStableTextAndJson) {
    const std::filesystem::path scene = WriteEnvelope("valid.uvscene", Asset::AssetKind::Scene);
    const std::string before = [] (const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }(scene);

    const ProjectCheckReport first = ProjectChecker{}.Run(ProjectCheckOptions{m_root, {}});
    const ProjectCheckReport second = ProjectChecker{}.Run(ProjectCheckOptions{m_root, {}});
    EXPECT_FALSE(first.HasErrors());
    EXPECT_EQ(RenderProjectCheckText(first), RenderProjectCheckText(second));
    const std::string json = RenderProjectCheckJson(first);
    EXPECT_EQ(json, RenderProjectCheckJson(second));
    EXPECT_NE(json.find("\"hasErrors\":false,\"diagnostics\":["), std::string::npos);

    std::ifstream input(scene, std::ios::binary);
    const std::string after{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    EXPECT_EQ(before, after);
    EXPECT_FALSE(std::filesystem::exists(m_root / ".uvassetdb"));
}

TEST_F(ProjectCheckerTest, Run_ResolvesRelativeAssetDatabasePathFromProjectRoot) {
    const std::filesystem::path databaseDirectory = m_root / "registry";
    ASSERT_TRUE(std::filesystem::create_directories(databaseDirectory));
    const std::filesystem::path databasePath = databaseDirectory / "custom.uvassetdb";
    {
        std::ofstream databaseFile(databasePath);
        databaseFile << "malformed registry";
    }

    const ProjectCheckReport report =
        ProjectChecker{}.Run(ProjectCheckOptions{m_root, "registry/custom.uvassetdb"});
    const auto iterator = std::find_if(report.diagnostics.begin(), report.diagnostics.end(),
                                       [&databasePath](const ProjectCheckDiagnostic& diagnostic) {
                                           return diagnostic.code == "registry.load.failed" &&
                                                  diagnostic.path == databasePath.lexically_normal();
                                       });
    EXPECT_NE(iterator, report.diagnostics.end());
}

TEST_F(ProjectCheckerTest, Run_IsolatesCorruptFilesAndAggregatesIndependentDiagnostics) {
    static_cast<void>(WriteEnvelope("good.uvscene", Asset::AssetKind::Scene));
    static_cast<void>(WriteEnvelope("wrong.uvmodel", Asset::AssetKind::Blob));
    {
        std::ofstream corrupt(m_root / "broken.uvmodel", std::ios::binary);
        corrupt << "not-a-uve-envelope";
    }

    const ProjectCheckReport report = ProjectChecker{}.Run(ProjectCheckOptions{m_root, {}});
    ASSERT_TRUE(report.HasErrors());
    bool foundDecodeFailure = false;
    bool foundKindMismatch = false;
    bool foundWrongUnregistered = false;
    bool foundGoodUnregistered = false;
    for (const ProjectCheckDiagnostic& diagnostic : report.diagnostics) {
        foundDecodeFailure |= diagnostic.code == "envelope.decode.failed" && diagnostic.path.filename() == "broken.uvmodel";
        foundKindMismatch |= diagnostic.code == "envelope.kind.mismatch" && diagnostic.path.filename() == "wrong.uvmodel";
        foundWrongUnregistered |= diagnostic.code == "registry.file.unregistered" && diagnostic.path.filename() == "wrong.uvmodel";
        foundGoodUnregistered |= diagnostic.code == "registry.file.unregistered" && diagnostic.path.filename() == "good.uvscene";
    }
    EXPECT_TRUE(foundDecodeFailure);
    EXPECT_TRUE(foundKindMismatch);
    EXPECT_TRUE(foundWrongUnregistered);
    EXPECT_TRUE(foundGoodUnregistered);
    EXPECT_TRUE(std::is_sorted(report.diagnostics.begin(), report.diagnostics.end(), [](const ProjectCheckDiagnostic& lhs,
                                                                                          const ProjectCheckDiagnostic& rhs) {
        if (lhs.path.generic_string() != rhs.path.generic_string()) return lhs.path.generic_string() < rhs.path.generic_string();
        return lhs.code < rhs.code;
    }));
}

// The Inspector's "New Standard / ORM / Shader Material" writes `.uvmaterial`, which is as much a
// material as an importer's `.uvmat`. Not knowing the extension meant the check skipped every
// material an author created by hand - never decoding it, never reporting it unregistered - so a
// corrupt one went unnoticed until something tried to draw with it.
TEST_F(ProjectCheckerTest, Run_ChecksMaterialsWrittenByTheInspectorAsWellAsImportedOnes) {
    static_cast<void>(WriteEnvelope("authored.uvmaterial", Asset::AssetKind::Material));
    static_cast<void>(WriteEnvelope("imported.uvmat", Asset::AssetKind::Material));
    static_cast<void>(WriteEnvelope("wrongkind.uvmaterial", Asset::AssetKind::Blob));
    {
        std::ofstream corrupt(m_root / "corrupt.uvmaterial", std::ios::binary);
        corrupt << "not-a-uve-envelope";
    }

    const ProjectCheckReport report = ProjectChecker{}.Run(ProjectCheckOptions{m_root, {}});
    ASSERT_TRUE(report.HasErrors());
    bool foundAuthoredUnregistered = false;
    bool foundImportedUnregistered = false;
    bool foundKindMismatch = false;
    bool foundDecodeFailure = false;
    for (const ProjectCheckDiagnostic& diagnostic : report.diagnostics) {
        const bool wellFormedMaterial = diagnostic.path.filename() == "authored.uvmaterial" ||
                                        diagnostic.path.filename() == "imported.uvmat";
        if (wellFormedMaterial) {
            // A material envelope of the right kind is accepted under either spelling, which is
            // what "the check knows this extension" means.
            EXPECT_NE(diagnostic.code, "envelope.kind.mismatch") << diagnostic.path.filename();
            EXPECT_NE(diagnostic.code, "envelope.decode.failed") << diagnostic.path.filename();
        }
        foundAuthoredUnregistered |=
            diagnostic.code == "registry.file.unregistered" && diagnostic.path.filename() == "authored.uvmaterial";
        foundImportedUnregistered |=
            diagnostic.code == "registry.file.unregistered" && diagnostic.path.filename() == "imported.uvmat";
        foundKindMismatch |=
            diagnostic.code == "envelope.kind.mismatch" && diagnostic.path.filename() == "wrongkind.uvmaterial";
        foundDecodeFailure |=
            diagnostic.code == "envelope.decode.failed" && diagnostic.path.filename() == "corrupt.uvmaterial";
    }
    EXPECT_TRUE(foundAuthoredUnregistered) << "the authored material is a supported asset, so it is reported";
    EXPECT_TRUE(foundImportedUnregistered);
    EXPECT_TRUE(foundKindMismatch);
    EXPECT_TRUE(foundDecodeFailure);
}

TEST_F(ProjectCheckerTest, Run_ValidatesAllRegisteredEnvelopeAssetKindsCaseInsensitively) {
    static_cast<void>(WriteEnvelope("player.uvbp", Asset::AssetKind::Blueprint));
    static_cast<void>(WriteEnvelope("sparks.UVFX", Asset::AssetKind::ParticleSystem));
    static_cast<void>(WriteEnvelope("hero.uvskel", Asset::AssetKind::Skeleton));
    static_cast<void>(WriteEnvelope("locomotion.uvanimlib", Asset::AssetKind::AnimationLibrary));
    static_cast<void>(WriteEnvelope("attack.uvmontage", Asset::AssetKind::MotionMontage));
    static_cast<void>(WriteEnvelope("combat.uvmontagelib", Asset::AssetKind::MontageLibrary));
    static_cast<void>(WriteEnvelope("hud.uvwbp", Asset::AssetKind::Prefab));
    static_cast<void>(WriteEnvelope("items.uvtable", Asset::AssetKind::DataTable));
    static_cast<void>(WriteEnvelope("bgm.uvaudio", Asset::AssetKind::Audio));
    static_cast<void>(WriteEnvelope("ui.uvfont", Asset::AssetKind::Font));
    static_cast<void>(WriteEnvelope("bad_bp.UVBP", Asset::AssetKind::Blob));

    const ProjectCheckReport report = ProjectChecker{}.Run(ProjectCheckOptions{m_root, {}});
    ASSERT_TRUE(report.HasErrors());
    bool foundBadBpMismatch = false;
    bool foundSparksUnregistered = false;
    for (const ProjectCheckDiagnostic& diagnostic : report.diagnostics) {
        if (diagnostic.path.filename() == "sparks.UVFX") {
            EXPECT_NE(diagnostic.code, "envelope.kind.mismatch");
            EXPECT_NE(diagnostic.code, "envelope.decode.failed");
            foundSparksUnregistered |= diagnostic.code == "registry.file.unregistered";
        }
        foundBadBpMismatch |=
            diagnostic.code == "envelope.kind.mismatch" && diagnostic.path.filename() == "bad_bp.UVBP";
    }
    EXPECT_TRUE(foundBadBpMismatch);
    EXPECT_TRUE(foundSparksUnregistered);
}

} // namespace
} // namespace UVE::ProjectCheck::Tests
