// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_serialization.h"

#include <charconv>
#include <cstddef>
#include <exception>
#include <utility>

#include <nlohmann/json.hpp>

namespace UVE::Blueprint::Serialization {
namespace {

using JsonUVE = nlohmann::json;

[[nodiscard]] JsonUVE PinToJsonUVE(const Asset::BlueprintPinSpecUVE& pin) {
    return JsonUVE{{"name", pin.name},
                   {"type", BlueprintPinTypeToNameUVE(pin.type)},
                   {"default", pin.defaultText}};
}

[[nodiscard]] std::optional<Asset::BlueprintPinSpecUVE> PinFromJsonUVE(const JsonUVE& j) {
    if (!j.is_object()) {
        return std::nullopt;
    }
    Asset::BlueprintPinSpecUVE pin;
    pin.name = j.value("name", std::string{});
    if (const auto it = j.find("type"); it != j.end()) {
        if (it->is_string()) {
            const auto parsed = ParseBlueprintPinTypeNameUVE(it->get<std::string>());
            if (!parsed.has_value()) {
                return std::nullopt;
            }
            pin.type = *parsed;
        } else if (it->is_number_unsigned()) {
            const auto raw = it->get<std::uint8_t>();
            if (raw > static_cast<std::uint8_t>(Asset::BlueprintPinTypeUVE::StringArray)) {
                return std::nullopt;
            }
            pin.type = static_cast<Asset::BlueprintPinTypeUVE>(raw);
        } else {
            return std::nullopt;
        }
    }
    pin.defaultText = j.value("default", std::string{});
    return pin;
}

[[nodiscard]] JsonUVE PinsArrayToJsonUVE(const std::vector<Asset::BlueprintPinSpecUVE>& pins) {
    JsonUVE arr = JsonUVE::array();
    for (const Asset::BlueprintPinSpecUVE& p : pins) {
        arr.push_back(PinToJsonUVE(p));
    }
    return arr;
}

[[nodiscard]] std::optional<std::vector<Asset::BlueprintPinSpecUVE>> PinsArrayFromJsonUVE(const JsonUVE& arr) {
    if (!arr.is_array()) {
        return std::nullopt;
    }
    std::vector<Asset::BlueprintPinSpecUVE> out;
    out.reserve(arr.size());
    for (const JsonUVE& item : arr) {
        auto pin = PinFromJsonUVE(item);
        if (!pin.has_value()) {
            return std::nullopt;
        }
        out.push_back(std::move(*pin));
    }
    return out;
}

[[nodiscard]] std::optional<std::uint32_t> ParseUInt32UVE(const std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint32_t value = 0U;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

// ---------------------------------------------------------------------------
// Versioning
// ---------------------------------------------------------------------------

std::string FormatBlueprintSchemaVersionUVE(const BlueprintSchemaVersionUVE version) {
    if (version.minor == 0U) {
        return "uve-blueprint-v" + std::to_string(version.major);
    }
    return "uve-blueprint-v" + std::to_string(version.major) + "." + std::to_string(version.minor);
}

std::optional<BlueprintSchemaVersionUVE> ParseBlueprintSchemaVersionUVE(const std::string_view schemaText) {
    constexpr std::string_view prefix = "uve-blueprint-v";
    if (!schemaText.starts_with(prefix)) {
        return std::nullopt;
    }
    const std::string_view rest = schemaText.substr(prefix.size());
    const std::size_t dot = rest.find('.');
    if (dot == std::string_view::npos) {
        const auto maj = ParseUInt32UVE(rest);
        if (!maj.has_value()) {
            return std::nullopt;
        }
        return BlueprintSchemaVersionUVE{*maj, 0U};
    }
    const auto maj = ParseUInt32UVE(rest.substr(0U, dot));
    const auto min = ParseUInt32UVE(rest.substr(dot + 1U));
    if (!maj.has_value() || !min.has_value()) {
        return std::nullopt;
    }
    return BlueprintSchemaVersionUVE{*maj, *min};
}

// ---------------------------------------------------------------------------
// Pin / Node / Wire / Asset Serialization
// ---------------------------------------------------------------------------

std::string_view BlueprintPinTypeToNameUVE(const Asset::BlueprintPinTypeUVE type) noexcept {
    using T = Asset::BlueprintPinTypeUVE;
    switch (type) {
        case T::Exec: return "Exec";
        case T::Bool: return "Bool";
        case T::Int: return "Int";
        case T::Float: return "Float";
        case T::Vector: return "Vector";
        case T::String: return "String";
        case T::Object: return "Object";
        case T::ObjectArray: return "ObjectArray";
        case T::IntArray: return "IntArray";
        case T::FloatArray: return "FloatArray";
        case T::StringArray: return "StringArray";
    }
    return "Float";
}

std::optional<Asset::BlueprintPinTypeUVE> ParseBlueprintPinTypeNameUVE(const std::string_view name) noexcept {
    using T = Asset::BlueprintPinTypeUVE;
    if (name == "Exec") return T::Exec;
    if (name == "Bool") return T::Bool;
    if (name == "Int") return T::Int;
    if (name == "Float") return T::Float;
    if (name == "Vector") return T::Vector;
    if (name == "String") return T::String;
    if (name == "Object") return T::Object;
    if (name == "ObjectArray") return T::ObjectArray;
    if (name == "IntArray") return T::IntArray;
    if (name == "FloatArray") return T::FloatArray;
    if (name == "StringArray") return T::StringArray;
    return std::nullopt;
}

std::string SerializeBlueprintPinSpecJsonUVE(const Asset::BlueprintPinSpecUVE& pin) {
    return PinToJsonUVE(pin).dump();
}

std::optional<Asset::BlueprintPinSpecUVE> ParseBlueprintPinSpecJsonUVE(const std::string_view jsonText) {
    try {
        return PinFromJsonUVE(JsonUVE::parse(jsonText));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string SerializeBlueprintNodeJsonUVE(const Asset::BlueprintNodeUVE& node) {
    JsonUVE j{{"id", node.id},
              {"type", node.type},
              {"x", node.x},
              {"y", node.y},
              {"inputs", node.inputValues}};
    if (!node.variable.empty()) {
        j["variable"] = node.variable;
    }
    return j.dump();
}

std::optional<Asset::BlueprintNodeUVE> ParseBlueprintNodeJsonUVE(const std::string_view jsonText) {
    try {
        const JsonUVE j = JsonUVE::parse(jsonText);
        if (!j.is_object()) {
            return std::nullopt;
        }
        Asset::BlueprintNodeUVE node;
        node.id = j.value("id", 0U);
        node.type = j.value("type", std::string{});
        node.variable = j.value("variable", std::string{});
        node.x = j.value("x", 0.0F);
        node.y = j.value("y", 0.0F);
        node.inputValues = j.value("inputs", std::vector<std::string>{});
        if (node.id == 0U || node.type.empty()) {
            return std::nullopt;
        }
        return node;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string SerializeBlueprintWireJsonUVE(const Asset::BlueprintWireUVE& wire) {
    const JsonUVE j{{"fromNode", wire.fromNode},
                    {"fromPin", wire.fromPin},
                    {"toNode", wire.toNode},
                    {"toPin", wire.toPin}};
    return j.dump();
}

std::optional<Asset::BlueprintWireUVE> ParseBlueprintWireJsonUVE(const std::string_view jsonText) {
    try {
        const JsonUVE j = JsonUVE::parse(jsonText);
        if (j.is_array() && j.size() == 4U) {
            return Asset::BlueprintWireUVE{j[0].get<std::uint32_t>(), j[1].get<std::uint32_t>(),
                                           j[2].get<std::uint32_t>(), j[3].get<std::uint32_t>()};
        }
        if (j.is_object()) {
            return Asset::BlueprintWireUVE{j.value("fromNode", 0U), j.value("fromPin", 0U),
                                           j.value("toNode", 0U), j.value("toPin", 0U)};
        }
        return std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string SerializeFunctionLibraryAssetUVE(const Assets::BlueprintFunctionLibraryAssetUVE& library) {
    JsonUVE functions = JsonUVE::array();
    for (const Assets::BlueprintFunctionDefinitionUVE& fn : library.functions) {
        functions.push_back({{"name", fn.name},
                             {"category", fn.category},
                             {"tooltip", fn.tooltip},
                             {"isPure", fn.isPure},
                             {"isStatic", fn.isStatic},
                             {"inputs", PinsArrayToJsonUVE(fn.inputs)},
                             {"outputs", PinsArrayToJsonUVE(fn.outputs)},
                             {"bodyGraph", Asset::SerializeBlueprintAssetUVE(fn.bodyGraph)}});
    }
    const JsonUVE doc{{"schema", "uve-blueprint-function-library-v1"},
                      {"libraryName", library.libraryName},
                      {"description", library.description},
                      {"functions", std::move(functions)}};
    return doc.dump(1);
}

std::optional<Assets::BlueprintFunctionLibraryAssetUVE> ParseFunctionLibraryAssetUVE(const std::string_view jsonText) {
    try {
        const JsonUVE doc = JsonUVE::parse(jsonText);
        if (!doc.is_object() || doc.value("schema", std::string{}) != "uve-blueprint-function-library-v1") {
            return std::nullopt;
        }
        Assets::BlueprintFunctionLibraryAssetUVE library;
        library.libraryName = doc.value("libraryName", std::string{});
        library.description = doc.value("description", std::string{});
        for (const JsonUVE& fnJson : doc.value("functions", JsonUVE::array())) {
            Assets::BlueprintFunctionDefinitionUVE fn;
            fn.name = fnJson.value("name", std::string{});
            fn.category = fnJson.value("category", std::string{"Functions"});
            fn.tooltip = fnJson.value("tooltip", std::string{});
            fn.isPure = fnJson.value("isPure", false);
            fn.isStatic = fnJson.value("isStatic", true);
            const auto inputs = PinsArrayFromJsonUVE(fnJson.value("inputs", JsonUVE::array()));
            const auto outputs = PinsArrayFromJsonUVE(fnJson.value("outputs", JsonUVE::array()));
            if (!inputs.has_value() || !outputs.has_value()) {
                return std::nullopt;
            }
            fn.inputs = *inputs;
            fn.outputs = *outputs;
            const auto parsedGraph = Asset::ParseBlueprintAssetUVE(fnJson.value("bodyGraph", std::string{}));
            if (!parsedGraph.has_value()) {
                return std::nullopt;
            }
            fn.bodyGraph = *parsedGraph;
            library.functions.push_back(std::move(fn));
        }
        if (!Assets::IsFunctionLibraryAssetValidUVE(library)) {
            return std::nullopt;
        }
        return library;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string SerializeMacroLibraryAssetUVE(const Assets::BlueprintMacroLibraryAssetUVE& library) {
    JsonUVE macros = JsonUVE::array();
    for (const Assets::BlueprintMacroDefinitionUVE& macro : library.macros) {
        macros.push_back({{"name", macro.name},
                          {"category", macro.category},
                          {"tooltip", macro.tooltip},
                          {"inputs", PinsArrayToJsonUVE(macro.inputs)},
                          {"outputs", PinsArrayToJsonUVE(macro.outputs)},
                          {"subgraph", Asset::SerializeBlueprintAssetUVE(macro.subgraph)}});
    }
    const JsonUVE doc{{"schema", "uve-blueprint-macro-library-v1"},
                      {"libraryName", library.libraryName},
                      {"targetParentKind", library.targetParentKind},
                      {"description", library.description},
                      {"macros", std::move(macros)}};
    return doc.dump(1);
}

std::optional<Assets::BlueprintMacroLibraryAssetUVE> ParseMacroLibraryAssetUVE(const std::string_view jsonText) {
    try {
        const JsonUVE doc = JsonUVE::parse(jsonText);
        if (!doc.is_object() || doc.value("schema", std::string{}) != "uve-blueprint-macro-library-v1") {
            return std::nullopt;
        }
        Assets::BlueprintMacroLibraryAssetUVE library;
        library.libraryName = doc.value("libraryName", std::string{});
        library.targetParentKind = doc.value("targetParentKind", std::string{"Object3D"});
        library.description = doc.value("description", std::string{});
        for (const JsonUVE& mJson : doc.value("macros", JsonUVE::array())) {
            Assets::BlueprintMacroDefinitionUVE macro;
            macro.name = mJson.value("name", std::string{});
            macro.category = mJson.value("category", std::string{"Macros"});
            macro.tooltip = mJson.value("tooltip", std::string{});
            const auto inputs = PinsArrayFromJsonUVE(mJson.value("inputs", JsonUVE::array()));
            const auto outputs = PinsArrayFromJsonUVE(mJson.value("outputs", JsonUVE::array()));
            if (!inputs.has_value() || !outputs.has_value()) {
                return std::nullopt;
            }
            macro.inputs = *inputs;
            macro.outputs = *outputs;
            const auto parsedGraph = Asset::ParseBlueprintAssetUVE(mJson.value("subgraph", std::string{}));
            if (!parsedGraph.has_value()) {
                return std::nullopt;
            }
            macro.subgraph = *parsedGraph;
            library.macros.push_back(std::move(macro));
        }
        if (!Assets::IsMacroLibraryAssetValidUVE(library)) {
            return std::nullopt;
        }
        return library;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string SerializeInterfaceAssetUVE(const Assets::BlueprintInterfaceAssetUVE& iface) {
    JsonUVE methods = JsonUVE::array();
    for (const Assets::BlueprintInterfaceMethodUVE& method : iface.methods) {
        methods.push_back({{"name", method.name},
                           {"tooltip", method.tooltip},
                           {"isConst", method.isConst},
                           {"inputs", PinsArrayToJsonUVE(method.inputs)},
                           {"outputs", PinsArrayToJsonUVE(method.outputs)}});
    }
    const JsonUVE doc{{"schema", "uve-blueprint-interface-v1"},
                      {"interfaceName", iface.interfaceName},
                      {"description", iface.description},
                      {"methods", std::move(methods)}};
    return doc.dump(1);
}

std::optional<Assets::BlueprintInterfaceAssetUVE> ParseInterfaceAssetUVE(const std::string_view jsonText) {
    try {
        const JsonUVE doc = JsonUVE::parse(jsonText);
        if (!doc.is_object() || doc.value("schema", std::string{}) != "uve-blueprint-interface-v1") {
            return std::nullopt;
        }
        Assets::BlueprintInterfaceAssetUVE iface;
        iface.interfaceName = doc.value("interfaceName", std::string{});
        iface.description = doc.value("description", std::string{});
        for (const JsonUVE& mJson : doc.value("methods", JsonUVE::array())) {
            Assets::BlueprintInterfaceMethodUVE method;
            method.name = mJson.value("name", std::string{});
            method.tooltip = mJson.value("tooltip", std::string{});
            method.isConst = mJson.value("isConst", false);
            const auto inputs = PinsArrayFromJsonUVE(mJson.value("inputs", JsonUVE::array()));
            const auto outputs = PinsArrayFromJsonUVE(mJson.value("outputs", JsonUVE::array()));
            if (!inputs.has_value() || !outputs.has_value()) {
                return std::nullopt;
            }
            method.inputs = *inputs;
            method.outputs = *outputs;
            iface.methods.push_back(std::move(method));
        }
        if (!Assets::IsInterfaceAssetValidUVE(iface)) {
            return std::nullopt;
        }
        return iface;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// Migration Registry
// ---------------------------------------------------------------------------

BlueprintMigrationRegistryUVE BlueprintMigrationRegistryUVE::CreateDefaultUVE() {
    BlueprintMigrationRegistryUVE registry;
    // Upgrade legacy v0 documents (missing "schema" or "uve-blueprint-v0", optional "parentClass" key).
    registry.RegisterStepUVE(BlueprintMigrationStepUVE{
        BlueprintSchemaVersionUVE{0U, 0U},
        BlueprintSchemaVersionUVE{1U, 0U},
        "Upgrade legacy v0 Blueprint schema to uve-blueprint-v1",
        [](const std::string_view text) -> std::optional<std::string> {
            try {
                JsonUVE doc = JsonUVE::parse(text);
                if (!doc.is_object()) {
                    return std::nullopt;
                }
                doc["schema"] = "uve-blueprint-v1";
                if (!doc.contains("parentKind")) {
                    doc["parentKind"] = doc.value("parentClass", std::string{"Object3D"});
                    doc.erase("parentClass");
                }
                if (!doc.contains("variables")) doc["variables"] = JsonUVE::array();
                if (!doc.contains("nodes")) doc["nodes"] = JsonUVE::array();
                if (!doc.contains("wires")) doc["wires"] = JsonUVE::array();
                if (!doc.contains("comments")) doc["comments"] = JsonUVE::array();
                return doc.dump(1);
            } catch (const std::exception&) {
                return std::nullopt;
            }
        }});
    return registry;
}

void BlueprintMigrationRegistryUVE::RegisterStepUVE(BlueprintMigrationStepUVE step) {
    m_steps.push_back(std::move(step));
}

BlueprintMigrationResultUVE BlueprintMigrationRegistryUVE::MigrateJsonUVE(
    const std::string_view jsonText, const BlueprintSchemaVersionUVE targetVersion) const {
    BlueprintMigrationResultUVE result;
    result.toVersion = targetVersion;

    JsonUVE doc;
    try {
        doc = JsonUVE::parse(jsonText);
    } catch (const std::exception& e) {
        result.error = std::string("Invalid JSON payload: ") + e.what();
        return result;
    }
    if (!doc.is_object()) {
        result.error = "Blueprint JSON root must be an object.";
        return result;
    }

    BlueprintSchemaVersionUVE current{0U, 0U};
    if (const auto it = doc.find("schema"); it != doc.end() && it->is_string()) {
        const auto parsed = ParseBlueprintSchemaVersionUVE(it->get<std::string>());
        if (!parsed.has_value()) {
            result.error = "Unsupported Blueprint schema tag: " + it->get<std::string>();
            return result;
        }
        current = *parsed;
    }

    result.fromVersion = current;
    if (current > targetVersion) {
        result.error = "Blueprint schema version is newer than supported target version.";
        return result;
    }

    std::string payload(jsonText);
    constexpr std::size_t kMaxMigrationHopsUVE = 32U;
    std::size_t hops = 0U;
    while (current < targetVersion && hops < kMaxMigrationHopsUVE) {
        const BlueprintMigrationStepUVE* nextStep = nullptr;
        for (const BlueprintMigrationStepUVE& step : m_steps) {
            if (step.fromVersion == current && step.toVersion > current && step.toVersion <= targetVersion) {
                if (nextStep == nullptr || step.toVersion > nextStep->toVersion) {
                    nextStep = &step;
                }
            }
        }
        if (nextStep == nullptr || !nextStep->transform) {
            result.error = "No migration path from " + FormatBlueprintSchemaVersionUVE(current) + " to " +
                           FormatBlueprintSchemaVersionUVE(targetVersion);
            return result;
        }
        const std::optional<std::string> transformed = nextStep->transform(payload);
        if (!transformed.has_value()) {
            result.error = "Migration step failed: " + nextStep->description;
            return result;
        }
        payload = *transformed;
        current = nextStep->toVersion;
        result.appliedSteps.push_back(nextStep->description);
        ++hops;
    }

    if (current != targetVersion) {
        result.error = "Migration exceeded maximum step limit.";
        return result;
    }
    result.success = true;
    result.toVersion = current;
    result.migratedJson = std::move(payload);
    return result;
}

// ---------------------------------------------------------------------------
// Import & Export
// ---------------------------------------------------------------------------

std::string ExportBlueprintAssetUVE(const Asset::BlueprintAssetUVE& blueprint, const BlueprintExportFormatUVE format,
                                    const std::string_view objectName) {
    if (format == BlueprintExportFormatUVE::UVScriptSource) {
        return Asset::TranslateBlueprintToUVScriptUVE(blueprint, objectName, blueprint.parentKind).source;
    }
    const std::string pretty = Asset::SerializeBlueprintAssetUVE(blueprint);
    if (format == BlueprintExportFormatUVE::JsonCompact) {
        try {
            return JsonUVE::parse(pretty).dump();
        } catch (const std::exception&) {
            return pretty;
        }
    }
    return pretty;
}

BlueprintImportResultUVE ImportBlueprintFromJsonUVE(const std::string_view jsonText,
                                                    const BlueprintMigrationRegistryUVE* const customRegistry) {
    BlueprintImportResultUVE result;
    const BlueprintMigrationRegistryUVE defaultRegistry = BlueprintMigrationRegistryUVE::CreateDefaultUVE();
    const BlueprintMigrationRegistryUVE& registry = customRegistry != nullptr ? *customRegistry : defaultRegistry;

    const BlueprintMigrationResultUVE migration = registry.MigrateJsonUVE(jsonText, kCurrentBlueprintSchemaVersionUVE);
    result.sourceVersion = migration.fromVersion;
    result.notes = migration.appliedSteps;
    if (!migration.success) {
        result.error = migration.error;
        return result;
    }
    result.migrated = !migration.appliedSteps.empty();
    result.asset = Asset::ParseBlueprintAssetUVE(migration.migratedJson);
    if (!result.asset.has_value()) {
        result.error = "Blueprint payload failed validation after deserialization.";
    }
    return result;
}

} // namespace UVE::Blueprint::Serialization
