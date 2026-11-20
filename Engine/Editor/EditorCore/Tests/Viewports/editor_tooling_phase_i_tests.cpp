// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "editor_tooling_services.h"
#include "entity_manager.h"
#include "mesh_component.h"
#include "rigid_3d.h"

namespace UVE::Editor {
namespace {

TEST(EditorToolingPhaseIUVETest, DependencyIndexSearchAndValidatorWorkEndToEnd) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "uve_test_phase_i_project";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "Materials");
    std::filesystem::create_directories(root / "Textures");
    {
        std::ofstream f(root / "Main.uvscene");
        f << R"({"material":"Materials/Stone.uvmaterial"})";
    }
    {
        std::ofstream f(root / "Materials/Stone.uvmaterial");
        f << R"({"albedo":"Textures/Stone.uvtex"})";
    }
    {
        std::ofstream f(root / "Textures/Stone.uvtex");
        f << "TEX_USED";
    }
    {
        std::ofstream f(root / "Textures/Orphan.uvtex");
        f << "TEX_UNUSED";
    }

    EditorAssetDependencyIndexUVE depIndex;
    depIndex.RebuildFromProjectUVE(root);
    const auto stoneRefs = depIndex.GetReferencersUVE("Textures/Stone.uvtex");
    ASSERT_EQ(stoneRefs.size(), 1U);
    EXPECT_EQ(stoneRefs[0], "Materials/Stone.uvmaterial");

    const auto hits = SearchProjectContentUVE(root, "Stone.uvtex");
    EXPECT_FALSE(hits.empty());

    Scene::EntityManagerUVE em;
    const Scene::EntityUVE badMesh = em.CreateEntityUVE();
    em.AddComponentUVE<Scene::MeshComponentUVE>(badMesh, Scene::MeshComponentUVE{});
    const Scene::EntityUVE bareRigid = em.CreateEntityUVE();
    em.AddComponentUVE<Scene::Rigid3DComponentUVE>(bareRigid, Scene::Rigid3DComponentUVE{});

    const auto issues = RunEditorSceneValidationUVE(
        em, &depIndex, {"Main.uvscene", "Materials/Stone.uvmaterial", "Textures/Stone.uvtex", "Textures/Orphan.uvtex"});
    EXPECT_GE(issues.size(), 3U); // broken mesh + missing collider + unused Orphan.uvtex

    std::filesystem::remove_all(root);
}

TEST(EditorToolingPhaseIUVETest, MultiScenePrefabDiffTerrainBrushGitVcsAndExtensionsWork) {
    EditorMultiSceneSessionUVE session;
    session.SetPrimarySceneUVE("Levels/Main.uvscene");
    ASSERT_TRUE(session.AddSubSceneUVE("Levels/Lighting.uvscene"));
    ASSERT_TRUE(session.SetActiveEditSceneUVE("Levels/Lighting.uvscene"));
    EXPECT_EQ(session.ActiveEditSceneUVE(), "Levels/Lighting.uvscene");
    ASSERT_TRUE(session.SetSubSceneLoadedUVE("Levels/Lighting.uvscene", false));
    EXPECT_EQ(session.ActiveEditSceneUVE(), "Levels/Main.uvscene");

    EditorPrefabTransformSnapshotUVE prefab{"Crate", {}};
    EditorPrefabTransformSnapshotUVE inst = prefab;
    inst.transform.localPosition = Math::Vector3UVE{5.0F, 0.0F, 0.0F};
    const auto diffs = DiffPrefabInstanceUVE(prefab, inst);
    ASSERT_EQ(diffs.size(), 1U);
    EXPECT_EQ(diffs[0].propertyName, "Position");
    RevertInstanceToPrefabUVE(prefab, inst);
    EXPECT_TRUE(DiffPrefabInstanceUVE(prefab, inst).empty());

    Scene::Terrain3DComponentUVE terrain{};
    terrain.resolution = 16U;
    Scene::Terrain3DUVE::InitializeHeightDataUVE(terrain);
    const float hBefore = Scene::Terrain3DUVE::SampleHeightLocalUVE(terrain, 0.0F, 0.0F);
    EditorTerrainBrushSettingsUVE brush{};
    brush.mode = EditorTerrainBrushModeUVE::Raise;
    brush.radiusMetres = 8.0F;
    brush.strength = 2.0F;
    ApplyEditorTerrainBrushStrokeUVE(terrain, 0.0F, 0.0F, brush, 0.5F);
    EXPECT_GT(Scene::Terrain3DUVE::SampleHeightLocalUVE(terrain, 0.0F, 0.0F), hBefore);

    EditorGitVcsProviderUVE vcs;
    vcs.ParsePorcelainOutputUVE(" M Assets/Main.uvscene\n?? Assets/NewMat.uvmaterial\nA  Assets/Added.uvtex\n");
    EXPECT_EQ(vcs.GetFileStatusUVE("Assets/Main.uvscene"), EditorVcsFileStatusUVE::Modified);
    EXPECT_EQ(vcs.GetFileStatusUVE("Assets/NewMat.uvmaterial"), EditorVcsFileStatusUVE::Untracked);
    EXPECT_EQ(vcs.GetFileStatusUVE("Assets/Added.uvtex"), EditorVcsFileStatusUVE::Added);
    EXPECT_EQ(vcs.GetFileStatusUVE("Assets/Clean.uvtex"), EditorVcsFileStatusUVE::Clean);

    EditorExtensionRegistryUVE ext;
    bool invoked = false;
    ext.RegisterMenuItemUVE({"Tools", "Custom Bake", [&]() { invoked = true; }});
    ext.RegisterPanelUVE({"custom.panel", "Custom Panel"});
    ext.RegisterImporterUVE({".myfmt", "CustomImporter", [](const std::filesystem::path&) { return true; }});
    EXPECT_TRUE(ext.InvokeMenuItemUVE("Tools", "Custom Bake"));
    EXPECT_TRUE(invoked);
    EXPECT_TRUE(ext.TryImportFileUVE("test/asset.MYFMT"));
}

} // namespace
} // namespace UVE::Editor
