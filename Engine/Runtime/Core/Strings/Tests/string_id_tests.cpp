// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "string_id.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Strings::Tests {
namespace {

TEST(StringIdTest, SameText_MintsEqualIdsWithStableRoundTrip) {
    const StringId first("component.transform");
    const StringId second(std::string("component.transform"));
    const std::string_view view = "component.transform";
    const StringId third(view);

    EXPECT_EQ(first, second);
    EXPECT_EQ(first, third);
    EXPECT_FALSE(first != second);
    EXPECT_EQ(first.ToString(), "component.transform");
    EXPECT_STREQ(first.ToCString(), "component.transform");
    // Dedup: one table entry, so all three spellings share one index.
    EXPECT_EQ(first.GetIndex(), second.GetIndex());
    EXPECT_EQ(first.GetIndex(), third.GetIndex());
}

TEST(StringIdTest, DifferentTexts_MintDistinctIds) {
    const StringId transform("component.transform.0_4_a");
    const StringId mesh("component.mesh.0_4_b");

    EXPECT_NE(transform, mesh);
    EXPECT_NE(transform.GetIndex(), mesh.GetIndex());
    EXPECT_EQ(mesh.ToString(), "component.mesh.0_4_b");
}

TEST(StringIdTest, DefaultConstructed_IsTheEmptyString) {
    const StringId empty;
    EXPECT_TRUE(empty.IsEmpty());
    EXPECT_EQ(empty.GetIndex(), 0U);
    EXPECT_EQ(empty.ToString(), "");
    EXPECT_STREQ(empty.ToCString(), "");
    EXPECT_EQ(empty, StringId(""));
    EXPECT_NE(empty, StringId("component.transform"));
}

TEST(StringIdTest, ImplicitConversion_KeepsCallSitesSpellingStrings) {
    // The migration ergonomics: literals, views and owning strings all convert in one step.
    auto takeId = [](StringId id) { return id.ToString(); };
    EXPECT_EQ(takeId("literal.0_4"), "literal.0_4");
    const std::string owned = "owned.0_4";
    EXPECT_EQ(takeId(owned), "owned.0_4");
    const std::string_view view = "view.0_4";
    EXPECT_EQ(takeId(view), "view.0_4");
    EXPECT_EQ(StringId("eq.0_4"), "eq.0_4");
}

TEST(StringIdTest, HashAndStream_SupportMapsAndMessages) {
    const StringId id("hashable.0_4");
    EXPECT_EQ(std::hash<StringId>{}(id), std::hash<std::uint32_t>{}(id.GetIndex()));

    std::unordered_set<StringId> set;
    set.insert(id);
    EXPECT_TRUE(set.contains(StringId("hashable.0_4")));

    std::ostringstream message;
    message << "type " << id;
    EXPECT_EQ(message.str(), "type hashable.0_4");
}

TEST(StringIdTest, ConcurrentInterning_MintsOneIdPerText) {
    constexpr int kThreadCount = 4;
    std::vector<std::uint32_t> indices(static_cast<std::size_t>(kThreadCount), 0U);
    std::vector<std::thread> threads;
    for (int thread = 0; thread < kThreadCount; ++thread) {
        threads.emplace_back([&indices, thread] {
            indices[static_cast<std::size_t>(thread)] =
                StringId("concurrent.0_4").GetIndex();
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    for (int thread = 1; thread < kThreadCount; ++thread) {
        EXPECT_EQ(indices[static_cast<std::size_t>(thread)], indices[0U]);
    }
    EXPECT_EQ(StringId("concurrent.0_4").ToString(), "concurrent.0_4");
}

}  // namespace
}  // namespace UVE::Strings::Tests
