// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "log_format.h"

#include <gtest/gtest.h>

namespace UVE::Debug::Tests {
namespace {

TEST(LogFormatTest, NoArguments_ReturnsLiteralString) {
    EXPECT_EQ(FormatLogMessage("hello world"), "hello world");
}

TEST(LogFormatTest, SingleIntegerArgument_Substituted) {
    EXPECT_EQ(FormatLogMessage("value={}", 42), "value=42");
}

TEST(LogFormatTest, MultipleMixedArguments_SubstitutedInOrder) {
    EXPECT_EQ(FormatLogMessage("{} and {} and {}", 1, "two", 3.5), "1 and two and 3.5");
}

TEST(LogFormatTest, FloatingPointPrecisionSpec_Formatted) {
    EXPECT_EQ(FormatLogMessage("pi={:.2f}", 3.14159), "pi=3.14");
}

} // namespace
} // namespace UVE::Debug::Tests
