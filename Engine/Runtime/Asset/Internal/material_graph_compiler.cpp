// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "material_graph_asset.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace UVE::Asset {
namespace {

[[nodiscard]] std::string FormatFloatUVE(const float v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.5f", static_cast<double>(v));
    return std::string(buf);
}

[[nodiscard]] MaterialPinTypeUVE OutputTypeForNodeUVE(const MaterialGraphNodeKindUVE kind) noexcept {
    switch (kind) {
        case MaterialGraphNodeKindUVE::ConstantFloat:
        case MaterialGraphNodeKindUVE::Time:
        case MaterialGraphNodeKindUVE::Sin:
        case MaterialGraphNodeKindUVE::Fresnel:
        case MaterialGraphNodeKindUVE::SimplexNoise:
            return MaterialPinTypeUVE::Float;
        case MaterialGraphNodeKindUVE::UvCoord:
            return MaterialPinTypeUVE::Vec2;
        case MaterialGraphNodeKindUVE::ConstantVec3:
        case MaterialGraphNodeKindUVE::Color:
        case MaterialGraphNodeKindUVE::TextureSample:
        case MaterialGraphNodeKindUVE::Add:
        case MaterialGraphNodeKindUVE::Subtract:
        case MaterialGraphNodeKindUVE::Multiply:
        case MaterialGraphNodeKindUVE::Lerp:
        case MaterialGraphNodeKindUVE::Clamp:
        case MaterialGraphNodeKindUVE::Pow:
        case MaterialGraphNodeKindUVE::PbrOutput:
            return MaterialPinTypeUVE::Vec3;
    }
    return MaterialPinTypeUVE::Float;
}

[[nodiscard]] std::string CastExprUVE(const std::string& expr, const MaterialPinTypeUVE fromType,
                                      const MaterialPinTypeUVE toType) {
    if (fromType == toType) {
        return expr;
    }
    if (toType == MaterialPinTypeUVE::Float) {
        if (fromType == MaterialPinTypeUVE::Vec2 || fromType == MaterialPinTypeUVE::Vec3 ||
            fromType == MaterialPinTypeUVE::Vec4) {
            return "(" + expr + ").x";
        }
    } else if (toType == MaterialPinTypeUVE::Vec2) {
        if (fromType == MaterialPinTypeUVE::Float) return "vec2(" + expr + ")";
        if (fromType == MaterialPinTypeUVE::Vec3 || fromType == MaterialPinTypeUVE::Vec4)
            return "(" + expr + ").xy";
    } else if (toType == MaterialPinTypeUVE::Vec3) {
        if (fromType == MaterialPinTypeUVE::Float) return "vec3(" + expr + ")";
        if (fromType == MaterialPinTypeUVE::Vec2) return "vec3(" + expr + ", 0.0)";
        if (fromType == MaterialPinTypeUVE::Vec4) return "(" + expr + ").xyz";
    } else if (toType == MaterialPinTypeUVE::Vec4) {
        if (fromType == MaterialPinTypeUVE::Float) return "vec4(" + expr + ")";
        if (fromType == MaterialPinTypeUVE::Vec2) return "vec4(" + expr + ", 0.0, 1.0)";
        if (fromType == MaterialPinTypeUVE::Vec3) return "vec4(" + expr + ", 1.0)";
    }
    return expr;
}

} // namespace

MaterialGraphAssetUVE CreateDefaultPbrMaterialGraphUVE() {
    MaterialGraphAssetUVE graph{};
    graph.name = "DefaultPbrMaterial";
    MaterialGraphNodeUVE colorNode{};
    colorNode.id = 1U;
    colorNode.kind = MaterialGraphNodeKindUVE::Color;
    colorNode.label = "Base Color";
    colorNode.posX = 80.0F;
    colorNode.posY = 120.0F;
    colorNode.value = {0.75F, 0.78F, 0.82F, 1.0F};

    MaterialGraphNodeUVE roughNode{};
    roughNode.id = 2U;
    roughNode.kind = MaterialGraphNodeKindUVE::ConstantFloat;
    roughNode.label = "Roughness";
    roughNode.posX = 80.0F;
    roughNode.posY = 260.0F;
    roughNode.value = {0.45F, 0.0F, 0.0F, 0.0F};

    MaterialGraphNodeUVE outNode{};
    outNode.id = 3U;
    outNode.kind = MaterialGraphNodeKindUVE::PbrOutput;
    outNode.label = "PBR Surface Output";
    outNode.posX = 420.0F;
    outNode.posY = 160.0F;

    graph.nodes = {colorNode, roughNode, outNode};
    // Output inputs: 0=BaseColor, 1=Metallic, 2=Roughness, 3=Emissive, 4=Opacity, 5=WorldPositionOffset
    graph.links = {
        MaterialGraphLinkUVE{1U, 0U, 3U, 0U},
        MaterialGraphLinkUVE{2U, 0U, 3U, 2U},
    };
    return graph;
}

const char* MaterialGraphNodeKindNameUVE(const MaterialGraphNodeKindUVE kind) noexcept {
    switch (kind) {
        case MaterialGraphNodeKindUVE::PbrOutput: return "PBR Output";
        case MaterialGraphNodeKindUVE::ConstantFloat: return "Float";
        case MaterialGraphNodeKindUVE::ConstantVec3: return "Vector3";
        case MaterialGraphNodeKindUVE::Color: return "Color";
        case MaterialGraphNodeKindUVE::UvCoord: return "UV";
        case MaterialGraphNodeKindUVE::Time: return "Time";
        case MaterialGraphNodeKindUVE::TextureSample: return "Texture Sample";
        case MaterialGraphNodeKindUVE::Add: return "Add";
        case MaterialGraphNodeKindUVE::Subtract: return "Subtract";
        case MaterialGraphNodeKindUVE::Multiply: return "Multiply";
        case MaterialGraphNodeKindUVE::Lerp: return "Lerp";
        case MaterialGraphNodeKindUVE::Clamp: return "Clamp";
        case MaterialGraphNodeKindUVE::Pow: return "Pow";
        case MaterialGraphNodeKindUVE::Sin: return "Sin";
        case MaterialGraphNodeKindUVE::Fresnel: return "Fresnel";
        case MaterialGraphNodeKindUVE::SimplexNoise: return "Noise";
    }
    return "Node";
}

MaterialGraphCompileResultUVE CompileMaterialGraphToGlslUVE(const MaterialGraphAssetUVE& graph) {
    MaterialGraphCompileResultUVE result{};
    std::unordered_map<std::uint32_t, const MaterialGraphNodeUVE*> byId;
    const MaterialGraphNodeUVE* outputNode = nullptr;
    for (const MaterialGraphNodeUVE& node : graph.nodes) {
        if (byId.find(node.id) != byId.end()) {
            result.errors.push_back("Duplicate node ID " + std::to_string(node.id));
            return result;
        }
        byId.emplace(node.id, &node);
        if (node.kind == MaterialGraphNodeKindUVE::PbrOutput) {
            if (outputNode != nullptr) {
                result.errors.push_back("Multiple PBR Output nodes are not allowed");
                return result;
            }
            outputNode = &node;
        }
    }
    if (outputNode == nullptr) {
        result.errors.push_back("Graph is missing a PBR Output node");
        return result;
    }

    // Build incoming links map per (toNodeId, toInputIndex) and validate links.
    struct LinkKeyUVE final {
        std::uint32_t nodeId = 0U;
        std::uint32_t inputIdx = 0U;
        bool operator==(const LinkKeyUVE&) const = default;
    };
    struct LinkKeyHashUVE final {
        std::size_t operator()(const LinkKeyUVE& k) const noexcept {
            return (static_cast<std::size_t>(k.nodeId) << 16U) ^ static_cast<std::size_t>(k.inputIdx);
        }
    };
    std::unordered_map<LinkKeyUVE, std::uint32_t, LinkKeyHashUVE> inputSource;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> deps;
    for (const MaterialGraphLinkUVE& link : graph.links) {
        if (byId.find(link.fromNodeId) == byId.end() || byId.find(link.toNodeId) == byId.end()) {
            result.errors.push_back("Link references unknown node ID");
            return result;
        }
        inputSource[LinkKeyUVE{link.toNodeId, link.toInputIndex}] = link.fromNodeId;
        deps[link.toNodeId].push_back(link.fromNodeId);
    }

    // Topological sort from PbrOutput with cycle detection.
    std::vector<std::uint32_t> order;
    std::unordered_map<std::uint32_t, int> state; // 0=unvisited, 1=visiting, 2=done
    bool hasCycle = false;
    auto dfs = [&](auto& self, const std::uint32_t id) -> void {
        if (hasCycle) return;
        const int st = state[id];
        if (st == 1) {
            hasCycle = true;
            return;
        }
        if (st == 2) return;
        state[id] = 1;
        const auto depIt = deps.find(id);
        if (depIt != deps.end()) {
            for (const std::uint32_t dep : depIt->second) {
                self(self, dep);
            }
        }
        state[id] = 2;
        order.push_back(id);
    };
    dfs(dfs, outputNode->id);
    if (hasCycle) {
        result.errors.push_back("Cycle detected in material graph");
        return result;
    }

    std::ostringstream ss;
    ss << "// Generated by UniVex Visual Material Graph: " << graph.name << "\n";
    ss << "#version 430 core\n";
    ss << "#ifdef FRAGMENT_SHADER\n";
    ss << "float uveHash21(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }\n";
    ss << "float uveNoise2D(vec2 p) {\n"
       << "    vec2 i = floor(p); vec2 f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
       << "    float a = uveHash21(i); float b = uveHash21(i + vec2(1.0, 0.0));\n"
       << "    float c = uveHash21(i + vec2(0.0, 1.0)); float d = uveHash21(i + vec2(1.0, 1.0));\n"
       << "    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);\n"
       << "}\n";
    ss << "struct UveMaterialSurface {\n"
       << "    vec3 baseColor;\n"
       << "    float metallic;\n"
       << "    float roughness;\n"
       << "    vec3 emissive;\n"
       << "    float opacity;\n"
       << "    vec3 worldOffset;\n"
       << "};\n";
    ss << "UveMaterialSurface EvaluateMaterialGraphUVE(vec2 vUv, vec3 vNormal, vec3 vViewDir, float uTime, sampler2D uAlbedoMap) {\n";

    std::unordered_map<std::uint32_t, std::string> varName;
    std::unordered_map<std::uint32_t, MaterialPinTypeUVE> varType;

    const auto readInput = [&](const std::uint32_t nodeId, const std::uint32_t idx,
                               const MaterialPinTypeUVE expected, const std::string& fallback) -> std::string {
        const auto it = inputSource.find(LinkKeyUVE{nodeId, idx});
        if (it == inputSource.end()) {
            return fallback;
        }
        return CastExprUVE(varName[it->second], varType[it->second], expected);
    };

    for (const std::uint32_t id : order) {
        const MaterialGraphNodeUVE& n = *byId[id];
        if (n.kind == MaterialGraphNodeKindUVE::PbrOutput) {
            continue;
        }
        const std::string vn = "n_" + std::to_string(id);
        const MaterialPinTypeUVE outT = OutputTypeForNodeUVE(n.kind);
        varName[id] = vn;
        varType[id] = outT;

        switch (n.kind) {
            case MaterialGraphNodeKindUVE::ConstantFloat:
                ss << "    float " << vn << " = " << FormatFloatUVE(n.value[0]) << ";\n";
                break;
            case MaterialGraphNodeKindUVE::ConstantVec3:
            case MaterialGraphNodeKindUVE::Color:
                ss << "    vec3 " << vn << " = vec3(" << FormatFloatUVE(n.value[0]) << ", "
                   << FormatFloatUVE(n.value[1]) << ", " << FormatFloatUVE(n.value[2]) << ");\n";
                break;
            case MaterialGraphNodeKindUVE::UvCoord:
                ss << "    vec2 " << vn << " = vUv;\n";
                break;
            case MaterialGraphNodeKindUVE::Time:
                ss << "    float " << vn << " = uTime;\n";
                break;
            case MaterialGraphNodeKindUVE::TextureSample: {
                const std::string uv = readInput(id, 0U, MaterialPinTypeUVE::Vec2, "vUv");
                ss << "    vec3 " << vn << " = texture(uAlbedoMap, " << uv << ").rgb;\n";
                break;
            }
            case MaterialGraphNodeKindUVE::Add: {
                const std::string a = readInput(id, 0U, MaterialPinTypeUVE::Vec3, "vec3(0.0)");
                const std::string b = readInput(id, 1U, MaterialPinTypeUVE::Vec3, "vec3(0.0)");
                ss << "    vec3 " << vn << " = (" << a << " + " << b << ");\n";
                break;
            }
            case MaterialGraphNodeKindUVE::Subtract: {
                const std::string a = readInput(id, 0U, MaterialPinTypeUVE::Vec3, "vec3(0.0)");
                const std::string b = readInput(id, 1U, MaterialPinTypeUVE::Vec3, "vec3(0.0)");
                ss << "    vec3 " << vn << " = (" << a << " - " << b << ");\n";
                break;
            }
            case MaterialGraphNodeKindUVE::Multiply: {
                const std::string a = readInput(id, 0U, MaterialPinTypeUVE::Vec3, "vec3(1.0)");
                const std::string b = readInput(id, 1U, MaterialPinTypeUVE::Vec3, "vec3(1.0)");
                ss << "    vec3 " << vn << " = (" << a << " * " << b << ");\n";
                break;
            }
            case MaterialGraphNodeKindUVE::Lerp: {
                const std::string a = readInput(id, 0U, MaterialPinTypeUVE::Vec3, "vec3(0.0)");
                const std::string b = readInput(id, 1U, MaterialPinTypeUVE::Vec3, "vec3(1.0)");
                const std::string t = readInput(id, 2U, MaterialPinTypeUVE::Float, "0.5");
                ss << "    vec3 " << vn << " = mix(" << a << ", " << b << ", clamp(" << t << ", 0.0, 1.0));\n";
                break;
            }
            case MaterialGraphNodeKindUVE::Clamp: {
                const std::string x = readInput(id, 0U, MaterialPinTypeUVE::Vec3, "vec3(0.0)");
                ss << "    vec3 " << vn << " = clamp(" << x << ", vec3(0.0), vec3(1.0));\n";
                break;
            }
            case MaterialGraphNodeKindUVE::Pow: {
                const std::string a = readInput(id, 0U, MaterialPinTypeUVE::Vec3, "vec3(1.0)");
                const std::string b = readInput(id, 1U, MaterialPinTypeUVE::Vec3, "vec3(1.0)");
                ss << "    vec3 " << vn << " = pow(max(" << a << ", vec3(1e-4)), " << b << ");\n";
                break;
            }
            case MaterialGraphNodeKindUVE::Sin: {
                const std::string x = readInput(id, 0U, MaterialPinTypeUVE::Float, "uTime");
                ss << "    float " << vn << " = sin(" << x << ");\n";
                break;
            }
            case MaterialGraphNodeKindUVE::Fresnel: {
                const std::string exp = readInput(id, 0U, MaterialPinTypeUVE::Float, FormatFloatUVE(std::max(n.value[0], 1.0F)));
                ss << "    float " << vn << " = pow(1.0 - max(dot(normalize(vNormal), normalize(vViewDir)), 0.0), " << exp << ");\n";
                break;
            }
            case MaterialGraphNodeKindUVE::SimplexNoise: {
                const std::string uv = readInput(id, 0U, MaterialPinTypeUVE::Vec2, "vUv * 4.0");
                ss << "    float " << vn << " = uveNoise2D(" << uv << ");\n";
                break;
            }
            case MaterialGraphNodeKindUVE::PbrOutput:
                break;
        }
    }

    const std::uint32_t outId = outputNode->id;
    ss << "    UveMaterialSurface s;\n";
    ss << "    s.baseColor = " << readInput(outId, 0U, MaterialPinTypeUVE::Vec3, "vec3(0.8)") << ";\n";
    ss << "    s.metallic = clamp(" << readInput(outId, 1U, MaterialPinTypeUVE::Float, "0.0") << ", 0.0, 1.0);\n";
    ss << "    s.roughness = clamp(" << readInput(outId, 2U, MaterialPinTypeUVE::Float, "0.5") << ", 0.04, 1.0);\n";
    ss << "    s.emissive = " << readInput(outId, 3U, MaterialPinTypeUVE::Vec3, "vec3(0.0)") << ";\n";
    ss << "    s.opacity = clamp(" << readInput(outId, 4U, MaterialPinTypeUVE::Float, "1.0") << ", 0.0, 1.0);\n";
    ss << "    s.worldOffset = " << readInput(outId, 5U, MaterialPinTypeUVE::Vec3, "vec3(0.0)") << ";\n";
    ss << "    return s;\n";
    ss << "}\n";
    ss << "#endif\n";

    result.glslSource = ss.str();
    result.ok = true;
    return result;
}

bool SaveMaterialGraphAssetUVE(const MaterialGraphAssetUVE& graph, const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "UVMATGRAPH 1\n";
    out << "NAME " << graph.name << "\n";
    for (const MaterialGraphNodeUVE& n : graph.nodes) {
        out << "NODE " << n.id << " " << static_cast<int>(n.kind) << " " << n.posX << " " << n.posY << " "
            << n.value[0] << " " << n.value[1] << " " << n.value[2] << " " << n.value[3] << " |" << n.label
            << "|" << n.textureSlotName << "\n";
    }
    for (const MaterialGraphLinkUVE& l : graph.links) {
        out << "LINK " << l.fromNodeId << " " << l.fromOutputIndex << " " << l.toNodeId << " "
            << l.toInputIndex << "\n";
    }
    return static_cast<bool>(out);
}

bool LoadMaterialGraphAssetUVE(const std::filesystem::path& path, MaterialGraphAssetUVE& outGraph) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    MaterialGraphAssetUVE parsed{};
    std::string line;
    if (!std::getline(in, line) || line.rfind("UVMATGRAPH", 0U) != 0U) {
        return false;
    }
    while (std::getline(in, line)) {
        if (line.rfind("NAME ", 0U) == 0U) {
            parsed.name = line.substr(5U);
        } else if (line.rfind("NODE ", 0U) == 0U) {
            const std::size_t bar1 = line.find('|');
            const std::string header = bar1 == std::string::npos ? line.substr(5U) : line.substr(5U, bar1 - 5U);
            std::istringstream ls(header);
            MaterialGraphNodeUVE n{};
            int kindInt = 0;
            if (!(ls >> n.id >> kindInt >> n.posX >> n.posY >> n.value[0] >> n.value[1] >> n.value[2] >>
                  n.value[3])) {
                continue;
            }
            n.kind = static_cast<MaterialGraphNodeKindUVE>(kindInt);
            if (bar1 != std::string::npos) {
                const std::size_t bar2 = line.find('|', bar1 + 1U);
                if (bar2 != std::string::npos) {
                    n.label = line.substr(bar1 + 1U, bar2 - bar1 - 1U);
                    n.textureSlotName = line.substr(bar2 + 1U);
                } else {
                    n.label = line.substr(bar1 + 1U);
                }
            }
            parsed.nodes.push_back(std::move(n));
        } else if (line.rfind("LINK ", 0U) == 0U) {
            std::istringstream ls(line.substr(5U));
            MaterialGraphLinkUVE l{};
            if (ls >> l.fromNodeId >> l.fromOutputIndex >> l.toNodeId >> l.toInputIndex) {
                parsed.links.push_back(l);
            }
        }
    }
    outGraph = std::move(parsed);
    return true;
}

} // namespace UVE::Asset
