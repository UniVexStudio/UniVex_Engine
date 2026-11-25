// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "engine_services.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

#include <gtest/gtest.h>

#include "i_asset_bundle.h"
#include "i_asset_database.h"
#include "i_asset_importer.h"
#include "i_asset_import_queue.h"
#include "i_asset_manager.h"
#include "i_derived_artifact_cache.h"
#include "i_file_system.h"
#include "i_hot_reload.h"
#include "i_project_file_index.h"
#include "i_audio_device.h"
#include "i_audio_source_system.h"
#include "i_audio_system.h"
#include "i_command_line.h"
#include "i_config_manager.h"
#include "i_logger.h"
#include "i_event_system.h"
#include "gamepad_input_system.h"
#include "i_input_system.h"
#include "mobile_gesture_system.h"
#include "mobile_input_system.h"
#include "frustum.h"
#include "matrix4x4.h"
#include "i_memory_manager.h"
#include "i_collision_system.h"
#include "i_physics_system.h"
#include "physics_constraint_system.h"
#include "physics_query_system.h"
#include "i_raycast_system.h"
#include "i_camera_system.h"
#include "i_compute_system.h"
#include "i_light_system.h"
#include "i_mesh_renderer.h"
#include "i_render_device.h"
#include "i_render_system.h"
#include "i_renderer_3d.h"
#include "i_shader_manager.h"
#include "i_checkpoint_manager.h"
#include "i_save_game_system.h"
#include "i_entity_manager.h"
#include "i_particle_runtime.h"
#include "i_prefab_system.h"
#include "i_scene_graph.h"
#include "i_scene_serializer.h"
#include "i_thread_pool.h"
#include "i_timer.h"
#include "i_window_manager.h"

// These hand-written fakes exist to prove that EngineServices (and, by
// extension, anything that consumes ILogger/ITimer/IEventSystem/
// IMemoryManager/IThreadPool/ICommandLine/IConfigManager/
// IEntityManager/ISceneGraph/IAssetDatabase/ISceneSerializer/
// IPrefabSystem/IHotReload/IAssetManager/IAssetImporter/
// IAssetBundle/IFileSystem/IRenderDevice/Shader::IShaderManager/IRenderSystem/
// ICameraSystem/IMeshRenderer) works against ANY conforming
// implementation, independent of the concrete Logger/Timer/
// EventSystem/MemoryManager/ThreadPool/CommandLine/
// ConfigManager/EntityManager/SceneGraph/AssetDatabase/
// SceneSerializer/PrefabSystem/HotReload/AssetManager/
// AssetImporter/AssetBundle/FileSystem/NullRenderDevice/
// RenderSystem/CameraSystem/MeshRenderer classes used by
// EngineCore — this is the whole point of introducing the interfaces.

namespace UVE::Core::Tests {
namespace {

class FakeLogger final : public Debug::ILogger {
public:
    void Init(Debug::LogLevel) override {}
    void Shutdown() override {}
    void AddSink(std::unique_ptr<Debug::ILogSink>) override {}
    void SetMinLevel(Debug::LogLevel level) override { minLevel = level; }
    [[nodiscard]] Debug::LogLevel GetMinLevel() const override { return minLevel; }
    void LogFormatted(Debug::LogLevel, std::string_view, const char*, int, std::string) override {}

    Debug::LogLevel minLevel = Debug::LogLevel::Trace;
};

class FakeTimer final : public Utilities::ITimer {
public:
    void Tick() override { ++tickCount; }
    [[nodiscard]] double GetDeltaTime() const override { return 0.0; }
    [[nodiscard]] float GetDeltaTimeFloat() const override { return 0.0F; }
    [[nodiscard]] double GetTotalTime() const override { return 0.0; }
    void Reset() override {}
    void SetMaxDeltaTime(double) override {}
    void SetFixedTimestep(double) override {}
    void SetMaxStepsPerTick(int) override {}
    Utilities::FixedStepResult AdvanceFixedStep() override { return {}; }
    void DiscardFixedStepAccumulator() noexcept override { ++discardCount; }

    int tickCount = 0;
    int discardCount = 0;
};

class FakeEventSystem final : public Events::IEventSystem {
public:
    void Unsubscribe(const Events::EventSubscription&) override {}
    void DispatchQueued() override { ++dispatchCount; }
    void Clear() override {}

    int dispatchCount = 0;

protected:
    std::uint64_t SubscribeErased(std::type_index, std::function<void(const void*)>) override { return 1; }
    void PublishErased(std::type_index, const void*) override {}
    void QueueErased(std::type_index, Events::EventPriority, std::function<void()>) override {}
};

class FakeAllocator final : public Memory::IAllocator {
public:
    [[nodiscard]] void* Allocate(std::size_t, std::size_t, const char*, int) override {
        return nullptr;
    }
    void Deallocate(void*) override {}
    [[nodiscard]] std::size_t GetAllocatedBytes() const override { return 0; }
};

class FakeMemoryManager final : public Memory::IMemoryManager {
public:
    void RecordAllocation(void*, std::size_t, std::size_t, std::string_view, const char*,
                              int) override {
        ++recordAllocationCount;
    }
    void RecordDeallocation(void*) override { ++recordDeallocationCount; }
    [[nodiscard]] Memory::IAllocator& GetDefaultAllocator() override { return allocator; }
    [[nodiscard]] std::size_t GetActiveAllocationCount() const override { return 0; }
    [[nodiscard]] std::size_t GetActiveBytes() const override { return 0; }
    [[nodiscard]] std::size_t GetPeakBytes() const override { return 0; }
    [[nodiscard]] std::uint64_t GetTotalAllocationsEver() const override { return 0; }
    [[nodiscard]] bool HasLeaks() const override { return false; }
    [[nodiscard]] std::vector<Memory::AllocationRecord> GetLeakedAllocations() const override {
        return {};
    }
    void LogLeakReport() override { ++logLeakReportCallCount; }

    FakeAllocator allocator;
    int recordAllocationCount = 0;
    int recordDeallocationCount = 0;
    int logLeakReportCallCount = 0;
};

class FakeThreadPool final : public Threading::IThreadPool {
public:
    void Submit(Threading::Job job) override {
        ++submitCount;
        job();
    }
    void Submit(Threading::Job job, Threading::JobCounter& counter) override {
        ++submitCount;
        counter.Increment();
        job();
        counter.DecrementAndNotify();
    }
    [[nodiscard]] std::size_t GetWorkerCount() const noexcept override { return 1; }
    [[nodiscard]] std::size_t GetPendingJobCount() const noexcept override { return 0; }
    [[nodiscard]] std::size_t GetActiveWorkerCount() const noexcept override { return 0; }
    [[nodiscard]] std::uint64_t GetStolenJobCount() const noexcept override { return 0; }
    [[nodiscard]] bool IsWorkerThread() const noexcept override { return false; }

    int submitCount = 0;
};

class FakeCommandLine final : public CommandLine::ICommandLine {
public:
    [[nodiscard]] bool HasFlag(std::string_view name) const noexcept override {
        ++hasFlagCallCount;
        return name == "server";
    }
    [[nodiscard]] std::string GetValue(std::string_view /*name*/,
                                           std::string_view defaultValue) const override {
        return std::string(defaultValue);
    }
    [[nodiscard]] std::optional<std::string> GetOptionalValue(std::string_view) const override {
        return std::nullopt;
    }

    mutable int hasFlagCallCount = 0;
};

class FakeConfigManager final : public Config::IConfigManager {
public:
    bool Load(const std::filesystem::path&) override { return true; }
    bool Save() override {
        ++saveCallCount;
        return true;
    }
    bool Save(const std::filesystem::path&) override {
        ++saveCallCount;
        return true;
    }
    [[nodiscard]] std::string GetString(std::string_view,
                                            std::string_view defaultValue) const override {
        return std::string(defaultValue);
    }
    [[nodiscard]] std::int64_t GetInt(std::string_view, std::int64_t defaultValue) const override {
        return defaultValue;
    }
    [[nodiscard]] double GetDouble(std::string_view, double defaultValue) const override {
        return defaultValue;
    }
    [[nodiscard]] bool GetBool(std::string_view, bool defaultValue) const override {
        return defaultValue;
    }
    void SetString(std::string_view, std::string) override {}
    void SetInt(std::string_view, std::int64_t) override {}
    void SetDouble(std::string_view, double) override {}
    void SetBool(std::string_view, bool) override {}
    [[nodiscard]] bool HasKey(std::string_view) const override { return false; }
    bool RemoveKey(std::string_view) override { return false; }

    int saveCallCount = 0;
};

class FakeEntityManager final : public Scene::IEntityManager {
public:
    [[nodiscard]] Scene::Entity CreateEntity() override {
        ++createEntityCallCount;
        return Scene::Entity{0, 0};
    }
    void DestroyEntity(Scene::Entity) override { ++destroyEntityCallCount; }
    [[nodiscard]] bool IsAlive(Scene::Entity) const noexcept override { return true; }
    [[nodiscard]] std::size_t GetEntityCount() const noexcept override { return 0; }
    [[nodiscard]] std::vector<std::type_index> GetComponentTypes(Scene::Entity) const override {
        return {};
    }

    int createEntityCallCount = 0;
    int destroyEntityCallCount = 0;

protected:
    [[nodiscard]] void* AddComponentErased(Scene::Entity, std::type_index,
                                            const Scene::ComponentTypeInfo&) override {
        return nullptr;
    }
    void RemoveComponentErased(Scene::Entity, std::type_index) override {}
    [[nodiscard]] bool HasComponentErased(Scene::Entity, std::type_index) const override { return false; }
    [[nodiscard]] void* GetComponentErased(Scene::Entity, std::type_index) override { return nullptr; }
    void ForEachErased(const std::vector<std::type_index>&,
                        const std::function<void(Scene::Entity, const std::vector<void*>&)>&) override {}
};

class FakeSceneGraph final : public Scene::ISceneGraph {
public:
    void AttachTransform(Scene::IEntityManager&, Scene::Entity,
                             const Scene::TransformComponent&) override {}
    void SetLocalTransform(Scene::IEntityManager&, Scene::Entity,
                               const Scene::TransformComponent&) override {}
    void SetParent(Scene::IEntityManager&, Scene::Entity, Scene::Entity) override {}
    void Update(Scene::IEntityManager&) override { ++updateCallCount; }
    [[nodiscard]] std::vector<Scene::Entity> GetChildren(Scene::IEntityManager&,
                                                                Scene::Entity) override {
        return {};
    }
    [[nodiscard]] std::optional<std::size_t> GetSiblingIndex(Scene::IEntityManager&,
                                                                 Scene::Entity) override {
        return std::nullopt;
    }
    [[nodiscard]] bool SetSiblingIndex(Scene::IEntityManager&, Scene::Entity, std::size_t) override {
        return false;
    }
    [[nodiscard]] std::optional<Scene::ResolvedObjectModes> TryGetResolvedObjectModes(
        Scene::Entity) const override {
        return std::nullopt;
    }

    int updateCallCount = 0;
};

class FakeParticleRuntime final : public Scene::IParticleRuntime {
public:
    [[nodiscard]] Scene::ParticleRuntimeResult AttachDetailed(
        Scene::Entity, const Scene::ParticleEmitterComponent&) override {
        ++attachCallCount;
        return {Scene::ParticleRuntimeCode::Applied, "attached"};
    }
    [[nodiscard]] Scene::ParticleRuntimeResult DetachDetailed(Scene::Entity) noexcept override {
        ++detachCallCount;
        return {Scene::ParticleRuntimeCode::Applied, "detached"};
    }
    [[nodiscard]] Scene::ParticleRuntimeResult SetEnabledDetailed(Scene::Entity, bool) noexcept override {
        ++setEnabledCallCount;
        return {Scene::ParticleRuntimeCode::Applied, "enabled"};
    }
    [[nodiscard]] Scene::ParticleRuntimeResult SetLiveParticleCountDetailed(
        Scene::Entity, std::uint32_t) noexcept override {
        ++setLiveParticleCountCallCount;
        return {Scene::ParticleRuntimeCode::Applied, "live count"};
    }
    [[nodiscard]] Scene::ParticleRuntimeResult EmitDetailed(
        Scene::Entity, const Scene::ParticleEmission&) override {
        ++emitCallCount;
        return {Scene::ParticleRuntimeCode::Applied, "emitted"};
    }
    [[nodiscard]] Scene::ParticleRuntimeResult SimulateDetailed(
        float, const Math::Vector3&) noexcept override {
        ++simulateCallCount;
        return {Scene::ParticleRuntimeCode::Applied, "simulated"};
    }
    [[nodiscard]] Scene::ParticleRuntimeSnapshot GetSnapshot() const override {
        ++snapshotCallCount;
        return {};
    }
    [[nodiscard]] std::optional<Scene::ParticleStateSnapshot> GetParticleSnapshot(
        Scene::Entity) const override {
        ++particleSnapshotCallCount;
        return std::nullopt;
    }
    [[nodiscard]] bool HasInstance(Scene::Entity) const noexcept override { return false; }
    [[nodiscard]] std::size_t GetInstanceCount() const noexcept override { return 0U; }
    [[nodiscard]] std::uint64_t GetTotalBudget() const noexcept override { return 0U; }

    mutable int snapshotCallCount = 0;
    mutable int particleSnapshotCallCount = 0;
    int attachCallCount = 0;
    int detachCallCount = 0;
    int setEnabledCallCount = 0;
    int setLiveParticleCountCallCount = 0;
    int emitCallCount = 0;
    int simulateCallCount = 0;
};

class FakeAssetDatabase final : public Asset::IAssetDatabase {
public:
    bool Load(const std::filesystem::path&) override { return true; }
    bool Save() override {
        ++saveCallCount;
        return true;
    }
    bool Save(const std::filesystem::path&) override {
        ++saveCallCount;
        return true;
    }
    [[nodiscard]] Asset::AssetGuid Register(const std::filesystem::path&) override {
        return Asset::AssetGuid{1};
    }
    [[nodiscard]] std::filesystem::path Resolve(Asset::AssetGuid) const override { return {}; }
    [[nodiscard]] bool HasGuid(Asset::AssetGuid) const override { return false; }
    [[nodiscard]] std::vector<Asset::AssetRecord> GetRegisteredAssets() const override { return {}; }

    int saveCallCount = 0;
};

class FakeProjectFileIndex final : public Asset::IProjectFileIndex {
public:
    bool Refresh(const Asset::IAssetDatabase&) override {
        ++refreshCallCount;
        ++snapshot.refreshGeneration;
        return true;
    }

    [[nodiscard]] Asset::ProjectFileSnapshot GetSnapshot() const override { return snapshot; }

    int refreshCallCount = 0;
    Asset::ProjectFileSnapshot snapshot;
};

class FakeProjectChangeWatcher final : public Asset::IProjectChangeWatcher {
public:
    [[nodiscard]] bool Poll(double, const Asset::IAssetDatabase&,
                               Asset::IDerivedArtifactCache&) override {
        ++pollCallCount;
        return true;
    }

    [[nodiscard]] bool PollNow(const Asset::IAssetDatabase&,
                                  Asset::IDerivedArtifactCache&) override {
        ++pollNowCallCount;
        return true;
    }

    [[nodiscard]] Asset::ProjectChangeSnapshot GetSnapshot() const override { return snapshot; }

    void AcknowledgeThrough(std::uint64_t sequence) override { acknowledgedThrough = sequence; }

    void AcknowledgeRescan() override { snapshot.rescanRequired = false; }

    int pollCallCount = 0;
    int pollNowCallCount = 0;
    std::uint64_t acknowledgedThrough = 0U;
    Asset::ProjectChangeSnapshot snapshot;
};

class FakeDerivedArtifactCache final : public Asset::IDerivedArtifactCache {
public:
    [[nodiscard]] std::optional<Asset::DerivedArtifactCacheRecord>
    LoadImportRecord(const std::filesystem::path&) const override {
        ++loadCallCount;
        return std::nullopt;
    }

    [[nodiscard]] bool StoreImportRecord(const std::filesystem::path&,
                                             const Asset::DerivedArtifactCacheRecord&) override {
        ++storeCallCount;
        return true;
    }

    [[nodiscard]] std::size_t MarkStaleForSource(const std::filesystem::path&) override {
        ++markStaleCallCount;
        return 0U;
    }

    [[nodiscard]] std::filesystem::path GetCacheRoot() const override { return "fake-derived-cache"; }

    mutable int loadCallCount = 0;
    int storeCallCount = 0;
    int markStaleCallCount = 0;
};

class FakeAssetImportQueue final : public Asset::IAssetImportQueue {
public:
    [[nodiscard]] std::optional<Asset::AssetImportJobId> Enqueue(Asset::AssetImportRequest) override {
        ++enqueueCallCount;
        return Asset::AssetImportJobId{1U};
    }

    [[nodiscard]] bool Tick() override {
        ++tickCallCount;
        return false;
    }

    [[nodiscard]] bool Retry(Asset::AssetImportJobId) override {
        ++retryCallCount;
        return false;
    }

    [[nodiscard]] std::vector<Asset::AssetImportJob> GetJobs() const override { return {}; }

    int enqueueCallCount = 0;
    int tickCallCount = 0;
    int retryCallCount = 0;
};

class FakeSceneSerializer final : public Scene::ISceneSerializer {
public:
    [[nodiscard]] std::optional<Scene::SceneSnapshot> Capture(
        Scene::IEntityManager&, const std::vector<Scene::Entity>&, Scene::SceneAssetType assetType) const override {
        return Scene::SceneSnapshot{{}, assetType};
    }
    [[nodiscard]] std::vector<Scene::Entity> Restore(Scene::IEntityManager&,
                                                            const Scene::SceneSnapshot&) const override {
        return {};
    }
    [[nodiscard]] bool Save(Scene::IEntityManager&, const std::vector<Scene::Entity>&,
                                const std::filesystem::path&, Scene::SceneAssetType) override {
        ++saveCallCount;
        return true;
    }
    [[nodiscard]] std::vector<Scene::Entity> Load(Scene::IEntityManager&,
                                                          const std::filesystem::path&) override {
        return {};
    }

    int saveCallCount = 0;
};

class FakePrefabSystem final : public Scene::IPrefabSystem {
public:
    [[nodiscard]] Asset::AssetGuid SavePrefab(Scene::IEntityManager&,
                                                     Asset::IAssetDatabase&, Scene::Entity,
                                                     const std::filesystem::path&) override {
        ++savePrefabCallCount;
        return Asset::AssetGuid{1};
    }
    [[nodiscard]] Scene::Entity Instantiate(Scene::IEntityManager&, Scene::ISceneGraph&,
                                                   Asset::IAssetDatabase&, Asset::AssetGuid,
                                                   Scene::Entity) override {
        return Scene::kInvalidEntity;
    }
    [[nodiscard]] Scene::PrefabRefreshResult RefreshInstance(
        Scene::IEntityManager&, Scene::ISceneGraph&, Asset::IAssetDatabase&,
        Scene::Entity instanceRoot, bool) override {
        return {Scene::PrefabRefreshCode::InvalidInstance, instanceRoot, 0U, "fake"};
    }

    int savePrefabCallCount = 0;
};

class FakeHotReload final : public Asset::IHotReload {
public:
    void Track(Asset::AssetGuid) override {}
    void Untrack(Asset::AssetGuid) override {}
    void Poll(Asset::IAssetManager&, Asset::IAssetDatabase&, double) override { ++pollCallCount; }

    int pollCallCount = 0;
};

class FakeAssetManager final : public Asset::IAssetManager {
public:
    void CollectGarbage() override { ++collectGarbageCallCount; }
    void Reload(Asset::AssetGuid, Asset::IAssetDatabase&) override { ++reloadCallCount; }
    [[nodiscard]] std::size_t GetLoadedAssetCount() const override { return 0; }

    int collectGarbageCallCount = 0;
    int reloadCallCount = 0;

protected:
    void RegisterLoaderErased(std::type_index, Asset::AssetLoaderInfo) override {}
    void LoadErased(Asset::AssetGuid, std::type_index, Asset::IAssetDatabase&) override {}
    [[nodiscard]] Asset::AssetLoadState GetStateErased(Asset::AssetGuid) const override {
        return Asset::AssetLoadState::NotLoaded;
    }
    [[nodiscard]] std::string GetFailureReasonErased(Asset::AssetGuid) const override { return {}; }
    [[nodiscard]] void* TryGetErased(Asset::AssetGuid) override { return nullptr; }
    void AddRefErased(Asset::AssetGuid) override {}
    void ReleaseErased(Asset::AssetGuid) override {}
};

class FakeAssetImporter final : public Asset::IAssetImporter {
public:
    void RegisterImporter(std::string,
                              std::function<bool(const std::filesystem::path&, const std::filesystem::path&,
                                                  const Asset::AssetImportSettings&)>) override {}
    [[nodiscard]] Asset::AssetGuid Import(const std::filesystem::path&, const std::filesystem::path&,
                                                 Asset::IAssetDatabase&,
                                                 const Asset::AssetImportSettings&) override {
        ++importCallCount;
        return Asset::AssetGuid{1};
    }

    int importCallCount = 0;
};

class FakeAssetBundle final : public Asset::IAssetBundle {
public:
    [[nodiscard]] bool Pack(const std::vector<Asset::AssetBundleEntry>&,
                                const std::filesystem::path&) override {
        ++packCallCount;
        return true;
    }
    [[nodiscard]] bool Unpack(const std::filesystem::path&, const std::filesystem::path&) override {
        return true;
    }
    [[nodiscard]] bool HasEntry(const std::filesystem::path&, std::string_view) const override {
        return false;
    }
    [[nodiscard]] std::optional<std::vector<std::byte>> ReadEntry(const std::filesystem::path&,
                                                                       std::string_view) const override {
        return std::nullopt;
    }

    int packCallCount = 0;
};

class FakeFileSystem final : public Asset::IFileSystem {
public:
    [[nodiscard]] Asset::MountHandle MountDirectory(std::string, std::filesystem::path, int) override {
        return 1;
    }
    [[nodiscard]] Asset::MountHandle MountBundle(std::string, std::filesystem::path, int) override {
        return 1;
    }
    void Unmount(Asset::MountHandle) override {}
    [[nodiscard]] bool HasFile(std::string_view) const override { return false; }
    [[nodiscard]] std::optional<std::vector<std::byte>> ReadFile(std::string_view) const override {
        return std::nullopt;
    }
    [[nodiscard]] bool WriteFile(std::string_view, const std::vector<std::byte>&) override {
        ++writeCallCount;
        return true;
    }
    [[nodiscard]] std::filesystem::path ResolveRealPath(std::string_view) const override { return {}; }

    int writeCallCount = 0;
};

class FakeCommandBuffer final : public Render::ICommandBuffer {
public:
    void BeginRenderPass(const Render::RenderPassDesc&) override {}
    void EndRenderPass() override {}
    void BindPipeline(Render::PipelineHandle) override {}
    void BindVertexBuffer(Render::BufferHandle, std::uint32_t) override {}
    void BindIndexBuffer(Render::BufferHandle) override {}
    void BindTexture(Render::TextureHandle, std::uint32_t) override {}
    void BindSampler(Render::SamplerHandle, std::uint32_t) override {}
    void BindUniformBuffer(Render::BufferHandle, std::uint32_t) override {}
    void BindStorageBuffer(Render::BufferHandle, std::uint32_t) override {}
    void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void DrawIndexedIndirect(Render::BufferHandle, std::uint64_t) override {}
    void Draw(std::uint32_t, std::uint32_t) override {}
    void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void SetUniformFloat(std::string_view, float) override {}
    void SetUniformInt(std::string_view, std::int32_t) override {}
    void SetUniformBool(std::string_view, bool) override {}
    void SetUniformVector2(std::string_view, const Math::Vector2&) override {}
    void SetUniformVector3(std::string_view, const Math::Vector3&) override {}
    void SetUniformVector4(std::string_view, const Math::Vector4&) override {}
    void SetUniformMatrix4x4(std::string_view, const Math::Matrix4x4&) override {}
};

class FakeRenderDevice final : public Render::IRenderDevice {
public:
    [[nodiscard]] Render::BufferHandle CreateBuffer(const Render::BufferDesc&,
                                                            std::span<const std::byte>) override {
        ++createBufferCallCount;
        return Render::BufferHandle{1};
    }
    void DestroyBuffer(Render::BufferHandle) override {}
    [[nodiscard]] bool UpdateBuffer(Render::BufferHandle, std::span<const std::byte>, std::uint64_t) override {
        return true;
    }
    [[nodiscard]] bool ReadbackBuffer(Render::BufferHandle, std::span<std::byte>, std::uint64_t) override {
        return false; // this fake owns no memory - it never pretends to have contents to hand back
    }
    [[nodiscard]] Render::TextureHandle CreateTexture(const Render::TextureDesc&,
                                                              std::span<const std::byte>) override {
        return Render::TextureHandle{1};
    }
    [[nodiscard]] bool SupportsTextureFormat(const Render::TextureFormat format,
                                                const Render::TextureColorSpace colorSpace) const noexcept override {
        const bool formatKnown = Render::GetTextureFormatBlockInfo(format).bytes != 0U;
        switch (colorSpace) {
            case Render::TextureColorSpace::Linear:
                return formatKnown && !Render::IsTextureFormatCompressed(format);
            case Render::TextureColorSpace::Srgb:
                return formatKnown && Render::IsTextureFormatSrgbCapable(format) &&
                       !Render::IsTextureFormatCompressed(format);
        }
        return false;
    }
    void DestroyTexture(Render::TextureHandle) override {}
    [[nodiscard]] Render::SamplerHandle CreateSampler(const Render::SamplerDesc&) override {
        return Render::SamplerHandle{1};
    }
    void DestroySampler(Render::SamplerHandle) override {}
    [[nodiscard]] Render::ShaderHandle CreateShader(const Render::ShaderDesc&, std::string*) override {
        return Render::ShaderHandle{1};
    }
    void DestroyShader(Render::ShaderHandle) override {}
    [[nodiscard]] Render::PipelineHandle CreatePipeline(const Render::PipelineDesc&, std::string*) override {
        return Render::PipelineHandle{1};
    }
    [[nodiscard]] Render::PipelineHandle CreateComputePipeline(const Render::ComputePipelineDesc&,
                                                                      std::string*) override {
        return Render::PipelineHandle{1};
    }
    void DestroyPipeline(Render::PipelineHandle) override {}
    [[nodiscard]] std::vector<Render::UniformReflection> GetPipelineUniforms(
        Render::PipelineHandle) const override {
        return {};
    }
    [[nodiscard]] bool GetPipelineBinary(Render::PipelineHandle, std::vector<std::byte>&,
                                             std::uint32_t&) const override {
        return false;
    }
    [[nodiscard]] Render::PipelineHandle CreatePipelineFromBinary(
        std::span<const std::byte>, std::uint32_t, const Render::PipelineBinaryDesc&) override {
        return Render::PipelineHandle{1};
    }
    [[nodiscard]] std::unique_ptr<Render::ICommandBuffer> CreateCommandBuffer() override { return nullptr; }
    void Submit(std::unique_ptr<Render::ICommandBuffer>) override {}
    void Present() override { ++presentCallCount; }
    [[nodiscard]] bool IsUsable() const noexcept override { return true; }
    [[nodiscard]] std::string_view GetBackendName() const noexcept override { return "Fake"; }

    int createBufferCallCount = 0;
    int presentCallCount = 0;
};

class FakeShaderManager final : public Render::Shader::IShaderManager {
public:
    [[nodiscard]] std::shared_ptr<Render::Shader::ShaderSource> CreateSource(
        const Render::Shader::ShaderSourceCompileDesc&) override {
        return nullptr;
    }
    [[nodiscard]] std::shared_ptr<Render::Shader::ShaderProgram> CreateProgram(
        const Render::Shader::ShaderProgramDesc&) override {
        return nullptr;
    }
    [[nodiscard]] std::shared_ptr<Render::Shader::ShaderProgram> CreateProgramFromStages(
        const Render::Shader::ShaderProgramStagesDesc&) override {
        return nullptr;
    }
    void Update(double) override { ++updateCallCount; }

    int updateCallCount = 0;
};

class FakeRenderSystem final : public Render::IRenderSystem {
public:
    void BeginFrame() override { ++beginFrameCallCount; }
    [[nodiscard]] Render::ICommandBuffer& GetFrameCommandBuffer() override { return commandBuffer; }
    void EndFrame() override {}
    [[nodiscard]] std::uint64_t GetFrameIndex() const noexcept override { return 0; }

    FakeCommandBuffer commandBuffer;
    int beginFrameCallCount = 0;
};

class FakeComputeSystem final : public Render::IComputeSystem {
public:
    [[nodiscard]] Render::PipelineHandle CreateProgram(const Render::ComputeProgramDesc&,
                                                              std::string*) override {
        return Render::kInvalidPipelineHandle;
    }
    void DestroyProgram(Render::PipelineHandle) override {}
    [[nodiscard]] bool IsProgramLive(Render::PipelineHandle) const noexcept override { return false; }
    [[nodiscard]] bool EnqueueDispatch(const Render::ComputeDispatchDesc&) override { return false; }
    std::size_t ExecuteQueuedDispatches(Render::ICommandBuffer&) override { return 0U; }
    [[nodiscard]] std::size_t GetQueuedDispatchCount() const noexcept override { return 0U; }
    void ClearQueue() override {}
    [[nodiscard]] const Render::ComputeSystemDiagnostics& GetDiagnostics() const noexcept override {
        return diagnostics;
    }

    Render::ComputeSystemDiagnostics diagnostics;
};

class FakeCameraSystem final : public Render::ICameraSystem {
public:
    [[nodiscard]] Math::Matrix4x4 ComputeViewMatrix(const Scene::IEntityManager&,
                                                            Scene::Entity) const override {
        ++computeViewCallCount;
        return Math::Matrix4x4::Identity();
    }
    [[nodiscard]] Math::Matrix4x4 ComputeProjectionMatrix(const Scene::IEntityManager&, Scene::Entity,
                                                                 float) const override {
        return Math::Matrix4x4::Identity();
    }
    [[nodiscard]] Math::Matrix4x4 ComputeViewProjection(const Scene::IEntityManager&, Scene::Entity,
                                                               float) const override {
        return Math::Matrix4x4::Identity();
    }
    [[nodiscard]] Math::Frustum ExtractFrustum(const Math::Matrix4x4&) const override { return {}; }
    [[nodiscard]] Render::CameraFrustumCorners ComputeFrustumCorners(
        const Scene::IEntityManager&, Scene::Entity, float) const override {
        return {};
    }
    [[nodiscard]] Math::Vector3 GetWorldPosition(const Scene::IEntityManager&,
                                                          Scene::Entity) const override {
        ++getWorldPositionCallCount;
        return {};
    }

    mutable int computeViewCallCount = 0;
    mutable int getWorldPositionCallCount = 0;
};

class FakeMeshRenderer final : public Render::IMeshRenderer {
public:
    [[nodiscard]] Render::RenderQueue ExtractRenderQueue(Scene::IEntityManager&, Asset::IAssetManager&,
                                                                 Asset::IAssetDatabase&,
                                                                 const Math::Frustum&) const override {
        ++extractRenderQueueCallCount;
        return {};
    }

    mutable int extractRenderQueueCallCount = 0;
};

class FakeLightSystem final : public Render::ILightSystem {
public:
    [[nodiscard]] Render::LightList ExtractActiveLights(Scene::IEntityManager&) const override {
        ++extractActiveLightCallCount;
        return {};
    }

    mutable int extractActiveLightCallCount = 0;
};

class FakeRenderer3D final : public Render::IRenderer3D {
public:
    float lastPhysicsInterpolationAlpha = -1.0F;

    void RenderFrame(Scene::IEntityManager&, Scene::Entity) override { ++renderFrameCallCount; }

    void SetPhysicsInterpolationAlpha(const float alpha) noexcept override {
        // Recorded rather than ignored: a fake that silently swallows the value cannot tell a
        // caller that forgot to set it from one that set it to zero, and this fake exists
        // precisely to observe what the engine hands the renderer.
        lastPhysicsInterpolationAlpha = alpha;
    }

    [[nodiscard]] Render::Renderer3DFrameDiagnostics GetLastFrameDiagnostics() const noexcept override {
        ++getDiagnosticsCallCount;
        return diagnostics;
    }

    int renderFrameCallCount = 0;
    mutable int getDiagnosticsCallCount = 0;
    Render::Renderer3DFrameDiagnostics diagnostics{};
};

class FakeCollisionSystem final : public Physics::ICollisionSystem {
public:
    [[nodiscard]] std::vector<Physics::CollisionPair> DetectCollisions(
        Scene::IEntityManager&) const override {
        ++detectCollisionsCallCount;
        return {};
    }

    mutable int detectCollisionsCallCount = 0;
};

class FakePhysicsSystem final : public Physics::IPhysicsSystem {
public:
    void Step(Scene::IEntityManager&, Scene::ISceneGraph&, float) override { ++stepCallCount; }

    int stepCallCount = 0;
};

class FakeRaycastSystem final : public Physics::IRaycastSystem {
public:
    [[nodiscard]] std::optional<Physics::RaycastHit> Raycast(
        Scene::IEntityManager&, const Physics::RaycastQuery&) const override {
        ++raycastCallCount;
        return std::nullopt;
    }

    mutable int raycastCallCount = 0;
};

class FakeInputSystem final : public Input::IInputSystem {
public:
    void SetKeyState(Input::KeyCode, bool) override {}
    void SetMouseButtonState(Input::MouseButton, bool) override {}
    void SetMousePosition(Math::Vector2) override {}
    void SetMouseScrollDelta(float) override {}
    void Update() override { ++updateCallCount; }

    [[nodiscard]] bool IsKeyDown(Input::KeyCode) const override { return false; }
    [[nodiscard]] bool WasKeyPressedThisFrame(Input::KeyCode) const override { return false; }
    [[nodiscard]] bool WasKeyReleasedThisFrame(Input::KeyCode) const override { return false; }
    [[nodiscard]] bool IsMouseButtonDown(Input::MouseButton) const override { return false; }
    [[nodiscard]] bool WasMouseButtonPressedThisFrame(Input::MouseButton) const override { return false; }
    [[nodiscard]] bool WasMouseButtonReleasedThisFrame(Input::MouseButton) const override { return false; }
    [[nodiscard]] Math::Vector2 GetMousePosition() const override { return {}; }
    [[nodiscard]] Math::Vector2 GetMouseDelta() const override { return {}; }
    [[nodiscard]] float GetMouseScrollDelta() const override { return 0.0F; }

    void RegisterAction(Input::InputAction&&) override {}
    bool UnregisterAction(std::string_view) override { return false; }
    bool RemapAction(std::string_view, std::vector<Input::InputBinding>,
                       std::vector<Input::InputBinding>) override {
        return false;
    }
    [[nodiscard]] bool IsActionTriggered(std::string_view) const override { return false; }
    [[nodiscard]] bool IsActionHeld(std::string_view) const override { return false; }
    [[nodiscard]] bool IsActionReleased(std::string_view) const override { return false; }
    [[nodiscard]] float GetAxisValue(std::string_view) const override { return 0.0F; }
    [[nodiscard]] bool HasAction(std::string_view) const override { return false; }

    int updateCallCount = 0;
};

class FakeAudioDevice final : public Audio::IAudioDevice {
public:
    [[nodiscard]] Audio::VoiceHandle CreateVoice(const Audio::AudioVoiceDesc&) override {
        return Audio::VoiceHandle{1};
    }
    void DestroyVoice(Audio::VoiceHandle) override {}
    [[nodiscard]] bool Play(Audio::VoiceHandle) override { return true; }
    [[nodiscard]] bool Stop(Audio::VoiceHandle) override { return true; }
    [[nodiscard]] bool SetVoiceParams(Audio::VoiceHandle, const Audio::AudioVoiceParams&) override {
        return true;
    }
    [[nodiscard]] Audio::VoicePlaybackState GetVoiceState(Audio::VoiceHandle) const override {
        return Audio::VoicePlaybackState::Stopped;
    }
    [[nodiscard]] std::string_view GetBackendName() const noexcept override { return "Fake"; }

    [[nodiscard]] Audio::VoiceHandle CreateStreamVoice(const Audio::AudioStreamFormat&) override {
        return Audio::VoiceHandle{1};
    }
    [[nodiscard]] std::size_t PushStreamFrames(Audio::VoiceHandle, std::span<const float>) override {
        return 0U;
    }
    [[nodiscard]] bool EndStream(Audio::VoiceHandle) override { return true; }
    [[nodiscard]] std::size_t GetStreamBufferedFrames(Audio::VoiceHandle) const override { return 0U; }
    [[nodiscard]] std::size_t GetStreamFreeFrames(Audio::VoiceHandle) const override { return 0U; }
};

class FakeAudioSystem final : public Audio::IAudioSystem {
public:
    void SetListenerPosition(Math::Vector3) override {}
    void SetListenerOrientation(Math::Vector3, Math::Vector3) override {}
    [[nodiscard]] Math::Vector3 GetListenerPosition() const noexcept override { return {}; }
    [[nodiscard]] Audio::VoiceHandle CreateSource(const Audio::AudioSourceDesc&) override {
        return Audio::VoiceHandle{1};
    }
    void DestroySource(Audio::VoiceHandle) override {}
    [[nodiscard]] bool Play(Audio::VoiceHandle) override { return true; }
    [[nodiscard]] bool Stop(Audio::VoiceHandle) override { return true; }
    [[nodiscard]] Audio::VoicePlaybackState GetSourceState(Audio::VoiceHandle) const override {
        return Audio::VoicePlaybackState::Stopped;
    }
    void SetSourcePosition(Audio::VoiceHandle, Math::Vector3) override {}
    void SetSourceVolume(Audio::VoiceHandle, float) override {}
    void SetSourcePitch(Audio::VoiceHandle, float) override {}
    bool RegisterMixerGroup(std::string_view, std::string_view, float, float) override { return false; }
    bool RegisterMixerGroup(std::string_view, float, float) override { return false; }
    bool RemoveMixerGroup(std::string_view) override { return false; }
    bool SetMixerGroupParent(std::string_view, std::string_view) override { return false; }
    bool SetMixerGroupVolume(std::string_view, float) override { return false; }
    bool SetMixerGroupPitch(std::string_view, float) override { return false; }
    bool SetMixerGroupMuted(std::string_view, bool) override { return false; }
    bool SetMixerGroupSoloed(std::string_view, bool) override { return false; }
    bool SetMixerInsert(std::string_view, std::size_t, const Audio::AudioInsertDesc&) override {
        return false;
    }
    bool ClearMixerInsert(std::string_view, std::size_t) override { return false; }
    bool SetMixerSampleRate(float) override { return false; }
    bool SetSourceMixerGroup(Audio::VoiceHandle, std::string_view) override { return false; }
    [[nodiscard]] Audio::AudioMixerDiagnostics GetMixerDiagnostics(std::size_t) const override { return {}; }
    [[nodiscard]] bool ResetSourceStream(Audio::VoiceHandle, std::size_t, bool, std::size_t,
                                             std::size_t) override {
        return false;
    }
    [[nodiscard]] bool ScheduleSourceStreamWindow(Audio::VoiceHandle, std::size_t) override {
        return false;
    }
    [[nodiscard]] bool PopSourceStreamWindow(Audio::VoiceHandle,
                                                 Audio::Pcm16StreamWindowPlan&) override {
        return false;
    }
    [[nodiscard]] bool ScheduleSourcePcmGainWindow(
        Audio::VoiceHandle, const Audio::PcmGainEffectWindow&) override {
        return false;
    }
    [[nodiscard]] bool ApplySourcePcmGainEffects(
        Audio::VoiceHandle, const std::vector<float>&, std::vector<float>&) override {
        return false;
    }
    [[nodiscard]] bool ApplySourceBusChain(
        Audio::VoiceHandle, const std::vector<float>&, std::vector<float>&) override {
        return false;
    }
    void Update(float) override { ++updateCallCount; }
    void SetOcclusionProvider(const Audio::IAudioOcclusionProvider*) override {}
    [[nodiscard]] Audio::AudioMeterReading GetSourceMeter(Audio::VoiceHandle) const override {
        return Audio::kSilentAudioMeter;
    }
    [[nodiscard]] Audio::AudioMeterReading GetMixerGroupMeter(std::string_view) const override {
        return Audio::kSilentAudioMeter;
    }
    [[nodiscard]] Audio::VoiceHandle CreateStreamingSource(const Audio::AudioSourceDesc&,
                                                                 Audio::IAudioStreamSource&) override {
        return Audio::VoiceHandle{1};
    }
    [[nodiscard]] std::size_t PumpStreamingSource(Audio::VoiceHandle) override { return 0U; }
    [[nodiscard]] std::size_t GetStreamingSourceBufferedFrames(Audio::VoiceHandle) const override {
        return 0U;
    }

    int updateCallCount = 0;
};

class FakeAudioSourceSystem final : public Audio::IAudioSourceSystem {
public:
    void Sync(Scene::IEntityManager&, Audio::IAudioSystem&) override { ++syncCallCount; }
    [[nodiscard]] bool PlayEntity(Scene::Entity, Scene::IEntityManager&,
                                     Audio::IAudioSystem&) override {
        return false;
    }
    [[nodiscard]] bool StopEntity(Scene::Entity, Audio::IAudioSystem&) override { return false; }
    [[nodiscard]] bool IsEntityPlaying(Scene::Entity, const Audio::IAudioSystem&) const override {
        return false;
    }

    int syncCallCount = 0;
};

class FakeSaveGameSystem final : public Save::ISaveGameSystem {
public:
    [[nodiscard]] bool Save(int, Scene::IEntityManager&, const std::vector<Scene::Entity>&,
                                const Save::GameStateMetadata&) override {
        ++saveCallCount;
        return true;
    }
    [[nodiscard]] Save::SaveMigrationRegistrationResult RegisterMigration(
        std::uint32_t, std::uint32_t, Save::SavePayloadMigrationTransform) override {
        return {Save::SaveMigrationRegistrationCode::Accepted, "fake migration accepted"};
    }
    [[nodiscard]] std::vector<Scene::Entity> Load(int, Scene::IEntityManager&) override { return {}; }
    [[nodiscard]] bool DeleteSave(int) override { return false; }
    [[nodiscard]] bool HasSave(int) const override { return false; }
    [[nodiscard]] std::optional<Save::GameStateMetadata> GetSaveMetadata(int) const override {
        return std::nullopt;
    }
    [[nodiscard]] std::vector<int> ListUsedSlots() const override { return {}; }
    [[nodiscard]] Save::SaveMigrationDiagnostics GetLastMigrationDiagnostics() const override { return {}; }

    int saveCallCount = 0;
};

class FakeCheckpointManager final : public Save::ICheckpointManager {
public:
    void Update(double, Scene::IEntityManager&, const std::vector<Scene::Entity>&) override {
        ++updateCallCount;
    }
    [[nodiscard]] bool Checkpoint(Scene::IEntityManager&, const std::vector<Scene::Entity>&) override {
        return true;
    }
    void SetAutoSaveIntervalSeconds(double) noexcept override {}
    [[nodiscard]] double GetAutoSaveIntervalSeconds() const noexcept override { return 0.0; }
    [[nodiscard]] double GetElapsedSinceLastSaveSeconds() const noexcept override { return 0.0; }
    [[nodiscard]] double GetTotalPlaytimeSeconds() const noexcept override { return 0.0; }

    int updateCallCount = 0;
};

class FakeWindowManager final : public Window::IWindowManager {
public:
    [[nodiscard]] bool IsValid() const noexcept override { return true; }
    void AttachInputSystem(Input::IInputSystem* /*inputSystem*/) noexcept override {}
    void PollEvents() override { ++pollEventsCallCount; }
    void SwapBuffers() override {}
    [[nodiscard]] bool IsCloseRequested() const noexcept override { return false; }
    void SetVSyncEnabled(bool) override {}
    [[nodiscard]] bool IsVSyncEnabled() const noexcept override { return true; }
    void SetFullscreen(bool) override {}
    [[nodiscard]] bool IsFullscreen() const noexcept override { return false; }
    [[nodiscard]] std::uint32_t GetWidth() const noexcept override { return 0; }
    [[nodiscard]] std::uint32_t GetHeight() const noexcept override { return 0; }
    [[nodiscard]] std::vector<Window::MonitorInfo> EnumerateMonitors() const override { return {}; }
    [[nodiscard]] void* GetNativeWindowHandle() const noexcept override { return nullptr; }
    [[nodiscard]] std::string_view GetBackendName() const noexcept override { return "Fake"; }

    int pollEventsCallCount = 0;
};

TEST(EngineServicesTest, Accessors_ReturnExactSameInstancesPassedIn) {
    FakeLogger logger;
    FakeTimer timer;
    FakeEventSystem eventSystem;
    FakeMemoryManager memoryManager;
    FakeThreadPool threadPool;
    FakeCommandLine commandLine;
    FakeConfigManager configManager;
    FakeEntityManager entityManager;
    FakeSceneGraph sceneGraph;
    FakeAssetDatabase assetDatabase;
    FakeProjectFileIndex projectFileIndex;
    FakeDerivedArtifactCache derivedArtifactCache;
    FakeProjectChangeWatcher projectChangeWatcher;
    FakeSceneSerializer sceneSerializer;
    FakePrefabSystem prefabSystem;
    FakeParticleRuntime particleRuntime;
    FakeHotReload hotReload;
    FakeAssetManager assetManager;
    FakeAssetImporter assetImporter;
    FakeAssetImportQueue assetImportQueue;
    FakeAssetBundle assetBundle;
    FakeFileSystem fileSystem;
    FakeRenderDevice renderDevice;
    FakeShaderManager shaderManager;
    FakeRenderSystem renderSystem;
    FakeComputeSystem computeSystem;
    FakeCameraSystem cameraSystem;
    FakeMeshRenderer meshRenderer;
    FakeLightSystem lightSystem;
    FakeRenderer3D renderer3D;
    FakeCollisionSystem collisionSystem;
    FakePhysicsSystem physicsSystem;
    Physics::PhysicsQuerySystem physicsQuerySystem(collisionSystem);
    Physics::PhysicsConstraintSystem physicsConstraintSystem;
    FakeRaycastSystem raycastSystem;
    FakeInputSystem inputSystem;
    Input::GamepadInputSystem gamepadInputSystem;
    Input::MobileInputSystem mobileInputSystem;
    Input::MobileGestureSystem mobileGestureSystem(mobileInputSystem);
    FakeAudioDevice audioDevice;
    FakeAudioSystem audioSystem;
    FakeAudioSourceSystem audioSourceSystem;
    FakeSaveGameSystem saveGameSystem;
    FakeCheckpointManager checkpointManager;
    FakeWindowManager windowManager;
    Config::SettingsDocument projectSettings;
    Core::InputMapDocument inputMap;

    const EngineServices services(logger, timer, eventSystem, memoryManager, threadPool,
                                      commandLine, configManager, projectSettings, inputMap, entityManager, sceneGraph,
                                      assetDatabase, projectFileIndex, derivedArtifactCache, projectChangeWatcher,
                                      sceneSerializer, prefabSystem, particleRuntime,
                                      hotReload, assetManager, assetImporter, assetImportQueue, assetBundle, fileSystem,
                                      renderDevice, shaderManager, renderSystem, computeSystem, cameraSystem,
                                      meshRenderer, lightSystem, renderer3D, collisionSystem, physicsSystem,
                                      physicsQuerySystem, raycastSystem, physicsConstraintSystem, inputSystem,
                                      gamepadInputSystem, mobileInputSystem, mobileGestureSystem, audioDevice, audioSystem,
                                      audioSourceSystem, saveGameSystem, checkpointManager,
                                      windowManager);

    EXPECT_EQ(&services.GetLogger(), &logger);
    EXPECT_EQ(&services.GetTimer(), &timer);
    EXPECT_EQ(&services.GetEventSystem(), &eventSystem);
    EXPECT_EQ(&services.GetMemoryManager(), &memoryManager);
    EXPECT_EQ(&services.GetThreadPool(), &threadPool);
    EXPECT_EQ(&services.GetProjectSettings(), &projectSettings);
    EXPECT_EQ(&services.GetInputMap(), &inputMap);
    EXPECT_EQ(&services.GetCommandLine(), &commandLine);
    EXPECT_EQ(&services.GetConfigManager(), &configManager);
    EXPECT_EQ(&services.GetEntityManager(), &entityManager);
    EXPECT_EQ(&services.GetSceneGraph(), &sceneGraph);
    EXPECT_EQ(&services.GetAssetDatabase(), &assetDatabase);
    EXPECT_EQ(&services.GetProjectFileIndex(), &projectFileIndex);
    EXPECT_EQ(&services.GetDerivedArtifactCache(), &derivedArtifactCache);
    EXPECT_EQ(&services.GetProjectChangeWatcher(), &projectChangeWatcher);
    EXPECT_EQ(&services.GetSceneSerializer(), &sceneSerializer);
    EXPECT_EQ(&services.GetPrefabSystem(), &prefabSystem);
    EXPECT_EQ(&services.GetParticleRuntime(), &particleRuntime);
    EXPECT_EQ(&services.GetHotReload(), &hotReload);
    EXPECT_EQ(&services.GetAssetManager(), &assetManager);
    EXPECT_EQ(&services.GetAssetImporter(), &assetImporter);
    EXPECT_EQ(&services.GetAssetBundle(), &assetBundle);
    EXPECT_EQ(&services.GetFileSystem(), &fileSystem);
    EXPECT_EQ(&services.GetRenderDevice(), &renderDevice);
    EXPECT_EQ(&services.GetShaderManager(), &shaderManager);
    EXPECT_EQ(&services.GetRenderSystem(), &renderSystem);
    EXPECT_EQ(&services.GetComputeSystem(), &computeSystem);
    EXPECT_EQ(&services.GetCameraSystem(), &cameraSystem);
    EXPECT_EQ(&services.GetMeshRenderer(), &meshRenderer);
    EXPECT_EQ(&services.GetLightSystem(), &lightSystem);
    EXPECT_EQ(&services.GetRenderer3D(), &renderer3D);
    EXPECT_EQ(&services.GetCollisionSystem(), &collisionSystem);
    EXPECT_EQ(&services.GetPhysicsSystem(), &physicsSystem);
    EXPECT_EQ(&services.GetPhysicsQuerySystem(), &physicsQuerySystem);
    EXPECT_EQ(&services.GetRaycastSystem(), &raycastSystem);
    EXPECT_EQ(&services.GetPhysicsConstraintSystem(), &physicsConstraintSystem);
    EXPECT_EQ(&services.GetInputSystem(), &inputSystem);
    EXPECT_EQ(&services.GetGamepadInputSystem(), &gamepadInputSystem);
    EXPECT_EQ(&services.GetMobileInputSystem(), &mobileInputSystem);
    EXPECT_EQ(&services.GetMobileGestureSystem(), &mobileGestureSystem);
    EXPECT_EQ(&services.GetAudioDevice(), &audioDevice);
    EXPECT_EQ(&services.GetAudioSystem(), &audioSystem);
    EXPECT_EQ(&services.GetAudioSourceSystem(), &audioSourceSystem);
    EXPECT_EQ(&services.GetSaveGameSystem(), &saveGameSystem);
    EXPECT_EQ(&services.GetCheckpointManager(), &checkpointManager);
    EXPECT_EQ(&services.GetWindowManager(), &windowManager);
}

TEST(EngineServicesTest, Accessors_ProveInterfacesAreGenuinelySubstitutable) {
    FakeLogger logger;
    FakeTimer timer;
    FakeEventSystem eventSystem;
    FakeMemoryManager memoryManager;
    FakeThreadPool threadPool;
    FakeCommandLine commandLine;
    FakeConfigManager configManager;
    FakeEntityManager entityManager;
    FakeSceneGraph sceneGraph;
    FakeAssetDatabase assetDatabase;
    FakeProjectFileIndex projectFileIndex;
    FakeDerivedArtifactCache derivedArtifactCache;
    FakeProjectChangeWatcher projectChangeWatcher;
    FakeSceneSerializer sceneSerializer;
    FakePrefabSystem prefabSystem;
    FakeParticleRuntime particleRuntime;
    FakeHotReload hotReload;
    FakeAssetManager assetManager;
    FakeAssetImporter assetImporter;
    FakeAssetImportQueue assetImportQueue;
    FakeAssetBundle assetBundle;
    FakeFileSystem fileSystem;
    FakeRenderDevice renderDevice;
    FakeShaderManager shaderManager;
    FakeRenderSystem renderSystem;
    FakeComputeSystem computeSystem;
    FakeCameraSystem cameraSystem;
    FakeMeshRenderer meshRenderer;
    FakeLightSystem lightSystem;
    FakeRenderer3D renderer3D;
    FakeCollisionSystem collisionSystem;
    FakePhysicsSystem physicsSystem;
    Physics::PhysicsQuerySystem physicsQuerySystem(collisionSystem);
    Physics::PhysicsConstraintSystem physicsConstraintSystem;
    FakeRaycastSystem raycastSystem;
    FakeInputSystem inputSystem;
    Input::GamepadInputSystem gamepadInputSystem;
    Input::MobileInputSystem mobileInputSystem;
    Input::MobileGestureSystem mobileGestureSystem(mobileInputSystem);
    FakeAudioDevice audioDevice;
    FakeAudioSystem audioSystem;
    FakeAudioSourceSystem audioSourceSystem;
    FakeSaveGameSystem saveGameSystem;
    FakeCheckpointManager checkpointManager;
    FakeWindowManager windowManager;
    Config::SettingsDocument projectSettings;
    Core::InputMapDocument inputMap;
    const EngineServices services(logger, timer, eventSystem, memoryManager, threadPool,
                                      commandLine, configManager, projectSettings, inputMap, entityManager, sceneGraph,
                                      assetDatabase, projectFileIndex, derivedArtifactCache, projectChangeWatcher,
                                      sceneSerializer, prefabSystem, particleRuntime,
                                      hotReload, assetManager, assetImporter, assetImportQueue, assetBundle, fileSystem,
                                      renderDevice, shaderManager, renderSystem, computeSystem, cameraSystem,
                                      meshRenderer, lightSystem, renderer3D, collisionSystem, physicsSystem,
                                      physicsQuerySystem, raycastSystem, physicsConstraintSystem, inputSystem,
                                      gamepadInputSystem, mobileInputSystem, mobileGestureSystem, audioDevice, audioSystem,
                                      audioSourceSystem, saveGameSystem, checkpointManager,
                                      windowManager);

    services.GetTimer().Tick();
    services.GetEventSystem().DispatchQueued();
    services.GetMemoryManager().LogLeakReport();
    services.GetThreadPool().Submit([] {});
    static_cast<void>(services.GetCommandLine().HasFlag("server"));
    services.GetConfigManager().Save();
    static_cast<void>(services.GetEntityManager().CreateEntity());
    services.GetSceneGraph().Update(services.GetEntityManager());
    EXPECT_TRUE(services.GetParticleRuntime().Attach(Scene::Entity{1, 1}, Scene::ParticleEmitterComponent{}));
    services.GetAssetDatabase().Save();
    static_cast<void>(services.GetProjectFileIndex().Refresh(services.GetAssetDatabase()));
    static_cast<void>(services.GetDerivedArtifactCache().LoadImportRecord("unused.asset"));
    EXPECT_TRUE(services.GetProjectChangeWatcher().PollNow(services.GetAssetDatabase(),
                                                                   services.GetDerivedArtifactCache()));
    static_cast<void>(services.GetAssetImportQueue().Tick());
    static_cast<void>(services.GetSceneSerializer().Save(
        services.GetEntityManager(), {}, "unused.uvscene", Scene::SceneAssetType::Scene));
    static_cast<void>(services.GetPrefabSystem().SavePrefab(
        services.GetEntityManager(), services.GetAssetDatabase(), Scene::kInvalidEntity,
        "unused.uvprefab"));
    services.GetHotReload().Poll(services.GetAssetManager(), services.GetAssetDatabase(), 0.0);
    services.GetAssetManager().CollectGarbage();
    static_cast<void>(
        services.GetAssetImporter().Import("unused_source", "unused_dest", services.GetAssetDatabase()));
    static_cast<void>(services.GetAssetBundle().Pack({}, "unused.uvbundle"));
    static_cast<void>(services.GetFileSystem().WriteFile("unused.txt", {}));
    static_cast<void>(services.GetRenderDevice().CreateBuffer(Render::BufferDesc{}));
    services.GetShaderManager().Update(0.0);
    services.GetRenderSystem().BeginFrame();
    static_cast<void>(
        services.GetCameraSystem().ComputeViewMatrix(services.GetEntityManager(), Scene::kInvalidEntity));
    static_cast<void>(
        services.GetCameraSystem().GetWorldPosition(services.GetEntityManager(), Scene::kInvalidEntity));
    static_cast<void>(services.GetMeshRenderer().ExtractRenderQueue(
        services.GetEntityManager(), services.GetAssetManager(), services.GetAssetDatabase(),
        Math::Frustum{}));
    static_cast<void>(services.GetLightSystem().ExtractActiveLights(services.GetEntityManager()));
    services.GetRenderer3D().RenderFrame(services.GetEntityManager(), Scene::kInvalidEntity);
    renderer3D.diagnostics.primitiveItemsExtracted = 7U;
    EXPECT_EQ(services.GetRenderer3D().GetLastFrameDiagnostics().primitiveItemsExtracted, 7U);
    static_cast<void>(services.GetRaycastSystem().Raycast(services.GetEntityManager(), Physics::RaycastQuery{}));
    services.GetGamepadInputSystem().Update();
    services.GetMobileInputSystem().Update();
    services.GetMobileGestureSystem().Update(0.0F);
    services.GetInputSystem().Update();
    services.GetAudioSystem().Update(0.0F);
    services.GetAudioSourceSystem().Sync(services.GetEntityManager(), services.GetAudioSystem());
    static_cast<void>(services.GetSaveGameSystem().Save(0, services.GetEntityManager(), {},
                                                                Save::GameStateMetadata{}));
    services.GetCheckpointManager().Update(0.0, services.GetEntityManager(), {});
    services.GetWindowManager().PollEvents();

    EXPECT_EQ(timer.tickCount, 1);
    EXPECT_EQ(eventSystem.dispatchCount, 1);
    EXPECT_EQ(memoryManager.logLeakReportCallCount, 1);
    EXPECT_EQ(threadPool.submitCount, 1);
    EXPECT_EQ(commandLine.hasFlagCallCount, 1);
    EXPECT_EQ(configManager.saveCallCount, 1);
    EXPECT_EQ(entityManager.createEntityCallCount, 1);
    EXPECT_EQ(sceneGraph.updateCallCount, 1);
    EXPECT_EQ(particleRuntime.attachCallCount, 1);
    EXPECT_EQ(assetDatabase.saveCallCount, 1);
    EXPECT_EQ(projectFileIndex.refreshCallCount, 1);
    EXPECT_EQ(derivedArtifactCache.loadCallCount, 1);
    EXPECT_EQ(projectChangeWatcher.pollNowCallCount, 1);
    EXPECT_EQ(assetImportQueue.tickCallCount, 1);
    EXPECT_EQ(sceneSerializer.saveCallCount, 1);
    EXPECT_EQ(prefabSystem.savePrefabCallCount, 1);
    EXPECT_EQ(hotReload.pollCallCount, 1);
    EXPECT_EQ(assetManager.collectGarbageCallCount, 1);
    EXPECT_EQ(assetImporter.importCallCount, 1);
    EXPECT_EQ(assetBundle.packCallCount, 1);
    EXPECT_EQ(fileSystem.writeCallCount, 1);
    EXPECT_EQ(renderDevice.createBufferCallCount, 1);
    EXPECT_EQ(shaderManager.updateCallCount, 1);
    EXPECT_EQ(renderSystem.beginFrameCallCount, 1);
    EXPECT_EQ(cameraSystem.computeViewCallCount, 1);
    EXPECT_EQ(cameraSystem.getWorldPositionCallCount, 1);
    EXPECT_EQ(meshRenderer.extractRenderQueueCallCount, 1);
    EXPECT_EQ(lightSystem.extractActiveLightCallCount, 1);
    EXPECT_EQ(renderer3D.renderFrameCallCount, 1);
    EXPECT_EQ(renderer3D.getDiagnosticsCallCount, 1);
    EXPECT_EQ(raycastSystem.raycastCallCount, 1);
    EXPECT_EQ(inputSystem.updateCallCount, 1);
    EXPECT_EQ(audioSystem.updateCallCount, 1);
    EXPECT_EQ(audioSourceSystem.syncCallCount, 1);
    EXPECT_EQ(saveGameSystem.saveCallCount, 1);
    EXPECT_EQ(checkpointManager.updateCallCount, 1);
    EXPECT_EQ(windowManager.pollEventsCallCount, 1);
}

} // namespace
} // namespace UVE::Core::Tests
