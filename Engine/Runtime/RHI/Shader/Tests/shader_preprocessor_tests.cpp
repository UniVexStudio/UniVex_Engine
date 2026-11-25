// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "shader_preprocessor.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "i_file_system.h"
#include "built_in_shaders.h"

namespace UVE::Render::Shader::Detail::Tests {
namespace {

// A pure in-memory IFileSystem - the preprocessor's own doc comment states it is
// "independently unit-testable against a fake IFileSystem" since it never touches threading or
// GL, so no real mount/directory is needed here.
class FakeFileSystem final : public Asset::IFileSystem {
public:
    Asset::MountHandle MountDirectory(std::string, std::filesystem::path, int) override { return 1; }
    Asset::MountHandle MountBundle(std::string, std::filesystem::path, int) override { return 1; }
    void Unmount(Asset::MountHandle) override {}

    [[nodiscard]] bool HasFile(std::string_view virtualPath) const override {
        return files.find(std::string(virtualPath)) != files.end();
    }

    [[nodiscard]] std::optional<std::vector<std::byte>> ReadFile(std::string_view virtualPath) const override {
        const auto it = files.find(std::string(virtualPath));
        if (it == files.end()) {
            return std::nullopt;
        }
        std::vector<std::byte> bytes(it->second.size());
        std::transform(it->second.begin(), it->second.end(), bytes.begin(),
                        [](char character) { return static_cast<std::byte>(character); });
        return bytes;
    }

    [[nodiscard]] bool WriteFile(std::string_view, const std::vector<std::byte>&) override { return false; }
    [[nodiscard]] std::filesystem::path ResolveRealPath(std::string_view) const override { return {}; }

    void AddFile(std::string virtualPath, std::string content) {
        files.emplace(std::move(virtualPath), std::move(content));
    }

    std::unordered_map<std::string, std::string> files;
};

TEST(ShaderPreprocessorTest, MobileLitMaterialUsesCpuUniformDefaultsAndDesktopKeepsInitializers) {
    FakeFileSystem files;
    const std::string source{BuiltIn::kLitShadowed3DSource};
    const auto mobile = PreprocessShaderSource(files, "", source,
        {{"FRAGMENT_SHADER", "1"}, {"UVE_MOBILE", "1"}});
    const auto desktop = PreprocessShaderSource(files, "", source,
        {{"FRAGMENT_SHADER", "1"}, {"UVE_MOBILE", "0"}});
    ASSERT_TRUE(mobile.success) << mobile.errorMessage;
    ASSERT_TRUE(desktop.success) << desktop.errorMessage;
    EXPECT_NE(mobile.resolvedSource.find("uniform float uNormalScale;"), std::string::npos);
    EXPECT_EQ(mobile.resolvedSource.find("uniform float uNormalScale ="), std::string::npos);
    EXPECT_NE(desktop.resolvedSource.find("uniform float uNormalScale = 1.0;"), std::string::npos);
    EXPECT_NE(mobile.resolvedSource.find("uniform float uAlbedoAlpha;"), std::string::npos);
    EXPECT_NE(desktop.resolvedSource.find("uniform float uAlbedoAlpha = 1.0;"), std::string::npos);
}

TEST(ShaderPreprocessorTest, LeadingBomAndComments_PreserveVersionOrderAndSourceLineMapping) {
    FakeFileSystem fileSystem;
    const std::string source = "\xEF\xBB\xBF// Copyright header\n\n/* module header */\n#version 330 core // authored version\nvoid main() {}\n";

    const PreprocessResult result =
        PreprocessShaderSource(fileSystem, "shaders/versioned.glsl", source, {});

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.resolvedSource.find(std::string("\xEF\xBB\xBF")), std::string::npos);
    const std::size_t versionOffset = result.resolvedSource.find("#version 330 core // authored version");
    const std::size_t lineDirectiveOffset = result.resolvedSource.find("#line 5 0");
    ASSERT_NE(versionOffset, std::string::npos);
    ASSERT_NE(lineDirectiveOffset, std::string::npos);
    EXPECT_LT(versionOffset, lineDirectiveOffset);
    EXPECT_NE(result.resolvedSource.find("void main() {}"), std::string::npos);
}

TEST(ShaderPreprocessorTest, NoFileOnDisk_UsesEmbeddedFallback) {
    FakeFileSystem fileSystem;
    const PreprocessResult result =
        PreprocessShaderSource(fileSystem, "shaders/missing.glsl", "void main() {}\n", {});

    ASSERT_TRUE(result.success);
    EXPECT_NE(result.resolvedSource.find("void main() {}"), std::string::npos);
    EXPECT_TRUE(result.dependencyClosure.empty());
}

TEST(ShaderPreprocessorTest, FileOnDisk_PrefersFileOverEmbeddedFallback) {
    FakeFileSystem fileSystem;
    fileSystem.AddFile("shaders/real.glsl", "void realMain() {}\n");
    const PreprocessResult result =
        PreprocessShaderSource(fileSystem, "shaders/real.glsl", "void embeddedMain() {}\n", {});

    ASSERT_TRUE(result.success);
    EXPECT_NE(result.resolvedSource.find("realMain"), std::string::npos);
    EXPECT_EQ(result.resolvedSource.find("embeddedMain"), std::string::npos);
    ASSERT_EQ(result.dependencyClosure.size(), 1U);
    EXPECT_EQ(result.dependencyClosure[0], "shaders/real.glsl");
}

TEST(ShaderPreprocessorTest, IncludeDirective_ExpandsRecursivelyAndTracksDependencyClosure) {
    FakeFileSystem fileSystem;
    fileSystem.AddFile("shaders/inc/common.glsl", "vec3 commonHelper() { return vec3(1.0); }\n");
    fileSystem.AddFile("shaders/main.glsl", "#include \"shaders/inc/common.glsl\"\nvoid main() {}\n");

    const PreprocessResult result = PreprocessShaderSource(fileSystem, "shaders/main.glsl", "", {});

    ASSERT_TRUE(result.success);
    EXPECT_NE(result.resolvedSource.find("commonHelper"), std::string::npos);
    EXPECT_EQ(result.dependencyClosure.size(), 2U);
    EXPECT_NE(std::find(result.dependencyClosure.begin(), result.dependencyClosure.end(), "shaders/main.glsl"),
              result.dependencyClosure.end());
    EXPECT_NE(
        std::find(result.dependencyClosure.begin(), result.dependencyClosure.end(), "shaders/inc/common.glsl"),
        result.dependencyClosure.end());
}

TEST(ShaderPreprocessorTest, IncludeCycle_FailsCleanlyRatherThanInfiniteLooping) {
    FakeFileSystem fileSystem;
    fileSystem.AddFile("shaders/a.glsl", "#include \"shaders/b.glsl\"\n");
    fileSystem.AddFile("shaders/b.glsl", "#include \"shaders/a.glsl\"\n");

    const PreprocessResult result = PreprocessShaderSource(fileSystem, "shaders/a.glsl", "", {});

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
}

TEST(ShaderPreprocessorTest, MissingIncludeTarget_FailsWithErrorMessage) {
    FakeFileSystem fileSystem;
    fileSystem.AddFile("shaders/main.glsl", "#include \"shaders/does_not_exist.glsl\"\n");

    const PreprocessResult result = PreprocessShaderSource(fileSystem, "shaders/main.glsl", "", {});

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
}

TEST(ShaderPreprocessorTest, IncludedOnceGuard_SameFileIncludedTwice_ExpandsOnlyOnce) {
    FakeFileSystem fileSystem;
    fileSystem.AddFile("shaders/inc/common.glsl", "vec3 commonHelper() { return vec3(1.0); }\n");
    fileSystem.AddFile("shaders/main.glsl",
                           "#include \"shaders/inc/common.glsl\"\n#include \"shaders/inc/common.glsl\"\n");

    const PreprocessResult result = PreprocessShaderSource(fileSystem, "shaders/main.glsl", "", {});

    ASSERT_TRUE(result.success);
    const std::size_t firstOccurrence = result.resolvedSource.find("commonHelper");
    ASSERT_NE(firstOccurrence, std::string::npos);
    const std::size_t secondOccurrence = result.resolvedSource.find("commonHelper", firstOccurrence + 1);
    EXPECT_EQ(secondOccurrence, std::string::npos);
}

TEST(ShaderPreprocessorTest, IfdefDefined_KeepsTrueBranchDropsElseBranch) {
    FakeFileSystem fileSystem;
    constexpr std::string_view source = "#ifdef UVE_TEST_FLAG\nkeep_this();\n#else\ndrop_this();\n#endif\n";

    const PreprocessResult result =
        PreprocessShaderSource(fileSystem, "shaders/x.glsl", std::string(source), {{"UVE_TEST_FLAG", ""}});

    ASSERT_TRUE(result.success);
    EXPECT_NE(result.resolvedSource.find("keep_this"), std::string::npos);
    EXPECT_EQ(result.resolvedSource.find("drop_this"), std::string::npos);
}

TEST(ShaderPreprocessorTest, IfDefinedOr_EitherMacroOpensTheBranch) {
    FakeFileSystem fileSystem;
    constexpr std::string_view source =
        "#if defined(UVE_INSTANCED) || defined(UVE_INDIRECT_INSTANCED)\nkeep_this();\n#else\ndrop_this();\n#endif\n";

    const PreprocessResult neither =
        PreprocessShaderSource(fileSystem, "shaders/x.glsl", std::string(source), {});
    ASSERT_TRUE(neither.success);
    EXPECT_NE(neither.resolvedSource.find("drop_this"), std::string::npos);
    EXPECT_EQ(neither.resolvedSource.find("keep_this"), std::string::npos);

    const PreprocessResult rightOnly = PreprocessShaderSource(
        fileSystem, "shaders/x.glsl", std::string(source), {{"UVE_INDIRECT_INSTANCED", "1"}});
    ASSERT_TRUE(rightOnly.success);
    EXPECT_NE(rightOnly.resolvedSource.find("keep_this"), std::string::npos);
    EXPECT_EQ(rightOnly.resolvedSource.find("drop_this"), std::string::npos);
}

TEST(ShaderPreprocessorTest, IfDefinedAnd_ParenthesisedOperands_BothRequired) {
    FakeFileSystem fileSystem;
    constexpr std::string_view source =
        "#if defined(UVE_A) && (defined(UVE_B) || !defined(UVE_C))\nkeep_this();\n#else\ndrop_this();\n#endif\n";

    const PreprocessResult oneSide =
        PreprocessShaderSource(fileSystem, "shaders/x.glsl", std::string(source), {{"UVE_A", "1"}});
    ASSERT_TRUE(oneSide.success);
    EXPECT_NE(oneSide.resolvedSource.find("keep_this"), std::string::npos);

    const PreprocessResult andFails =
        PreprocessShaderSource(fileSystem, "shaders/x.glsl", std::string(source),
                                  {{"UVE_A", "1"}, {"UVE_C", "1"}});
    ASSERT_TRUE(andFails.success);
    EXPECT_EQ(andFails.resolvedSource.find("keep_this"), std::string::npos); // No UVE_B, and UVE_C kills the !defined term.
    EXPECT_NE(andFails.resolvedSource.find("drop_this"), std::string::npos);
}

TEST(ShaderPreprocessorTest, IfElifElseChain_TakesFirstMatchingBranchOnly) {
    FakeFileSystem fileSystem;
    constexpr std::string_view source =
        "#if defined(UVE_MODE) && UVE_MODE == 2\nbranch_two();\n#elif defined(UVE_MODE)\nbranch_one();\n#else\n"
        "branch_default();\n#endif\n";

    const PreprocessResult defaultBranch = PreprocessShaderSource(fileSystem, "shaders/x.glsl", std::string(source), {});
    ASSERT_TRUE(defaultBranch.success);
    EXPECT_NE(defaultBranch.resolvedSource.find("branch_default"), std::string::npos);
    EXPECT_EQ(defaultBranch.resolvedSource.find("branch_one"), std::string::npos);
    EXPECT_EQ(defaultBranch.resolvedSource.find("branch_two"), std::string::npos);

    const PreprocessResult oneBranch =
        PreprocessShaderSource(fileSystem, "shaders/x.glsl", std::string(source), {{"UVE_MODE", "1"}});
    ASSERT_TRUE(oneBranch.success);
    EXPECT_NE(oneBranch.resolvedSource.find("branch_one"), std::string::npos);
    EXPECT_EQ(oneBranch.resolvedSource.find("branch_two"), std::string::npos);

    const PreprocessResult twoBranch =
        PreprocessShaderSource(fileSystem, "shaders/x.glsl", std::string(source), {{"UVE_MODE", "2"}});
    ASSERT_TRUE(twoBranch.success);
    EXPECT_NE(twoBranch.resolvedSource.find("branch_two"), std::string::npos);
    EXPECT_EQ(twoBranch.resolvedSource.find("branch_one"), std::string::npos);
}

TEST(ShaderPreprocessorTest, IfNumericMacro_EvaluatesIntegerDefinitions) {
    FakeFileSystem fileSystem;
    constexpr std::string_view source =
        "#define UVE_QUALITY 3\n#if UVE_QUALITY > 1 && UVE_QUALITY != 4\nhigh();\n#else\nlow();\n#endif\n"
        "#if 0\nnever();\n#endif\n";

    const PreprocessResult result = PreprocessShaderSource(fileSystem, "shaders/x.glsl", std::string(source), {});

    ASSERT_TRUE(result.success);
    EXPECT_NE(result.resolvedSource.find("high"), std::string::npos);
    EXPECT_EQ(result.resolvedSource.find("low"), std::string::npos);
    EXPECT_EQ(result.resolvedSource.find("never"), std::string::npos);
}

TEST(ShaderPreprocessorTest, MalformedIfExpression_FailsCleanly) {
    FakeFileSystem fileSystem;
    const PreprocessResult result =
        PreprocessShaderSource(fileSystem, "shaders/x.glsl", "#if defined(\nvoid main() {}\n#endif\n", {});

    EXPECT_FALSE(result.success);
}

TEST(ShaderPreprocessorTest, UnmatchedElif_FailsCleanly) {
    FakeFileSystem fileSystem;
    const PreprocessResult result =
        PreprocessShaderSource(fileSystem, "shaders/x.glsl", "#elif defined(UVE_A)\nvoid main() {}\n", {});

    EXPECT_FALSE(result.success);
}

TEST(ShaderPreprocessorTest, IfndefUndefined_KeepsTrueBranch) {
    FakeFileSystem fileSystem;
    constexpr std::string_view source = "#ifndef UVE_TEST_UNDEFINED_FLAG\nkeep_this();\n#else\ndrop_this();\n#endif\n";

    const PreprocessResult result = PreprocessShaderSource(fileSystem, "shaders/x.glsl", std::string(source), {});

    ASSERT_TRUE(result.success);
    EXPECT_NE(result.resolvedSource.find("keep_this"), std::string::npos);
    EXPECT_EQ(result.resolvedSource.find("drop_this"), std::string::npos);
}

TEST(ShaderPreprocessorTest, UnmatchedEndif_FailsCleanly) {
    FakeFileSystem fileSystem;
    const PreprocessResult result =
        PreprocessShaderSource(fileSystem, "shaders/x.glsl", "#endif\nvoid main() {}\n", {});

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMessage.empty());
}

TEST(ShaderPreprocessorTest, UnmatchedIfdefAtEndOfFile_DoesNotCrash) {
    // Only unmatched #else/#endif are documented as hard failures (PreprocessShaderSource()'s
    // own doc comment); an #ifdef left open at end-of-file has no matching #else/#endif to ever
    // trip that check, so this is a deliberately-not-hardened edge case, not a promised failure -
    // the only contract this asserts is "never crashes."
    FakeFileSystem fileSystem;
    const PreprocessResult result =
        PreprocessShaderSource(fileSystem, "shaders/x.glsl", "#ifdef UVE_SOMETHING\nvoid main() {}\n", {});
    static_cast<void>(result);
    SUCCEED();
}

TEST(ShaderPreprocessorTest, MacroSubstitution_IsTokenBoundaryAware) {
    FakeFileSystem fileSystem;
    // UVE_VAL must substitute inside "x = UVE_VAL;" but the definition must NOT bleed into the
    // longer identifier "UVE_VALUE" elsewhere in the source.
    constexpr std::string_view source = "int x = UVE_VAL;\nint UVE_VALUE = 2;\n";

    const PreprocessResult result =
        PreprocessShaderSource(fileSystem, "shaders/x.glsl", std::string(source), {{"UVE_VAL", "42"}});

    ASSERT_TRUE(result.success);
    EXPECT_NE(result.resolvedSource.find("int x = 42;"), std::string::npos);
    EXPECT_NE(result.resolvedSource.find("int UVE_VALUE = 2;"), std::string::npos);
}

TEST(ShaderPreprocessorTest, DefineThenUndef_MacroNoLongerSubstitutedAfterUndef) {
    FakeFileSystem fileSystem;
    constexpr std::string_view source = "#define UVE_LOCAL 1\nfirst = UVE_LOCAL;\n#undef UVE_LOCAL\nsecond = UVE_LOCAL;\n";

    const PreprocessResult result = PreprocessShaderSource(fileSystem, "shaders/x.glsl", std::string(source), {});

    ASSERT_TRUE(result.success);
    EXPECT_NE(result.resolvedSource.find("first = 1;"), std::string::npos);
    EXPECT_NE(result.resolvedSource.find("second = UVE_LOCAL;"), std::string::npos);
}

TEST(ShaderPreprocessorTest, EmbeddedFallback_FileIndexTableHasSingleEntry) {
    FakeFileSystem fileSystem;
    const PreprocessResult result =
        PreprocessShaderSource(fileSystem, "shaders/missing.glsl", "void main() {}\n", {});

    ASSERT_TRUE(result.success);
    ASSERT_FALSE(result.fileIndexTable.empty());
    EXPECT_EQ(result.fileIndexTable[0], "shaders/missing.glsl (embedded fallback)");
}

// NOTE: the FNV-1a primitive these tests pinned (Detail::ComputeFnv1aHash) was deleted by the
// 1.7 hashing migration — ShaderManager now calls Utilities::HashString directly, and the
// determinism/difference/empty properties are pinned with known-answer vectors in
// Test/Core/Utilities/hash_uve_tests.cpp instead.

} // namespace
} // namespace UVE::Render::Shader::Detail::Tests
