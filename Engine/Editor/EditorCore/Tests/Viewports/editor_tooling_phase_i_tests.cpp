// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "editor_tooling_services.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "mesh_component.h"
#include "rigid_3d.h"

namespace UVE::Editor {
namespace {

TEST(EditorToolingPhaseITest, DependencyIndexSearchAndValidatorWorkEndToEnd) {
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

    EditorAssetDependencyIndex depIndex;
    depIndex.RebuildFromProject(root);
    const auto stoneRefs = depIndex.GetReferencers("Textures/Stone.uvtex");
    ASSERT_EQ(stoneRefs.size(), 1U);
    EXPECT_EQ(stoneRefs[0], "Materials/Stone.uvmaterial");

    const auto hits = SearchProjectContent(root, "Stone.uvtex");
    EXPECT_FALSE(hits.empty());

    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager em{memoryManager.GetDefaultAllocator(), eventSystem};
    const Scene::Entity badMesh = em.CreateEntity();
    em.AddComponent<Scene::MeshComponent>(badMesh, Scene::MeshComponent{});
    const Scene::Entity bareRigid = em.CreateEntity();
    em.AddComponent<Scene::Rigid3DComponent>(bareRigid, Scene::Rigid3DComponent{});

    const auto issues = RunEditorSceneValidation(
        em, &depIndex, {"Main.uvscene", "Materials/Stone.uvmaterial", "Textures/Stone.uvtex", "Textures/Orphan.uvtex"});
    EXPECT_GE(issues.size(), 3U); // broken mesh + missing collider + unused Orphan.uvtex

    std::filesystem::remove_all(root);
}

TEST(EditorToolingPhaseITest, MultiScenePrefabDiffTerrainBrushGitVcsAndExtensionsWork) {
    EditorMultiSceneSession session;
    session.SetPrimaryScene("Levels/Main.uvscene");
    ASSERT_TRUE(session.AddSubScene("Levels/Lighting.uvscene"));
    ASSERT_TRUE(session.SetActiveEditScene("Levels/Lighting.uvscene"));
    EXPECT_EQ(session.ActiveEditScene(), "Levels/Lighting.uvscene");
    ASSERT_TRUE(session.SetSubSceneLoaded("Levels/Lighting.uvscene", false));
    EXPECT_EQ(session.ActiveEditScene(), "Levels/Main.uvscene");

    EditorPrefabTransformSnapshot prefab{"Crate", {}};
    EditorPrefabTransformSnapshot inst = prefab;
    inst.transform.localPosition = Math::Vector3{5.0F, 0.0F, 0.0F};
    const auto diffs = DiffPrefabInstance(prefab, inst);
    ASSERT_EQ(diffs.size(), 1U);
    EXPECT_EQ(diffs[0].propertyName, "Position");
    RevertInstanceToPrefab(prefab, inst);
    EXPECT_TRUE(DiffPrefabInstance(prefab, inst).empty());

    Scene::Terrain3DComponent terrain{};
    terrain.resolution = 16U;
    Scene::Terrain3D::InitializeHeightData(terrain);
    const float hBefore = Scene::Terrain3D::SampleHeightLocal(terrain, 0.0F, 0.0F);
    EditorTerrainBrushSettings brush{};
    brush.mode = EditorTerrainBrushMode::Raise;
    brush.radiusMetres = 8.0F;
    brush.strength = 2.0F;
    ApplyEditorTerrainBrushStroke(terrain, 0.0F, 0.0F, brush, 0.5F);
    EXPECT_GT(Scene::Terrain3D::SampleHeightLocal(terrain, 0.0F, 0.0F), hBefore);

    EditorGitVcsProvider vcs;
    vcs.ParsePorcelainOutput(" M Assets/Main.uvscene\n?? Assets/NewMat.uvmaterial\nA  Assets/Added.uvtex\n");
    EXPECT_EQ(vcs.GetFileStatus("Assets/Main.uvscene"), EditorVcsFileStatus::Modified);
    EXPECT_EQ(vcs.GetFileStatus("Assets/NewMat.uvmaterial"), EditorVcsFileStatus::Untracked);
    EXPECT_EQ(vcs.GetFileStatus("Assets/Added.uvtex"), EditorVcsFileStatus::Added);
    EXPECT_EQ(vcs.GetFileStatus("Assets/Clean.uvtex"), EditorVcsFileStatus::Clean);

    EditorExtensionRegistry ext;
    bool invoked = false;
    ext.RegisterMenuItem({"Tools", "Custom Bake", [&]() { invoked = true; }});
    ext.RegisterPanel({"custom.panel", "Custom Panel"});
    ext.RegisterImporter({".myfmt", "CustomImporter", [](const std::filesystem::path&) { return true; }});
    EXPECT_TRUE(ext.InvokeMenuItem("Tools", "Custom Bake"));
    EXPECT_TRUE(invoked);
    EXPECT_TRUE(ext.TryImportFile("test/asset.MYFMT"));
}

} // namespace
} // namespace UVE::Editor
