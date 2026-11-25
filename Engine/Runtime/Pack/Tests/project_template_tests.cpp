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

inline constexpr Core::Version kEngineVersion{3U, 1U, 4U, 1U};

void WriteText(const std::filesystem::path& path, const std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

class ProjectTemplateTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = ::UVE::Tests::MakeTestCaseDirectory("templates");
        templatesRoot = root / "templates";
        projectsRoot = root / "projects";
        std::filesystem::create_directories(projectsRoot);
    }

    void TearDown() override { std::filesystem::remove_all(root); }

    /// A template with content, a preview and two starter objects.
    [[nodiscard]] std::filesystem::path WriteStarterTemplate() {
        const std::filesystem::path directory = templatesRoot / "starter";
        WriteText(directory / "preview.png", "preview");
        WriteText(directory / "Content" / "Materials" / "Floor.uvmat", "material");
        WriteText(directory / kProjectTemplateManifestFileName, R"({
            "format": "uvtemplate", "schemaVersion": 1, "id": "starter", "displayName": "Starter",
            "description": "With objects.", "previewImage": "preview.png", "contentDirectory": "Content",
            "startupScenePath": "Maps/Main.uvscene",
            "starterObjects": [
                {"kind": "camera", "name": "Camera", "position": [0, 2, 6], "rotationDegrees": [-15, 0, 0]},
                {"kind": "plane", "scale": [10, 1, 10]}
            ]})");
        return directory;
    }

    [[nodiscard]] ProjectTemplate LoadStarterTemplate() {
        ProjectTemplateLoadResult loaded = LoadProjectTemplate(WriteStarterTemplate());
        EXPECT_TRUE(loaded.IsLoaded()) << loaded.message;
        return loaded.projectTemplate.value_or(ProjectTemplate{});
    }

    std::filesystem::path root;
    std::filesystem::path templatesRoot;
    std::filesystem::path projectsRoot;
};

TEST_F(ProjectTemplateTest, Load_ReadsDescriptorAndStarterObjects) {
    const ProjectTemplate projectTemplate = LoadStarterTemplate();

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

TEST_F(ProjectTemplateTest, Load_RejectsMalformedAndEscapingDescriptors) {
    WriteText(templatesRoot / "broken" / kProjectTemplateManifestFileName, "{ not json");
    EXPECT_EQ(LoadProjectTemplate(templatesRoot / "broken").code, ProjectTemplateCode::InvalidDescriptor);

    WriteText(templatesRoot / "escape" / kProjectTemplateManifestFileName,
                 R"({"format": "uvtemplate", "schemaVersion": 1, "id": "e", "displayName": "E",
                     "contentDirectory": "../outside"})");
    EXPECT_EQ(LoadProjectTemplate(templatesRoot / "escape").code, ProjectTemplateCode::InvalidPath);

    WriteText(templatesRoot / "future" / kProjectTemplateManifestFileName,
                 R"({"format": "uvtemplate", "schemaVersion": 99, "id": "f", "displayName": "F"})");
    EXPECT_EQ(LoadProjectTemplate(templatesRoot / "future").code, ProjectTemplateCode::UnsupportedSchema);

    WriteText(templatesRoot / "badobject" / kProjectTemplateManifestFileName,
                 R"({"format": "uvtemplate", "schemaVersion": 1, "id": "b", "displayName": "B",
                     "startupScenePath": "Main.uvscene", "starterObjects": [{"kind": "cube", "position": [1, 2]}]})");
    EXPECT_EQ(LoadProjectTemplate(templatesRoot / "badobject").code, ProjectTemplateCode::InvalidDescriptor);

    EXPECT_EQ(LoadProjectTemplate(templatesRoot / "missing").code, ProjectTemplateCode::MissingDescriptor);
}

TEST_F(ProjectTemplateTest, Discover_ListsValidTemplatesAndReportsTheRest) {
    static_cast<void>(WriteStarterTemplate());
    WriteText(templatesRoot / "blank" / kProjectTemplateManifestFileName,
                 R"({"format": "uvtemplate", "schemaVersion": 1, "id": "blank", "displayName": "Blank"})");
    WriteText(templatesRoot / "broken" / kProjectTemplateManifestFileName, "[]");

    const ProjectTemplateDiscovery discovery = DiscoverProjectTemplates(templatesRoot);

    ASSERT_EQ(discovery.templates.size(), 2U);
    EXPECT_EQ(discovery.templates[0].displayName, "Blank");
    EXPECT_EQ(discovery.templates[1].displayName, "Starter");
    EXPECT_EQ(discovery.problems.size(), 1U);
}

TEST_F(ProjectTemplateTest, Create_AssemblesProjectFromTemplate) {
    const ProjectTemplate projectTemplate = LoadStarterTemplate();

    const ProjectCreationResult created =
        CreateProjectFromTemplate(projectTemplate, projectsRoot, "My Game", kEngineVersion);

    ASSERT_TRUE(created.IsCreated()) << created.message;
    const std::filesystem::path projectRoot = projectsRoot / "My Game";
    EXPECT_EQ(created.projectFile, projectRoot / "My Game.uvproject");
    EXPECT_TRUE(std::filesystem::is_regular_file(projectRoot / "Content" / "Materials" / "Floor.uvmat"));

    const Platform::EditorProjectPackageLoadResult manifest =
        Platform::EditorProjectPackageCodec::Load(created.projectFile);
    ASSERT_TRUE(manifest.IsAccepted()) << manifest.result.message;
    EXPECT_EQ(manifest.package->displayName, "My Game");
    EXPECT_EQ(manifest.package->projectId.rfind("my-game-", 0U), 0U);
    EXPECT_EQ(manifest.package->engineVersion, (Platform::EditorProjectVersion{3U, 1U, 4U, 1U}));
    EXPECT_EQ(manifest.package->contentRoot, std::filesystem::path("Content"));
    EXPECT_EQ(manifest.package->startupScenePath, std::filesystem::path("Maps/Main.uvscene"));

    const StarterSceneLoadResult starter = LoadProjectStarterScene(projectRoot);
    ASSERT_EQ(starter.code, StarterSceneCode::Loaded) << starter.message;
    ASSERT_EQ(starter.scene.objects.size(), 2U);
    EXPECT_EQ(starter.scene.objects[0].name, "Camera");
    EXPECT_TRUE(RemoveProjectStarterScene(projectRoot));
    EXPECT_EQ(LoadProjectStarterScene(projectRoot).code, StarterSceneCode::Absent);

    // Nothing of the staging directory is left behind.
    std::size_t entries = 0U;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(projectsRoot)) {
        ++entries;
    }
    EXPECT_EQ(entries, 1U);
}

TEST_F(ProjectTemplateTest, Create_GivesEachProjectItsOwnId) {
    const ProjectTemplate projectTemplate = LoadStarterTemplate();
    const ProjectCreationResult first =
        CreateProjectFromTemplate(projectTemplate, projectsRoot, "One", kEngineVersion);
    const ProjectCreationResult second =
        CreateProjectFromTemplate(projectTemplate, projectsRoot, "Two", kEngineVersion);
    ASSERT_TRUE(first.IsCreated());
    ASSERT_TRUE(second.IsCreated());
    EXPECT_NE(Platform::EditorProjectPackageCodec::Load(first.projectFile).package->projectId,
              Platform::EditorProjectPackageCodec::Load(second.projectFile).package->projectId);
}

TEST_F(ProjectTemplateTest, Create_RefusesNonEmptyDestinationWithoutTouchingIt) {
    const ProjectTemplate projectTemplate = LoadStarterTemplate();
    WriteText(projectsRoot / "Taken" / "keep.txt", "mine");

    const ProjectCreationResult created =
        CreateProjectFromTemplate(projectTemplate, projectsRoot, "Taken", kEngineVersion);

    EXPECT_EQ(created.code, ProjectCreationCode::DestinationNotEmpty);
    EXPECT_TRUE(std::filesystem::is_regular_file(projectsRoot / "Taken" / "keep.txt"));
    EXPECT_FALSE(std::filesystem::exists(projectsRoot / "Taken" / "Taken.uvproject"));
}

TEST_F(ProjectTemplateTest, Create_AcceptsEmptyDestinationDirectory) {
    const ProjectTemplate projectTemplate = LoadStarterTemplate();
    std::filesystem::create_directories(projectsRoot / "Empty");

    EXPECT_TRUE(CreateProjectFromTemplate(projectTemplate, projectsRoot, "Empty", kEngineVersion).IsCreated());
}

TEST_F(ProjectTemplateTest, Create_RejectsUnusableNamesAndLocations) {
    const ProjectTemplate projectTemplate = LoadStarterTemplate();
    EXPECT_EQ(CreateProjectFromTemplate(projectTemplate, projectsRoot, "   ", kEngineVersion).code,
              ProjectCreationCode::InvalidName);
    EXPECT_EQ(CreateProjectFromTemplate(projectTemplate, projectsRoot, "///", kEngineVersion).code,
              ProjectCreationCode::InvalidName);
    EXPECT_EQ(CreateProjectFromTemplate(projectTemplate, root / "missing", "Game", kEngineVersion).code,
              ProjectCreationCode::InvalidLocation);
}

TEST(ProjectFileStemTest, ReplacesReservedCharactersAndTrims) {
    EXPECT_EQ(MakeProjectFileStem("  My Game  "), "My Game");
    EXPECT_EQ(MakeProjectFileStem("a/b:c"), "a_b_c");
    EXPECT_EQ(MakeProjectFileStem("..hidden.."), "hidden");
    EXPECT_EQ(MakeProjectFileStem(""), "");
}

TEST(ShippedProjectTemplatesTest, EveryShippedTemplateLoads) {
    const ProjectTemplateDiscovery discovery =
        DiscoverProjectTemplates(::UVE::Tests::RepositoryRoot() / "Engine" / "Templates" / "Projects");
    EXPECT_TRUE(discovery.problems.empty()) << (discovery.problems.empty() ? "" : discovery.problems.front());
    EXPECT_GE(discovery.templates.size(), 2U);
}

TEST_F(ProjectTemplateTest, Create_StampsSelectedRendererProfileIntoProjectManifest) {
    const ProjectTemplate projectTemplate = LoadStarterTemplate();

    const ProjectCreationResult compat = CreateProjectFromTemplate(
        projectTemplate, projectsRoot, "Compat Game", kEngineVersion, Platform::RendererProfile::Compatibility);
    ASSERT_TRUE(compat.IsCreated()) << compat.message;
    const Platform::EditorProjectPackageLoadResult compatLoaded =
        Platform::EditorProjectPackageCodec::Load(compat.projectFile);
    ASSERT_TRUE(compatLoaded.IsAccepted()) << compatLoaded.result.message;
    EXPECT_EQ(compatLoaded.package->rendererProfile, Platform::RendererProfile::Compatibility);

    const ProjectCreationResult mobile = CreateProjectFromTemplate(
        projectTemplate, projectsRoot, "Mobile Game", kEngineVersion, Platform::RendererProfile::Mobile);
    ASSERT_TRUE(mobile.IsCreated()) << mobile.message;
    const Platform::EditorProjectPackageLoadResult mobileLoaded =
        Platform::EditorProjectPackageCodec::Load(mobile.projectFile);
    ASSERT_TRUE(mobileLoaded.IsAccepted()) << mobileLoaded.result.message;
    EXPECT_EQ(mobileLoaded.package->rendererProfile, Platform::RendererProfile::Mobile);
}

} // namespace
} // namespace UVE::Pack::Tests
