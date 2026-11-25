// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// An entity file's thumbnail scene: its meshes and primitives gathered into one mesh, each where the
// entity's transforms put it, so the thumbnail renderer frames and shades them with its own camera.
#include "editor_entity_thumbnail.h"

#include "i_asset_database.h"
#include "matrix4x4.h"
#include "primitive_geometry.h"
#include "primitive_mesh_component.h"
#include "quaternion.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <unordered_map>

namespace UVE::Editor {
namespace {

struct EntityNode final {
    std::int64_t parent = -1;
    Math::Vector3 position{};
    Math::Quaternion rotation{};
    Math::Vector3 scale{1.0F, 1.0F, 1.0F};
    std::string name;
    bool camera = false;
    // What it draws, if anything.
    bool primitive = false;
    Scene::PrimitiveMeshKind kind = Scene::PrimitiveMeshKind::Cube;
    Math::Vector3 colour{1.0F, 1.0F, 1.0F};
    Asset::AssetGuid primitiveMaterial{};
    Asset::AssetGuid mesh{};
    Asset::AssetGuid meshMaterial{};
};

[[nodiscard]] Math::Vector3 Vector3Of(const nlohmann::json& value, const Math::Vector3 fallback) {
    if (!value.is_array() || value.size() != 3U || !value[0].is_number() || !value[1].is_number() || !value[2].is_number()) {
        return fallback;
    }
    return Math::Vector3{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}

[[nodiscard]] Asset::AssetGuid GuidOf(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end()) {
        return Asset::AssetGuid{};
    }
    if (found->is_number_unsigned() || found->is_number_integer()) {
        return Asset::AssetGuid{found->get<std::uint64_t>()};
    }
    return Asset::AssetGuid{};
}

/// Appends `source` (in its own space) to `out` through `world`: vertices moved, normals turned,
/// each surface onto a new look slot.
void Append(EntityThumbnailScene& out, const Asset::MeshAsset& source, const Math::Matrix4x4& world,
               const std::vector<MeshThumbnailLook>& looksBySlot) {
    const auto base = static_cast<std::uint32_t>(out.mesh.vertices.size());
    out.mesh.vertices.reserve(out.mesh.vertices.size() + source.vertices.size());
    for (Asset::MeshVertex vertex : source.vertices) {
        const Math::Vector3 at = Math::TransformPoint(world, vertex.position);
        Math::Vector3 normal = Math::TransformPoint(world, vertex.position + vertex.normal) - at;
        const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        vertex.normal = length > 1.0e-8F ? normal * (1.0F / length) : vertex.normal;
        vertex.position = at;
        out.mesh.vertices.push_back(vertex);
    }
    for (std::size_t surface = 0U; surface < source.SurfaceCount(); ++surface) {
        const Asset::MeshSurface range = source.GetSurface(surface);
        Asset::MeshSurface copy;
        copy.firstIndex = static_cast<std::uint32_t>(out.mesh.indices.size());
        copy.indexCount = range.indexCount;
        copy.materialSlot = static_cast<std::uint32_t>(out.looks.size());
        for (std::uint32_t i = 0U; i < range.indexCount && range.firstIndex + i < source.indices.size(); ++i) {
            out.mesh.indices.push_back(base + source.indices[range.firstIndex + i]);
        }
        const std::size_t slot = range.materialSlot;
        out.looks.push_back(slot < looksBySlot.size() ? looksBySlot[slot] : (looksBySlot.empty() ? MeshThumbnailLook{} : looksBySlot.front()));
        out.mesh.materialGuids.push_back(Asset::AssetGuid{});
        out.mesh.surfaces.push_back(copy);
    }
}

} // namespace

bool BuildEntityThumbnailScene(const std::filesystem::path& entityFile, Asset::IAssetDatabase& database,
                                  const std::function<MeshThumbnailLook(Asset::AssetGuid material)>& lookOf,
                                  const std::function<void(Asset::MeshAsset& mesh, const std::filesystem::path& file)>& poseMesh,
                                  EntityThumbnailScene& out) {
    out = EntityThumbnailScene{};
    out.sources.push_back(entityFile);
    std::ifstream stream(entityFile, std::ios::binary);
    if (!stream) {
        return false;
    }
    const std::string bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    // The file's envelope comes first; its payload is the JSON document.
    const std::size_t start = bytes.find('{');
    if (start == std::string::npos) {
        return false;
    }
    const nlohmann::json document = nlohmann::json::parse(bytes.begin() + static_cast<std::ptrdiff_t>(start), bytes.end(), nullptr, false);
    if (!document.is_object() || !document.contains("entities") || !document["entities"].is_array()) {
        return false;
    }
    std::unordered_map<std::int64_t, EntityNode> nodes;
    std::vector<std::int64_t> order;
    for (const nlohmann::json& entity : document["entities"]) {
        if (!entity.is_object() || !entity.contains("components") || !entity["components"].is_object()) {
            continue;
        }
        const std::int64_t id = entity.value("localId", std::int64_t{-1});
        const nlohmann::json& components = entity["components"];
        EntityNode node;
        if (const auto h = components.find("HierarchyComponent"); h != components.end() && h->is_object()) {
            node.parent = h->value("parentLocalId", std::int64_t{-1});
        }
        if (const auto t = components.find("TransformComponent"); t != components.end() && t->is_object()) {
            node.position = Vector3Of(t->value("localPosition", nlohmann::json{}), Math::Vector3{});
            node.scale = Vector3Of(t->value("localScale", nlohmann::json{}), Math::Vector3{1.0F, 1.0F, 1.0F});
            const nlohmann::json r = t->value("localRotation", nlohmann::json{});
            if (r.is_array() && r.size() == 4U) {
                node.rotation = Math::Quaternion{r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>()};
            }
        }
        if (const auto n = components.find("NameComponent"); n != components.end() && n->is_object()) {
            node.name = n->value("name", std::string{});
        }
        node.camera = components.contains("CameraComponent");
        if (const auto p = components.find("PrimitiveMeshComponent"); p != components.end() && p->is_object()) {
            node.primitive = true;
            node.kind = static_cast<Scene::PrimitiveMeshKind>(std::clamp(p->value("kind", 0), 0, 4));
            node.colour = Vector3Of(p->value("baseColor", nlohmann::json{}), Math::Vector3{1.0F, 1.0F, 1.0F});
            node.primitiveMaterial = GuidOf(*p, "materialGuid");
        }
        if (const auto m = components.find("MeshComponent"); m != components.end() && m->is_object()) {
            node.mesh = GuidOf(*m, "meshGuid");
            node.meshMaterial = GuidOf(*m, "materialGuid");
        }
        nodes.emplace(id, std::move(node));
        order.push_back(id);
    }

    // World matrices, parents first (guarding against a loop in a damaged file).
    std::unordered_map<std::int64_t, Math::Matrix4x4> world;
    const auto worldOf = [&](const std::int64_t id) {
        std::vector<std::int64_t> chain;
        for (std::int64_t at = id; at >= 0 && chain.size() <= nodes.size(); ) {
            const auto found = nodes.find(at);
            if (found == nodes.end()) {
                break;
            }
            chain.push_back(at);
            at = found->second.parent;
        }
        Math::Matrix4x4 m = Math::Matrix4x4::Identity();
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const EntityNode& node = nodes[*it];
            // The entity's root stands at the origin, as it is placed in a level.
            const Math::Vector3 position = node.parent < 0 || nodes.find(node.parent) == nodes.end() ? Math::Vector3{} : node.position;
            m = m * Math::Matrix4x4::ComposeTrs(position, node.rotation, node.scale);
        }
        return m;
    };
    std::optional<Math::Vector3> cameraAt;
    for (const std::int64_t id : order) {
        const EntityNode& node = nodes[id];
        const Math::Matrix4x4 m = worldOf(id);
        world.emplace(id, m);
        if (node.camera && !cameraAt.has_value()) {
            std::string lower = node.name;
            std::ranges::transform(lower, lower.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("thumbnail") != std::string::npos) {
                cameraAt = Math::TransformPoint(m, Math::Vector3{});
            }
        }
        if (node.primitive) {
            const Render::PrimitiveGeometry& geometry = Render::GetPrimitiveGeometry(node.kind);
            Asset::MeshAsset primitive;
            primitive.vertices = geometry.vertices;
            primitive.indices = geometry.indices;
            primitive.localBounds = geometry.localBounds;
            MeshThumbnailLook look;
            look.colour = {node.colour.x, node.colour.y, node.colour.z};
            if (node.primitiveMaterial != Asset::AssetGuid{} && lookOf) {
                look = lookOf(node.primitiveMaterial);
            }
            Append(out, primitive, m, {look});
        }
        if (node.mesh != Asset::AssetGuid{}) {
            const std::filesystem::path file = database.Resolve(node.mesh);
            Asset::MeshAsset mesh;
            if (file.empty() || !Asset::LoadMeshAsset(file, mesh)) {
                continue;
            }
            out.sources.push_back(file);
            if (poseMesh) {
                poseMesh(mesh, file);
            }
            std::vector<MeshThumbnailLook> looks(std::max<std::size_t>(1U, mesh.materialGuids.size()));
            for (std::size_t slot = 0U; slot < mesh.materialGuids.size() && lookOf; ++slot) {
                const Asset::AssetGuid material = node.meshMaterial != Asset::AssetGuid{} ? node.meshMaterial : mesh.materialGuids[slot];
                if (material != Asset::AssetGuid{}) {
                    looks[slot] = lookOf(material);
                }
            }
            if (mesh.materialGuids.empty() && node.meshMaterial != Asset::AssetGuid{} && lookOf) {
                looks[0] = lookOf(node.meshMaterial);
            }
            Append(out, mesh, m, looks);
        }
    }
    if (out.mesh.vertices.empty() || out.mesh.indices.empty()) {
        return false;
    }
    Math::Aabb bounds{out.mesh.vertices.front().position, out.mesh.vertices.front().position};
    for (const Asset::MeshVertex& vertex : out.mesh.vertices) {
        bounds.min = Math::Vector3{std::min(bounds.min.x, vertex.position.x), std::min(bounds.min.y, vertex.position.y),
                                      std::min(bounds.min.z, vertex.position.z)};
        bounds.max = Math::Vector3{std::max(bounds.max.x, vertex.position.x), std::max(bounds.max.y, vertex.position.y),
                                      std::max(bounds.max.z, vertex.position.z)};
    }
    out.mesh.localBounds = bounds;
    if (cameraAt.has_value()) {
        const Math::Vector3 d = *cameraAt - bounds.GetCenter();
        const float length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (length > 1.0e-4F) {
            out.viewDirection = d * (1.0F / length);
        }
    }
    return true;
}

} // namespace UVE::Editor
