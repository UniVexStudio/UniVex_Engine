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

using Asset::ParticleModuleTypeUVE;
using Asset::ParticleSystemAssetUVE;
using VFX::ParticleTemplateKindUVE;

constexpr std::array<ParticleTemplateKindUVE, 5> kAllTemplatesUVE{
    ParticleTemplateKindUVE::Empty, ParticleTemplateKindUVE::Fountain, ParticleTemplateKindUVE::Sparks,
    ParticleTemplateKindUVE::Fire, ParticleTemplateKindUVE::Smoke};

[[nodiscard]] Core::EngineConfigUVE MakeVfxEditorTestConfigUVE() {
    Core::EngineConfigUVE config{};
    config.headlessUVE = true;
    config.logFilePath = "uve_vfx_editor_tests.log";
    config.settingsFilePath = "uve_vfx_editor_tests_settings.json";
    config.projectSettingsFilePath = "uve_vfx_editor_tests_project_settings.json";
    config.inputMapFilePath = "uve_vfx_editor_tests_input_map.json";
    config.assetDatabaseFilePath = "uve_vfx_editor_tests_assets.json";
    config.saveDirectoryPath = "uve_vfx_editor_tests_saves";
    config.shaderCachePath = "uve_vfx_editor_tests_shader_cache";
    config.shaderSourceRealDirectoryUVE = ::UVE::Tests::RepositoryRootUVE() / "Engine/Runtime/RHI/Shader/built_in";
    config.shaderSourceMountPrefixUVE = "shaders";
    return config;
}

/// PRISM widgets measure text with the theme's fonts, so a hosted PRISM application (headless
/// platform, no window) must exist before any widget is built.
void EnsurePrismHostedUVE() {
    static prism::Application app;
    static const bool ready = [] {
        prism::HostDesc desc;
        desc.assetDir = (::UVE::Tests::RepositoryRootUVE() / "Engine/Runtime/Prism/assets").string();
        desc.width = 600;
        desc.height = 400;
        return app.initHosted(desc, std::make_shared<prism::HeadlessPlatform>());
    }();
    ASSERT_TRUE(ready);
}

TEST(VfxEditorGraphUVETest, BuildThenExtract_RoundTripsEveryTemplateEmitter) {
    EnsurePrismHostedUVE();
    for (const ParticleTemplateKindUVE kind : kAllTemplatesUVE) {
        const ParticleSystemAssetUVE system = VFX::CreateParticleTemplateUVE(kind);
        for (std::size_t i = 0U; i < system.emitters.size(); ++i) {
            prism::BlueprintGraph graph;
            BuildEmitterGraphUVE(system, i, graph);
            Asset::ParticleEmitterAssetUVE extracted;
            ASSERT_TRUE(ExtractEmitterFromGraphUVE(graph, system, i, extracted))
                << "template " << static_cast<int>(kind) << " emitter " << i;
            // Templates ship unplaced (0,0); the graph auto-lays them out, so compare everything but position.
            const auto stripPlacement = [](Asset::ParticleEmitterAssetUVE e) {
                for (auto* stage : {&e.emitterUpdate, &e.particleSpawn, &e.particleUpdate, &e.render}) {
                    for (Asset::ParticleModuleUVE& m : *stage) {
                        m.graphX = 0.0F;
                        m.graphY = 0.0F;
                        m.graphPosAtOrigin = false;
                    }
                }
                for (Asset::ParticleMathNodeUVE& m : e.mathNodes) {
                    m.graphX = 0.0F;
                    m.graphY = 0.0F;
                }
                e.stageNodePositions = {};
                return e;
            };
            const Asset::ParticleEmitterAssetUVE ref = stripPlacement(system.emitters[i]);
            extracted = stripPlacement(std::move(extracted));
            EXPECT_TRUE(extracted == ref) << "template " << static_cast<int>(kind) << " emitter " << i;
        }
    }
}

TEST(VfxEditorCurveUVETest, AddMoveDeleteAndTangentsKeepKeysSortedAndBounded) {
    Asset::ParticleCurveUVE curve;
    ApplyVfxCurvePresetUVE(curve, VfxCurvePresetUVE::Linear);
    ASSERT_GE(curve.keys.size(), 2U);

    const std::size_t added = AddVfxCurveKeyUVE(curve, 0.5F, 0.75F);
    ASSERT_LT(added, curve.keys.size());
    EXPECT_FLOAT_EQ(curve.keys[added].time, 0.5F);
    EXPECT_TRUE(std::is_sorted(curve.keys.begin(), curve.keys.end(),
                               [](const auto& a, const auto& b) { return a.time < b.time; }));

    const std::size_t moved = MoveVfxCurveKeyUVE(curve, added, 0.25F, 0.4F);
    ASSERT_LT(moved, curve.keys.size());
    EXPECT_FLOAT_EQ(curve.keys[moved].time, 0.25F);
    EXPECT_FLOAT_EQ(curve.keys[moved].value, 0.4F);

    EXPECT_TRUE(SetVfxCurveKeyTangentsUVE(curve, moved, 1.5F, -0.5F));
    EXPECT_FLOAT_EQ(curve.keys[moved].inTangent, 1.5F);
    EXPECT_FLOAT_EQ(curve.keys[moved].outTangent, -0.5F);

    EXPECT_TRUE(DeleteVfxCurveKeyUVE(curve, moved));
    while (curve.keys.size() > 1U) {
        ASSERT_TRUE(DeleteVfxCurveKeyUVE(curve, 0U));
    }
    EXPECT_FALSE(DeleteVfxCurveKeyUVE(curve, 0U)) << "the last key must be kept";
    EXPECT_EQ(curve.keys.size(), 1U);
}

TEST(VfxEditorCurveUVETest, EveryPresetProducesAValidCurve) {
    for (const VfxCurvePresetUVE preset :
         {VfxCurvePresetUVE::Linear, VfxCurvePresetUVE::EaseIn, VfxCurvePresetUVE::EaseOut,
          VfxCurvePresetUVE::Bell, VfxCurvePresetUVE::FadeOut}) {
        Asset::ParticleCurveUVE curve;
        ApplyVfxCurvePresetUVE(curve, preset);
        EXPECT_GE(curve.keys.size(), 2U);
        EXPECT_LE(curve.keys.size(), Asset::kMaximumCurveKeysUVE);
        for (const auto& key : curve.keys) {
            EXPECT_GE(key.time, 0.0F);
            EXPECT_LE(key.time, 1.0F);
            EXPECT_TRUE(std::isfinite(key.value));
        }
    }
}

TEST(VfxEditorGradientUVETest, AddMoveDeleteStops) {
    Asset::ParticleGradientUVE gradient;
    AddVfxGradientStopUVE(gradient, 0.0F, Math::ColorUVE{1.0F, 0.0F, 0.0F}, 1.0F);
    AddVfxGradientStopUVE(gradient, 1.0F, Math::ColorUVE{0.0F, 0.0F, 1.0F}, 0.0F);
    const std::size_t mid = AddVfxGradientStopUVE(gradient, 0.5F, Math::ColorUVE{0.0F, 1.0F, 0.0F}, 0.5F);
    ASSERT_LT(mid, gradient.stops.size());
    EXPECT_EQ(gradient.stops.size(), 3U);

    const std::size_t moved = MoveVfxGradientStopUVE(gradient, mid, 0.3F);
    ASSERT_LT(moved, gradient.stops.size());
    EXPECT_FLOAT_EQ(gradient.stops[moved].time, 0.3F);

    EXPECT_TRUE(DeleteVfxGradientStopUVE(gradient, moved));
    EXPECT_EQ(gradient.stops.size(), 2U);
    EXPECT_TRUE(DeleteVfxGradientStopUVE(gradient, 0U));
    EXPECT_FALSE(DeleteVfxGradientStopUVE(gradient, 0U)) << "the last stop must be kept";
}

TEST(VfxEditorGradientUVETest, SyncThroughWidgetRoundTripsStopColoursAndAlpha) {
    EnsurePrismHostedUVE();
    Asset::ParticleGradientUVE source;
    AddVfxGradientStopUVE(source, 0.0F, Math::ColorUVE{1.0F, 0.5F, 0.0F}, 1.0F);
    AddVfxGradientStopUVE(source, 1.0F, Math::ColorUVE{0.0F, 0.5F, 1.0F}, 0.25F);

    prism::GradientEditor widget;
    SyncGradientEditorFromAssetUVE(source, widget);
    Asset::ParticleGradientUVE back;
    SyncAssetFromGradientEditorUVE(widget, back);

    ASSERT_EQ(back.stops.size(), source.stops.size());
    for (std::size_t i = 0U; i < source.stops.size(); ++i) {
        EXPECT_NEAR(back.stops[i].time, source.stops[i].time, 1.0e-3F);
        EXPECT_NEAR(back.stops[i].color.r, source.stops[i].color.r, 2.0e-2F);
        EXPECT_NEAR(back.stops[i].color.g, source.stops[i].color.g, 2.0e-2F);
        EXPECT_NEAR(back.stops[i].color.b, source.stops[i].color.b, 2.0e-2F);
        EXPECT_NEAR(back.stops[i].alpha, source.stops[i].alpha, 2.0e-2F);
    }
}

class VfxEditorSessionUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        m_engine = std::make_unique<Core::EngineCoreUVE>(MakeVfxEditorTestConfigUVE());
        m_engine->Init();
        ASSERT_TRUE(m_engine->Load());
        m_editor = std::make_unique<EditorUVE>(m_engine->GetServicesUVE(), "uve_vfx_editor_tests.uvscene");
        m_editor->InitUVE();
        m_dir = ::UVE::Tests::MakeTestCaseDirectoryUVE();
    }

    void TearDown() override {
        if (m_editor) {
            m_editor->ShutdownUVE();
        }
        m_editor.reset();
        if (m_engine) {
            m_engine->Shutdown();
        }
    }

    [[nodiscard]] std::filesystem::path CreateSparksUVE() {
        return m_editor->CreateParticleSystemAssetUVE(m_dir, "VFX_Sparks", ParticleTemplateKindUVE::Sparks);
    }

    std::unique_ptr<Core::EngineCoreUVE> m_engine;
    std::unique_ptr<EditorUVE> m_editor;
    std::filesystem::path m_dir;
};

TEST_F(VfxEditorSessionUVETest, CreateFromTemplate_WritesAValidUvfxThatOpens) {
    const std::filesystem::path path = CreateSparksUVE();
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.extension().string(), ".uvfx");
    EXPECT_TRUE(std::filesystem::exists(path));

    ASSERT_TRUE(m_editor->OpenVfxEditorUVE(path));
    EXPECT_TRUE(m_editor->IsVfxEditorOpenUVE());
    EXPECT_FALSE(m_editor->IsVfxEditorDirtyUVE());
    ASSERT_NE(m_editor->VfxEditSessionPtrUVE(), nullptr);
    EXPECT_FALSE(m_editor->VfxEditSessionPtrUVE()->system.emitters.empty());
    EXPECT_TRUE(m_editor->VfxEditSessionPtrUVE()->previewHandle.IsValidUVE());
    EXPECT_FALSE(m_editor->VfxEditSessionPtrUVE()->playing)
        << "preview simulation must start stopped when opening a particle system";
}

TEST_F(VfxEditorSessionUVETest, PlaybackControls_StartStoppedAndStopResetsPlayhead) {
    ASSERT_TRUE(m_editor->OpenVfxEditorUVE(CreateSparksUVE()));
    EditorUVE::VfxEditSessionUVE* const session = m_editor->VfxEditSessionPtrUVE();
    ASSERT_NE(session, nullptr);

    EXPECT_FALSE(session->playing);
    EXPECT_DOUBLE_EQ(session->playhead, 0.0);

    // Ticking while stopped must not advance the playhead.
    m_editor->TickVfxEditorUVE(0.1);
    EXPECT_DOUBLE_EQ(session->playhead, 0.0);

    // Play advances the playhead.
    m_editor->PlayVfxPreviewUVE();
    EXPECT_TRUE(session->playing);
    m_editor->TickVfxEditorUVE(0.1);
    EXPECT_GT(session->playhead, 0.0);
    const double afterPlay = session->playhead;

    // Pause freezes the playhead at its current time.
    m_editor->PauseVfxPreviewUVE();
    EXPECT_FALSE(session->playing);
    m_editor->TickVfxEditorUVE(0.1);
    EXPECT_DOUBLE_EQ(session->playhead, afterPlay);

    // Stop pauses and resets the playhead back to t = 0.
    m_editor->PlayVfxPreviewUVE();
    m_editor->TickVfxEditorUVE(0.05);
    EXPECT_GT(session->playhead, afterPlay);
    m_editor->StopVfxPreviewUVE();
    EXPECT_FALSE(session->playing);
    EXPECT_DOUBLE_EQ(session->playhead, 0.0);
    EXPECT_TRUE(session->previewHandle.IsValidUVE());
}

TEST_F(VfxEditorSessionUVETest, EditUndoRedoSave_TracksDirtyStateAndPersists) {
    const std::filesystem::path path = CreateSparksUVE();
    ASSERT_TRUE(m_editor->OpenVfxEditorUVE(path));

    ASSERT_TRUE(m_editor->EditVfxSystemUVE([](ParticleSystemAssetUVE& sys) {
        sys.emitters[0].emitterUpdate[0].floats["Rate"].constant = 321.0F;
    }));
    EXPECT_TRUE(m_editor->IsVfxEditorDirtyUVE());
    EXPECT_FLOAT_EQ(m_editor->VfxEditSessionPtrUVE()->system.emitters[0].emitterUpdate[0].floats["Rate"].constant, 321.0F);

    ASSERT_TRUE(m_editor->UndoVfxEditUVE());
    EXPECT_FALSE(m_editor->IsVfxEditorDirtyUVE());
    ASSERT_TRUE(m_editor->RedoVfxEditUVE());
    EXPECT_TRUE(m_editor->IsVfxEditorDirtyUVE());

    ASSERT_TRUE(m_editor->SaveVfxEditorUVE());
    EXPECT_FALSE(m_editor->IsVfxEditorDirtyUVE());

    ParticleSystemAssetUVE reloaded;
    ASSERT_TRUE(Asset::LoadParticleSystemAssetUVE(path, reloaded));
    EXPECT_FLOAT_EQ(reloaded.emitters[0].emitterUpdate[0].floats["Rate"].constant, 321.0F);

    EXPECT_FALSE(m_editor->EditVfxSystemUVE([](ParticleSystemAssetUVE&) {})) << "a no-op edit is not an edit";
}

TEST_F(VfxEditorSessionUVETest, ScrubbingIsDeterministic) {
    ASSERT_TRUE(m_editor->OpenVfxEditorUVE(CreateSparksUVE()));
    EditorUVE::VfxEditSessionUVE* const session = m_editor->VfxEditSessionPtrUVE();
    ASSERT_NE(session, nullptr);

    const auto liveAfterScrub = [&](const double seconds) {
        m_editor->SetVfxPlayheadUVE(seconds);
        const VFX::VfxInstanceUVE* const instance = session->previewWorld.TryGetInstanceUVE(session->previewHandle);
        std::size_t live = 0U;
        if (instance != nullptr) {
            for (std::size_t i = 0U; i < instance->GetEmitterCountUVE(); ++i) {
                live += instance->GetEmitterStateUVE(i).liveCount;
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

TEST_F(VfxEditorSessionUVETest, CompileError_ReportsOnRenderStageAndPreviewKeepsLastGoodProgram) {
    ASSERT_TRUE(m_editor->OpenVfxEditorUVE(CreateSparksUVE()));
    EditorUVE::VfxEditSessionUVE* const session = m_editor->VfxEditSessionPtrUVE();
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(session->hasGoodSystem);
    ASSERT_TRUE(session->compileMessages.empty() ||
                std::none_of(session->compileMessages.begin(), session->compileMessages.end(), [](const auto& m) {
                    return m.severity == VFX::ParticleCompileMessageUVE::Severity::Error;
                }));
    const VFX::VfxHandleUVE handleBefore = session->previewHandle;
    ASSERT_TRUE(handleBefore.IsValidUVE());

    ASSERT_TRUE(m_editor->EditVfxSystemUVE([](ParticleSystemAssetUVE& sys) {
        std::erase_if(sys.emitters[0].render, [](const Asset::ParticleModuleUVE& m) {
            return m.type == ParticleModuleTypeUVE::SpriteRenderer;
        });
    }));

    const bool hasError = std::any_of(session->compileMessages.begin(), session->compileMessages.end(), [](const auto& m) {
        return m.severity == VFX::ParticleCompileMessageUVE::Severity::Error;
    });
    EXPECT_TRUE(hasError);
    EXPECT_FALSE(m_editor->CompileVfxEditorUVE());

    // The preview carries on with the last good version.
    EXPECT_EQ(session->previewHandle, handleBefore);
    EXPECT_TRUE(session->previewWorld.IsAliveUVE(session->previewHandle));
    bool lastGoodHasRenderer = false;
    for (const Asset::ParticleModuleUVE& m : session->lastGoodSystem.emitters[0].render) {
        lastGoodHasRenderer = lastGoodHasRenderer || m.type == ParticleModuleTypeUVE::SpriteRenderer;
    }
    EXPECT_TRUE(lastGoodHasRenderer);

    // Undo repairs it: no errors, a fresh preview.
    ASSERT_TRUE(m_editor->UndoVfxEditUVE());
    EXPECT_TRUE(std::none_of(session->compileMessages.begin(), session->compileMessages.end(), [](const auto& m) {
        return m.severity == VFX::ParticleCompileMessageUVE::Severity::Error;
    }));
}

TEST_F(VfxEditorSessionUVETest, SoloEmitter_PlaysOnlyThatEmitter) {
    ASSERT_TRUE(m_editor->OpenVfxEditorUVE(CreateSparksUVE()));
    EditorUVE::VfxEditSessionUVE* const session = m_editor->VfxEditSessionPtrUVE();
    ASSERT_NE(session, nullptr);
    ASSERT_TRUE(m_editor->EditVfxSystemUVE([](ParticleSystemAssetUVE& sys) {
        Asset::ParticleEmitterAssetUVE second = sys.emitters[0];
        second.name = "Second";
        sys.emitters.push_back(second);
    }));
    session->soloEmitter = 1U;
    ASSERT_TRUE(m_editor->CompileVfxEditorUVE());
    m_editor->SetVfxPlayheadUVE(0.3);
    const VFX::VfxInstanceUVE* const instance = session->previewWorld.TryGetInstanceUVE(session->previewHandle);
    ASSERT_NE(instance, nullptr);
    ASSERT_GE(instance->GetEmitterCountUVE(), 2U);
    EXPECT_EQ(instance->GetEmitterStateUVE(0U).liveCount, 0U);
    EXPECT_GT(instance->GetEmitterStateUVE(1U).liveCount, 0U);
}

TEST_F(VfxEditorSessionUVETest, StudioPreview_ElevatesEmitterAboveFloorWithGridDisabledByDefault) {
    ASSERT_TRUE(m_editor->OpenVfxEditorUVE(CreateSparksUVE()));
    const EditorUVE::VfxEditSessionUVE* const session = m_editor->VfxEditSessionPtrUVE();
    ASSERT_NE(session, nullptr);
    EXPECT_FALSE(session->showGrid);
    ASSERT_TRUE(session->previewHandle.IsValidUVE());

    const VFX::VfxOwnerUVE* const owner = session->previewWorld.TryGetOwnerUVE(session->previewHandle);
    ASSERT_NE(owner, nullptr);
    EXPECT_FLOAT_EQ(owner->position.x, 0.0F);
    EXPECT_GE(owner->position.y, 1.5F);
    EXPECT_FLOAT_EQ(owner->position.z, 0.0F);

    const Math::AabbUVE deepBounds{Math::Vector3UVE{-2.0F, -2.5F, -2.0F},
                                   Math::Vector3UVE{2.0F, 2.5F, 2.0F}};
    const Math::Vector3UVE deepOrigin = EditorUVE::ResolveVfxPreviewOriginUVE(deepBounds);
    EXPECT_NEAR(deepOrigin.y + deepBounds.min.y, 0.35F, 1.0e-5F);
}

} // namespace
} // namespace UVE::Editor::Tests
