// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "shader_program.h"

#include <algorithm>
#include <memory>
#include <thread>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "asset_bundle.h"
#include "file_system.h"
#include "event_system.h"
#include "matrix4x4.h"
#include "vector3.h"
#include "null_render_device.h"
#include "built_in_shaders.h"
#include "shader_manager.h"
#include "thread_pool.h"

namespace UVE::Render::Shader::Tests {
namespace {

// Bounded busy-poll (never a fixed sleep), matching this codebase's established
// WaitForTerminalState pattern (see tests/asset/asset_manager_uve_tests.cpp) - readiness only
// advances when Update() is called, since ShaderManager's real compile step is
// main-thread-driven, not automatic.
[[nodiscard]] bool WaitUntilProgramReady(ShaderManager& shaderManager, const ShaderProgram& program,
                                             int maxIterations = 200000) {
    for (int iteration = 0; iteration < maxIterations; ++iteration) {
        shaderManager.Update(0.0);
        if (program.IsReady()) {
            return true;
        }
        std::this_thread::yield();
    }
    return false;
}

class ShaderProgramTest : public ::testing::Test {
protected:
    void SetUp() override {
        assetBundle = std::make_unique<Asset::AssetBundle>();
        fileSystem = std::make_unique<Asset::FileSystem>(*assetBundle);
        threadPool = std::make_unique<Threading::ThreadPool>(1);
        eventSystem = std::make_unique<Events::EventSystem>();
        renderDevice = std::make_unique<NullRenderDevice>();
        shaderManager = std::make_unique<ShaderManager>(*threadPool, *eventSystem, *renderDevice, *fileSystem,
                                                             ShaderManagerConfig{});
    }

    [[nodiscard]] std::shared_ptr<ShaderProgram> MakeReadyProgram() {
        ShaderProgramDesc desc;
        desc.virtualFilePath = std::string(BuiltIn::kBasic3DVirtualPath);
        desc.embeddedFallbackSourceCode = std::string(BuiltIn::kBasic3DSource);
        desc.vertexLayout = {VertexAttribute{"POSITION", VertexAttributeFormat::Float3, 0}};
        desc.vertexStride = 3U * static_cast<std::uint32_t>(sizeof(float));
        std::shared_ptr<ShaderProgram> program = shaderManager->CreateProgram(desc);
        EXPECT_TRUE(WaitUntilProgramReady(*shaderManager, *program));
        return program;
    }

    std::unique_ptr<Asset::IAssetBundle> assetBundle;
    std::unique_ptr<Asset::IFileSystem> fileSystem;
    std::unique_ptr<Threading::IThreadPool> threadPool;
    std::unique_ptr<Events::IEventSystem> eventSystem;
    std::unique_ptr<IRenderDevice> renderDevice;
    std::unique_ptr<ShaderManager> shaderManager;
};

TEST_F(ShaderProgramTest, CreateProgram_EmbeddedFallback_BecomesReadyAndValid) {
    const std::shared_ptr<ShaderProgram> program = MakeReadyProgram();
    ASSERT_TRUE(program->IsReady());
    EXPECT_TRUE(program->IsValid());
    EXPECT_NE(program->GetPipelineHandle(), kInvalidPipelineHandle);
}

TEST_F(ShaderProgramTest, FindUniform_UnknownName_ReturnsNullopt) {
    const std::shared_ptr<ShaderProgram> program = MakeReadyProgram();
    EXPECT_FALSE(program->FindUniform("uDefinitelyNotDeclared").has_value());
}

TEST_F(ShaderProgramTest, ApplyTo_InvalidProgram_LogsWarningAndSkipsBindWithoutCrashing) {
    // A freshly-constructed program (never ready, never linked) is !IsValid() - ApplyTo()
    // must be a safe no-op rather than binding a stale/invalid pipeline handle.
    ShaderProgramDesc desc;
    desc.virtualFilePath = "shaders/definitely_missing_and_broken.glsl";
    desc.embeddedFallbackSourceCode = "not glsl at all {{{";
    const std::shared_ptr<ShaderProgram> program = shaderManager->CreateProgram(desc);
    ASSERT_FALSE(program->IsValid());

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    program->ApplyTo(*commandBuffer);
    commandBuffer->EndRenderPass();
    SUCCEED();
}

TEST_F(ShaderProgramTest, ApplyTo_BindsPipelineThenFlushesEachPendingUniformKind) {
    const std::shared_ptr<ShaderProgram> program = MakeReadyProgram();

    program->SetMatrix4x4("uModel", Math::Matrix4x4::Identity());
    program->SetVector3("uColor", Math::Vector3{0.0F, 0.83137255F, 1.0F});
    program->SetFloat("uSomeFloat", 3.5F);
    program->SetInt("uSomeInt", 7);
    program->SetBool("uSomeBool", true);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    program->ApplyTo(*commandBuffer);
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));

    auto* const nullDevice = static_cast<NullRenderDevice*>(renderDevice.get());
    const std::vector<RecordedCommand>& recorded = nullDevice->GetLastSubmittedCommands();

    ASSERT_GE(recorded.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<BeginRenderPassCommand>(recorded[0]));
    ASSERT_TRUE(std::holds_alternative<BindPipelineCommand>(recorded[1]));
    EXPECT_EQ(std::get<BindPipelineCommand>(recorded[1]).pipeline, program->GetPipelineHandle());

    bool foundMatrix = false;
    bool foundVector3 = false;
    bool foundFloat = false;
    bool foundInt = false;
    bool foundBool = false;
    for (const RecordedCommand& command : recorded) {
        if (const auto* matrixCommand = std::get_if<SetUniformMatrix4x4Command>(&command)) {
            if (matrixCommand->name == "uModel") {
                foundMatrix = true;
            }
        } else if (const auto* vectorCommand = std::get_if<SetUniformVector3Command>(&command)) {
            if (vectorCommand->name == "uColor") {
                EXPECT_FLOAT_EQ(vectorCommand->value.y, 0.83137255F);
                foundVector3 = true;
            }
        } else if (const auto* floatCommand = std::get_if<SetUniformFloatCommand>(&command)) {
            if (floatCommand->name == "uSomeFloat") {
                EXPECT_FLOAT_EQ(floatCommand->value, 3.5F);
                foundFloat = true;
            }
        } else if (const auto* intCommand = std::get_if<SetUniformIntCommand>(&command)) {
            if (intCommand->name == "uSomeInt") {
                EXPECT_EQ(intCommand->value, 7);
                foundInt = true;
            }
        } else if (const auto* boolCommand = std::get_if<SetUniformBoolCommand>(&command)) {
            if (boolCommand->name == "uSomeBool") {
                EXPECT_TRUE(boolCommand->value);
                foundBool = true;
            }
        }
    }
    EXPECT_TRUE(foundMatrix);
    EXPECT_TRUE(foundVector3);
    EXPECT_TRUE(foundFloat);
    EXPECT_TRUE(foundInt);
    EXPECT_TRUE(foundBool);
}

TEST_F(ShaderProgramTest, SetFloat_CalledTwiceForSameName_LastValueWins) {
    const std::shared_ptr<ShaderProgram> program = MakeReadyProgram();
    program->SetFloat("uRepeated", 1.0F);
    program->SetFloat("uRepeated", 9.0F);

    std::unique_ptr<ICommandBuffer> commandBuffer = renderDevice->CreateCommandBuffer();
    commandBuffer->BeginRenderPass(RenderPassDesc{});
    program->ApplyTo(*commandBuffer);
    commandBuffer->EndRenderPass();
    renderDevice->Submit(std::move(commandBuffer));

    auto* const nullDevice = static_cast<NullRenderDevice*>(renderDevice.get());
    int matchCount = 0;
    for (const RecordedCommand& command : nullDevice->GetLastSubmittedCommands()) {
        if (const auto* floatCommand = std::get_if<SetUniformFloatCommand>(&command)) {
            if (floatCommand->name == "uRepeated") {
                EXPECT_FLOAT_EQ(floatCommand->value, 9.0F);
                ++matchCount;
            }
        }
    }
    EXPECT_EQ(matchCount, 1);
}

class RetainingCommandBufferStub final : public ICommandBuffer {
public:
    void BeginRenderPass(const RenderPassDesc&) override {}
    void EndRenderPass() override {}
    void BindPipeline(const PipelineHandle) override { ++pipelineBinds; }
    void BindVertexBuffer(const BufferHandle, const std::uint32_t) override {}
    void BindIndexBuffer(const BufferHandle) override {}
    void BindTexture(const TextureHandle, const std::uint32_t) override {}
    void BindSampler(const SamplerHandle, const std::uint32_t) override {}
    void BindUniformBuffer(const BufferHandle, const std::uint32_t) override {}
    void BindStorageBuffer(const BufferHandle, const std::uint32_t) override {}
    void SetUniformFloat(const std::string_view name, const float) override {
        pushedUniforms.emplace_back(name);
    }
    void SetUniformInt(const std::string_view name, const std::int32_t) override {
        pushedUniforms.emplace_back(name);
    }
    void SetUniformBool(const std::string_view name, const bool) override {
        pushedUniforms.emplace_back(name);
    }
    void SetUniformVector2(const std::string_view name, const Math::Vector2&) override {
        pushedUniforms.emplace_back(name);
    }
    void SetUniformVector3(const std::string_view name, const Math::Vector3&) override {
        pushedUniforms.emplace_back(name);
    }
    void SetUniformVector4(const std::string_view name, const Math::Vector4&) override {
        pushedUniforms.emplace_back(name);
    }
    void SetUniformMatrix4x4(const std::string_view name, const Math::Matrix4x4&) override {
        pushedUniforms.emplace_back(name);
    }
    void DrawIndexed(const std::uint32_t, const std::uint32_t, const std::uint32_t) override {}
    void Draw(const std::uint32_t, const std::uint32_t) override {}
    void DrawIndexedIndirect(const BufferHandle, const std::uint64_t) override {}
    void Dispatch(const std::uint32_t, const std::uint32_t, const std::uint32_t) override {}
    [[nodiscard]] bool RetainsUniformStatePerPipeline() const noexcept override { return true; }

    std::size_t pipelineBinds = 0U;
    std::vector<std::string> pushedUniforms;
};

TEST_F(ShaderProgramTest, ApplyTo_RetainingBufferFlushesOnlyDirtyUniformsOnSubsequentDraws) {
    const std::shared_ptr<ShaderProgram> program = MakeReadyProgram();
    RetainingCommandBufferStub firstBuffer;

    program->SetFloat("uStaticParam", 2.0F);
    program->SetFloat("uDynamicParam", 1.0F);
    program->ApplyTo(firstBuffer);
    ASSERT_EQ(firstBuffer.pushedUniforms.size(), 2U);

    // Updating only one uniform pushes strictly that dirty uniform on the same command buffer.
    firstBuffer.pushedUniforms.clear();
    program->SetFloat("uDynamicParam", 4.0F);
    program->ApplyTo(firstBuffer);
    ASSERT_EQ(firstBuffer.pushedUniforms.size(), 1U);
    EXPECT_EQ(firstBuffer.pushedUniforms[0], "uDynamicParam");

    // Re-setting an identical value marks nothing dirty and pushes zero uniforms.
    firstBuffer.pushedUniforms.clear();
    program->SetFloat("uDynamicParam", 4.0F);
    program->ApplyTo(firstBuffer);
    EXPECT_TRUE(firstBuffer.pushedUniforms.empty());

    // Switching to a new command buffer flushes the full uniform state cleanly even if memory is reused.
    RetainingCommandBufferStub secondBuffer;
    EXPECT_NE(firstBuffer.GetInstanceId(), secondBuffer.GetInstanceId());
    program->ApplyTo(secondBuffer);
    EXPECT_EQ(secondBuffer.pushedUniforms.size(), 2U);
}

} // namespace
} // namespace UVE::Render::Shader::Tests
