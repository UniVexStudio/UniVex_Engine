// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "editor_entity_diff.h"

namespace UVE::Editor {
namespace {

using Kind = EntityDiffRow::Kind;

// A root "Prop" (collider) with a child "Wheel" (transform), as the scene serializer writes it.
constexpr const char* kSaved = R"({"entities":[
  {"localId":0,"components":{"NameComponent":{"name":"Prop"},"HierarchyComponent":{"parentLocalId":-1},
                             "ColliderComponent":{"friction":0.0,"disabled":false}}},
  {"localId":1,"components":{"NameComponent":{"name":"Wheel"},"HierarchyComponent":{"parentLocalId":0},
                             "TransformComponent":{"localPosition":[0.0,0.0,0.0]}}}
]})";

[[nodiscard]] const EntityDiffRow* Find(const std::vector<EntityDiffRow>& rows, const Kind kind,
                                              const std::string& object, const std::string& what) {
    for (const EntityDiffRow& row : rows) {
        if (row.kind == kind && row.object == object && row.what == what) {
            return &row;
        }
    }
    return nullptr;
}

TEST(EditorEntityDiffTest, SamePayload_HasNoDifferences) {
    EXPECT_TRUE(DiffEntityPayloads(kSaved, kSaved).empty());
}

TEST(EditorEntityDiffTest, RootRenamedAfterTheFile_IsNotADifference) {
    const std::string current = R"({"entities":[
      {"localId":0,"components":{"NameComponent":{"name":"Crate"},"HierarchyComponent":{"parentLocalId":-1},
                                 "ColliderComponent":{"friction":0.0,"disabled":false}}},
      {"localId":1,"components":{"NameComponent":{"name":"Wheel"},"HierarchyComponent":{"parentLocalId":0},
                                 "TransformComponent":{"localPosition":[0.0,0.0,0.0]}}}]})";
    EXPECT_TRUE(DiffEntityPayloads(kSaved, current).empty());
}

TEST(EditorEntityDiffTest, ChangedProperty_AddedObject_RemovedComponent) {
    // friction changed, the Wheel lost its transform and gained a light, a Lamp was added under the root.
    const std::string current = R"({"entities":[
      {"localId":0,"components":{"NameComponent":{"name":"Prop"},"HierarchyComponent":{"parentLocalId":-1},
                                 "ColliderComponent":{"friction":0.5,"disabled":false}}},
      {"localId":1,"components":{"NameComponent":{"name":"Wheel"},"HierarchyComponent":{"parentLocalId":0},
                                 "LightComponent":{"intensity":2.0}}},
      {"localId":2,"components":{"NameComponent":{"name":"Lamp"},"HierarchyComponent":{"parentLocalId":0}}}]})";
    const std::vector<EntityDiffRow> rows = DiffEntityPayloads(kSaved, current);
    const EntityDiffRow* friction = Find(rows, Kind::Changed, "(root)", "Collider.friction");
    ASSERT_NE(friction, nullptr);
    EXPECT_EQ(friction->before, "0");
    EXPECT_EQ(friction->after, "0.5");
    EXPECT_NE(Find(rows, Kind::Removed, "(root)/Wheel", "Transform"), nullptr);
    EXPECT_NE(Find(rows, Kind::Added, "(root)/Wheel", "Light"), nullptr);
    EXPECT_NE(Find(rows, Kind::Added, "(root)/Lamp", ""), nullptr);
    EXPECT_EQ(rows.size(), 4U);
}

TEST(EditorEntityDiffTest, RemovedObject_AndSameNamedSiblings) {
    const std::string saved = R"({"entities":[
      {"localId":0,"components":{"NameComponent":{"name":"Car"},"HierarchyComponent":{"parentLocalId":-1}}},
      {"localId":1,"components":{"NameComponent":{"name":"Wheel"},"HierarchyComponent":{"parentLocalId":0}}},
      {"localId":2,"components":{"NameComponent":{"name":"Wheel"},"HierarchyComponent":{"parentLocalId":0}}}]})";
    const std::string current = R"({"entities":[
      {"localId":0,"components":{"NameComponent":{"name":"Car"},"HierarchyComponent":{"parentLocalId":-1}}},
      {"localId":1,"components":{"NameComponent":{"name":"Wheel"},"HierarchyComponent":{"parentLocalId":0}}}]})";
    const std::vector<EntityDiffRow> rows = DiffEntityPayloads(saved, current);
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].kind, Kind::Removed);
    EXPECT_EQ(rows[0].object, "(root)/Wheel #2");
}

TEST(EditorEntityDiffTest, UnreadablePayloads_GiveNoRows) {
    EXPECT_TRUE(DiffEntityPayloads("not json", "{]").empty());
}

} // namespace
} // namespace UVE::Editor
