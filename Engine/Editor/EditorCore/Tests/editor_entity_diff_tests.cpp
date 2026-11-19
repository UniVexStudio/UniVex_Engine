// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "editor_entity_diff.h"

namespace UVE::Editor {
namespace {

using KindUVE = EntityDiffRowUVE::KindUVE;

// A root "Prop" (collider) with a child "Wheel" (transform), as the scene serializer writes it.
constexpr const char* kSavedUVE = R"({"entities":[
  {"localId":0,"components":{"NameComponentUVE":{"name":"Prop"},"HierarchyComponentUVE":{"parentLocalId":-1},
                             "ColliderComponentUVE":{"friction":0.0,"disabled":false}}},
  {"localId":1,"components":{"NameComponentUVE":{"name":"Wheel"},"HierarchyComponentUVE":{"parentLocalId":0},
                             "TransformComponentUVE":{"localPosition":[0.0,0.0,0.0]}}}
]})";

[[nodiscard]] const EntityDiffRowUVE* FindUVE(const std::vector<EntityDiffRowUVE>& rows, const KindUVE kind,
                                              const std::string& object, const std::string& what) {
    for (const EntityDiffRowUVE& row : rows) {
        if (row.kind == kind && row.object == object && row.what == what) {
            return &row;
        }
    }
    return nullptr;
}

TEST(EditorEntityDiffUVETest, SamePayload_HasNoDifferences) {
    EXPECT_TRUE(DiffEntityPayloadsUVE(kSavedUVE, kSavedUVE).empty());
}

TEST(EditorEntityDiffUVETest, RootRenamedAfterTheFile_IsNotADifference) {
    const std::string current = R"({"entities":[
      {"localId":0,"components":{"NameComponentUVE":{"name":"Crate"},"HierarchyComponentUVE":{"parentLocalId":-1},
                                 "ColliderComponentUVE":{"friction":0.0,"disabled":false}}},
      {"localId":1,"components":{"NameComponentUVE":{"name":"Wheel"},"HierarchyComponentUVE":{"parentLocalId":0},
                                 "TransformComponentUVE":{"localPosition":[0.0,0.0,0.0]}}}]})";
    EXPECT_TRUE(DiffEntityPayloadsUVE(kSavedUVE, current).empty());
}

TEST(EditorEntityDiffUVETest, ChangedProperty_AddedObject_RemovedComponent) {
    // friction changed, the Wheel lost its transform and gained a light, a Lamp was added under the root.
    const std::string current = R"({"entities":[
      {"localId":0,"components":{"NameComponentUVE":{"name":"Prop"},"HierarchyComponentUVE":{"parentLocalId":-1},
                                 "ColliderComponentUVE":{"friction":0.5,"disabled":false}}},
      {"localId":1,"components":{"NameComponentUVE":{"name":"Wheel"},"HierarchyComponentUVE":{"parentLocalId":0},
                                 "LightComponentUVE":{"intensity":2.0}}},
      {"localId":2,"components":{"NameComponentUVE":{"name":"Lamp"},"HierarchyComponentUVE":{"parentLocalId":0}}}]})";
    const std::vector<EntityDiffRowUVE> rows = DiffEntityPayloadsUVE(kSavedUVE, current);
    const EntityDiffRowUVE* friction = FindUVE(rows, KindUVE::Changed, "(root)", "Collider.friction");
    ASSERT_NE(friction, nullptr);
    EXPECT_EQ(friction->before, "0");
    EXPECT_EQ(friction->after, "0.5");
    EXPECT_NE(FindUVE(rows, KindUVE::Removed, "(root)/Wheel", "Transform"), nullptr);
    EXPECT_NE(FindUVE(rows, KindUVE::Added, "(root)/Wheel", "Light"), nullptr);
    EXPECT_NE(FindUVE(rows, KindUVE::Added, "(root)/Lamp", ""), nullptr);
    EXPECT_EQ(rows.size(), 4U);
}

TEST(EditorEntityDiffUVETest, RemovedObject_AndSameNamedSiblings) {
    const std::string saved = R"({"entities":[
      {"localId":0,"components":{"NameComponentUVE":{"name":"Car"},"HierarchyComponentUVE":{"parentLocalId":-1}}},
      {"localId":1,"components":{"NameComponentUVE":{"name":"Wheel"},"HierarchyComponentUVE":{"parentLocalId":0}}},
      {"localId":2,"components":{"NameComponentUVE":{"name":"Wheel"},"HierarchyComponentUVE":{"parentLocalId":0}}}]})";
    const std::string current = R"({"entities":[
      {"localId":0,"components":{"NameComponentUVE":{"name":"Car"},"HierarchyComponentUVE":{"parentLocalId":-1}}},
      {"localId":1,"components":{"NameComponentUVE":{"name":"Wheel"},"HierarchyComponentUVE":{"parentLocalId":0}}}]})";
    const std::vector<EntityDiffRowUVE> rows = DiffEntityPayloadsUVE(saved, current);
    ASSERT_EQ(rows.size(), 1U);
    EXPECT_EQ(rows[0].kind, KindUVE::Removed);
    EXPECT_EQ(rows[0].object, "(root)/Wheel #2");
}

TEST(EditorEntityDiffUVETest, UnreadablePayloads_GiveNoRows) {
    EXPECT_TRUE(DiffEntityPayloadsUVE("not json", "{]").empty());
}

} // namespace
} // namespace UVE::Editor
