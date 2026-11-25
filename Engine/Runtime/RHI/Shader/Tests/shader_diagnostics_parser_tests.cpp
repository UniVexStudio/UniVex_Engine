// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "shader_diagnostics_parser.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Render::Shader::Detail::Tests {
namespace {

TEST(ShaderDiagnosticsParserTest, MesaFormatWithColumn_ParsesLineAndMessage) {
    const std::vector<std::string> fileIndexTable = {"shaders/main.glsl"};
    const std::vector<ShaderCompileError> diagnostics =
        ParseGlInfoLog("0:12(5): error: 'foo' undeclared", fileIndexTable);

    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics[0].filePath, "shaders/main.glsl");
    EXPECT_EQ(diagnostics[0].lineNumber, 12U);
    EXPECT_EQ(diagnostics[0].columnNumber, 5U);
    EXPECT_NE(diagnostics[0].message.find("foo"), std::string::npos);
    EXPECT_FALSE(diagnostics[0].isWarning);
}

TEST(ShaderDiagnosticsParserTest, MesaFormatWithoutColumn_ParsesLineAndMessage) {
    const std::vector<std::string> fileIndexTable = {"shaders/main.glsl"};
    const std::vector<ShaderCompileError> diagnostics =
        ParseGlInfoLog("0:7: error: syntax error", fileIndexTable);

    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics[0].filePath, "shaders/main.glsl");
    EXPECT_EQ(diagnostics[0].lineNumber, 7U);
}

TEST(ShaderDiagnosticsParserTest, NvidiaFormat_ParsesLineAndMessage) {
    const std::vector<std::string> fileIndexTable = {"shaders/main.glsl"};
    const std::vector<ShaderCompileError> diagnostics =
        ParseGlInfoLog("ERROR: 0:12: 'foo' undeclared identifier", fileIndexTable);

    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics[0].filePath, "shaders/main.glsl");
    EXPECT_EQ(diagnostics[0].lineNumber, 12U);
}

TEST(ShaderDiagnosticsParserTest, MultipleLines_ProducesOneDiagnosticPerLineInOrder) {
    const std::vector<std::string> fileIndexTable = {"shaders/main.glsl"};
    const std::vector<ShaderCompileError> diagnostics =
        ParseGlInfoLog("0:3: error: first problem\n0:9: error: second problem\n", fileIndexTable);

    ASSERT_EQ(diagnostics.size(), 2U);
    EXPECT_EQ(diagnostics[0].lineNumber, 3U);
    EXPECT_EQ(diagnostics[1].lineNumber, 9U);
}

TEST(ShaderDiagnosticsParserTest, UnrecognizedFormat_StillProducesDiagnosticWithZeroLineNumber) {
    const std::vector<std::string> fileIndexTable = {"shaders/main.glsl"};
    const std::vector<ShaderCompileError> diagnostics =
        ParseGlInfoLog("this driver emitted something completely unstructured", fileIndexTable);

    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_EQ(diagnostics[0].lineNumber, 0U);
    EXPECT_NE(diagnostics[0].message.find("unstructured"), std::string::npos);
}

TEST(ShaderDiagnosticsParserTest, OutOfRangeFileIndex_LeavesFilePathEmptyRatherThanCrashing) {
    const std::vector<std::string> fileIndexTable = {"shaders/main.glsl"};
    // Source string index 5 does not exist in the (single-entry) table.
    const std::vector<ShaderCompileError> diagnostics = ParseGlInfoLog("5:12: error: oops", fileIndexTable);

    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_TRUE(diagnostics[0].filePath.empty());
    EXPECT_EQ(diagnostics[0].lineNumber, 12U);
}

TEST(ShaderDiagnosticsParserTest, EmptyFileIndexTable_LeavesFilePathEmptyRatherThanCrashing) {
    const std::vector<ShaderCompileError> diagnostics = ParseGlInfoLog("0:1: error: oops", {});

    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_TRUE(diagnostics[0].filePath.empty());
}

TEST(ShaderDiagnosticsParserTest, EmptyLog_ProducesNoDiagnostics) {
    EXPECT_TRUE(ParseGlInfoLog("", {"shaders/main.glsl"}).empty());
}

TEST(ShaderDiagnosticsParserTest, WarningKeyword_MarksDiagnosticAsWarningNotError) {
    const std::vector<std::string> fileIndexTable = {"shaders/main.glsl"};
    const std::vector<ShaderCompileError> diagnostics =
        ParseGlInfoLog("0:4: warning: unused variable 'x'", fileIndexTable);

    ASSERT_EQ(diagnostics.size(), 1U);
    EXPECT_TRUE(diagnostics[0].isWarning);
}

} // namespace
} // namespace UVE::Render::Shader::Detail::Tests
