// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <cmath>
#include <filesystem>

#include "Support/test_scratch.h"

#include "editor.h"
#include "editor_vfx_graph.h"
#include "editor_vfx_inspector.h"
#include "engine_core.h"
#include "particle_system_asset.h"
#include "vfx_templates.h"
#include "prism/Application.h"
#include "prism/BlueprintGraph.h"
#include "prism/ColorPicker.h"

namespace UVE::Editor::Tests {
namespace {

using Asset::ParticleModuleType;
using Asset::ParticleSystemAsset;
using VFX::ParticleTemplateKind;

constexpr std::array<ParticleTemplateKind, 5> kAllTemplates{
    ParticleTemplateKind::Empty, ParticleTemplateKind::Fountain, ParticleTemplateKind::Sparks,
    ParticleTemplateKind::Fire, ParticleTemplateKind::Smoke};

[[nodiscard]] Core::EngineConfig MakeVfxEditorTestConfig() {
    Core::EngineConfig config{};
    config.headless = true;
    config.logFilePath = "uve_vfx_editor_tests.log";
    config.settingsFilePath = "uve_vfx_editor_tests_settings.json";
    config.projectSettingsFilePath = "uve_vfx_editor_tests_project_settings.json";
    config.inputMapFilePath = "uve_vfx_editor_tests_input_map.json";
    config.assetDatabaseFilePath = "uve_vfx_editor_tests_assets.json";
    config.saveDirectoryPath = "uve_vfx_editor_tests_saves";
    config.shaderCachePath = "uve_vfx_editor_tests_shader_cache";
    config.shaderSourceRealDirectory = ::UVE::Tests::RepositoryRoot() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefix = "shaders";
    return config;
}

/// PRISM widgets measure text with the theme's fonts, so a hosted PRISM application (headless
/// platform, no window) must exist before any widget is built.
void EnsurePrismHosted() {
    static prism::Application app;
    static const bool ready = [] {
        prism::HostDesc desc;
        desc.assetDir = (::UVE::Tests::RepositoryRoot() / "Engine/Runtime/Prism/assets").string();
        desc.width = 600;
        desc.height = 400;
        return app.initHosted(desc, std::make_shared<prism::HeadlessPlatform>());
    }();
    ASSERT_TRUE(ready);
}

TEST(VfxEditorGraphTest, BuildThenExtract_RoundTripsEveryTemplateEmitter) {
    EnsurePrismHosted();
    for (const ParticleTemplateKind kind : kAllTemplates) {
        const ParticleSystemAsset system = VFX::CreateParticleTemplate(kind);
        for (std::size_t i = 0U; i < system.emitters.size(); ++i) {
            prism::BlueprintGraph graph;
            BuildEmitterGraph(system, i, graph);
            Asset::ParticleEmitterAsset extracted;
            ASSERT_TRUE(ExtractEmitterFromGraph(graph, system, i, extracted))
                << "template " << static_cast<int>(kind) << " emitter " << i;
            // Templates ship unplaced (0,0); the graph auto-lays them out, so compare everything but position.
            const auto stripPlacement = [](Asset::ParticleEmitterAsset e) {
                for (auto* stage : {&e.emitterUpdate, &e.particleSpawn, &e.particleUpdate, &e.render}) {
                    for (Asset::ParticleModule& m : *stage) {
                        m.graphX = 0.0F;
                        m.graphY = 0.0F;
                        m.graphPosAtOrigin = false;
                    }
                }
                for (Asset::ParticleMathNode& m : e.mathNodes) {
                    m.graphX = 0.0F;
                    m.graphY = 0.0F;
                }
                e.stageNodePositions = {};
                return e;
            };
            const Asset::ParticleEmitterAsset ref = stripPlacement(system.emitters[i]);
            extracted = stripPlacement(std::move(extracted));
            EXPECT_TRUE(extracted == ref) << "template " << static_cast<int>(kind) << " emitter " << i;
        }
    }
}

TEST(VfxEditorCurveTest, AddMoveDeleteAndTangentsKeepKeysSortedAndBounded) {
    Asset::ParticleCurve curve;
    ApplyVfxCurvePreset(curve, VfxCurvePreset::Linear);
    ASSERT_GE(curve.keys.size(), 2U);

    const std::size_t added = AddVfxCurveKey(curve, 0.5F, 0.75F);
    ASSERT_LT(added, curve.keys.size());
    EXPECT_FLOAT_EQ(curve.keys[added].time, 0.5F);
    EXPECT_TRUE(std::is_sorted(curve.keys.begin(), curve.keys.end(),
                               [](const auto& a, const auto& b) { return a.time < b.time; }));

    const std::size_t moved = MoveVfxCurveKey(curve, added, 0.25F, 0.4F);
    ASSERT_LT(moved, curve.keys.size());
    EXPECT_FLOAT_EQ(curve.keys[moved].time, 0.25F);
    EXPECT_FLOAT_EQ(curve.keys[moved].value, 0.4F);

    EXPECT_TRUE(SetVfxCurveKeyTangents(curve, moved, 1.5F, -0.5F));
    EXPECT_FLOAT_EQ(curve.keys[moved].inTangent, 1.5F);
    EXPECT_FLOAT_EQ(curve.keys[moved].outTangent, -0.5F);

    EXPECT_TRUE(DeleteVfxCurveKey(curve, moved));
    while (curve.keys.size() > 1U) {
        ASSERT_TRUE(DeleteVfxCurveKey(curve, 0U));
    }
    EXPECT_FALSE(DeleteVfxCurveKey(curve, 0U)) << "the last key must be kept";
    EXPECT_EQ(curve.keys.size(), 1U);
}

TEST(VfxEditorCurveTest, EveryPresetProducesAValidCurve) {
    for (const VfxCurvePreset preset :
         {VfxCurvePreset::Linear, VfxCurvePreset::EaseIn, VfxCurvePreset::EaseOut,
          VfxCurvePreset::Bell, VfxCurvePreset::FadeOut}) {
        Asset::ParticleCurve curve;
        ApplyVfxCurvePreset(curve, preset);
        EXPECT_GE(curve.keys.size(), 2U);
        EXPECT_LE(curve.keys.size(), Asset::kMaximumCurveKeys);
        for (const auto& key : curve.keys) {
            EXPECT_GE(key.time, 0.0F);
            EXPECT_LE(key.time, 1.0F);
            EXPECT_TRUE(std::isfinite(key.value));
        }
    }
}

TEST(VfxEditorGradientTest, AddMoveDeleteStops) {
    Asset::ParticleGradient gradient;
    AddVfxGradientStop(gradient, 0.0F, Math::Color{1.0F, 0.0F, 0.0F}, 1.0F);
    AddVfxGradientStop(gradient, 1.0F, Math::Color{0.0F, 0.0F, 1.0F}, 0.0F);
    const std::size_t mid = AddVfxGradientStop(gradient, 0.5F, Math::Color{0.0F, 1.0F, 0.0F}, 0.5F);
    ASSERT_LT(mid, gradient.stops.size());
    EXPECT_EQ(gradient.stops.size(), 3U);

    const std::size_t moved = MoveVfxGradientStop(gradient, mid, 0.3F);
    ASSERT_LT(moved, gradient.stops.size());
    EXPECT_FLOAT_EQ(gradient.stops[moved].time, 0.3F);

    EXPECT_TRUE(DeleteVfxGradientStop(gradient, moved));
    EXPECT_EQ(gradient.stops.size(), 2U);
    EXPECT_TRUE(DeleteVfxGradientStop(gradient, 0U));
    EXPECT_FALSE(DeleteVfxGradientStop(gradient, 0U)) << "the last stop must be kept";
}

TEST(VfxEditorGradientTest, SyncThroughWidgetRoundTripsStopColoursAndAlpha) {
    EnsurePrismHosted();
    Asset::ParticleGradient source;
    AddVfxGradientStop(source, 0.0F, Math::Color{1.0F, 0.5F, 0.0F}, 1.0F);
    AddVfxGradientStop(source, 1.0F, Math::Color{0.0F, 0.5F, 1.0F}, 0.25F);

    prism::GradientEditor widget;
    SyncGradientEditorFromAsset(source, widget);
    Asset::ParticleGradient back;
    SyncAssetFromGradientEditor(widget, back);

    ASSERT_EQ(back.stops.size(), source.stops.size());
    for (std::size_t i = 0U; i < source.stops.size(); ++i) {
        EXPECT_NEAR(back.stops[i].time, source.stops[i].time, 1.0e-3F);
        EXPECT_NEAR(back.stops[i].color.r, source.stops[i].color.r, 2.0e-2F);
        EXPECT_NEAR(back.stops[i].color.g, source.stops[i].color.g, 2.0e-2F);
        EXPECT_NEAR(back.stops[i].color.b, source.stops[i].color.b, 2.0e-2F);
        EXPECT_NEAR(back.stops[i].alpha, source.stops[i].alpha, 2.0e-2F);
    }
}

class VfxEditorSessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_engine = std::make_unique<Core::EngineCore>(MakeVfxEditorTestConfig());
        m_engine->Init();
        ASSERT_TRUE(m_engine->Load());
        m_editor = std::make_unique<Editor>(m_engine->GetServices(), "uve_vfx_editor_tests.uvscene");
        m_editor->Init();
        m_dir = ::UVE::Tests::MakeTestCaseDirectory();
    }

    void TearDown() override {
        if (m_editor) {
            m_editor->Shutdown();
        }
        m_editor.reset();
        if (m_engine) {
            m_engine->Shutdown();
        }
    }

    [[nodiscard]] std::filesystem::path CreateSparks() {
        return m_editor->CreateParticleSystemAsset(m_dir, "VFX_Sparks", ParticleTemplateKind::Sparks);
    }

    std::unique_ptr<Core::EngineCore> m_engine;
    std::unique_ptr<Editor> m_editor;
    std::filesystem::path m_dir;
};

TEST_F(VfxEditorSessionTest, CreateFromTemplate_WritesAValidUvfxThatOpens) {
    const std::filesystem::path path = CreateSparks();
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.extension().string(), ".uvfx");
    EXPECT_TRUE(std::filesystem::exists(path));

    ASSERT_TRUE(m_editor->OpenVfxEditor(path));
    EXPECT_TRUE(m_editor->IsVfxEditorOpen());
    EXPECT_FALSE(m_editor->IsVfxEditorDirty());
    ASSERT_NE(m_editor->VfxEditSessionPtr(), nullptr);
    EXPECT_FALSE(m_editor->VfxEditSessionPtr()->system.emitters.empty());
    EXPECT_TRUE(m_editor->VfxEditSessionPtr()->previewHandle.IsValid());
    EXPECT_FALSE(m_editor->VfxEditSessionPtr()->playing)
        << "preview simulation must start stopped when opening a particle system";
}

TEST_F(VfxEditorSessionTest, PlaybackControls_StartStoppedAndStopResetsPlayhead) {
    ASSERT_TRUE(m_editor->OpenVfxEditor(CreateSparks()));
    Editor::VfxEditSession* const session = m_editor->VfxEditSessionPtr();
    ASSERT_NE(session, nullptr);

    EXPECT_FALSE(session->playing);
    EXPECT_DOUBLE_EQ(session->playhead, 0.0);

    // Ticking while stopped must not advance the playhead.
    m_editor->TickVfxEditor(0.1);
    EXPECT_DOUBLE_EQ(session->playhead, 0.0);

    // Play advances the playhead.
    m_editor->PlayVfxPreview();
    EXPECT_TRUE(session->playing);
    m_editor->TickVfxEditor(0.1);
    EXPECT_GT(session->playhead, 0.0);
    const double afterPlay = session->playhead;

    // Pause freezes the playhead at its current time.
    m_editor->PauseVfxPreview();
    EXPECT_FALSE(session->playing);
    m_editor->TickVfxEditor(0.1);
    EXPECT_DOUBLE_EQ(session->playhead, afterPlay);

    // Stop pauses and resets the playhead back to t = 0.
    m_editor->PlayVfxPreview();
    m_editor->TickVfxEditor(0.05);
    EXPECT_GT(session->playhead, afterPlay);
    m_editor->StopVfxPreview();
    EXPECT_FALSE(session->playing);
    EXPECT_DOUBLE_EQ(session->playhead, 0.0);
    EXPECT_TRUE(session->previewHandle.IsValid());
}

TEST_F(VfxEditorSessionTest, EditUndoRedoSave_TracksDirtyStateAndPersists) {
    const std::filesystem::path path = CreateSparks();
    ASSERT_TRUE(m_editor->OpenVfxEditor(path));

    ASSERT_TRUE(m_editor->EditVfxSystem([](ParticleSystemAsset& sys) {
        sys.emitters[0].emitterUpdate[0].floats["Rate"].constant = 321.0F;
    }));
    EXPECT_TRUE(m_editor->IsVfxEditorDirty());
    EXPECT_FLOAT_EQ(m_editor->VfxEditSessionPtr()->system.emitters[0].emitterUpdate[0].floats["Rate"].constant, 321.0F);

    ASSERT_TRUE(m_editor->UndoVfxEdit());
    EXPECT_FALSE(m_editor->IsVfxEditorDirty());
    ASSERT_TRUE(m_editor->RedoVfxEdit());
    EXPECT_TRUE(m_editor->IsVfxEditorDirty());

    ASSERT_TRUE(m_editor->SaveVfxEditor());
    EXPECT_FALSE(m_editor->IsVfxEditorDirty());

    ParticleSystemAsset reloaded;
    ASSERT_TRUE(Asset::LoadParticleSystemAsset(path, reloaded));
    EXPECT_FLOAT_EQ(reloaded.emitters[0].emitterUpdate[0].floats["Rate"].constant, 321.0F);

    EXPECT_FALSE(m_editor->EditVfxSystem([](ParticleSystemAsset&) {})) << "a no-op edit is not an edit";
}

TEST_F(VfxEditorSessionTest, ScrubbingIsDeterministic) {
    ASSERT_TRUE(m_editor->OpenVfxEditor(CreateSparks()));
    Editor::VfxEditSession* const session = m_editor->VfxEditSessionPtr();
    ASSERT_NE(session, nullptr);

    const auto liveAfterScrub = [&](const double seconds) {
        m_editor->SetVfxPlayhead(seconds);
        const VFX::VfxInstance* const instance = session->previewWorld.TryGetInstance(session->previewHandle);
        std::size_t live = 0U;
        if (instance != nullptr) {
            for (std::size_t i = 0U; i < instance->GetEmitterCount(); ++i) {
                live += instance->GetEmitterState(i).liveCount;
            }
        }
        return live;
    };
    const std::size_t first = liveAfterScrub(0.4);
    EXPECT_GT(first, 0U);
    static_cast<void>(liveAfterScrub(0.1));
    EXPECT_EQ(liveAfterScrub(0.4), first);
    EXPECT_NEAR(session->playhead, 0.4, 1.0e-9);
    EXPECT_FALSE(session->playing);
}

TEST_F(VfxEditorSessionTest, CompileError_ReportsOnRenderStageAndPreviewKeepsLastGoodProgram) {
    ASSERT_TRUE(m_editor->OpenVfxEditor(CreateSparks()));
    Editor::VfxEditSession* const session = m_editor->VfxEditSessionPtr();
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->hasGoodSystem);
    ASSERT_TRUE(session->compileMessages.empty() ||
                std::none_of(session->compileMessages.begin(), session->compileMessages.end(), [](const auto& m) {
                    return m.severity == VFX::ParticleCompileMessage::Severity::Error;
                }));
    const VFX::VfxHandle handleBefore = session->previewHandle;
    ASSERT_TRUE(handleBefore.IsValid());

    ASSERT_TRUE(m_editor->EditVfxSystem([](ParticleSystemAsset& sys) {
        std::erase_if(sys.emitters[0].render, [](const Asset::ParticleModule& m) {
            return m.type == ParticleModuleType::SpriteRenderer;
        });
    }));

    const bool hasError = std::any_of(session->compileMessages.begin(), session->compileMessages.end(), [](const auto& m) {
        return m.severity == VFX::ParticleCompileMessage::Severity::Error;
    });
    EXPECT_TRUE(hasError);
    EXPECT_FALSE(m_editor->CompileVfxEditor());

    // The preview carries on with the last good version.
    EXPECT_EQ(session->previewHandle, handleBefore);
    EXPECT_TRUE(session->previewWorld.IsAlive(session->previewHandle));
    bool lastGoodHasRenderer = false;
    for (const Asset::ParticleModule& m : session->lastGoodSystem.emitters[0].render) {
        lastGoodHasRenderer = lastGoodHasRenderer || m.type == ParticleModuleType::SpriteRenderer;
    }
    EXPECT_TRUE(lastGoodHasRenderer);

    // Undo repairs it: no errors, a fresh preview.
    ASSERT_TRUE(m_editor->UndoVfxEdit());
    EXPECT_TRUE(std::none_of(session->compileMessages.begin(), session->compileMessages.end(), [](const auto& m) {
        return m.severity == VFX::ParticleCompileMessage::Severity::Error;
    }));
}

TEST_F(VfxEditorSessionTest, SoloEmitter_PlaysOnlyThatEmitter) {
    ASSERT_TRUE(m_editor->OpenVfxEditor(CreateSparks()));
    Editor::VfxEditSession* const session = m_editor->VfxEditSessionPtr();
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(m_editor->EditVfxSystem([](ParticleSystemAsset& sys) {
        Asset::ParticleEmitterAsset second = sys.emitters[0];
        second.name = "Second";
        sys.emitters.push_back(second);
    }));
    session->soloEmitter = 1U;
    ASSERT_TRUE(m_editor->CompileVfxEditor());
    m_editor->SetVfxPlayhead(0.3);
    const VFX::VfxInstance* const instance = session->previewWorld.TryGetInstance(session->previewHandle);
    ASSERT_NE(instance, nullptr);
    ASSERT_GE(instance->GetEmitterCount(), 2U);
    EXPECT_EQ(instance->GetEmitterState(0U).liveCount, 0U);
    EXPECT_GT(instance->GetEmitterState(1U).liveCount, 0U);
}

TEST_F(VfxEditorSessionTest, StudioPreview_SpawnsFlushOnFloorAndBouncesOffCollider3D) {
    ASSERT_TRUE(m_editor->OpenVfxEditor(CreateSparks()));
    const Editor::VfxEditSession* const session = m_editor->VfxEditSessionPtr();
    ASSERT_NE(session, nullptr);
    EXPECT_FALSE(session->showGrid);
    ASSERT_TRUE(session->previewHandle.IsValid());

    const VFX::VfxOwner* const owner = session->previewWorld.TryGetOwner(session->previewHandle);
    ASSERT_NE(owner, nullptr);
    EXPECT_FLOAT_EQ(owner->position.x, 0.0F);
    EXPECT_FLOAT_EQ(owner->position.y, 0.0F);
    EXPECT_FLOAT_EQ(owner->position.z, 0.0F);

    // Verify real-time Collider3D floor collision keeps falling sparks above the floor surface (y >= 0).
    VFX::VfxColliderVolume floorCol{};
    floorCol.kind = VFX::VfxColliderShapeKind::Box;
    floorCol.center = Math::Vector3{0.0F, -0.5F, 0.0F};
    floorCol.halfExtents = Math::Vector3{50.0F, 0.5F, 50.0F};
    VFX::VfxWorld world;
    world.SetSceneColliders({floorCol});
    const VFX::VfxHandle handle = world.SpawnFromAsset(
        "sparks_floor_bounce", VFX::CreateParticleTemplate(VFX::ParticleTemplateKind::Sparks),
        VFX::VfxOwner{{0.0F, 0.0F, 0.0F}, {}, {}, 2U}, 0ULL, 7U);
    ASSERT_TRUE(handle.IsValid());
    for (int i = 0; i < 45; ++i) {
        world.Tick(1.0F / 60.0F);
    }
    const VFX::VfxInstance* const inst = world.TryGetInstance(handle);
    ASSERT_NE(inst, nullptr);
    const VFX::VfxEmitterState& state = inst->GetEmitterState(0U);
    ASSERT_GT(state.liveCount, 0U);
    for (std::size_t i = 0U; i < state.liveCount; ++i) {
        EXPECT_GE(state.posY[i], -0.02F);
    }
}

} // namespace
} // namespace UVE::Editor::Tests
