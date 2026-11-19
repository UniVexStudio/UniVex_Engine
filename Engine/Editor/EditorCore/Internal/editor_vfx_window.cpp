// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor.h"

#include "document_scene.h"
#include "engine_services.h"
#include "i_asset_database.h"
#include "i_renderer_3d.h"
#include "i_project_file_index.h"
#include "i_scene_graph.h"
#include "light_component.h"
#include "particle_render_bridge.h"
#include "transform_component.h"
#include "vfx_instance.h"
#include "world_environment_3d.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <system_error>

namespace UVE::Editor {

namespace {

constexpr float kVfxStepSecondsUVE = 1.0F / 30.0F;
constexpr int kVfxThumbnailSizeUVE = 128;

void PopulateVfxDarkStudioSceneUVE(Core::EngineServicesUVE& services) {
    Scene::IEntityManagerUVE& em = services.GetEntityManagerUVE();
    Scene::ISceneGraphUVE& sg = services.GetSceneGraphUVE();

    const Scene::EntityUVE envEntity = em.CreateEntityUVE();
    sg.AttachTransformUVE(em, envEntity, Scene::TransformComponentUVE{});
    Scene::WorldEnvironment3DComponentUVE env{};
    env.skyColor = Math::Vector3UVE{0.022F, 0.025F, 0.032F};
    env.horizonColor = Math::Vector3UVE{0.036F, 0.040F, 0.050F};
    env.groundColor = Math::Vector3UVE{0.016F, 0.018F, 0.022F};
    env.ambientColor = Math::Vector3UVE{0.085F, 0.092F, 0.112F};
    env.ambientEnergy = 0.55F;
    env.exposure = 1.05F;
    env.postProcessingEnabled = true;
    env.bloomEnabled = true;
    env.bloomIntensity = 0.85F;
    env.bloomThreshold = 1.0F;
    env.bloomSoftKnee = 0.5F;
    env.ssaoEnabled = true;
    env.ssaoRadius = 0.55F;
    env.ssaoIntensity = 1.05F;
    em.AddComponentUVE<Scene::WorldEnvironment3DComponentUVE>(envEntity, env);

    const auto addStudioPointLight = [&](const Math::Vector3UVE& position,
                                         const Math::ColorUVE& color,
                                         const float intensity,
                                         const float range) {
        const Scene::EntityUVE lightEntity = em.CreateEntityUVE();
        Scene::TransformComponentUVE transform{};
        transform.localPosition = position;
        sg.AttachTransformUVE(em, lightEntity, transform);
        Scene::LightComponentUVE light{};
        light.type = Scene::LightTypeUVE::Point;
        light.color = color;
        light.intensity = intensity;
        light.range = range;
        em.AddComponentUVE<Scene::LightComponentUVE>(lightEntity, light);
    };
    // 3-point studio light rig (Warm Key, Cool Rim, Soft Fill) around the elevated emitter stage.
    addStudioPointLight(Math::Vector3UVE{3.6F, 5.4F, 3.8F}, Math::ColorUVE{1.00F, 0.96F, 0.90F}, 2.25F, 22.0F);
    addStudioPointLight(Math::Vector3UVE{-4.0F, 3.9F, -3.6F}, Math::ColorUVE{0.58F, 0.72F, 1.00F}, 1.45F, 20.0F);
    addStudioPointLight(Math::Vector3UVE{-3.0F, 2.7F, 4.2F}, Math::ColorUVE{0.78F, 0.84F, 0.96F}, 0.95F, 18.0F);
    sg.UpdateUVE(em);
    services.GetRenderer3DUVE().SetSceneClearColorUVE({0.028F, 0.031F, 0.038F, 1.0F});
}

[[nodiscard]] bool HasErrorUVE(const std::vector<VFX::ParticleCompileMessageUVE>& messages) noexcept {
    return std::ranges::any_of(messages, [](const VFX::ParticleCompileMessageUVE& m) {
        return m.severity == VFX::ParticleCompileMessageUVE::Severity::Error;
    });
}

/// The system the preview actually plays: the last good one, with every emitter but the solo one off.
[[nodiscard]] Asset::ParticleSystemAssetUVE BuildPreviewSystemUVE(const Asset::ParticleSystemAssetUVE& source,
                                                                  const std::optional<std::size_t>& solo) {
    Asset::ParticleSystemAssetUVE system = source;
    if (solo.has_value()) {
        for (std::size_t i = 0U; i < system.emitters.size(); ++i) {
            if (i != *solo) {
                system.emitters[i].enabled = false;
            }
        }
    }
    return system;
}

/// Runs a compile over every emitter that will play. Returns true when nothing errored.
[[nodiscard]] bool CompileSystemUVE(const Asset::ParticleSystemAssetUVE& system,
                                    const std::optional<std::size_t>& solo,
                                    const std::filesystem::path& projectRoot,
                                    std::vector<VFX::ParticleCompileMessageUVE>& outMessages) {
    outMessages.clear();
    VFX::VfxCompileContextUVE context;
    context.projectRoot = projectRoot;
    if (system.emitters.empty()) {
        outMessages.push_back({VFX::ParticleCompileMessageUVE::Severity::Warning, 0U,
                               "The system has no emitters."});
    }
    for (std::size_t i = 0U; i < system.emitters.size(); ++i) {
        if (!system.emitters[i].enabled || (solo.has_value() && *solo != i)) {
            continue;
        }
        VFX::ParticleProgramUVE program;
        std::vector<VFX::ParticleCompileMessageUVE> messages;
        const bool ok = VFX::CompileParticleEmitterUVE(system, i, context, program, messages);
        if (!ok && !HasErrorUVE(messages)) {
            messages.push_back({VFX::ParticleCompileMessageUVE::Severity::Error, 0U,
                                "Emitter '" + system.emitters[i].name + "' did not compile."});
        }
        for (VFX::ParticleCompileMessageUVE& m : messages) {
            m.emitterIndex = static_cast<std::uint32_t>(i);
        }
        outMessages.insert(outMessages.end(), messages.begin(), messages.end());
    }
    return !HasErrorUVE(outMessages);
}

[[nodiscard]] std::string LowerExtensionUVE(const std::filesystem::path& path) {
    std::string extension = path.extension().generic_string();
    std::ranges::transform(extension, extension.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

} // namespace

bool EditorUVE::IsVfxEditorDirtyUVE() const noexcept {
    return m_vfxEdit != nullptr && !(m_vfxEdit->system == m_vfxEdit->savedSystem);
}

bool EditorUVE::OpenVfxEditorUVE(const std::filesystem::path& absolutePath) {
    if (m_vfxEdit != nullptr && m_vfxEdit->path == absolutePath) {
        m_vfxEdit->focusPending = true;
        return true;
    }
    Asset::ParticleSystemAssetUVE system;
    if (!Asset::LoadParticleSystemAssetUVE(absolutePath, system)) {
        m_contentStatusMessage = "Could not open " + absolutePath.filename().string() + " as a particle system.";
        return false;
    }
    auto session = std::make_unique<VfxEditSessionUVE>();
    session->path = absolutePath;
    session->system = system;
    session->savedSystem = system;
    session->loop = system.looping;
    m_vfxEdit = std::move(session);
    m_vfxEdit->previewWorld.SetProjectRootUVE(m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot.parent_path());
    static_cast<void>(CompileVfxEditorUVE());
    m_contentStatusMessage = "Opened " + absolutePath.filename().string() + " in the Particle System Editor.";
    return true;
}

bool EditorUVE::SaveVfxEditorUVE() {
    if (m_vfxEdit == nullptr) {
        return false;
    }
    if (!Asset::IsParticleSystemAssetValidUVE(m_vfxEdit->system)) {
        m_vfxEdit->status = "Not saved: the particle system is not valid.";
        return false;
    }
    if (!Asset::SaveParticleSystemAssetUVE(m_vfxEdit->system, m_vfxEdit->path)) {
        m_vfxEdit->status = "Could not write " + m_vfxEdit->path.filename().string() + ".";
        return false;
    }
    m_vfxEdit->savedSystem = m_vfxEdit->system;
    m_vfxEdit->status = "Saved " + m_vfxEdit->path.filename().string() + ".";
    InvalidateVfxThumbnailCacheUVE(m_vfxEdit->path);
    return true;
}

void EditorUVE::CloseVfxEditorUVE() {
    if (m_vfxEdit != nullptr) {
        m_vfxEdit->previewWorld.ClearAllInstancesUVE();
    }
    if (m_vfxScene != nullptr && m_documentSceneClosing) {
        m_documentSceneClosing(m_vfxScene->GetServicesUVE());
    }
    m_vfxScene.reset();
    m_vfxEdit.reset();
}

bool EditorUVE::EditVfxSystemUVE(const std::function<void(Asset::ParticleSystemAssetUVE&)>& edit) {
    if (m_vfxEdit == nullptr || !edit) {
        return false;
    }
    VfxEditSessionUVE& session = *m_vfxEdit;
    Asset::ParticleSystemAssetUVE changed = session.system;
    edit(changed);
    if (changed == session.system) {
        return false;
    }
    session.undo.push_back(session.system);
    constexpr std::size_t kMaxUndoUVE = 200U;
    if (session.undo.size() > kMaxUndoUVE) {
        session.undo.erase(session.undo.begin());
    }
    session.redo.clear();
    session.system = std::move(changed);
    if (session.selectedEmitter >= session.system.emitters.size() && !session.system.emitters.empty()) {
        session.selectedEmitter = session.system.emitters.size() - 1U;
    }
    if (session.soloEmitter.has_value() && *session.soloEmitter >= session.system.emitters.size()) {
        session.soloEmitter.reset();
    }
    static_cast<void>(CompileVfxEditorUVE());
    return true;
}

bool EditorUVE::UndoVfxEditUVE() {
    if (m_vfxEdit == nullptr || m_vfxEdit->undo.empty()) {
        return false;
    }
    VfxEditSessionUVE& session = *m_vfxEdit;
    session.redo.push_back(session.system);
    session.system = std::move(session.undo.back());
    session.undo.pop_back();
    if (session.selectedEmitter >= session.system.emitters.size() && !session.system.emitters.empty()) {
        session.selectedEmitter = session.system.emitters.size() - 1U;
    }
    static_cast<void>(CompileVfxEditorUVE());
    return true;
}

bool EditorUVE::RedoVfxEditUVE() {
    if (m_vfxEdit == nullptr || m_vfxEdit->redo.empty()) {
        return false;
    }
    VfxEditSessionUVE& session = *m_vfxEdit;
    session.undo.push_back(session.system);
    session.system = std::move(session.redo.back());
    session.redo.pop_back();
    if (session.selectedEmitter >= session.system.emitters.size() && !session.system.emitters.empty()) {
        session.selectedEmitter = session.system.emitters.size() - 1U;
    }
    static_cast<void>(CompileVfxEditorUVE());
    return true;
}

bool EditorUVE::CompileVfxEditorUVE() {
    if (m_vfxEdit == nullptr) {
        return false;
    }
    VfxEditSessionUVE& session = *m_vfxEdit;
    const std::filesystem::path projectRoot = session.previewWorld.GetProjectRootUVE();
    const bool ok = CompileSystemUVE(session.system, session.soloEmitter, projectRoot, session.compileMessages);
    if (ok) {
        session.lastGoodSystem = session.system;
        session.hasGoodSystem = true;
        session.status = "Compiled.";
        RestartVfxPreviewUVE();
    } else {
        const auto errors = std::ranges::count_if(session.compileMessages, [](const VFX::ParticleCompileMessageUVE& m) {
            return m.severity == VFX::ParticleCompileMessageUVE::Severity::Error;
        });
        session.status = std::to_string(errors) + (errors == 1 ? " compile error - preview keeps the last good version."
                                                                : " compile errors - preview keeps the last good version.");
    }
    return ok;
}

void EditorUVE::PlayVfxPreviewUVE() {
    if (m_vfxEdit == nullptr) {
        return;
    }
    VfxEditSessionUVE& session = *m_vfxEdit;
    const VFX::VfxInstanceUVE* const instance = session.previewWorld.TryGetInstanceUVE(session.previewHandle);
    const double duration = std::max(0.1, static_cast<double>(session.system.duration));
    if (!session.previewHandle.IsValidUVE() ||
        (!session.loop && session.playhead >= duration && (instance == nullptr || !instance->IsAliveUVE()))) {
        RestartVfxPreviewUVE();
    }
    session.playing = true;
}

void EditorUVE::PauseVfxPreviewUVE() noexcept {
    if (m_vfxEdit != nullptr) {
        m_vfxEdit->playing = false;
    }
}

void EditorUVE::StopVfxPreviewUVE() {
    if (m_vfxEdit == nullptr) {
        return;
    }
    m_vfxEdit->playing = false;
    RestartVfxPreviewUVE();
}

Math::Vector3UVE EditorUVE::ResolveVfxPreviewOriginUVE(const Math::AabbUVE& fixedBounds) noexcept {
    const float minY = std::isfinite(fixedBounds.min.y) ? fixedBounds.min.y : -1.0F;
    const float elevatedY = std::clamp(std::max(1.5F, -minY + 0.35F), 1.5F, 4.0F);
    return Math::Vector3UVE{0.0F, elevatedY, 0.0F};
}

void EditorUVE::RestartVfxPreviewUVE() {
    if (m_vfxEdit == nullptr) {
        return;
    }
    VfxEditSessionUVE& session = *m_vfxEdit;
    std::uint32_t seed = 1U;
    if (!session.fixedSeed) {
        seed = ++session.restartCounter + 1U;
    }
    session.previewWorld.ClearAllInstancesUVE();
    session.previewWorld.ClearCacheUVE();
    session.previewHandle = VFX::kInvalidVfxHandleUVE;
    session.playhead = 0.0;
    if (!session.hasGoodSystem) {
        return;
    }
    VFX::VfxOwnerUVE owner{};
    owner.position = ResolveVfxPreviewOriginUVE(session.lastGoodSystem.fixedBounds);
    session.previewHandle = session.previewWorld.SpawnFromAssetUVE(
        "vfx_editor_preview", BuildPreviewSystemUVE(session.lastGoodSystem, session.soloEmitter), owner, 0ULL, seed);
}

void EditorUVE::SetVfxPlayheadUVE(const double seconds) {
    if (m_vfxEdit == nullptr || !std::isfinite(seconds)) {
        return;
    }
    VfxEditSessionUVE& session = *m_vfxEdit;
    const double target = std::max(0.0, seconds);
    // A scrub is replayed from the start with the seed the preview already runs, so the same
    // time always shows the same picture.
    const bool wasFixed = session.fixedSeed;
    const std::uint32_t counter = session.restartCounter;
    session.fixedSeed = true;
    RestartVfxPreviewUVE();
    session.fixedSeed = wasFixed;
    session.restartCounter = counter;
    double time = 0.0;
    while (time + 1.0e-9 < target && session.previewHandle.IsValidUVE()) {
        const double step = std::min(static_cast<double>(kVfxStepSecondsUVE), target - time);
        session.previewWorld.TickUVE(static_cast<float>(step));
        time += step;
    }
    session.playhead = target;
    session.playing = false;
}

void EditorUVE::TickVfxEditorUVE(const double deltaSeconds) {
    if (m_vfxEdit == nullptr) {
        return;
    }
    VfxEditSessionUVE& session = *m_vfxEdit;
    if (!session.playing || !session.previewHandle.IsValidUVE()) {
        return;
    }
    const double dt = std::clamp(deltaSeconds, 0.0, 0.1) * std::clamp(session.speed, 0.01, 16.0);
    const auto begin = std::chrono::steady_clock::now();
    session.previewWorld.TickUVE(static_cast<float>(dt));
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    session.playhead += dt;

    // The measured time is split among emitters by their share of the live particles.
    const VFX::VfxInstanceUVE* const instance = session.previewWorld.TryGetInstanceUVE(session.previewHandle);
    session.lastEmitterCpuMs.assign(session.system.emitters.size(), 0.0F);
    if (instance != nullptr) {
        std::size_t total = 0U;
        for (std::size_t i = 0U; i < instance->GetEmitterCountUVE(); ++i) {
            total += instance->GetEmitterStateUVE(i).liveCount;
        }
        for (std::size_t i = 0U; i < instance->GetEmitterCountUVE() && i < session.lastEmitterCpuMs.size(); ++i) {
            const std::size_t live = instance->GetEmitterStateUVE(i).liveCount;
            session.lastEmitterCpuMs[i] =
                total > 0U ? static_cast<float>(elapsedMs * static_cast<double>(live) / static_cast<double>(total)) : 0.0F;
        }
    }

    const double duration = std::max(0.1, static_cast<double>(session.system.duration));
    if (session.playhead >= duration) {
        if (session.loop && session.system.looping) {
            session.playhead = std::fmod(session.playhead, duration);
        } else if (session.loop) {
            RestartVfxPreviewUVE();
        } else if (instance != nullptr && !instance->IsAliveUVE()) {
            session.playing = false;
        }
    }
}

std::uint64_t EditorUVE::RenderVfxPreviewUVE(const Math::Vector2UVE& size, Math::Vector2UVE& outUsed,
                                             const EditorViewportInputUVE& input) {
    outUsed = Math::Vector2UVE{0.0F, 0.0F};
    if (m_vfxEdit == nullptr || !m_viewports.vfx || !m_documentSceneFactory || size.x < 8.0F || size.y < 8.0F) {
        return 0U;
    }
    if (m_vfxScene == nullptr) {
        m_vfxScene = m_documentSceneFactory();
        if (m_vfxScene == nullptr) {
            return 0U;
        }
        PopulateVfxDarkStudioSceneUVE(m_vfxScene->GetServicesUVE());
    }
    VfxEditSessionUVE& session = *m_vfxEdit;
    Core::EngineServicesUVE& services = m_vfxScene->GetServicesUVE();
    const Math::Vector3UVE previewOrigin = ResolveVfxPreviewOriginUVE(session.system.fixedBounds);
    if (session.previewHandle.IsValidUVE()) {
        VFX::VfxOwnerUVE owner{};
        if (const VFX::VfxOwnerUVE* const existingOwner = session.previewWorld.TryGetOwnerUVE(session.previewHandle)) {
            owner = *existingOwner;
        }
        owner.position = previewOrigin;
        static_cast<void>(session.previewWorld.SetOwnerUVE(session.previewHandle, owner));
    }

    ViewportOverlayStateUVE& state = m_vfxViewportOverlayState;
    if (session.previewCameraResetPending) {
        const auto& b = session.system.fixedBounds;
        const float centerX = (b.min.x + b.max.x) * 0.5F + previewOrigin.x;
        const float centerY = (b.min.y + b.max.y) * 0.5F + previewOrigin.y;
        const float centerZ = (b.min.z + b.max.z) * 0.5F + previewOrigin.z;
        state.studioTarget = {centerX, std::max(previewOrigin.y, centerY), centerZ};
        const float radius = std::max({b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z, 1.5F});
        state.studioRadius = radius * 0.85F;
        ++state.studioFramingSerial;
        session.previewCameraResetPending = false;
    }
    state.studioView = true;
    state.entityAuthoringView = true;
    state.gameWorkspaceActive = false;
    state.hostDrawsGizmos = false;
    state.pointerOverOverlay = false;
    session.showGrid = false;
    state.gridVisible = false;
    state.bones.clear();
    state.colliderLines.clear();
    state.collisionShapesVisible = session.showBounds;
    if (session.showBounds) {
        const auto& b = session.system.fixedBounds;
        const float minX = b.min.x + previewOrigin.x;
        const float maxX = b.max.x + previewOrigin.x;
        const float minY = b.min.y + previewOrigin.y;
        const float maxY = b.max.y + previewOrigin.y;
        const float minZ = b.min.z + previewOrigin.z;
        const float maxZ = b.max.z + previewOrigin.z;
        const std::array<std::array<float, 3>, 8> c{{
            {minX, minY, minZ}, {maxX, minY, minZ}, {maxX, maxY, minZ},
            {minX, maxY, minZ}, {minX, minY, maxZ}, {maxX, minY, maxZ},
            {maxX, maxY, maxZ}, {minX, maxY, maxZ}}};
        constexpr std::array<std::array<int, 2>, 12> edges{{{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                                           {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
        for (const auto& e : edges) {
            Editor::EditorUVE::ViewportColliderLineUVE line;
            line.from = c[static_cast<std::size_t>(e[0])];
            line.to = c[static_cast<std::size_t>(e[1])];
            state.colliderLines.push_back(line);
        }
    }
    state.input = input;

    // This view's own scene draws the live preview world's particles using the live studio orbit camera.
    services.SetVfxWorldUVE(&session.previewWorld);
    const Math::Vector3UVE extractCameraEye =
        state.viewCamera.valid
            ? Math::Vector3UVE{state.viewCamera.eye[0], state.viewCamera.eye[1], state.viewCamera.eye[2]}
            : Math::Vector3UVE{0.0F, previewOrigin.y + 0.6F, 5.5F};
    const Math::Vector3UVE extractCameraForward =
        state.viewCamera.valid
            ? Math::Vector3UVE{state.viewCamera.forward[0], state.viewCamera.forward[1], state.viewCamera.forward[2]}
            : Math::Vector3UVE{0.0F, -0.1F, -1.0F};
    Render::ParticleRenderBridgeUVE::ExtractVfxUVE(session.previewWorld, session.vfxDrawList,
                                                   extractCameraEye,
                                                   extractCameraForward);
    session.lastInstancesDrawn = session.vfxDrawList.stats.instancesExtracted;
    Render::IRenderer3DUVE& renderer = services.GetRenderer3DUVE();
    renderer.SetVfxDrawListUVE(&session.vfxDrawList);

    Core::EngineServicesUVE* const previous = m_services;
    m_services = &services;
    std::uint64_t texture = 0U;
    try {
        texture = m_viewports.vfx(size, outUsed, state);
    } catch (...) {
        m_services = previous;
        renderer.SetVfxDrawListUVE(nullptr);
        throw;
    }
    m_services = previous;
    renderer.SetVfxDrawListUVE(nullptr);
    return texture;
}

std::filesystem::path EditorUVE::CreateParticleSystemAssetUVE(const std::filesystem::path& directory,
                                                              const std::string& name,
                                                              const VFX::ParticleTemplateKindUVE kind) {
    std::string stem = name.empty() ? std::string("VFX_NewEffect") : name;
    std::filesystem::path target = directory;
    if (target.empty()) {
        target = m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
    }
    std::error_code error;
    std::filesystem::create_directories(target, error);
    std::filesystem::path path = target / (stem + ".uvfx");
    for (int n = 1; std::filesystem::exists(path, error) && n < 10000; ++n) {
        path = target / (stem + "_" + std::to_string(n) + ".uvfx");
    }
    if (!Asset::SaveParticleSystemAssetUVE(VFX::CreateParticleTemplateUVE(kind), path)) {
        return {};
    }
    return path;
}

std::uintptr_t EditorUVE::GetVfxThumbnailUVE(const std::filesystem::path& relativePath) {
    const std::string cacheKey = relativePath.generic_string();
    if (const auto it = m_vfxThumbnailCache.find(cacheKey); it != m_vfxThumbnailCache.end()) {
        return it->second;
    }
    std::uintptr_t texture = 0U;
    const std::filesystem::path absolute =
        m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot / relativePath;
    Asset::ParticleSystemAssetUVE system;
    if (LowerExtensionUVE(relativePath) == ".uvfx" && Asset::LoadParticleSystemAssetUVE(absolute, system)) {
        VFX::VfxWorldUVE world;
        const VFX::VfxHandleUVE handle = world.SpawnFromAssetUVE("vfx_thumbnail", system, {}, 0ULL, 1U);
        const double target = std::max(0.1, static_cast<double>(system.duration)) * 0.6;
        for (double t = 0.0; t + 1.0e-9 < target && handle.IsValidUVE(); t += kVfxStepSecondsUVE) {
            world.TickUVE(kVfxStepSecondsUVE);
        }
        constexpr int size = kVfxThumbnailSizeUVE;
        std::vector<float> accum(static_cast<std::size_t>(size) * size * 3U, 0.0F);
        if (const VFX::VfxInstanceUVE* const instance = world.TryGetInstanceUVE(handle)) {
            const auto& b = system.fixedBounds;
            const float spanX = std::max(1.0e-3F, b.max.x - b.min.x);
            const float spanY = std::max(1.0e-3F, b.max.y - b.min.y);
            const float scale = static_cast<float>(size) / std::max(spanX, spanY);
            const float cx = (b.min.x + b.max.x) * 0.5F;
            const float cy = (b.min.y + b.max.y) * 0.5F;
            for (std::size_t e = 0U; e < instance->GetEmitterCountUVE(); ++e) {
                const VFX::VfxEmitterStateUVE& state = instance->GetEmitterStateUVE(e);
                for (std::size_t p = 0U; p < state.liveCount && p < state.posX.size() && p < state.posY.size(); ++p) {
                    const float r = p < state.colR.size() ? state.colR[p] : 1.0F;
                    const float g = p < state.colG.size() ? state.colG[p] : 1.0F;
                    const float bl = p < state.colB.size() ? state.colB[p] : 1.0F;
                    const float a = p < state.colA.size() ? state.colA[p] : 1.0F;
                    const float worldSize = p < state.sizeX.size() ? state.sizeX[p] : 0.1F;
                    const float radius = std::clamp(worldSize * scale * 0.5F, 1.0F, 12.0F);
                    const float px = (state.posX[p] - cx) * scale + static_cast<float>(size) * 0.5F;
                    const float py = static_cast<float>(size) * 0.5F - (state.posY[p] - cy) * scale;
                    const int x0 = std::max(0, static_cast<int>(std::floor(px - radius)));
                    const int x1 = std::min(size - 1, static_cast<int>(std::ceil(px + radius)));
                    const int y0 = std::max(0, static_cast<int>(std::floor(py - radius)));
                    const int y1 = std::min(size - 1, static_cast<int>(std::ceil(py + radius)));
                    for (int y = y0; y <= y1; ++y) {
                        for (int x = x0; x <= x1; ++x) {
                            const float dx = (static_cast<float>(x) + 0.5F) - px;
                            const float dy = (static_cast<float>(y) + 0.5F) - py;
                            const float falloff = 1.0F - std::sqrt(dx * dx + dy * dy) / radius;
                            if (falloff <= 0.0F) {
                                continue;
                            }
                            const float w = falloff * std::clamp(a, 0.0F, 1.0F);
                            float* const out = &accum[(static_cast<std::size_t>(y) * size + static_cast<std::size_t>(x)) * 3U];
                            out[0] += r * w;
                            out[1] += g * w;
                            out[2] += bl * w;
                        }
                    }
                }
            }
        }
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(size) * size * 4U);
        constexpr std::array<float, 3> background{0.12F, 0.12F, 0.14F};
        for (std::size_t i = 0U; i < static_cast<std::size_t>(size) * size; ++i) {
            for (std::size_t c = 0U; c < 3U; ++c) {
                const float v = std::clamp(background[c] + accum[i * 3U + c], 0.0F, 1.0F);
                pixels[i * 4U + c] = static_cast<std::uint8_t>(std::lround(v * 255.0F));
            }
            pixels[i * 4U + 3U] = 255U;
        }
        texture = EditorUiAssetsUVE::UploadDynamicTextureUVE(pixels.data(), size, size);
    }
    m_vfxThumbnailCache.emplace(cacheKey, texture);
    return texture;
}

void EditorUVE::ClearVfxThumbnailCacheUVE() noexcept {
    for (auto& [path, texture] : m_vfxThumbnailCache) {
        EditorUiAssetsUVE::DeleteDynamicTextureUVE(texture);
    }
    m_vfxThumbnailCache.clear();
}

void EditorUVE::InvalidateVfxThumbnailCacheUVE(const std::filesystem::path& absolutePath) {
    const std::filesystem::path contentRoot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(absolutePath, contentRoot, error);
    const std::string key = error ? absolutePath.generic_string() : relative.generic_string();
    if (const auto it = m_vfxThumbnailCache.find(key); it != m_vfxThumbnailCache.end()) {
        EditorUiAssetsUVE::DeleteDynamicTextureUVE(it->second);
        m_vfxThumbnailCache.erase(it);
    }
}

} // namespace UVE::Editor
