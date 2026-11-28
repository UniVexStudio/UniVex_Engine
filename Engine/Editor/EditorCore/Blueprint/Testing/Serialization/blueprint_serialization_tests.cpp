// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>

#include <gtest/gtest.h>

#include "blueprint_asset.h"
#include "blueprint_asset_types.h"
#include "blueprint_resources.h"
#include "blueprint_serialization.h"

namespace UVE::Blueprint::Tests {
namespace {

TEST(BlueprintSerializationTest, SchemaVersionFormattingAndParsing) {
    EXPECT_EQ(Serialization::FormatBlueprintSchemaVersion({1U, 0U}), "uve-blueprint-v1");
    EXPECT_EQ(Serialization::FormatBlueprintSchemaVersion({2U, 3U}), "uve-blueprint-v2.3");

    const auto v1 = Serialization::ParseBlueprintSchemaVersion("uve-blueprint-v1");
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(*v1, (Serialization::BlueprintSchemaVersion{1U, 0U}));

    const auto v23 = Serialization::ParseBlueprintSchemaVersion("uve-blueprint-v2.3");
    ASSERT_TRUE(v23.has_value());
    EXPECT_EQ(*v23, (Serialization::BlueprintSchemaVersion{2U, 3U}));

    EXPECT_FALSE(Serialization::ParseBlueprintSchemaVersion("invalid-v1").has_value());
}

TEST(BlueprintSerializationTest, PinNodeWireAndLibraryAssetsRoundTrip) {
    const Asset::BlueprintPinSpec pin{"Health", Asset::BlueprintPinType::Float, "100.0"};
    const auto parsedPin = Serialization::ParseBlueprintPinSpecJson(Serialization::SerializeBlueprintPinSpecJson(pin));
    ASSERT_TRUE(parsedPin.has_value());
    EXPECT_EQ(parsedPin->name, "Health");
    EXPECT_EQ(parsedPin->type, Asset::BlueprintPinType::Float);
    EXPECT_EQ(parsedPin->defaultText, "100.0");

    const Asset::BlueprintNode node{7U, "print", {}, 120.0F, 240.0F, {"", "Hi"}};
    const auto parsedNode =
        Serialization::ParseBlueprintNodeJson(Serialization::SerializeBlueprintNodeJson(node));
    ASSERT_TRUE(parsedNode.has_value());
    EXPECT_EQ(*parsedNode, node);

    const Asset::BlueprintWire wire{1U, 0U, 7U, 0U};
    const auto parsedWire =
        Serialization::ParseBlueprintWireJson(Serialization::SerializeBlueprintWireJson(wire));
    ASSERT_TRUE(parsedWire.has_value());
    EXPECT_EQ(*parsedWire, wire);

    Assets::BlueprintFunctionLibraryAsset fnLib;
    fnLib.libraryName = "MathHelpers";
    fnLib.description = "Reusable math functions";
    Assets::BlueprintFunctionDefinition fn;
    fn.name = "DoubleValue";
    fn.isPure = true;
    fn.inputs.push_back({"Value", Asset::BlueprintPinType::Float, "1.0"});
    fn.outputs.push_back({"Result", Asset::BlueprintPinType::Float, "0.0"});
    fnLib.functions.push_back(fn);

    const auto parsedFnLib =
        Serialization::ParseFunctionLibraryAsset(Serialization::SerializeFunctionLibraryAsset(fnLib));
    ASSERT_TRUE(parsedFnLib.has_value());
    EXPECT_EQ(parsedFnLib->libraryName, "MathHelpers");
    ASSERT_EQ(parsedFnLib->functions.size(), 1U);
    EXPECT_EQ(parsedFnLib->functions[0].name, "DoubleValue");

    Assets::BlueprintMacroLibraryAsset macroLib;
    macroLib.libraryName = "FlowMacros";
    macroLib.targetParentKind = "Object3D";
    Assets::BlueprintMacroDefinition macro;
    macro.name = "SafePrint";
    macro.inputs.push_back({"In", Asset::BlueprintPinType::Exec, ""});
    macro.outputs.push_back({"Out", Asset::BlueprintPinType::Exec, ""});
    macroLib.macros.push_back(macro);

    const auto parsedMacroLib =
        Serialization::ParseMacroLibraryAsset(Serialization::SerializeMacroLibraryAsset(macroLib));
    ASSERT_TRUE(parsedMacroLib.has_value());
    EXPECT_EQ(parsedMacroLib->libraryName, "FlowMacros");
    ASSERT_EQ(parsedMacroLib->macros.size(), 1U);

    Assets::BlueprintInterfaceAsset iface;
    iface.interfaceName = "IDamageable";
    Assets::BlueprintInterfaceMethod method;
    method.name = "ApplyDamage";
    method.inputs.push_back({"Amount", Asset::BlueprintPinType::Float, "10.0"});
    iface.methods.push_back(method);

    const auto parsedIface =
        Serialization::ParseInterfaceAsset(Serialization::SerializeInterfaceAsset(iface));
    ASSERT_TRUE(parsedIface.has_value());
    EXPECT_EQ(parsedIface->interfaceName, "IDamageable");
    ASSERT_EQ(parsedIface->methods.size(), 1U);
    EXPECT_EQ(parsedIface->methods[0].name, "ApplyDamage");
}

TEST(BlueprintSerializationTest, LegacyV0MigrationAndExportImport) {
    constexpr const char* kLegacyV0 =
        R"({"parentClass":"Character3D","nodes":[{"id":1,"type":"event_ready","x":10,"y":20,"inputs":[]}]})";

    const Serialization::BlueprintImportResult imported = Serialization::ImportBlueprintFromJson(kLegacyV0);
    ASSERT_TRUE(imported.IsSuccess()) << imported.error;
    EXPECT_TRUE(imported.migrated);
    EXPECT_EQ(imported.sourceVersion, (Serialization::BlueprintSchemaVersion{0U, 0U}));
    EXPECT_EQ(imported.asset->parentKind, "Character3D");
    ASSERT_EQ(imported.asset->nodes.size(), 1U);

    const Asset::BlueprintAsset starter =
        Resources::InstantiateBlueprintTemplate(Resources::BlueprintTemplateKind::InteractiveTrigger);
    const std::string compact =
        Serialization::ExportBlueprintAsset(starter, Serialization::BlueprintExportFormat::JsonCompact);
    const Serialization::BlueprintImportResult roundTrip = Serialization::ImportBlueprintFromJson(compact);
    ASSERT_TRUE(roundTrip.IsSuccess());
    EXPECT_FALSE(roundTrip.migrated);
    EXPECT_EQ(*roundTrip.asset, starter);
}

} // namespace
} // namespace UVE::Blueprint::Tests
