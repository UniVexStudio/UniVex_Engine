// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>

#include <gtest/gtest.h>

#include "blueprint_asset.h"
#include "blueprint_asset_types.h"
#include "blueprint_resources.h"
#include "blueprint_serialization.h"

namespace UVE::Blueprint::Tests {
namespace {

TEST(BlueprintSerializationUVETest, SchemaVersionFormattingAndParsing) {
    EXPECT_EQ(Serialization::FormatBlueprintSchemaVersionUVE({1U, 0U}), "uve-blueprint-v1");
    EXPECT_EQ(Serialization::FormatBlueprintSchemaVersionUVE({2U, 3U}), "uve-blueprint-v2.3");

    const auto v1 = Serialization::ParseBlueprintSchemaVersionUVE("uve-blueprint-v1");
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(*v1, (Serialization::BlueprintSchemaVersionUVE{1U, 0U}));

    const auto v23 = Serialization::ParseBlueprintSchemaVersionUVE("uve-blueprint-v2.3");
    ASSERT_TRUE(v23.has_value());
    EXPECT_EQ(*v23, (Serialization::BlueprintSchemaVersionUVE{2U, 3U}));

    EXPECT_FALSE(Serialization::ParseBlueprintSchemaVersionUVE("invalid-v1").has_value());
}

TEST(BlueprintSerializationUVETest, PinNodeWireAndLibraryAssetsRoundTrip) {
    const Asset::BlueprintPinSpecUVE pin{"Health", Asset::BlueprintPinTypeUVE::Float, "100.0"};
    const auto parsedPin = Serialization::ParseBlueprintPinSpecJsonUVE(Serialization::SerializeBlueprintPinSpecJsonUVE(pin));
    ASSERT_TRUE(parsedPin.has_value());
    EXPECT_EQ(parsedPin->name, "Health");
    EXPECT_EQ(parsedPin->type, Asset::BlueprintPinTypeUVE::Float);
    EXPECT_EQ(parsedPin->defaultText, "100.0");

    const Asset::BlueprintNodeUVE node{7U, "print", {}, 120.0F, 240.0F, {"", "Hi"}};
    const auto parsedNode =
        Serialization::ParseBlueprintNodeJsonUVE(Serialization::SerializeBlueprintNodeJsonUVE(node));
    ASSERT_TRUE(parsedNode.has_value());
    EXPECT_EQ(*parsedNode, node);

    const Asset::BlueprintWireUVE wire{1U, 0U, 7U, 0U};
    const auto parsedWire =
        Serialization::ParseBlueprintWireJsonUVE(Serialization::SerializeBlueprintWireJsonUVE(wire));
    ASSERT_TRUE(parsedWire.has_value());
    EXPECT_EQ(*parsedWire, wire);

    Assets::BlueprintFunctionLibraryAssetUVE fnLib;
    fnLib.libraryName = "MathHelpers";
    fnLib.description = "Reusable math functions";
    Assets::BlueprintFunctionDefinitionUVE fn;
    fn.name = "DoubleValue";
    fn.isPure = true;
    fn.inputs.push_back({"Value", Asset::BlueprintPinTypeUVE::Float, "1.0"});
    fn.outputs.push_back({"Result", Asset::BlueprintPinTypeUVE::Float, "0.0"});
    fnLib.functions.push_back(fn);

    const auto parsedFnLib =
        Serialization::ParseFunctionLibraryAssetUVE(Serialization::SerializeFunctionLibraryAssetUVE(fnLib));
    ASSERT_TRUE(parsedFnLib.has_value());
    EXPECT_EQ(parsedFnLib->libraryName, "MathHelpers");
    ASSERT_EQ(parsedFnLib->functions.size(), 1U);
    EXPECT_EQ(parsedFnLib->functions[0].name, "DoubleValue");

    Assets::BlueprintMacroLibraryAssetUVE macroLib;
    macroLib.libraryName = "FlowMacros";
    macroLib.targetParentKind = "Object3D";
    Assets::BlueprintMacroDefinitionUVE macro;
    macro.name = "SafePrint";
    macro.inputs.push_back({"In", Asset::BlueprintPinTypeUVE::Exec, ""});
    macro.outputs.push_back({"Out", Asset::BlueprintPinTypeUVE::Exec, ""});
    macroLib.macros.push_back(macro);

    const auto parsedMacroLib =
        Serialization::ParseMacroLibraryAssetUVE(Serialization::SerializeMacroLibraryAssetUVE(macroLib));
    ASSERT_TRUE(parsedMacroLib.has_value());
    EXPECT_EQ(parsedMacroLib->libraryName, "FlowMacros");
    ASSERT_EQ(parsedMacroLib->macros.size(), 1U);

    Assets::BlueprintInterfaceAssetUVE iface;
    iface.interfaceName = "IDamageable";
    Assets::BlueprintInterfaceMethodUVE method;
    method.name = "ApplyDamage";
    method.inputs.push_back({"Amount", Asset::BlueprintPinTypeUVE::Float, "10.0"});
    iface.methods.push_back(method);

    const auto parsedIface =
        Serialization::ParseInterfaceAssetUVE(Serialization::SerializeInterfaceAssetUVE(iface));
    ASSERT_TRUE(parsedIface.has_value());
    EXPECT_EQ(parsedIface->interfaceName, "IDamageable");
    ASSERT_EQ(parsedIface->methods.size(), 1U);
    EXPECT_EQ(parsedIface->methods[0].name, "ApplyDamage");
}

TEST(BlueprintSerializationUVETest, LegacyV0MigrationAndExportImport) {
    constexpr const char* kLegacyV0 =
        R"({"parentClass":"Character3D","nodes":[{"id":1,"type":"event_ready","x":10,"y":20,"inputs":[]}]})";

    const Serialization::BlueprintImportResultUVE imported = Serialization::ImportBlueprintFromJsonUVE(kLegacyV0);
    ASSERT_TRUE(imported.IsSuccessUVE()) << imported.error;
    EXPECT_TRUE(imported.migrated);
    EXPECT_EQ(imported.sourceVersion, (Serialization::BlueprintSchemaVersionUVE{0U, 0U}));
    EXPECT_EQ(imported.asset->parentKind, "Character3D");
    ASSERT_EQ(imported.asset->nodes.size(), 1U);

    const Asset::BlueprintAssetUVE starter =
        Resources::InstantiateBlueprintTemplateUVE(Resources::BlueprintTemplateKindUVE::InteractiveTrigger);
    const std::string compact =
        Serialization::ExportBlueprintAssetUVE(starter, Serialization::BlueprintExportFormatUVE::JsonCompact);
    const Serialization::BlueprintImportResultUVE roundTrip = Serialization::ImportBlueprintFromJsonUVE(compact);
    ASSERT_TRUE(roundTrip.IsSuccessUVE());
    EXPECT_FALSE(roundTrip.migrated);
    EXPECT_EQ(*roundTrip.asset, starter);

    const std::string script = Serialization::ExportBlueprintAssetUVE(
        starter, Serialization::BlueprintExportFormatUVE::UVScriptSource, "DoorTrigger");
    EXPECT_NE(script.find("entity DoorTrigger : Object3D"), std::string::npos);
}

} // namespace
} // namespace UVE::Blueprint::Tests
