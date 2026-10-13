// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/strings/string_id_uve.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Strings::Tests {
namespace {

TEST(StringIdUVETest, SameText_MintsEqualIdsWithStableRoundTrip) {
    const StringIdUVE first("component.transform");
    const StringIdUVE second(std::string("component.transform"));
    const std::string_view view = "component.transform";
    const StringIdUVE third(view);

    EXPECT_EQ(first, second);
    EXPECT_EQ(first, third);
    EXPECT_FALSE(first != second);
    EXPECT_EQ(first.ToStringUVE(), "component.transform");
    EXPECT_STREQ(first.ToCStringUVE(), "component.transform");
    // Dedup: one table entry, so all three spellings share one index.
    EXPECT_EQ(first.GetIndexUVE(), second.GetIndexUVE());
    EXPECT_EQ(first.GetIndexUVE(), third.GetIndexUVE());
}

TEST(StringIdUVETest, DifferentTexts_MintDistinctIds) {
    const StringIdUVE transform("component.transform.0_4_a");
    const StringIdUVE mesh("component.mesh.0_4_b");

    EXPECT_NE(transform, mesh);
    EXPECT_NE(transform.GetIndexUVE(), mesh.GetIndexUVE());
    EXPECT_EQ(mesh.ToStringUVE(), "component.mesh.0_4_b");
}

TEST(StringIdUVETest, DefaultConstructed_IsTheEmptyString) {
    const StringIdUVE empty;
    EXPECT_TRUE(empty.IsEmptyUVE());
    EXPECT_EQ(empty.GetIndexUVE(), 0U);
    EXPECT_EQ(empty.ToStringUVE(), "");
    EXPECT_STREQ(empty.ToCStringUVE(), "");
    EXPECT_EQ(empty, StringIdUVE(""));
    EXPECT_NE(empty, StringIdUVE("component.transform"));
}

TEST(StringIdUVETest, ImplicitConversion_KeepsCallSitesSpellingStrings) {
    // The migration ergonomics: literals, views and owning strings all convert in one step.
    auto takeId = [](StringIdUVE id) { return id.ToStringUVE(); };
    EXPECT_EQ(takeId("literal.0_4"), "literal.0_4");
    const std::string owned = "owned.0_4";
    EXPECT_EQ(takeId(owned), "owned.0_4");
    const std::string_view view = "view.0_4";
    EXPECT_EQ(takeId(view), "view.0_4");
    EXPECT_EQ(StringIdUVE("eq.0_4"), "eq.0_4");
}

TEST(StringIdUVETest, HashAndStream_SupportMapsAndMessages) {
    const StringIdUVE id("hashable.0_4");
    EXPECT_EQ(std::hash<StringIdUVE>{}(id), std::hash<std::uint32_t>{}(id.GetIndexUVE()));

    std::unordered_set<StringIdUVE> set;
    set.insert(id);
    EXPECT_TRUE(set.contains(StringIdUVE("hashable.0_4")));

    std::ostringstream message;
    message << "type " << id;
    EXPECT_EQ(message.str(), "type hashable.0_4");
}

TEST(StringIdUVETest, ConcurrentInterning_MintsOneIdPerText) {
    constexpr int kThreadCount = 4;
    std::vector<std::uint32_t> indices(static_cast<std::size_t>(kThreadCount), 0U);
    std::vector<std::thread> threads;
    for (int thread = 0; thread < kThreadCount; ++thread) {
        threads.emplace_back([&indices, thread] {
            indices[static_cast<std::size_t>(thread)] =
                StringIdUVE("concurrent.0_4").GetIndexUVE();
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    for (int thread = 1; thread < kThreadCount; ++thread) {
        EXPECT_EQ(indices[static_cast<std::size_t>(thread)], indices[0U]);
    }
    EXPECT_EQ(StringIdUVE("concurrent.0_4").ToStringUVE(), "concurrent.0_4");
}

}  // namespace
}  // namespace UVE::Strings::Tests
