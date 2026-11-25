// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "shader_manager.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "asset_bundle.h"
#include "file_system.h"
#include "event_system.h"
#include "null_render_device.h"
#include "built_in_shaders.h"
#include "job_counter.h"
#include "thread_pool.h"

namespace UVE::Render::Shader::Tests {
namespace {

// Bounded busy-poll (never a fixed sleep), matching this codebase's established
// WaitForTerminalState pattern (see tests/asset/asset_manager_uve_tests.cpp). Readiness only
// advances when Update() is called (the background job just does file I/O/preprocessing; the
// real "compile" and the ready-flag flip both happen inside Update(), on the calling thread).
template <typename T>
[[nodiscard]] bool WaitUntilReady(ShaderManager& shaderManager, const T& obj, int maxIterations = 200000) {
    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        shaderManager.Update(0.0);
        if (obj.IsReady()) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

/// Runs jobs only when asked, on the calling thread, and holds on to each job it has run until
/// told to let go: the window a real worker has between a job returning and the job's captures
/// being destroyed. Counters are released after the captures, as ThreadPool does.
class RetainingThreadPool final : public Threading::IThreadPool {
public:
    void Submit(Threading::Job job) override { m_queued.push_back(Job{std::move(job), nullptr}); }
    void Submit(Threading::Job job, Threading::JobCounter& counter) override {
        counter.Increment();
        m_queued.push_back(Job{std::move(job), &counter});
    }
    [[nodiscard]] std::size_t GetWorkerCount() const noexcept override { return 1U; }
    [[nodiscard]] std::size_t GetPendingJobCount() const noexcept override {
        return m_queued.size() + m_finished.size();
    }
    [[nodiscard]] std::size_t GetActiveWorkerCount() const noexcept override { return 0U; }
    [[nodiscard]] std::uint64_t GetStolenJobCount() const noexcept override { return 0U; }
    [[nodiscard]] bool IsWorkerThread() const noexcept override { return false; }

    void RunQueuedJobs() {
        std::vector<Job> jobs;
        jobs.swap(m_queued);
        for (Job& job : jobs) {
            job.function();
            m_finished.push_back(std::move(job));
        }
    }

    void ReleaseFinishedJobs() {
        std::vector<Job> finished;
        finished.swap(m_finished);
        for (Job& job : finished) {
            job.function = nullptr;
            if (job.counter != nullptr) {
                job.counter->DecrementAndNotify();
            }
        }
    }

    ~RetainingThreadPool() override { ReleaseFinishedJobs(); }

private:
    struct Job {
        Threading::Job function;
        Threading::JobCounter* counter = nullptr;
    };
    std::vector<Job> m_queued;
    std::vector<Job> m_finished;
};

class ShaderManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        assetBundle = std::make_unique<Asset::AssetBundle>();
        fileSystem = std::make_unique<Asset::FileSystem>(*assetBundle);
        threadPool = std::make_unique<Threading::ThreadPool>(1);
        eventSystem = std::make_unique<Events::EventSystem>();
        renderDevice = std::make_unique<NullRenderDevice>();
    }

    void TearDown() override {
        std::filesystem::remove_all(tempDirectory);
        std::filesystem::remove_all(tempCacheDirectory);
    }

    [[nodiscard]] std::unique_ptr<ShaderManager> MakeManager(ShaderManagerConfig config = {}) {
        return std::make_unique<ShaderManager>(*threadPool, *eventSystem, *renderDevice, *fileSystem, config);
    }

    std::unique_ptr<Asset::IAssetBundle> assetBundle;
    std::unique_ptr<Asset::IFileSystem> fileSystem;
    std::unique_ptr<Threading::IThreadPool> threadPool;
    std::unique_ptr<Events::IEventSystem> eventSystem;
    std::unique_ptr<IRenderDevice> renderDevice;

    const std::filesystem::path tempDirectory = "uve_shader_manager_tests_shaders";
    const std::filesystem::path tempCacheDirectory = "uve_shader_manager_tests_cache";
};

TEST_F(ShaderManagerTest, CreateSource_EmbeddedFallback_BecomesReadyAndValid) {
    const std::unique_ptr<ShaderManager> shaderManager = MakeManager();

    ShaderSourceCompileDesc desc;
    desc.stage = ShaderStage::Vertex;
    desc.virtualFilePath = "shaders/missing_on_disk.glsl";
    desc.embeddedFallbackSourceCode = "#version 330 core\nvoid main() { gl_Position = vec4(0.0); }\n";

    const std::shared_ptr<ShaderSource> source = shaderManager->CreateSource(desc);
    ASSERT_TRUE(WaitUntilReady(*shaderManager, *source));
    EXPECT_TRUE(source->IsValid());
    EXPECT_NE(source->GetHandle(), kInvalidShaderHandle);
}

TEST_F(ShaderManagerTest, SynchronousOnDemandCreationCompletesSourceAndProgramsBeforeReturning) {
    ShaderManagerConfig config;
    config.compileSynchronously = true;
    const std::unique_ptr<ShaderManager> shaderManager = MakeManager(config);

    ShaderSourceCompileDesc sourceDesc;
    sourceDesc.stage = ShaderStage::Vertex;
    sourceDesc.virtualFilePath = "shaders/missing_on_disk.glsl";
    sourceDesc.embeddedFallbackSourceCode = "#version 330 core\nvoid main() { gl_Position = vec4(0.0); }\n";
    const std::shared_ptr<ShaderSource> source = shaderManager->CreateSource(sourceDesc);
    ASSERT_TRUE(source->IsReady());
    EXPECT_TRUE(source->IsValid());
    EXPECT_NE(source->GetHandle(), kInvalidShaderHandle);
    EXPECT_EQ(shaderManager->GetPendingJobCount(), 0U);
    EXPECT_EQ(threadPool->GetPendingJobCount(), 0U);

    ShaderProgramDesc programDesc;
    programDesc.virtualFilePath = std::string(BuiltIn::kBasic3DVirtualPath);
    programDesc.embeddedFallbackSourceCode = std::string(BuiltIn::kBasic3DSource);
    programDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    programDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const std::shared_ptr<ShaderProgram> program = shaderManager->CreateProgram(programDesc);
    ASSERT_TRUE(program->IsReady());
    EXPECT_TRUE(program->IsValid());
    EXPECT_NE(program->GetPipelineHandle(), kInvalidPipelineHandle);
    EXPECT_EQ(shaderManager->GetPendingJobCount(), 0U);
    EXPECT_EQ(threadPool->GetPendingJobCount(), 0U);

    ShaderProgramStagesDesc stagesDesc;
    stagesDesc.vertexSource.embeddedFallbackSourceCode =
        "#version 330 core\nlayout(location = 0) in vec3 aPosition;\n"
        "void main() { gl_Position = vec4(aPosition, 1.0); }\n";
    stagesDesc.fragmentSource.stage = ShaderStage::Fragment;
    stagesDesc.fragmentSource.embeddedFallbackSourceCode =
        "#version 330 core\nout vec4 FragColor;\nvoid main() { FragColor = vec4(1.0); }\n";
    stagesDesc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    stagesDesc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    const std::shared_ptr<ShaderProgram> stagesProgram = shaderManager->CreateProgramFromStages(stagesDesc);
    ASSERT_TRUE(stagesProgram->IsReady());
    EXPECT_TRUE(stagesProgram->IsValid());
    EXPECT_NE(stagesProgram->GetPipelineHandle(), kInvalidPipelineHandle);
    EXPECT_EQ(shaderManager->GetPendingJobCount(), 0U);
    EXPECT_EQ(threadPool->GetPendingJobCount(), 0U);
}

TEST_F(ShaderManagerTest, UIOverlayFirstUpdateCannotGuaranteeReadinessBeforeAsyncJobsRun) {
    RetainingThreadPool pool;
    ShaderManager manager{pool, *eventSystem, *renderDevice, *fileSystem, ShaderManagerConfig{}};
    ShaderProgramDesc desc;
    desc.virtualFilePath = std::string(BuiltIn::kUIOverlayVirtualPath);
    desc.embeddedFallbackSourceCode = std::string(BuiltIn::kUIOverlaySource);
    desc.vertexLayout = {
        VertexAttribute{"POSITION", VertexAttributeFormat::Float2, 0U},
        VertexAttribute{"TEXCOORD", VertexAttributeFormat::Float2, 2U * sizeof(float)},
        VertexAttribute{"COLOR", VertexAttributeFormat::Float4, 4U * sizeof(float)}};
    desc.vertexStride = 8U * sizeof(float);
    desc.depthTestEnabled = false;
    desc.depthWriteEnabled = false;
    desc.blendMode = PipelineBlendMode::SourceAlphaOver;
    const auto program = manager.CreateProgram(desc);
    manager.Update(0.0);
    EXPECT_FALSE(program->IsReady());
    EXPECT_FALSE(program->IsValid());
    EXPECT_EQ(program->GetPipelineHandle(), kInvalidPipelineHandle);

    // Release every job before fatal assertions: the manager drains its job counter on teardown.
    pool.RunQueuedJobs();
    pool.ReleaseFinishedJobs();
    manager.Update(0.0);
    ASSERT_TRUE(program->IsReady());
    EXPECT_TRUE(program->IsValid());
    EXPECT_NE(program->GetPipelineHandle(), kInvalidPipelineHandle);

    ShaderManagerConfig synchronous;
    synchronous.compileSynchronously = true;
    ShaderManager syncManager{pool, *eventSystem, *renderDevice, *fileSystem, synchronous};
    const auto synchronousProgram = syncManager.CreateProgram(desc);
    EXPECT_TRUE(synchronousProgram->IsReady());
    EXPECT_TRUE(synchronousProgram->IsValid());
    EXPECT_EQ(pool.GetPendingJobCount(), 0U);
}

TEST_F(ShaderManagerTest, CreateProgram_BuiltInBasic3D_BecomesReadyAndValid) {
    const std::unique_ptr<ShaderManager> shaderManager = MakeManager();

    ShaderProgramDesc desc;
    desc.virtualFilePath = std::string(BuiltIn::kBasic3DVirtualPath);
    desc.embeddedFallbackSourceCode = std::string(BuiltIn::kBasic3DSource);
    desc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    desc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));

    const std::shared_ptr<ShaderProgram> program = shaderManager->CreateProgram(desc);
    ASSERT_TRUE(WaitUntilReady(*shaderManager, *program));
    EXPECT_TRUE(program->IsValid());
    EXPECT_NE(program->GetPipelineHandle(), kInvalidPipelineHandle);
}

TEST_F(ShaderManagerTest, CreateProgram_LinkedStagesAreReleasedByTheManagerNotByTheWorker) {
    RetainingThreadPool pool;
    ShaderManager shaderManager{pool, *eventSystem, *renderDevice, *fileSystem, ShaderManagerConfig{}};
    const auto& device = static_cast<const NullRenderDevice&>(*renderDevice);

    ShaderProgramDesc desc;
    desc.virtualFilePath = std::string(BuiltIn::kBasic3DVirtualPath);
    desc.embeddedFallbackSourceCode = std::string(BuiltIn::kBasic3DSource);
    desc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    desc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));

    const std::shared_ptr<ShaderProgram> program = shaderManager.CreateProgram(desc);
    pool.RunQueuedJobs();
    shaderManager.Update(0.0);
    ASSERT_TRUE(program->IsReady());
    ASSERT_TRUE(program->IsValid());

    // Linking is done and the stages are no longer needed. The worker letting go of the jobs it
    // ran must not destroy anything: GPU objects are released on the thread that drives the
    // manager, at a point that thread chooses.
    const std::size_t liveAfterLink = device.GetLiveResourceCount();
    pool.ReleaseFinishedJobs();
    EXPECT_EQ(device.GetLiveResourceCount(), liveAfterLink);
}

TEST_F(ShaderManagerTest, CreateProgramFromStages_SeparateEmbeddedStages_BecomesReadyAndValid) {
    const std::unique_ptr<ShaderManager> shaderManager = MakeManager();

    ShaderProgramStagesDesc desc;
    desc.vertexSource.stage = ShaderStage::Vertex;
    desc.vertexSource.embeddedFallbackSourceCode =
        "#version 330 core\nlayout(location = 0) in vec3 aPosition;\nvoid main() { gl_Position = vec4(aPosition, 1.0); }\n";
    desc.fragmentSource.stage = ShaderStage::Fragment;
    desc.fragmentSource.embeddedFallbackSourceCode =
        "#version 330 core\nout vec4 FragColor;\nvoid main() { FragColor = vec4(1.0); }\n";
    desc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    desc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
    desc.debugName = "SeparateEmbeddedStages";

    const std::shared_ptr<ShaderProgram> program = shaderManager->CreateProgramFromStages(desc);
    ASSERT_TRUE(WaitUntilReady(*shaderManager, *program));
    EXPECT_TRUE(program->IsValid());
    EXPECT_NE(program->GetPipelineHandle(), kInvalidPipelineHandle);
}

TEST_F(ShaderManagerTest, HotReload_SeparateFragmentStage_RebuildsSameProgram) {
    std::filesystem::remove_all(tempDirectory);
    std::filesystem::create_directories(tempDirectory);
    const std::filesystem::path vertexPath = tempDirectory / "material.vert";
    const std::filesystem::path fragmentPath = tempDirectory / "material.frag";
    {
        std::ofstream vertexFile(vertexPath);
        vertexFile << "#version 330 core\nlayout(location = 0) in vec3 aPosition;\n"
                      "void main() { gl_Position = vec4(aPosition, 1.0); }\n";
    }
    {
        std::ofstream fragmentFile(fragmentPath);
        fragmentFile << "#version 330 core\nout vec4 FragColor;\n"
                        "void main() { FragColor = vec4(1.0); }\n// marker-v1\n";
    }
    fileSystem->MountDirectory("material", tempDirectory, 0);

    ShaderManagerConfig config;
    config.hotReloadEnabled = true;
    config.hotReloadPollIntervalSeconds = 0.0;
    const std::unique_ptr<ShaderManager> shaderManager = MakeManager(config);

    ShaderProgramStagesDesc desc;
    desc.vertexSource.virtualFilePath = "material/material.vert";
    desc.vertexSource.embeddedFallbackSourceCode = "#version 330 core\nvoid main() {}\n";
    desc.fragmentSource.virtualFilePath = "material/material.frag";
    desc.fragmentSource.embeddedFallbackSourceCode = "#version 330 core\nout vec4 FragColor;\nvoid main() {}\n";
    desc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    desc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));

    const std::shared_ptr<ShaderProgram> program = shaderManager->CreateProgramFromStages(desc);
    ASSERT_TRUE(WaitUntilReady(*shaderManager, *program));
    ASSERT_TRUE(program->IsValid());
    const std::uint64_t initialHash = program->GetContentHash();

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::ofstream fragmentFile(fragmentPath, std::ios::trunc);
        fragmentFile << "#version 330 core\nout vec4 FragColor;\n"
                        "void main() { FragColor = vec4(0.5); }\n// marker-v2\n";
    }

    bool reloaded = false;
    for (int iteration = 0; iteration < 200000 && !reloaded; ++iteration) {
        shaderManager->Update(1.0);
        reloaded = program->IsValid() && program->GetContentHash() != initialHash;
        if (!reloaded) {
            std::this_thread::yield();
        }
    }
    EXPECT_TRUE(reloaded);
}

TEST_F(ShaderManagerTest, CreateSource_IncludeDirectiveOnRealDisk_ResolvesThroughVfs) {
    std::filesystem::remove_all(tempDirectory);
    std::filesystem::create_directories(tempDirectory / "inc");
    {
        std::ofstream includeFile(tempDirectory / "inc" / "common.glsl");
        includeFile << "vec3 uveTestHelper() { return vec3(1.0); }\n";
    }
    {
        std::ofstream mainFile(tempDirectory / "main.glsl");
        mainFile << "#include \"shaders/inc/common.glsl\"\nvoid main() {}\n";
    }
    fileSystem->MountDirectory("shaders", tempDirectory, 0);

    const std::unique_ptr<ShaderManager> shaderManager = MakeManager();
    ShaderSourceCompileDesc desc;
    desc.stage = ShaderStage::Vertex;
    desc.virtualFilePath = "shaders/main.glsl";
    desc.embeddedFallbackSourceCode = "#version 330 core\nvoid main() {}\n";

    const std::shared_ptr<ShaderSource> source = shaderManager->CreateSource(desc);
    ASSERT_TRUE(WaitUntilReady(*shaderManager, *source));
    EXPECT_NE(source->GetResolvedSource().find("uveTestHelper"), std::string::npos);
}

TEST_F(ShaderManagerTest, HotReload_FileChangeOnDisk_TriggersRecompileWithNewResolvedSource) {
    std::filesystem::remove_all(tempDirectory);
    std::filesystem::create_directories(tempDirectory);
    const std::filesystem::path shaderFile = tempDirectory / "reloadable.glsl";
    {
        std::ofstream file(shaderFile);
        file << "#version 330 core\nvoid main() { gl_Position = vec4(0.0); }\n// marker-v1\n";
    }
    fileSystem->MountDirectory("shaders", tempDirectory, 0);

    ShaderManagerConfig config;
    config.hotReloadEnabled = true;
    config.hotReloadPollIntervalSeconds = 0.0;
    const std::unique_ptr<ShaderManager> shaderManager = MakeManager(config);

    ShaderSourceCompileDesc desc;
    desc.stage = ShaderStage::Vertex;
    desc.virtualFilePath = "shaders/reloadable.glsl";
    desc.embeddedFallbackSourceCode = "#version 330 core\nvoid main() {}\n";
    desc.hotReloadEnabled = true;

    const std::shared_ptr<ShaderSource> source = shaderManager->CreateSource(desc);
    ASSERT_TRUE(WaitUntilReady(*shaderManager, *source));
    ASSERT_NE(source->GetResolvedSource().find("marker-v1"), std::string::npos);

    // Sleep briefly so the modified file's mtime is guaranteed to differ from the original write
    // (some filesystems have coarse mtime resolution) before rewriting it with new content.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    {
        std::ofstream file(shaderFile, std::ios::trunc);
        file << "#version 330 core\nvoid main() { gl_Position = vec4(0.0); }\n// marker-v2\n";
    }

    bool sawUpdatedMarker = false;
    for (int iteration = 0; iteration < 200000 && !sawUpdatedMarker; ++iteration) {
        shaderManager->Update(1.0);
        if (source->GetResolvedSource().contains("marker-v2")) {
            sawUpdatedMarker = true;
        }
        std::this_thread::yield();
    }
    EXPECT_TRUE(sawUpdatedMarker);
}

TEST_F(ShaderManagerTest, GetPendingJobCount_DropsToZeroOnceCreationSettles) {
    const std::unique_ptr<ShaderManager> shaderManager = MakeManager();
    ShaderSourceCompileDesc desc;
    desc.stage = ShaderStage::Fragment;
    desc.embeddedFallbackSourceCode = "#version 330 core\nout vec4 c;\nvoid main() { c = vec4(1.0); }\n";

    const std::shared_ptr<ShaderSource> source = shaderManager->CreateSource(desc);
    ASSERT_TRUE(WaitUntilReady(*shaderManager, *source));
    EXPECT_EQ(shaderManager->GetPendingJobCount(), 0U);
}

TEST_F(ShaderManagerTest, NullRenderDeviceBackend_NeverReportsBinaryCacheHit) {
    // NullRenderDevice::GetPipelineBinary()/CreatePipelineFromBinary() are documented
    // no-op/false stand-ins - ShaderManager must therefore always take the from-source path
    // against this backend, never claim a cache hit that never happened.
    ShaderManagerConfig config;
    config.cachePath = tempCacheDirectory;
    const std::unique_ptr<ShaderManager> shaderManager = MakeManager(config);

    ShaderProgramDesc desc;
    desc.virtualFilePath = std::string(BuiltIn::kBasic3DVirtualPath);
    desc.embeddedFallbackSourceCode = std::string(BuiltIn::kBasic3DSource);
    desc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
    desc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));

    const std::shared_ptr<ShaderProgram> program = shaderManager->CreateProgram(desc);
    ASSERT_TRUE(WaitUntilReady(*shaderManager, *program));
    EXPECT_FALSE(shaderManager->GetLastCompileUsedCache());
}

// Guards against the built-in .glsl files under Engine/Runtime/RHI/Shader/built_in/ ever
// drifting from their embedded C++ string fallback counterparts (see built_in_shaders_uve.cpp's
// generation note) - both must be used by the engine at different times (real file when
// reachable, embedded string otherwise) and are expected to be byte-identical. Assumes the
// process's current working directory is the repository root, matching every other relative
// default path in this codebase (EngineConfig::shaderSourceRealDirectory,
// assetDatabaseFilePath, logFilePath, ...).
class BuiltInShaderParityTest : public ::testing::TestWithParam<std::pair<std::string_view, std::string_view>> {};

[[nodiscard]] std::optional<std::string> ReadFileToString(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

TEST_P(BuiltInShaderParityTest, EmbeddedSource_IsByteIdenticalToPhysicalFile) {
    const auto& [relativePath, embeddedSource] = GetParam();
    const std::filesystem::path fullPath =
        ::UVE::Tests::RepositoryRoot() / "Engine/Runtime/RHI/Shader/built_in" / relativePath;
    const std::optional<std::string> fileContent = ReadFileToString(fullPath);
    if (!fileContent.has_value()) {
        GTEST_SKIP() << "Could not read " << fullPath
                     << " - this parity test assumes the process's working directory is the "
                        "repository root (run ctest/uve_tests from there to exercise it).";
    }
    EXPECT_EQ(*fileContent, embeddedSource);
}

INSTANTIATE_TEST_SUITE_P(
    AllBuiltInShaders, BuiltInShaderParityTest,
    ::testing::Values(std::pair<std::string_view, std::string_view>{"basic_3d.glsl", BuiltIn::kBasic3DSource},
                       std::pair<std::string_view, std::string_view>{"basic_3d_textured.glsl",
                                                                      BuiltIn::kBasic3DTexturedSource},
                       std::pair<std::string_view, std::string_view>{"fullscreen_quad.glsl",
                                                                      BuiltIn::kFullscreenQuadSource},
                       std::pair<std::string_view, std::string_view>{"procedural_sky.glsl",
                                                                      BuiltIn::kProceduralSkySource},
                       std::pair<std::string_view, std::string_view>{"shadow_depth.glsl",
                                                                      BuiltIn::kShadowDepthSource},
                       std::pair<std::string_view, std::string_view>{"occlusion_proxy.glsl",
                                                                      BuiltIn::kOcclusionProxySource},
                       std::pair<std::string_view, std::string_view>{"lit_shadowed_3d.glsl",
                                                                      BuiltIn::kLitShadowed3DSource},
                       std::pair<std::string_view, std::string_view>{"particle.glsl", BuiltIn::kParticleSource},
                       std::pair<std::string_view, std::string_view>{"decal.glsl", BuiltIn::kDecalSource},
                       std::pair<std::string_view, std::string_view>{"debug_line.glsl",
                                                                      BuiltIn::kDebugLineSource},
                       std::pair<std::string_view, std::string_view>{"debug_point.glsl",
                                                                      BuiltIn::kDebugPointSource},
                       std::pair<std::string_view, std::string_view>{"particle_simulate.glsl",
                                                                      BuiltIn::kParticleSimulateSource},
                       std::pair<std::string_view, std::string_view>{"frustum_cull.glsl",
                                                                      BuiltIn::kFrustumCullSource},
                       std::pair<std::string_view, std::string_view>{"frustum_cull_indirect.glsl",
                                                                      BuiltIn::kFrustumCullIndirectSource},
                       std::pair<std::string_view, std::string_view>{"mesh_skin.glsl",
                                                                      BuiltIn::kMeshSkinSource},
                       std::pair<std::string_view, std::string_view>{"bloom_bright_pass.glsl",
                                                                      BuiltIn::kBloomBrightPassSource},
                       std::pair<std::string_view, std::string_view>{"bloom_downsample.glsl",
                                                                      BuiltIn::kBloomDownsampleSource},
                       std::pair<std::string_view, std::string_view>{"bloom_blur.glsl",
                                                                      BuiltIn::kBloomBlurSource},
                       std::pair<std::string_view, std::string_view>{"fullscreen_copy.glsl",
                                                                      BuiltIn::kFullscreenCopySource},
                       std::pair<std::string_view, std::string_view>{"ssao.glsl", BuiltIn::kSsaoSource},
                       std::pair<std::string_view, std::string_view>{"contact_shadow.glsl",
                                                                      BuiltIn::kContactShadowSource},
                       std::pair<std::string_view, std::string_view>{"ssr.glsl", BuiltIn::kSsrSource},
                        std::pair<std::string_view, std::string_view>{"water.glsl", BuiltIn::kWaterSource},
                       std::pair<std::string_view, std::string_view>{"ui_overlay.glsl",
                                                                      BuiltIn::kUIOverlaySource},
                       std::pair<std::string_view, std::string_view>{"vfx_sprite.glsl",
                                                                      BuiltIn::kVfxSpriteSource},
                       std::pair<std::string_view, std::string_view>{"studio_floor_pbr.glsl",
                                                                      BuiltIn::kStudioFloorPbrSource},
                       std::pair<std::string_view, std::string_view>{"taa_resolve.glsl",
                                                                      BuiltIn::kTaaResolveSource},
                       std::pair<std::string_view, std::string_view>{"wboit_composite.glsl",
                                                                      BuiltIn::kWboitCompositeSource},
                       std::pair<std::string_view, std::string_view>{"rt_trace.glsl",
                                                                      BuiltIn::kRtTraceSource},
                       std::pair<std::string_view, std::string_view>{"hiz_downsample.glsl",
                                                                      BuiltIn::kHiZDownsampleSource}));


// ---------------------------------------------------------------------------
// The instanced lit variant. lit_shadowed_3d.glsl carries both variants behind
// UVE_INSTANCED rather than existing as two files, specifically so the ~240
// lines of lighting cannot drift between an instanced object and a
// non-instanced one. These tests guard the properties that arrangement depends
// on - source-level, because a shading difference between two variants of one
// material is exactly the kind of bug that survives a green test run and is
// then blamed on the artist.
// ---------------------------------------------------------------------------

TEST(InstancedLitVariantTest, TheFragmentStageIsSharedVerbatimBetweenBothVariants) {
    const std::string_view source = BuiltIn::kLitShadowed3DSource;
    const std::size_t fragmentStart = source.find("#ifdef FRAGMENT_SHADER");
    ASSERT_NE(fragmentStart, std::string_view::npos);
    const std::string_view fragmentStage = source.substr(fragmentStart);

    // If UVE_INSTANCED ever appears in the fragment stage, the two variants have begun to shade
    // differently and the single-file arrangement has stopped buying what it was meant to buy.
    EXPECT_EQ(fragmentStage.find("UVE_INSTANCED"), std::string_view::npos)
        << "the instancing define must stay confined to the vertex stage";
}

TEST(InstancedLitVariantTest, BothVariantsComputeWorldSpaceThroughTheSameExpressions) {
    // Whatever supplies them, `model` and `normalMatrix` must be consumed identically - the
    // position through model, the normal through the inverse-transpose, the tangent through
    // model (which IS the correct convention for a surface-parameterization vector, unlike a
    // normal). One spelling of each, outside any #ifdef, is what makes that checkable at all.
    const std::string_view source = BuiltIn::kLitShadowed3DSource;
    EXPECT_NE(source.find("vec4 worldPosition = model * vec4(aPosition, 1.0);"), std::string_view::npos);
    EXPECT_NE(source.find("vWorldNormal = mat3(normalMatrix) * aNormal;"), std::string_view::npos);
    EXPECT_NE(source.find("vWorldTangent = mat3(model) * aTangent.xyz;"), std::string_view::npos);

    // And the non-instanced path must still be driven by the uniforms, not quietly dropped.
    EXPECT_NE(source.find("mat4 model = uModel;"), std::string_view::npos);
    EXPECT_NE(source.find("mat4 normalMatrix = uNormalMatrix;"), std::string_view::npos);
}

TEST(InstancedLitVariantTest, TheInstancedPathIndexesByBaseIndexPlusInstanceId) {
    // gl_InstanceID restarts at zero for every draw, so a batch that is not the first in the
    // frame would read another batch's transforms without the base offset. This is the assertion
    // that would have caught that.
    const std::string_view source = BuiltIn::kLitShadowed3DSource;
    EXPECT_NE(source.find("uInstanceBaseIndex + gl_InstanceID"), std::string_view::npos);
    EXPECT_NE(source.find("layout(std430, binding = 0) readonly buffer InstanceTransformBlock"),
              std::string_view::npos);
    EXPECT_NE(source.find("layout(std430, binding = 1) readonly buffer InstanceNormalTransformBlock"),
              std::string_view::npos);
    EXPECT_NE(source.find("layout(std430, binding = 2) readonly buffer InstanceBaseBlock"),
              std::string_view::npos);
}

} // namespace
} // namespace UVE::Render::Shader::Tests
