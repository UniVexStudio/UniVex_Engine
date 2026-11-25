// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor.h"

#include "collider_component.h"
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

constexpr float kVfxStepSeconds = 1.0F / 30.0F;
constexpr int kVfxThumbnailSize = 128;

[[nodiscard]] std::vector<VFX::VfxColliderVolume> CollectSceneVfxColliders(
    Scene::IEntityManager& em) {
    std::vector<VFX::VfxColliderVolume> colliders;
    for (auto entry : em.View<Scene::ColliderComponent>()) {
        const Scene::ColliderComponent& col = entry.Get<Scene::ColliderComponent>();
        if (col.disabled) {
            continue;
        }
        Math::Vector3 pos{};
        Math::Quaternion rot{};
        Math::Vector3 scale{1.0F, 1.0F, 1.0F};
        if (em.HasComponent<Scene::TransformComponent>(entry.entity)) {
            const Scene::TransformComponent& tf = em.GetComponent<Scene::TransformComponent>(entry.entity);
            pos = tf.localPosition;
            rot = tf.localRotation;
            scale = tf.localScale;
        }
        const Math::Vector3 absScale{
            std::max(Scene::kMinimumColliderWorldScale, std::abs(scale.x)),
            std::max(Scene::kMinimumColliderWorldScale, std::abs(scale.y)),
            std::max(Scene::kMinimumColliderWorldScale, std::abs(scale.z)),
        };

        VFX::VfxColliderVolume volume{};
        volume.center = pos;
        volume.rotation = rot;
        if (col.shapeType == Scene::ColliderShapeType::Sphere) {
            volume.kind = VFX::VfxColliderShapeKind::Sphere;
            volume.radius = col.radius * std::max({absScale.x, absScale.y, absScale.z});
        } else if (col.shapeType == Scene::ColliderShapeType::Capsule ||
                   col.shapeType == Scene::ColliderShapeType::Cylinder) {
            volume.kind = VFX::VfxColliderShapeKind::Capsule;
            volume.radius = col.radius * std::max(absScale.x, absScale.z);
            volume.height = col.height * absScale.y;
        } else {
            volume.kind = VFX::VfxColliderShapeKind::Box;
            volume.halfExtents = Math::Vector3{
                col.halfExtents.x * absScale.x,
                col.halfExtents.y * absScale.y,
                col.halfExtents.z * absScale.z,
            };
        }
        colliders.push_back(volume);
    }
    return colliders;
}

void PopulateVfxDarkStudioScene(Core::EngineServices& services) {
    Scene::IEntityManager& em = services.GetEntityManager();
    Scene::ISceneGraph& sg = services.GetSceneGraph();

    const Scene::Entity envEntity = em.CreateEntity();
    sg.AttachTransform(em, envEntity, Scene::TransformComponent{});
    Scene::WorldEnvironment3DComponent env{};
    env.skyColor = Math::Vector3{0.022F, 0.025F, 0.032F};
    env.horizonColor = Math::Vector3{0.036F, 0.040F, 0.050F};
    env.groundColor = Math::Vector3{0.016F, 0.018F, 0.022F};
    env.ambientColor = Math::Vector3{0.085F, 0.092F, 0.112F};
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
    em.AddComponent<Scene::WorldEnvironment3DComponent>(envEntity, env);

    // Real studio floor Collider3D (top surface flush at y = 0.0) so falling particles
    // bounce off the studio floor through the real-time Collider3D collision pipeline.
    const Scene::Entity floorColliderEntity = em.CreateEntity();
    Scene::TransformComponent floorTf{};
    floorTf.localPosition = Math::Vector3{0.0F, -0.5F, 0.0F};
    sg.AttachTransform(em, floorColliderEntity, floorTf);
    Scene::ColliderComponent floorCol{};
    floorCol.shapeType = Scene::ColliderShapeType::Box;
    floorCol.halfExtents = Math::Vector3{50.0F, 0.5F, 50.0F};
    floorCol.friction = 0.25F;
    floorCol.restitution = 0.55F;
    em.AddComponent<Scene::ColliderComponent>(floorColliderEntity, floorCol);

    const auto addStudioPointLight = [&](const Math::Vector3& position,
                                         const Math::Color& color,
                                         const float intensity,
                                         const float range) {
        const Scene::Entity lightEntity = em.CreateEntity();
        Scene::TransformComponent transform{};
        transform.localPosition = position;
        sg.AttachTransform(em, lightEntity, transform);
        Scene::LightComponent light{};
        light.type = Scene::LightType::Point;
        light.color = color;
        light.intensity = intensity;
        light.range = range;
        em.AddComponent<Scene::LightComponent>(lightEntity, light);
    };
    // 3-point studio light rig (Warm Key, Cool Rim, Soft Fill) around the studio stage.
    addStudioPointLight(Math::Vector3{3.6F, 5.4F, 3.8F}, Math::Color{1.00F, 0.96F, 0.90F}, 2.25F, 22.0F);
    addStudioPointLight(Math::Vector3{-4.0F, 3.9F, -3.6F}, Math::Color{0.58F, 0.72F, 1.00F}, 1.45F, 20.0F);
    addStudioPointLight(Math::Vector3{-3.0F, 2.7F, 4.2F}, Math::Color{0.78F, 0.84F, 0.96F}, 0.95F, 18.0F);
    sg.Update(em);
    services.GetRenderer3D().SetSceneClearColor({0.028F, 0.031F, 0.038F, 1.0F});
}

[[nodiscard]] bool HasError(const std::vector<VFX::ParticleCompileMessage>& messages) noexcept {
    return std::ranges::any_of(messages, [](const VFX::ParticleCompileMessage& m) {
        return m.severity == VFX::ParticleCompileMessage::Severity::Error;
    });
}

/// The system the preview actually plays: the last good one, with every emitter but the solo one off.
[[nodiscard]] Asset::ParticleSystemAsset BuildPreviewSystem(const Asset::ParticleSystemAsset& source,
                                                                  const std::optional<std::size_t>& solo) {
    Asset::ParticleSystemAsset system = source;
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
[[nodiscard]] bool CompileSystem(const Asset::ParticleSystemAsset& system,
                                    const std::optional<std::size_t>& solo,
                                    const std::filesystem::path& projectRoot,
                                    std::vector<VFX::ParticleCompileMessage>& outMessages) {
    outMessages.clear();
    VFX::VfxCompileContext context;
    context.projectRoot = projectRoot;
    if (system.emitters.empty()) {
        outMessages.push_back({VFX::ParticleCompileMessage::Severity::Warning, 0U,
                               "The system has no emitters."});
    }
    for (std::size_t i = 0U; i < system.emitters.size(); ++i) {
        if (!system.emitters[i].enabled || (solo.has_value() && *solo != i)) {
            continue;
        }
        VFX::ParticleProgram program;
        std::vector<VFX::ParticleCompileMessage> messages;
        const bool ok = VFX::CompileParticleEmitter(system, i, context, program, messages);
        if (!ok && !HasError(messages)) {
            messages.push_back({VFX::ParticleCompileMessage::Severity::Error, 0U,
                                "Emitter '" + system.emitters[i].name + "' did not compile."});
        }
        for (VFX::ParticleCompileMessage& m : messages) {
            m.emitterIndex = static_cast<std::uint32_t>(i);
        }
        outMessages.insert(outMessages.end(), messages.begin(), messages.end());
    }
    return !HasError(outMessages);
}

[[nodiscard]] std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().generic_string();
    std::ranges::transform(extension, extension.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

} // namespace

bool Editor::IsVfxEditorDirty() const noexcept {
    return m_vfxEdit != nullptr && !(m_vfxEdit->system == m_vfxEdit->savedSystem);
}

bool Editor::OpenVfxEditor(const std::filesystem::path& absolutePath) {
    if (m_vfxEdit != nullptr && m_vfxEdit->path == absolutePath) {
        m_vfxEdit->focusPending = true;
        return true;
    }
    Asset::ParticleSystemAsset system;
    if (!Asset::LoadParticleSystemAsset(absolutePath, system)) {
        m_contentStatusMessage = "Could not open " + absolutePath.filename().string() + " as a particle system.";
        return false;
    }
    auto session = std::make_unique<VfxEditSession>();
    session->path = absolutePath;
    session->system = system;
    session->savedSystem = system;
    session->loop = system.looping;
    m_vfxEdit = std::move(session);
    m_vfxEdit->previewWorld.SetProjectRoot(m_services->GetProjectFileIndex().GetSnapshot().contentRoot.parent_path());
    static_cast<void>(CompileVfxEditor());
    m_contentStatusMessage = "Opened " + absolutePath.filename().string() + " in the Particle System Editor.";
    return true;
}

bool Editor::SaveVfxEditor() {
    if (m_vfxEdit == nullptr) {
        return false;
    }
    if (!Asset::IsParticleSystemAssetValid(m_vfxEdit->system)) {
        m_vfxEdit->status = "Not saved: the particle system is not valid.";
        return false;
    }
    if (!Asset::SaveParticleSystemAsset(m_vfxEdit->system, m_vfxEdit->path)) {
        m_vfxEdit->status = "Could not write " + m_vfxEdit->path.filename().string() + ".";
        return false;
    }
    m_vfxEdit->savedSystem = m_vfxEdit->system;
    m_vfxEdit->status = "Saved " + m_vfxEdit->path.filename().string() + ".";
    InvalidateVfxThumbnailCache(m_vfxEdit->path);
    return true;
}

void Editor::CloseVfxEditor() {
    if (m_vfxEdit != nullptr) {
        m_vfxEdit->previewWorld.ClearAllInstances();
    }
    if (m_vfxScene != nullptr && m_documentSceneClosing) {
        m_documentSceneClosing(m_vfxScene->GetServices());
    }
    m_vfxScene.reset();
    m_vfxEdit.reset();
}

bool Editor::EditVfxSystem(const std::function<void(Asset::ParticleSystemAsset&)>& edit) {
    if (m_vfxEdit == nullptr || !edit) {
        return false;
    }
    VfxEditSession& session = *m_vfxEdit;
    Asset::ParticleSystemAsset changed = session.system;
    edit(changed);
    if (changed == session.system) {
        return false;
    }
    session.undo.push_back(session.system);
    constexpr std::size_t kMaxUndo = 200U;
    if (session.undo.size() > kMaxUndo) {
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
    static_cast<void>(CompileVfxEditor());
    return true;
}

bool Editor::UndoVfxEdit() {
    if (m_vfxEdit == nullptr || m_vfxEdit->undo.empty()) {
        return false;
    }
    VfxEditSession& session = *m_vfxEdit;
    session.redo.push_back(session.system);
    session.system = std::move(session.undo.back());
    session.undo.pop_back();
    if (session.selectedEmitter >= session.system.emitters.size() && !session.system.emitters.empty()) {
        session.selectedEmitter = session.system.emitters.size() - 1U;
    }
    static_cast<void>(CompileVfxEditor());
    return true;
}

bool Editor::RedoVfxEdit() {
    if (m_vfxEdit == nullptr || m_vfxEdit->redo.empty()) {
        return false;
    }
    VfxEditSession& session = *m_vfxEdit;
    session.undo.push_back(session.system);
    session.system = std::move(session.redo.back());
    session.redo.pop_back();
    if (session.selectedEmitter >= session.system.emitters.size() && !session.system.emitters.empty()) {
        session.selectedEmitter = session.system.emitters.size() - 1U;
    }
    static_cast<void>(CompileVfxEditor());
    return true;
}

bool Editor::CompileVfxEditor() {
    if (m_vfxEdit == nullptr) {
        return false;
    }
    VfxEditSession& session = *m_vfxEdit;
    const std::filesystem::path projectRoot = session.previewWorld.GetProjectRoot();
    const bool ok = CompileSystem(session.system, session.soloEmitter, projectRoot, session.compileMessages);
    if (ok) {
        session.lastGoodSystem = session.system;
        session.hasGoodSystem = true;
        session.status = "Compiled.";
        RestartVfxPreview();
    } else {
        const auto errors = std::ranges::count_if(session.compileMessages, [](const VFX::ParticleCompileMessage& m) {
            return m.severity == VFX::ParticleCompileMessage::Severity::Error;
        });
        session.status = std::to_string(errors) + (errors == 1 ? " compile error - preview keeps the last good version."
                                                                : " compile errors - preview keeps the last good version.");
    }
    return ok;
}

void Editor::PlayVfxPreview() {
    if (m_vfxEdit == nullptr) {
        return;
    }
    VfxEditSession& session = *m_vfxEdit;
    const VFX::VfxInstance* const instance = session.previewWorld.TryGetInstance(session.previewHandle);
    const double duration = std::max(0.1, static_cast<double>(session.system.duration));
    if (!session.previewHandle.IsValid() ||
        (!session.loop && session.playhead >= duration && (instance == nullptr || !instance->IsAlive()))) {
        RestartVfxPreview();
    }
    session.playing = true;
}

void Editor::PauseVfxPreview() noexcept {
    if (m_vfxEdit != nullptr) {
        m_vfxEdit->playing = false;
    }
}

void Editor::StopVfxPreview() {
    if (m_vfxEdit == nullptr) {
        return;
    }
    m_vfxEdit->playing = false;
    RestartVfxPreview();
}

Math::Vector3 Editor::ResolveVfxPreviewOrigin(const Math::Aabb& /*fixedBounds*/) noexcept {
    // Spawn flush on the studio floor (y = 0) so effects originate directly on the floor plane
    // and falling particles bounce off the real studio floor Collider3D.
    return Math::Vector3{0.0F, 0.0F, 0.0F};
}

void Editor::RestartVfxPreview() {
    if (m_vfxEdit == nullptr) {
        return;
    }
    VfxEditSession& session = *m_vfxEdit;
    std::uint32_t seed = 1U;
    if (!session.fixedSeed) {
        seed = ++session.restartCounter + 1U;
    }
    session.previewWorld.ClearAllInstances();
    session.previewWorld.ClearCache();
    session.previewHandle = VFX::kInvalidVfxHandle;
    session.playhead = 0.0;
    if (!session.hasGoodSystem) {
        return;
    }
    VFX::VfxOwner owner{};
    owner.position = ResolveVfxPreviewOrigin(session.lastGoodSystem.fixedBounds);
    session.previewHandle = session.previewWorld.SpawnFromAsset(
        "vfx_editor_preview", BuildPreviewSystem(session.lastGoodSystem, session.soloEmitter), owner, 0ULL, seed);
}

void Editor::SetVfxPlayhead(const double seconds) {
    if (m_vfxEdit == nullptr || !std::isfinite(seconds)) {
        return;
    }
    VfxEditSession& session = *m_vfxEdit;
    const double target = std::max(0.0, seconds);
    // A scrub is replayed from the start with the seed the preview already runs, so the same
    // time always shows the same picture.
    const bool wasFixed = session.fixedSeed;
    const std::uint32_t counter = session.restartCounter;
    session.fixedSeed = true;
    RestartVfxPreview();
    session.fixedSeed = wasFixed;
    session.restartCounter = counter;
    if (m_vfxScene != nullptr) {
        session.previewWorld.SetSceneColliders(
            CollectSceneVfxColliders(m_vfxScene->GetServices().GetEntityManager()));
    }
    double time = 0.0;
    while (time + 1.0e-9 < target && session.previewHandle.IsValid()) {
        const double step = std::min(static_cast<double>(kVfxStepSeconds), target - time);
        session.previewWorld.Tick(static_cast<float>(step));
        time += step;
    }
    session.playhead = target;
    session.playing = false;
}

void Editor::TickVfxEditor(const double deltaSeconds) {
    if (m_vfxEdit == nullptr) {
        return;
    }
    VfxEditSession& session = *m_vfxEdit;
    if (!session.playing || !session.previewHandle.IsValid()) {
        return;
    }
    if (m_vfxScene != nullptr) {
        session.previewWorld.SetSceneColliders(
            CollectSceneVfxColliders(m_vfxScene->GetServices().GetEntityManager()));
    }
    const double dt = std::clamp(deltaSeconds, 0.0, 0.1) * std::clamp(session.speed, 0.01, 16.0);
    const auto begin = std::chrono::steady_clock::now();
    session.previewWorld.Tick(static_cast<float>(dt));
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    session.playhead += dt;

    // The measured time is split among emitters by their share of the live particles.
    const VFX::VfxInstance* const instance = session.previewWorld.TryGetInstance(session.previewHandle);
    session.lastEmitterCpuMs.assign(session.system.emitters.size(), 0.0F);
    if (instance != nullptr) {
        std::size_t total = 0U;
        for (std::size_t i = 0U; i < instance->GetEmitterCount(); ++i) {
            total += instance->GetEmitterState(i).liveCount;
        }
        for (std::size_t i = 0U; i < instance->GetEmitterCount() && i < session.lastEmitterCpuMs.size(); ++i) {
            const std::size_t live = instance->GetEmitterState(i).liveCount;
            session.lastEmitterCpuMs[i] =
                total > 0U ? static_cast<float>(elapsedMs * static_cast<double>(live) / static_cast<double>(total)) : 0.0F;
        }
    }

    const double duration = std::max(0.1, static_cast<double>(session.system.duration));
    if (session.playhead >= duration) {
        if (session.loop && session.system.looping) {
            session.playhead = std::fmod(session.playhead, duration);
        } else if (session.loop) {
            RestartVfxPreview();
        } else if (instance != nullptr && !instance->IsAlive()) {
            session.playing = false;
        }
    }
}

std::uint64_t Editor::RenderVfxPreview(const Math::Vector2& size, Math::Vector2& outUsed,
                                             const EditorViewportInput& input) {
    outUsed = Math::Vector2{0.0F, 0.0F};
    if (m_vfxEdit == nullptr || !m_viewports.vfx || !m_documentSceneFactory || size.x < 8.0F || size.y < 8.0F) {
        return 0U;
    }
    if (m_vfxScene == nullptr) {
        m_vfxScene = m_documentSceneFactory();
        if (m_vfxScene == nullptr) {
            return 0U;
        }
        PopulateVfxDarkStudioScene(m_vfxScene->GetServices());
    }
    VfxEditSession& session = *m_vfxEdit;
    Core::EngineServices& services = m_vfxScene->GetServices();
    const Math::Vector3 previewOrigin = ResolveVfxPreviewOrigin(session.system.fixedBounds);
    if (session.previewHandle.IsValid()) {
        VFX::VfxOwner owner{};
        if (const VFX::VfxOwner* const existingOwner = session.previewWorld.TryGetOwner(session.previewHandle)) {
            owner = *existingOwner;
        }
        owner.position = previewOrigin;
        static_cast<void>(session.previewWorld.SetOwner(session.previewHandle, owner));
    }

    ViewportOverlayState& state = m_vfxViewportOverlayState;
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
            Editor::Editor::ViewportColliderLine line;
            line.from = c[static_cast<std::size_t>(e[0])];
            line.to = c[static_cast<std::size_t>(e[1])];
            state.colliderLines.push_back(line);
        }
    }
    state.input = input;

    // This view's own scene draws the live preview world's particles using the live studio orbit camera.
    services.SetVfxWorld(&session.previewWorld);
    const Math::Vector3 extractCameraEye =
        state.viewCamera.valid
            ? Math::Vector3{state.viewCamera.eye[0], state.viewCamera.eye[1], state.viewCamera.eye[2]}
            : Math::Vector3{0.0F, previewOrigin.y + 0.6F, 5.5F};
    const Math::Vector3 extractCameraForward =
        state.viewCamera.valid
            ? Math::Vector3{state.viewCamera.forward[0], state.viewCamera.forward[1], state.viewCamera.forward[2]}
            : Math::Vector3{0.0F, -0.1F, -1.0F};
    Render::ParticleRenderBridge::ExtractVfx(session.previewWorld, session.vfxDrawList,
                                                   extractCameraEye,
                                                   extractCameraForward);
    session.lastInstancesDrawn = session.vfxDrawList.stats.instancesExtracted;
    Render::IRenderer3D& renderer = services.GetRenderer3D();
    renderer.SetVfxDrawList(&session.vfxDrawList);

    Core::EngineServices* const previous = m_services;
    m_services = &services;
    std::uint64_t texture = 0U;
    try {
        texture = m_viewports.vfx(size, outUsed, state);
    } catch (...) {
        m_services = previous;
        renderer.SetVfxDrawList(nullptr);
        throw;
    }
    m_services = previous;
    renderer.SetVfxDrawList(nullptr);
    return texture;
}

std::filesystem::path Editor::CreateParticleSystemAsset(const std::filesystem::path& directory,
                                                              const std::string& name,
                                                              const VFX::ParticleTemplateKind kind) {
    std::string stem = name.empty() ? std::string("VFX_NewEffect") : name;
    std::filesystem::path target = directory;
    if (target.empty()) {
        target = m_services->GetProjectFileIndex().GetSnapshot().contentRoot;
    }
    std::error_code error;
    std::filesystem::create_directories(target, error);
    std::filesystem::path path = target / (stem + ".uvfx");
    for (int n = 1; std::filesystem::exists(path, error) && n < 10000; ++n) {
        path = target / (stem + "_" + std::to_string(n) + ".uvfx");
    }
    if (!Asset::SaveParticleSystemAsset(VFX::CreateParticleTemplate(kind), path)) {
        return {};
    }
    return path;
}

std::uintptr_t Editor::GetVfxThumbnail(const std::filesystem::path& relativePath) {
    const std::string cacheKey = relativePath.generic_string();
    if (const auto it = m_vfxThumbnailCache.find(cacheKey); it != m_vfxThumbnailCache.end()) {
        return it->second;
    }
    std::uintptr_t texture = 0U;
    const std::filesystem::path absolute =
        m_services->GetProjectFileIndex().GetSnapshot().contentRoot / relativePath;
    Asset::ParticleSystemAsset system;
    if (LowerExtension(relativePath) == ".uvfx" && Asset::LoadParticleSystemAsset(absolute, system)) {
        VFX::VfxWorld world;
        const VFX::VfxHandle handle = world.SpawnFromAsset("vfx_thumbnail", system, {}, 0ULL, 1U);
        const double target = std::max(0.1, static_cast<double>(system.duration)) * 0.6;
        for (double t = 0.0; t + 1.0e-9 < target && handle.IsValid(); t += kVfxStepSeconds) {
            world.Tick(kVfxStepSeconds);
        }
        constexpr int size = kVfxThumbnailSize;
        std::vector<float> accum(static_cast<std::size_t>(size) * size * 3U, 0.0F);
        if (const VFX::VfxInstance* const instance = world.TryGetInstance(handle)) {
            const auto& b = system.fixedBounds;
            const float spanX = std::max(1.0e-3F, b.max.x - b.min.x);
            const float spanY = std::max(1.0e-3F, b.max.y - b.min.y);
            const float scale = static_cast<float>(size) / std::max(spanX, spanY);
            const float cx = (b.min.x + b.max.x) * 0.5F;
            const float cy = (b.min.y + b.max.y) * 0.5F;
            for (std::size_t e = 0U; e < instance->GetEmitterCount(); ++e) {
                const VFX::VfxEmitterState& state = instance->GetEmitterState(e);
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
        texture = EditorUiAssets::UploadDynamicTexture(pixels.data(), size, size);
    }
    m_vfxThumbnailCache.emplace(cacheKey, texture);
    return texture;
}

void Editor::ClearVfxThumbnailCache() noexcept {
    for (auto& [path, texture] : m_vfxThumbnailCache) {
        EditorUiAssets::DeleteDynamicTexture(texture);
    }
    m_vfxThumbnailCache.clear();
}

void Editor::InvalidateVfxThumbnailCache(const std::filesystem::path& absolutePath) {
    const std::filesystem::path contentRoot = m_services->GetProjectFileIndex().GetSnapshot().contentRoot;
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(absolutePath, contentRoot, error);
    const std::string key = error ? absolutePath.generic_string() : relative.generic_string();
    if (const auto it = m_vfxThumbnailCache.find(key); it != m_vfxThumbnailCache.end()) {
        EditorUiAssets::DeleteDynamicTexture(it->second);
        m_vfxThumbnailCache.erase(it);
    }
}

} // namespace UVE::Editor
