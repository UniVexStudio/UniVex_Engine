#include "monitor_info_validation.h"

#include <limits>

#include <gtest/gtest.h>

namespace UVE::Window::Tests {

TEST(MonitorInfoValidationTest, ValidatesSignedDimensionsBeforeUnsignedConversionAtomically) {
    std::uint32_t width = 77U;
    std::uint32_t height = 88U;
    EXPECT_FALSE(ValidateMonitorDimensions(0, 1080, width, height));
    EXPECT_EQ(width, 77U);
    EXPECT_EQ(height, 88U);
    EXPECT_FALSE(ValidateMonitorDimensions(1920, -1, width, height));
    EXPECT_EQ(width, 77U);
    EXPECT_EQ(height, 88U);
    EXPECT_TRUE(ValidateMonitorDimensions(std::numeric_limits<int>::max(),
                                             std::numeric_limits<int>::max(), width, height));
    EXPECT_EQ(width, static_cast<std::uint32_t>(std::numeric_limits<int>::max()));
    EXPECT_EQ(height, static_cast<std::uint32_t>(std::numeric_limits<int>::max()));
}

TEST(MonitorInfoValidationTest, AcceptsValidSnapshotWithAtMostOnePrimary) {
    EXPECT_TRUE(ValidateMonitorInfo({"Primary", 1920U, 1080U, true}));
    EXPECT_TRUE(ValidateMonitorSnapshot({
        {"Primary", 1920U, 1080U, true}, {"Secondary", 1280U, 1024U, false}}));
    EXPECT_TRUE(ValidateMonitorSnapshot({}));
}

TEST(MonitorInfoValidationTest, RejectsEmbeddedNulMonitorName) {
    const std::string nameWithNul{"Monitor\0Name", 12U};
    EXPECT_FALSE(ValidateMonitorInfo({nameWithNul, 1920U, 1080U, false}));
}

TEST(MonitorInfoValidationTest, RejectsMonitorSnapshotBeyondBoundedEntryCap) {
    std::vector<MonitorInfo> monitors;
    monitors.resize(kMaximumMonitorSnapshotEntries + 1U,
                    MonitorInfo{"Monitor", 1920U, 1080U, false});
    EXPECT_FALSE(ValidateMonitorSnapshot(monitors));
}

TEST(MonitorInfoValidationTest, RejectsInvalidEntryAndDuplicatePrimary) {
    EXPECT_FALSE(ValidateMonitorInfo({"", 1920U, 1080U, true}));
    EXPECT_FALSE(ValidateMonitorInfo({"Primary", 0U, 1080U, true}));
    EXPECT_FALSE(ValidateMonitorSnapshot({
        {"Primary", 1920U, 1080U, true}, {"Secondary", 1280U, 720U, true}}));
}

} // namespace UVE::Window::Tests
