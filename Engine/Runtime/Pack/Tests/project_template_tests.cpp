// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_template.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "editor_project_package.h"

namespace UVE::Pack::Tests {
namespace {

inline constexpr Core::VersionUVE kEngineVersionUVE{3U, 1U, 4U, 1U};

void WriteTextUVE(const std::filesystem::path& path, const std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

class ProjectTemplateUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        root = ::UVE::Tests::MakeTestCaseDirectoryUVE("templates");
        templatesRoot = root / "templates";
        projectsRoot = root / "projects";
        std::filesystem::create_directories(projectsRoot);
    }

    void TearDown() override { std::filesystem::remove_all(root); }

    /// A template with content, a preview and two starter objects.
    [[nodiscard]] std::filesystem::path WriteStarterTemplateUVE() {
        const std::filesystem::path directory = templatesRoot / "starter";
        WriteTextUVE(directory / "preview.png", "preview");
        WriteTextUVE(directory / "Content" / "Materials" / "Floor.uvmat", "material");
        WriteTextUVE(directory / kProjectTemplateManifestFileNameUVE, R"({
            "format": "uvtemplate", "schemaVersion": 1, "id": "starter", "displayName": "Starter",
            "description": "With objects.", "previewImage": "preview.png", "contentDirectory": "Content",
            "startupScenePath": "Maps/Main.uvscene",
            "starterObjects": [
                {"kind": "camera", "name": "Camera", "position": [0, 2, 6], "rotationDegrees": [-15, 0, 0]},
                {"kind": "plane", "scale": [10, 1, 10]}
            ]})");
        return directory;
    }

    [[nodiscard]] ProjectTemplateUVE LoadStarterTemplateUVE() {
        ProjectTemplateLoadResultUVE loaded = LoadProjectTemplateUVE(WriteStarterTemplateUVE());
        EXPECT_TRUE(loaded.IsLoadedUVE()) << loaded.message;
        return loaded.projectTemplate.value_or(ProjectTemplateUVE{});
    }

    std::filesystem::path root;
    std::filesystem::path templatesRoot;
    std::filesystem::path projectsRoot;
};

TEST_F(ProjectTemplateUVETest, Load_ReadsDescriptorAndStarterObjects) {
    const ProjectTemplateUVE projectTemplate = LoadStarterTemplateUVE();

    EXPECT_EQ(projectTemplate.id, "starter");
    EXPECT_EQ(projectTemplate.displayName, "Starter");
    EXPECT_EQ(projectTemplate.previewImage.filename(), "preview.png");
    EXPECT_EQ(projectTemplate.startupScenePath, std::filesystem::path("Maps/Main.uvscene"));
    ASSERT_EQ(projectTemplate.starterScene.objects.size(), 2U);
    EXPECT_EQ(projectTemplate.starterScene.objects[0].kind, "camera");
    EXPECT_FLOAT_EQ(projectTemplate.starterScene.objects[0].rotationDegrees.x, -15.0F);
    EXPECT_TRUE(projectTemplate.starterScene.objects[1].name.empty());
    EXPECT_FLOAT_EQ(projectTemplate.starterScene.objects[1].scale.z, 10.0F);
}

TEST_F(ProjectTemplateUVETest, Load_RejectsMalformedAndEscapingDescriptors) {
    WriteTextUVE(templatesRoot / "broken" / kProjectTemplateManifestFileNameUVE, "{ not json");
    EXPECT_EQ(LoadProjectTemplateUVE(templatesRoot / "broken").code, ProjectTemplateCodeUVE::InvalidDescriptor);

    WriteTextUVE(templatesRoot / "escape" / kProjectTemplateManifestFileNameUVE,
                 R"({"format": "uvtemplate", "schemaVersion": 1, "id": "e", "displayName": "E",
                     "contentDirectory": "../outside"})");
    EXPECT_EQ(LoadProjectTemplateUVE(templatesRoot / "escape").code, ProjectTemplateCodeUVE::InvalidPath);

    WriteTextUVE(templatesRoot / "future" / kProjectTemplateManifestFileNameUVE,
                 R"({"format": "uvtemplate", "schemaVersion": 99, "id": "f", "displayName": "F"})");
    EXPECT_EQ(LoadProjectTemplateUVE(templatesRoot / "future").code, ProjectTemplateCodeUVE::UnsupportedSchema);

    WriteTextUVE(templatesRoot / "badobject" / kProjectTemplateManifestFileNameUVE,
                 R"({"format": "uvtemplate", "schemaVersion": 1, "id": "b", "displayName": "B",
                     "startupScenePath": "Main.uvscene", "starterObjects": [{"kind": "cube", "position": [1, 2]}]})");
    EXPECT_EQ(LoadProjectTemplateUVE(templatesRoot / "badobject").code, ProjectTemplateCodeUVE::InvalidDescriptor);

    EXPECT_EQ(LoadProjectTemplateUVE(templatesRoot / "missing").code, ProjectTemplateCodeUVE::MissingDescriptor);
}

TEST_F(ProjectTemplateUVETest, Discover_ListsValidTemplatesAndReportsTheRest) {
    static_cast<void>(WriteStarterTemplateUVE());
    WriteTextUVE(templatesRoot / "blank" / kProjectTemplateManifestFileNameUVE,
                 R"({"format": "uvtemplate", "schemaVersion": 1, "id": "blank", "displayName": "Blank"})");
    WriteTextUVE(templatesRoot / "broken" / kProjectTemplateManifestFileNameUVE, "[]");

    const ProjectTemplateDiscoveryUVE discovery = DiscoverProjectTemplatesUVE(templatesRoot);

    ASSERT_EQ(discovery.templates.size(), 2U);
    EXPECT_EQ(discovery.templates[0].displayName, "Blank");
    EXPECT_EQ(discovery.templates[1].displayName, "Starter");
    EXPECT_EQ(discovery.problems.size(), 1U);
}

TEST_F(ProjectTemplateUVETest, Create_AssemblesProjectFromTemplate) {
    const ProjectTemplateUVE projectTemplate = LoadStarterTemplateUVE();

    const ProjectCreationResultUVE created =
        CreateProjectFromTemplateUVE(projectTemplate, projectsRoot, "My Game", kEngineVersionUVE);

    ASSERT_TRUE(created.IsCreatedUVE()) << created.message;
    const std::filesystem::path projectRoot = projectsRoot / "My Game";
    EXPECT_EQ(created.projectFile, projectRoot / "My Game.uvproject");
    EXPECT_TRUE(std::filesystem::is_regular_file(projectRoot / "Content" / "Materials" / "Floor.uvmat"));

    const Platform::EditorProjectPackageLoadResultUVE manifest =
        Platform::EditorProjectPackageCodecUVE::LoadUVE(created.projectFile);
    ASSERT_TRUE(manifest.IsAcceptedUVE()) << manifest.result.message;
    EXPECT_EQ(manifest.package->displayName, "My Game");
    EXPECT_EQ(manifest.package->projectId.rfind("my-game-", 0U), 0U);
    EXPECT_EQ(manifest.package->engineVersion, (Platform::EditorProjectVersionUVE{3U, 1U, 4U, 1U}));
    EXPECT_EQ(manifest.package->contentRoot, std::filesystem::path("Content"));
    EXPECT_EQ(manifest.package->startupScenePath, std::filesystem::path("Maps/Main.uvscene"));

    const StarterSceneLoadResultUVE starter = LoadProjectStarterSceneUVE(projectRoot);
    ASSERT_EQ(starter.code, StarterSceneCodeUVE::Loaded) << starter.message;
    ASSERT_EQ(starter.scene.objects.size(), 2U);
    EXPECT_EQ(starter.scene.objects[0].name, "Camera");
    EXPECT_TRUE(RemoveProjectStarterSceneUVE(projectRoot));
    EXPECT_EQ(LoadProjectStarterSceneUVE(projectRoot).code, StarterSceneCodeUVE::Absent);

    // Nothing of the staging directory is left behind.
    std::size_t entries = 0U;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(projectsRoot)) {
        ++entries;
    }
    EXPECT_EQ(entries, 1U);
}

TEST_F(ProjectTemplateUVETest, Create_GivesEachProjectItsOwnId) {
    const ProjectTemplateUVE projectTemplate = LoadStarterTemplateUVE();
    const ProjectCreationResultUVE first =
        CreateProjectFromTemplateUVE(projectTemplate, projectsRoot, "One", kEngineVersionUVE);
    const ProjectCreationResultUVE second =
        CreateProjectFromTemplateUVE(projectTemplate, projectsRoot, "Two", kEngineVersionUVE);
    ASSERT_TRUE(first.IsCreatedUVE());
    ASSERT_TRUE(second.IsCreatedUVE());
    EXPECT_NE(Platform::EditorProjectPackageCodecUVE::LoadUVE(first.projectFile).package->projectId,
              Platform::EditorProjectPackageCodecUVE::LoadUVE(second.projectFile).package->projectId);
}

TEST_F(ProjectTemplateUVETest, Create_RefusesNonEmptyDestinationWithoutTouchingIt) {
    const ProjectTemplateUVE projectTemplate = LoadStarterTemplateUVE();
    WriteTextUVE(projectsRoot / "Taken" / "keep.txt", "mine");

    const ProjectCreationResultUVE created =
        CreateProjectFromTemplateUVE(projectTemplate, projectsRoot, "Taken", kEngineVersionUVE);

    EXPECT_EQ(created.code, ProjectCreationCodeUVE::DestinationNotEmpty);
    EXPECT_TRUE(std::filesystem::is_regular_file(projectsRoot / "Taken" / "keep.txt"));
    EXPECT_FALSE(std::filesystem::exists(projectsRoot / "Taken" / "Taken.uvproject"));
}

TEST_F(ProjectTemplateUVETest, Create_AcceptsEmptyDestinationDirectory) {
    const ProjectTemplateUVE projectTemplate = LoadStarterTemplateUVE();
    std::filesystem::create_directories(projectsRoot / "Empty");

    EXPECT_TRUE(CreateProjectFromTemplateUVE(projectTemplate, projectsRoot, "Empty", kEngineVersionUVE).IsCreatedUVE());
}

TEST_F(ProjectTemplateUVETest, Create_RejectsUnusableNamesAndLocations) {
    const ProjectTemplateUVE projectTemplate = LoadStarterTemplateUVE();
    EXPECT_EQ(CreateProjectFromTemplateUVE(projectTemplate, projectsRoot, "   ", kEngineVersionUVE).code,
              ProjectCreationCodeUVE::InvalidName);
    EXPECT_EQ(CreateProjectFromTemplateUVE(projectTemplate, projectsRoot, "///", kEngineVersionUVE).code,
              ProjectCreationCodeUVE::InvalidName);
    EXPECT_EQ(CreateProjectFromTemplateUVE(projectTemplate, root / "missing", "Game", kEngineVersionUVE).code,
              ProjectCreationCodeUVE::InvalidLocation);
}

TEST(ProjectFileStemUVETest, ReplacesReservedCharactersAndTrims) {
    EXPECT_EQ(MakeProjectFileStemUVE("  My Game  "), "My Game");
    EXPECT_EQ(MakeProjectFileStemUVE("a/b:c"), "a_b_c");
    EXPECT_EQ(MakeProjectFileStemUVE("..hidden.."), "hidden");
    EXPECT_EQ(MakeProjectFileStemUVE(""), "");
}

TEST(ShippedProjectTemplatesUVETest, EveryShippedTemplateLoads) {
    const ProjectTemplateDiscoveryUVE discovery =
        DiscoverProjectTemplatesUVE(::UVE::Tests::RepositoryRootUVE() / "Engine" / "Templates" / "Projects");
    EXPECT_TRUE(discovery.problems.empty()) << (discovery.problems.empty() ? "" : discovery.problems.front());
    EXPECT_GE(discovery.templates.size(), 2U);
}

TEST_F(ProjectTemplateUVETest, Create_StampsSelectedRendererProfileIntoProjectManifest) {
    const ProjectTemplateUVE projectTemplate = LoadStarterTemplateUVE();

    const ProjectCreationResultUVE compat = CreateProjectFromTemplateUVE(
        projectTemplate, projectsRoot, "Compat Game", kEngineVersionUVE, Platform::RendererProfileUVE::Compatibility);
    ASSERT_TRUE(compat.IsCreatedUVE()) << compat.message;
    const Platform::EditorProjectPackageLoadResultUVE compatLoaded =
        Platform::EditorProjectPackageCodecUVE::LoadUVE(compat.projectFile);
    ASSERT_TRUE(compatLoaded.IsAcceptedUVE()) << compatLoaded.result.message;
    EXPECT_EQ(compatLoaded.package->rendererProfile, Platform::RendererProfileUVE::Compatibility);

    const ProjectCreationResultUVE mobile = CreateProjectFromTemplateUVE(
        projectTemplate, projectsRoot, "Mobile Game", kEngineVersionUVE, Platform::RendererProfileUVE::Mobile);
    ASSERT_TRUE(mobile.IsCreatedUVE()) << mobile.message;
    const Platform::EditorProjectPackageLoadResultUVE mobileLoaded =
        Platform::EditorProjectPackageCodecUVE::LoadUVE(mobile.projectFile);
    ASSERT_TRUE(mobileLoaded.IsAcceptedUVE()) << mobileLoaded.result.message;
    EXPECT_EQ(mobileLoaded.package->rendererProfile, Platform::RendererProfileUVE::Mobile);
}

} // namespace
} // namespace UVE::Pack::Tests
