// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_asset_registry.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <set>
#include <unordered_map>
#include <utility>

#include "blueprint_core_types.h"
#include "blueprint_nodes.h"

namespace UVE::Blueprint::Assets {

// ---------------------------------------------------------------------------
// BlueprintAssetRegistryUVE
// ---------------------------------------------------------------------------

bool BlueprintAssetRegistryUVE::RegisterAssetUVE(BlueprintAssetDescriptorUVE descriptor) {
    if (descriptor.assetPath.empty()) {
        return false;
    }
    const std::string key = descriptor.assetPath;
    m_assets[key] = std::move(descriptor);
    return true;
}

bool BlueprintAssetRegistryUVE::UnregisterAssetUVE(const std::string_view assetPath) {
    const auto it = m_assets.find(assetPath);
    if (it == m_assets.end()) {
        return false;
    }
    m_assets.erase(it);
    return true;
}

const BlueprintAssetDescriptorUVE* BlueprintAssetRegistryUVE::FindAssetUVE(
    const std::string_view assetPath) const noexcept {
    const auto it = m_assets.find(assetPath);
    return it != m_assets.end() ? &it->second : nullptr;
}

std::vector<BlueprintAssetDescriptorUVE> BlueprintAssetRegistryUVE::ListByKindUVE(
    const BlueprintAssetKindUVE kind) const {
    std::vector<BlueprintAssetDescriptorUVE> out;
    for (const auto& entry : m_assets) {
        if (entry.second.kind == kind) {
            out.push_back(entry.second);
        }
    }
    return out;
}

std::vector<std::string> BlueprintAssetRegistryUVE::GetDependenciesUVE(const std::string_view assetPath) const {
    const BlueprintAssetDescriptorUVE* const desc = FindAssetUVE(assetPath);
    return desc != nullptr ? desc->dependencies : std::vector<std::string>{};
}

std::vector<std::string> BlueprintAssetRegistryUVE::GetDependentsUVE(const std::string_view assetPath) const {
    std::vector<std::string> dependents;
    for (const auto& entry : m_assets) {
        const auto& deps = entry.second.dependencies;
        if (std::find(deps.begin(), deps.end(), assetPath) != deps.end()) {
            dependents.push_back(entry.first);
        }
    }
    return dependents;
}

std::vector<std::string> BlueprintAssetRegistryUVE::GetTransitiveDependenciesUVE(
    const std::string_view assetPath) const {
    std::set<std::string> visited;
    std::vector<std::string> result;
    std::queue<std::string> pending;

    for (const std::string& dep : GetDependenciesUVE(assetPath)) {
        if (dep != assetPath && visited.insert(dep).second) {
            pending.push(dep);
        }
    }
    while (!pending.empty()) {
        std::string current = std::move(pending.front());
        pending.pop();
        result.push_back(current);
        for (const std::string& next : GetDependenciesUVE(current)) {
            if (next != assetPath && visited.insert(next).second) {
                pending.push(next);
            }
        }
    }
    return result;
}

std::vector<std::string> BlueprintAssetRegistryUVE::ComputeBuildOrderUVE() const {
    std::map<std::string, std::size_t> indegree;
    std::map<std::string, std::vector<std::string>> dependents;
    for (const auto& entry : m_assets) {
        indegree[entry.first] = 0U;
    }
    for (const auto& entry : m_assets) {
        for (const std::string& dep : entry.second.dependencies) {
            if (m_assets.contains(dep)) {
                dependents[dep].push_back(entry.first);
                ++indegree[entry.first];
            }
        }
    }

    std::queue<std::string> ready;
    for (const auto& entry : indegree) {
        if (entry.second == 0U) {
            ready.push(entry.first);
        }
    }

    std::vector<std::string> order;
    order.reserve(m_assets.size());
    while (!ready.empty()) {
        std::string cur = std::move(ready.front());
        ready.pop();
        order.push_back(cur);
        if (const auto it = dependents.find(cur); it != dependents.end()) {
            for (const std::string& next : it->second) {
                if (--indegree[next] == 0U) {
                    ready.push(next);
                }
            }
        }
    }

    if (order.size() != m_assets.size()) {
        return {};
    }
    return order;
}

bool BlueprintAssetRegistryUVE::HasDependencyCycleUVE() const {
    return !m_assets.empty() && ComputeBuildOrderUVE().empty();
}

// ---------------------------------------------------------------------------
// Validation Reports
// ---------------------------------------------------------------------------

bool BlueprintAssetValidationReportUVE::IsValidUVE() const noexcept { return ErrorCountUVE() == 0U; }

std::size_t BlueprintAssetValidationReportUVE::ErrorCountUVE() const noexcept {
    return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(), [](const BlueprintAssetValidationIssueUVE& i) {
        return i.severity == BlueprintAssetValidationSeverityUVE::Error;
    }));
}

std::size_t BlueprintAssetValidationReportUVE::WarningCountUVE() const noexcept {
    return static_cast<std::size_t>(std::count_if(issues.begin(), issues.end(), [](const BlueprintAssetValidationIssueUVE& i) {
        return i.severity == BlueprintAssetValidationSeverityUVE::Warning;
    }));
}

BlueprintAssetValidationReportUVE ValidateBlueprintAssetDetailedUVE(const Asset::BlueprintAssetUVE& blueprint) {
    BlueprintAssetValidationReportUVE report;
    const auto emitError = [&report](const std::uint32_t nodeId, std::string code, std::string msg) {
        report.issues.push_back(BlueprintAssetValidationIssueUVE{
            BlueprintAssetValidationSeverityUVE::Error, nodeId, std::move(code), std::move(msg)});
    };
    const auto emitWarning = [&report](const std::uint32_t nodeId, std::string code, std::string msg) {
        report.issues.push_back(BlueprintAssetValidationIssueUVE{
            BlueprintAssetValidationSeverityUVE::Warning, nodeId, std::move(code), std::move(msg)});
    };

    if (blueprint.parentKind.empty() || blueprint.parentKind.size() > Asset::kMaximumBlueprintNameBytesUVE) {
        emitError(0U, "BP_INVALID_PARENT_KIND", "Blueprint parentKind is empty or exceeds maximum length.");
    }
    if (blueprint.nodes.size() > Asset::kMaximumBlueprintNodesUVE) {
        emitError(0U, "BP_TOO_MANY_NODES", "Blueprint exceeds maximum node limit.");
    }
    if (blueprint.variables.size() > Asset::kMaximumBlueprintVariablesUVE) {
        emitError(0U, "BP_TOO_MANY_VARIABLES", "Blueprint exceeds maximum variable limit.");
    }

    std::set<std::string> varNames;
    for (const Asset::BlueprintVariableUVE& var : blueprint.variables) {
        if (var.name.empty() || var.name.size() > Asset::kMaximumBlueprintNameBytesUVE) {
            emitError(0U, "BP_INVALID_VAR_NAME", "Variable has empty or oversized name.");
        } else if (!varNames.insert(var.name).second) {
            emitError(0U, "BP_DUPLICATE_VAR", "Duplicate variable name: " + var.name);
        }
        if (var.type == Asset::BlueprintPinTypeUVE::Exec || var.type > Asset::BlueprintPinTypeUVE::StringArray) {
            emitError(0U, "BP_INVALID_VAR_TYPE", "Variable has invalid pin type: " + var.name);
        }
    }

    std::unordered_map<std::uint32_t, const Asset::BlueprintNodeUVE*> nodeById;
    for (const Asset::BlueprintNodeUVE& node : blueprint.nodes) {
        if (node.id == 0U) {
            emitError(0U, "BP_ZERO_NODE_ID", "Node ID cannot be 0.");
            continue;
        }
        if (!nodeById.emplace(node.id, &node).second) {
            emitError(node.id, "BP_DUPLICATE_NODE_ID", "Duplicate node ID.");
        }
        if (!std::isfinite(node.x) || !std::isfinite(node.y)) {
            emitError(node.id, "BP_NON_FINITE_POS", "Node position contains non-finite coordinates.");
        }
        const Asset::BlueprintNodeTypeUVE* const type = Asset::FindBlueprintNodeTypeUVE(node.type);
        if (type == nullptr) {
            emitError(node.id, "BP_UNKNOWN_NODE_TYPE", "Unknown node type: " + node.type);
        } else if ((type->role == Asset::BlueprintNodeRoleUVE::GetVariable ||
                    type->role == Asset::BlueprintNodeRoleUVE::SetVariable) &&
                   !varNames.contains(node.variable)) {
            emitWarning(node.id, "BP_UNDECLARED_VARIABLE",
                        "Variable node references undeclared variable: " + node.variable);
        }
    }

    for (const Asset::BlueprintWireUVE& wire : blueprint.wires) {
        const auto fromIt = nodeById.find(wire.fromNode);
        const auto toIt = nodeById.find(wire.toNode);
        if (fromIt == nodeById.end() || toIt == nodeById.end()) {
            emitError(wire.fromNode, "BP_DANGLING_WIRE", "Wire references a missing node.");
            continue;
        }
        if (wire.fromNode == wire.toNode) {
            emitError(wire.fromNode, "BP_SELF_WIRE", "Wire connects a node to itself.");
            continue;
        }
        const Asset::BlueprintNodePinsUVE fromPins = Asset::ResolveBlueprintNodePinsUVE(blueprint, *fromIt->second);
        const Asset::BlueprintNodePinsUVE toPins = Asset::ResolveBlueprintNodePinsUVE(blueprint, *toIt->second);
        if (wire.fromPin >= fromPins.outputs.size()) {
            emitError(wire.fromNode, "BP_INVALID_OUT_PIN", "Wire source pin index out of range.");
            continue;
        }
        if (wire.toPin >= toPins.inputs.size()) {
            emitError(wire.toNode, "BP_INVALID_IN_PIN", "Wire destination pin index out of range.");
            continue;
        }
        if (!Core::CanConnectBlueprintPinTypesUVE(fromPins.outputs[wire.fromPin].type, toPins.inputs[wire.toPin].type)) {
            emitError(wire.toNode, "BP_TYPE_MISMATCH", "Wire connects incompatible pin types.");
        }
    }

    return report;
}

BlueprintAssetValidationReportUVE ValidateFunctionLibraryAssetUVE(const BlueprintFunctionLibraryAssetUVE& library) {
    BlueprintAssetValidationReportUVE report;
    if (library.libraryName.empty()) {
        report.issues.push_back(
            {BlueprintAssetValidationSeverityUVE::Error, 0U, "BP_LIB_NAME", "Function library name is empty."});
    }
    std::set<std::string> seen;
    for (const BlueprintFunctionDefinitionUVE& fn : library.functions) {
        if (fn.name.empty()) {
            report.issues.push_back(
                {BlueprintAssetValidationSeverityUVE::Error, 0U, "BP_FN_NAME", "Function name is empty."});
        } else if (!seen.insert(fn.name).second) {
            report.issues.push_back({BlueprintAssetValidationSeverityUVE::Error, 0U, "BP_FN_DUPLICATE",
                                     "Duplicate function name: " + fn.name});
        }
        const BlueprintAssetValidationReportUVE sub = ValidateBlueprintAssetDetailedUVE(fn.bodyGraph);
        report.issues.insert(report.issues.end(), sub.issues.begin(), sub.issues.end());
    }
    return report;
}

BlueprintAssetValidationReportUVE ValidateMacroLibraryAssetUVE(const BlueprintMacroLibraryAssetUVE& library) {
    BlueprintAssetValidationReportUVE report;
    if (library.libraryName.empty()) {
        report.issues.push_back(
            {BlueprintAssetValidationSeverityUVE::Error, 0U, "BP_MACRO_LIB_NAME", "Macro library name is empty."});
    }
    std::set<std::string> seen;
    for (const BlueprintMacroDefinitionUVE& macro : library.macros) {
        if (macro.name.empty()) {
            report.issues.push_back(
                {BlueprintAssetValidationSeverityUVE::Error, 0U, "BP_MACRO_NAME", "Macro name is empty."});
        } else if (!seen.insert(macro.name).second) {
            report.issues.push_back({BlueprintAssetValidationSeverityUVE::Error, 0U, "BP_MACRO_DUPLICATE",
                                     "Duplicate macro name: " + macro.name});
        }
        const BlueprintAssetValidationReportUVE sub = ValidateBlueprintAssetDetailedUVE(macro.subgraph);
        report.issues.insert(report.issues.end(), sub.issues.begin(), sub.issues.end());
    }
    return report;
}

BlueprintAssetValidationReportUVE ValidateInterfaceAssetUVE(const BlueprintInterfaceAssetUVE& iface) {
    BlueprintAssetValidationReportUVE report;
    if (iface.interfaceName.empty()) {
        report.issues.push_back(
            {BlueprintAssetValidationSeverityUVE::Error, 0U, "BP_IFACE_NAME", "Interface name is empty."});
    }
    std::set<std::string> seen;
    for (const BlueprintInterfaceMethodUVE& method : iface.methods) {
        if (method.name.empty()) {
            report.issues.push_back(
                {BlueprintAssetValidationSeverityUVE::Error, 0U, "BP_IFACE_METHOD_NAME", "Method name is empty."});
        } else if (!seen.insert(method.name).second) {
            report.issues.push_back({BlueprintAssetValidationSeverityUVE::Error, 0U, "BP_IFACE_METHOD_DUPLICATE",
                                     "Duplicate interface method: " + method.name});
        }
    }
    return report;
}

} // namespace UVE::Blueprint::Assets
