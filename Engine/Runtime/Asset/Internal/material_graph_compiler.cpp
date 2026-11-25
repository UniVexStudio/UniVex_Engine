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

[[nodiscard]] std::string FormatFloat(const float v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.5f", static_cast<double>(v));
    return std::string(buf);
}

[[nodiscard]] MaterialPinType OutputTypeForNode(const MaterialGraphNodeKind kind) noexcept {
    switch (kind) {
        case MaterialGraphNodeKind::ConstantFloat:
        case MaterialGraphNodeKind::Time:
        case MaterialGraphNodeKind::Sin:
        case MaterialGraphNodeKind::Fresnel:
        case MaterialGraphNodeKind::SimplexNoise:
            return MaterialPinType::Float;
        case MaterialGraphNodeKind::UvCoord:
            return MaterialPinType::Vec2;
        case MaterialGraphNodeKind::ConstantVec3:
        case MaterialGraphNodeKind::Color:
        case MaterialGraphNodeKind::TextureSample:
        case MaterialGraphNodeKind::Add:
        case MaterialGraphNodeKind::Subtract:
        case MaterialGraphNodeKind::Multiply:
        case MaterialGraphNodeKind::Lerp:
        case MaterialGraphNodeKind::Clamp:
        case MaterialGraphNodeKind::Pow:
        case MaterialGraphNodeKind::PbrOutput:
            return MaterialPinType::Vec3;
    }
    return MaterialPinType::Float;
}

[[nodiscard]] std::string CastExpr(const std::string& expr, const MaterialPinType fromType,
                                      const MaterialPinType toType) {
    if (fromType == toType) {
        return expr;
    }
    if (toType == MaterialPinType::Float) {
        if (fromType == MaterialPinType::Vec2 || fromType == MaterialPinType::Vec3 ||
            fromType == MaterialPinType::Vec4) {
            return "(" + expr + ").x";
        }
    } else if (toType == MaterialPinType::Vec2) {
        if (fromType == MaterialPinType::Float) return "vec2(" + expr + ")";
        if (fromType == MaterialPinType::Vec3 || fromType == MaterialPinType::Vec4)
            return "(" + expr + ").xy";
    } else if (toType == MaterialPinType::Vec3) {
        if (fromType == MaterialPinType::Float) return "vec3(" + expr + ")";
        if (fromType == MaterialPinType::Vec2) return "vec3(" + expr + ", 0.0)";
        if (fromType == MaterialPinType::Vec4) return "(" + expr + ").xyz";
    } else if (toType == MaterialPinType::Vec4) {
        if (fromType == MaterialPinType::Float) return "vec4(" + expr + ")";
        if (fromType == MaterialPinType::Vec2) return "vec4(" + expr + ", 0.0, 1.0)";
        if (fromType == MaterialPinType::Vec3) return "vec4(" + expr + ", 1.0)";
    }
    return expr;
}

} // namespace

MaterialGraphAsset CreateDefaultPbrMaterialGraph() {
    MaterialGraphAsset graph{};
    graph.name = "DefaultPbrMaterial";
    MaterialGraphNode colorNode{};
    colorNode.id = 1U;
    colorNode.kind = MaterialGraphNodeKind::Color;
    colorNode.label = "Base Color";
    colorNode.posX = 80.0F;
    colorNode.posY = 120.0F;
    colorNode.value = {0.75F, 0.78F, 0.82F, 1.0F};

    MaterialGraphNode roughNode{};
    roughNode.id = 2U;
    roughNode.kind = MaterialGraphNodeKind::ConstantFloat;
    roughNode.label = "Roughness";
    roughNode.posX = 80.0F;
    roughNode.posY = 260.0F;
    roughNode.value = {0.45F, 0.0F, 0.0F, 0.0F};

    MaterialGraphNode outNode{};
    outNode.id = 3U;
    outNode.kind = MaterialGraphNodeKind::PbrOutput;
    outNode.label = "PBR Surface Output";
    outNode.posX = 420.0F;
    outNode.posY = 160.0F;

    graph.nodes = {colorNode, roughNode, outNode};
    // Output inputs: 0=BaseColor, 1=Metallic, 2=Roughness, 3=Emissive, 4=Opacity, 5=WorldPositionOffset
    graph.links = {
        MaterialGraphLink{1U, 0U, 3U, 0U},
        MaterialGraphLink{2U, 0U, 3U, 2U},
    };
    return graph;
}

const char* MaterialGraphNodeKindName(const MaterialGraphNodeKind kind) noexcept {
    switch (kind) {
        case MaterialGraphNodeKind::PbrOutput: return "PBR Output";
        case MaterialGraphNodeKind::ConstantFloat: return "Float";
        case MaterialGraphNodeKind::ConstantVec3: return "Vector3";
        case MaterialGraphNodeKind::Color: return "Color";
        case MaterialGraphNodeKind::UvCoord: return "UV";
        case MaterialGraphNodeKind::Time: return "Time";
        case MaterialGraphNodeKind::TextureSample: return "Texture Sample";
        case MaterialGraphNodeKind::Add: return "Add";
        case MaterialGraphNodeKind::Subtract: return "Subtract";
        case MaterialGraphNodeKind::Multiply: return "Multiply";
        case MaterialGraphNodeKind::Lerp: return "Lerp";
        case MaterialGraphNodeKind::Clamp: return "Clamp";
        case MaterialGraphNodeKind::Pow: return "Pow";
        case MaterialGraphNodeKind::Sin: return "Sin";
        case MaterialGraphNodeKind::Fresnel: return "Fresnel";
        case MaterialGraphNodeKind::SimplexNoise: return "Noise";
    }
    return "Node";
}

MaterialGraphCompileResult CompileMaterialGraphToGlsl(const MaterialGraphAsset& graph) {
    MaterialGraphCompileResult result{};
    std::unordered_map<std::uint32_t, const MaterialGraphNode*> byId;
    const MaterialGraphNode* outputNode = nullptr;
    for (const MaterialGraphNode& node : graph.nodes) {
        if (byId.find(node.id) != byId.end()) {
            result.errors.push_back("Duplicate node ID " + std::to_string(node.id));
            return result;
        }
        byId.emplace(node.id, &node);
        if (node.kind == MaterialGraphNodeKind::PbrOutput) {
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
    struct LinkKey final {
        std::uint32_t nodeId = 0U;
        std::uint32_t inputIdx = 0U;
        bool operator==(const LinkKey&) const = default;
    };
    struct LinkKeyHash final {
        std::size_t operator()(const LinkKey& k) const noexcept {
            return (static_cast<std::size_t>(k.nodeId) << 16U) ^ static_cast<std::size_t>(k.inputIdx);
        }
    };
    std::unordered_map<LinkKey, std::uint32_t, LinkKeyHash> inputSource;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> deps;
    for (const MaterialGraphLink& link : graph.links) {
        if (byId.find(link.fromNodeId) == byId.end() || byId.find(link.toNodeId) == byId.end()) {
            result.errors.push_back("Link references unknown node ID");
            return result;
        }
        inputSource[LinkKey{link.toNodeId, link.toInputIndex}] = link.fromNodeId;
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
    ss << "UveMaterialSurface EvaluateMaterialGraph(vec2 vUv, vec3 vNormal, vec3 vViewDir, float uTime, sampler2D uAlbedoMap) {\n";

    std::unordered_map<std::uint32_t, std::string> varName;
    std::unordered_map<std::uint32_t, MaterialPinType> varType;

    const auto readInput = [&](const std::uint32_t nodeId, const std::uint32_t idx,
                               const MaterialPinType expected, const std::string& fallback) -> std::string {
        const auto it = inputSource.find(LinkKey{nodeId, idx});
        if (it == inputSource.end()) {
            return fallback;
        }
        return CastExpr(varName[it->second], varType[it->second], expected);
    };

    for (const std::uint32_t id : order) {
        const MaterialGraphNode& n = *byId[id];
        if (n.kind == MaterialGraphNodeKind::PbrOutput) {
            continue;
        }
        const std::string vn = "n_" + std::to_string(id);
        const MaterialPinType outT = OutputTypeForNode(n.kind);
        varName[id] = vn;
        varType[id] = outT;

        switch (n.kind) {
            case MaterialGraphNodeKind::ConstantFloat:
                ss << "    float " << vn << " = " << FormatFloat(n.value[0]) << ";\n";
                break;
            case MaterialGraphNodeKind::ConstantVec3:
            case MaterialGraphNodeKind::Color:
                ss << "    vec3 " << vn << " = vec3(" << FormatFloat(n.value[0]) << ", "
                   << FormatFloat(n.value[1]) << ", " << FormatFloat(n.value[2]) << ");\n";
                break;
            case MaterialGraphNodeKind::UvCoord:
                ss << "    vec2 " << vn << " = vUv;\n";
                break;
            case MaterialGraphNodeKind::Time:
                ss << "    float " << vn << " = uTime;\n";
                break;
            case MaterialGraphNodeKind::TextureSample: {
                const std::string uv = readInput(id, 0U, MaterialPinType::Vec2, "vUv");
                ss << "    vec3 " << vn << " = texture(uAlbedoMap, " << uv << ").rgb;\n";
                break;
            }
            case MaterialGraphNodeKind::Add: {
                const std::string a = readInput(id, 0U, MaterialPinType::Vec3, "vec3(0.0)");
                const std::string b = readInput(id, 1U, MaterialPinType::Vec3, "vec3(0.0)");
                ss << "    vec3 " << vn << " = (" << a << " + " << b << ");\n";
                break;
            }
            case MaterialGraphNodeKind::Subtract: {
                const std::string a = readInput(id, 0U, MaterialPinType::Vec3, "vec3(0.0)");
                const std::string b = readInput(id, 1U, MaterialPinType::Vec3, "vec3(0.0)");
                ss << "    vec3 " << vn << " = (" << a << " - " << b << ");\n";
                break;
            }
            case MaterialGraphNodeKind::Multiply: {
                const std::string a = readInput(id, 0U, MaterialPinType::Vec3, "vec3(1.0)");
                const std::string b = readInput(id, 1U, MaterialPinType::Vec3, "vec3(1.0)");
                ss << "    vec3 " << vn << " = (" << a << " * " << b << ");\n";
                break;
            }
            case MaterialGraphNodeKind::Lerp: {
                const std::string a = readInput(id, 0U, MaterialPinType::Vec3, "vec3(0.0)");
                const std::string b = readInput(id, 1U, MaterialPinType::Vec3, "vec3(1.0)");
                const std::string t = readInput(id, 2U, MaterialPinType::Float, "0.5");
                ss << "    vec3 " << vn << " = mix(" << a << ", " << b << ", clamp(" << t << ", 0.0, 1.0));\n";
                break;
            }
            case MaterialGraphNodeKind::Clamp: {
                const std::string x = readInput(id, 0U, MaterialPinType::Vec3, "vec3(0.0)");
                ss << "    vec3 " << vn << " = clamp(" << x << ", vec3(0.0), vec3(1.0));\n";
                break;
            }
            case MaterialGraphNodeKind::Pow: {
                const std::string a = readInput(id, 0U, MaterialPinType::Vec3, "vec3(1.0)");
                const std::string b = readInput(id, 1U, MaterialPinType::Vec3, "vec3(1.0)");
                ss << "    vec3 " << vn << " = pow(max(" << a << ", vec3(1e-4)), " << b << ");\n";
                break;
            }
            case MaterialGraphNodeKind::Sin: {
                const std::string x = readInput(id, 0U, MaterialPinType::Float, "uTime");
                ss << "    float " << vn << " = sin(" << x << ");\n";
                break;
            }
            case MaterialGraphNodeKind::Fresnel: {
                const std::string exp = readInput(id, 0U, MaterialPinType::Float, FormatFloat(std::max(n.value[0], 1.0F)));
                ss << "    float " << vn << " = pow(1.0 - max(dot(normalize(vNormal), normalize(vViewDir)), 0.0), " << exp << ");\n";
                break;
            }
            case MaterialGraphNodeKind::SimplexNoise: {
                const std::string uv = readInput(id, 0U, MaterialPinType::Vec2, "vUv * 4.0");
                ss << "    float " << vn << " = uveNoise2D(" << uv << ");\n";
                break;
            }
            case MaterialGraphNodeKind::PbrOutput:
                break;
        }
    }

    const std::uint32_t outId = outputNode->id;
    ss << "    UveMaterialSurface s;\n";
    ss << "    s.baseColor = " << readInput(outId, 0U, MaterialPinType::Vec3, "vec3(0.8)") << ";\n";
    ss << "    s.metallic = clamp(" << readInput(outId, 1U, MaterialPinType::Float, "0.0") << ", 0.0, 1.0);\n";
    ss << "    s.roughness = clamp(" << readInput(outId, 2U, MaterialPinType::Float, "0.5") << ", 0.04, 1.0);\n";
    ss << "    s.emissive = " << readInput(outId, 3U, MaterialPinType::Vec3, "vec3(0.0)") << ";\n";
    ss << "    s.opacity = clamp(" << readInput(outId, 4U, MaterialPinType::Float, "1.0") << ", 0.0, 1.0);\n";
    ss << "    s.worldOffset = " << readInput(outId, 5U, MaterialPinType::Vec3, "vec3(0.0)") << ";\n";
    ss << "    return s;\n";
    ss << "}\n";
    ss << "#endif\n";

    result.glslSource = ss.str();
    result.ok = true;
    return result;
}

bool SaveMaterialGraphAsset(const MaterialGraphAsset& graph, const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    out << "UVMATGRAPH 1\n";
    out << "NAME " << graph.name << "\n";
    for (const MaterialGraphNode& n : graph.nodes) {
        out << "NODE " << n.id << " " << static_cast<int>(n.kind) << " " << n.posX << " " << n.posY << " "
            << n.value[0] << " " << n.value[1] << " " << n.value[2] << " " << n.value[3] << " |" << n.label
            << "|" << n.textureSlotName << "\n";
    }
    for (const MaterialGraphLink& l : graph.links) {
        out << "LINK " << l.fromNodeId << " " << l.fromOutputIndex << " " << l.toNodeId << " "
            << l.toInputIndex << "\n";
    }
    return static_cast<bool>(out);
}

bool LoadMaterialGraphAsset(const std::filesystem::path& path, MaterialGraphAsset& outGraph) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    MaterialGraphAsset parsed{};
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
            MaterialGraphNode n{};
            int kindInt = 0;
            if (!(ls >> n.id >> kindInt >> n.posX >> n.posY >> n.value[0] >> n.value[1] >> n.value[2] >>
                  n.value[3])) {
                continue;
            }
            n.kind = static_cast<MaterialGraphNodeKind>(kindInt);
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
            MaterialGraphLink l{};
            if (ls >> l.fromNodeId >> l.fromOutputIndex >> l.toNodeId >> l.toInputIndex) {
                parsed.links.push_back(l);
            }
        }
    }
    outGraph = std::move(parsed);
    return true;
}

} // namespace UVE::Asset
