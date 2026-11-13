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

struct EntityNodeUVE final {
    std::int64_t parent = -1;
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE scale{1.0F, 1.0F, 1.0F};
    std::string name;
    bool camera = false;
    // What it draws, if anything.
    bool primitive = false;
    Scene::PrimitiveMeshKindUVE kind = Scene::PrimitiveMeshKindUVE::Cube;
    Math::Vector3UVE colour{1.0F, 1.0F, 1.0F};
    Asset::AssetGuidUVE primitiveMaterial{};
    Asset::AssetGuidUVE mesh{};
    Asset::AssetGuidUVE meshMaterial{};
};

[[nodiscard]] Math::Vector3UVE Vector3OfUVE(const nlohmann::json& value, const Math::Vector3UVE fallback) {
    if (!value.is_array() || value.size() != 3U || !value[0].is_number() || !value[1].is_number() || !value[2].is_number()) {
        return fallback;
    }
    return Math::Vector3UVE{value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}

[[nodiscard]] Asset::AssetGuidUVE GuidOfUVE(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end()) {
        return Asset::AssetGuidUVE{};
    }
    if (found->is_number_unsigned() || found->is_number_integer()) {
        return Asset::AssetGuidUVE{found->get<std::uint64_t>()};
    }
    return Asset::AssetGuidUVE{};
}

/// Appends `source` (in its own space) to `out` through `world`: vertices moved, normals turned,
/// each surface onto a new look slot.
void AppendUVE(EntityThumbnailSceneUVE& out, const Asset::MeshAssetUVE& source, const Math::Matrix4x4UVE& world,
               const std::vector<MeshThumbnailLookUVE>& looksBySlot) {
    const auto base = static_cast<std::uint32_t>(out.mesh.vertices.size());
    out.mesh.vertices.reserve(out.mesh.vertices.size() + source.vertices.size());
    for (Asset::MeshVertexUVE vertex : source.vertices) {
        const Math::Vector3UVE at = Math::TransformPointUVE(world, vertex.position);
        Math::Vector3UVE normal = Math::TransformPointUVE(world, vertex.position + vertex.normal) - at;
        const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        vertex.normal = length > 1.0e-8F ? normal * (1.0F / length) : vertex.normal;
        vertex.position = at;
        out.mesh.vertices.push_back(vertex);
    }
    for (std::size_t surface = 0U; surface < source.SurfaceCountUVE(); ++surface) {
        const Asset::MeshSurfaceUVE range = source.GetSurfaceUVE(surface);
        Asset::MeshSurfaceUVE copy;
        copy.firstIndex = static_cast<std::uint32_t>(out.mesh.indices.size());
        copy.indexCount = range.indexCount;
        copy.materialSlot = static_cast<std::uint32_t>(out.looks.size());
        for (std::uint32_t i = 0U; i < range.indexCount && range.firstIndex + i < source.indices.size(); ++i) {
            out.mesh.indices.push_back(base + source.indices[range.firstIndex + i]);
        }
        const std::size_t slot = range.materialSlot;
        out.looks.push_back(slot < looksBySlot.size() ? looksBySlot[slot] : (looksBySlot.empty() ? MeshThumbnailLookUVE{} : looksBySlot.front()));
        out.mesh.materialGuids.push_back(Asset::AssetGuidUVE{});
        out.mesh.surfaces.push_back(copy);
    }
}

} // namespace

bool BuildEntityThumbnailSceneUVE(const std::filesystem::path& entityFile, Asset::IAssetDatabaseUVE& database,
                                  const std::function<MeshThumbnailLookUVE(Asset::AssetGuidUVE material)>& lookOf,
                                  const std::function<void(Asset::MeshAssetUVE& mesh, const std::filesystem::path& file)>& poseMesh,
                                  EntityThumbnailSceneUVE& out) {
    out = EntityThumbnailSceneUVE{};
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
    std::unordered_map<std::int64_t, EntityNodeUVE> nodes;
    std::vector<std::int64_t> order;
    for (const nlohmann::json& entity : document["entities"]) {
        if (!entity.is_object() || !entity.contains("components") || !entity["components"].is_object()) {
            continue;
        }
        const std::int64_t id = entity.value("localId", std::int64_t{-1});
        const nlohmann::json& components = entity["components"];
        EntityNodeUVE node;
        if (const auto h = components.find("HierarchyComponentUVE"); h != components.end() && h->is_object()) {
            node.parent = h->value("parentLocalId", std::int64_t{-1});
        }
        if (const auto t = components.find("TransformComponentUVE"); t != components.end() && t->is_object()) {
            node.position = Vector3OfUVE(t->value("localPosition", nlohmann::json{}), Math::Vector3UVE{});
            node.scale = Vector3OfUVE(t->value("localScale", nlohmann::json{}), Math::Vector3UVE{1.0F, 1.0F, 1.0F});
            const nlohmann::json r = t->value("localRotation", nlohmann::json{});
            if (r.is_array() && r.size() == 4U) {
                node.rotation = Math::QuaternionUVE{r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>()};
            }
        }
        if (const auto n = components.find("NameComponentUVE"); n != components.end() && n->is_object()) {
            node.name = n->value("name", std::string{});
        }
        node.camera = components.contains("CameraComponentUVE");
        if (const auto p = components.find("PrimitiveMeshComponentUVE"); p != components.end() && p->is_object()) {
            node.primitive = true;
            node.kind = static_cast<Scene::PrimitiveMeshKindUVE>(std::clamp(p->value("kind", 0), 0, 4));
            node.colour = Vector3OfUVE(p->value("baseColor", nlohmann::json{}), Math::Vector3UVE{1.0F, 1.0F, 1.0F});
            node.primitiveMaterial = GuidOfUVE(*p, "materialGuid");
        }
        if (const auto m = components.find("MeshComponentUVE"); m != components.end() && m->is_object()) {
            node.mesh = GuidOfUVE(*m, "meshGuid");
            node.meshMaterial = GuidOfUVE(*m, "materialGuid");
        }
        nodes.emplace(id, std::move(node));
        order.push_back(id);
    }

    // World matrices, parents first (guarding against a loop in a damaged file).
    std::unordered_map<std::int64_t, Math::Matrix4x4UVE> world;
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
        Math::Matrix4x4UVE m = Math::Matrix4x4UVE::IdentityUVE();
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const EntityNodeUVE& node = nodes[*it];
            // The entity's root stands at the origin, as it is placed in a level.
            const Math::Vector3UVE position = node.parent < 0 || nodes.find(node.parent) == nodes.end() ? Math::Vector3UVE{} : node.position;
            m = m * Math::Matrix4x4UVE::ComposeTrsUVE(position, node.rotation, node.scale);
        }
        return m;
    };
    std::optional<Math::Vector3UVE> cameraAt;
    for (const std::int64_t id : order) {
        const EntityNodeUVE& node = nodes[id];
        const Math::Matrix4x4UVE m = worldOf(id);
        world.emplace(id, m);
        if (node.camera && !cameraAt.has_value()) {
            std::string lower = node.name;
            std::ranges::transform(lower, lower.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.find("thumbnail") != std::string::npos) {
                cameraAt = Math::TransformPointUVE(m, Math::Vector3UVE{});
            }
        }
        if (node.primitive) {
            const Render::PrimitiveGeometryUVE& geometry = Render::GetPrimitiveGeometryUVE(node.kind);
            Asset::MeshAssetUVE primitive;
            primitive.vertices = geometry.vertices;
            primitive.indices = geometry.indices;
            primitive.localBounds = geometry.localBounds;
            MeshThumbnailLookUVE look;
            look.colour = {node.colour.x, node.colour.y, node.colour.z};
            if (node.primitiveMaterial != Asset::AssetGuidUVE{} && lookOf) {
                look = lookOf(node.primitiveMaterial);
            }
            AppendUVE(out, primitive, m, {look});
        }
        if (node.mesh != Asset::AssetGuidUVE{}) {
            const std::filesystem::path file = database.ResolveUVE(node.mesh);
            Asset::MeshAssetUVE mesh;
            if (file.empty() || !Asset::LoadMeshAssetUVE(file, mesh)) {
                continue;
            }
            out.sources.push_back(file);
            if (poseMesh) {
                poseMesh(mesh, file);
            }
            std::vector<MeshThumbnailLookUVE> looks(std::max<std::size_t>(1U, mesh.materialGuids.size()));
            for (std::size_t slot = 0U; slot < mesh.materialGuids.size() && lookOf; ++slot) {
                const Asset::AssetGuidUVE material = node.meshMaterial != Asset::AssetGuidUVE{} ? node.meshMaterial : mesh.materialGuids[slot];
                if (material != Asset::AssetGuidUVE{}) {
                    looks[slot] = lookOf(material);
                }
            }
            if (mesh.materialGuids.empty() && node.meshMaterial != Asset::AssetGuidUVE{} && lookOf) {
                looks[0] = lookOf(node.meshMaterial);
            }
            AppendUVE(out, mesh, m, looks);
        }
    }
    if (out.mesh.vertices.empty() || out.mesh.indices.empty()) {
        return false;
    }
    Math::AabbUVE bounds{out.mesh.vertices.front().position, out.mesh.vertices.front().position};
    for (const Asset::MeshVertexUVE& vertex : out.mesh.vertices) {
        bounds.min = Math::Vector3UVE{std::min(bounds.min.x, vertex.position.x), std::min(bounds.min.y, vertex.position.y),
                                      std::min(bounds.min.z, vertex.position.z)};
        bounds.max = Math::Vector3UVE{std::max(bounds.max.x, vertex.position.x), std::max(bounds.max.y, vertex.position.y),
                                      std::max(bounds.max.z, vertex.position.z)};
    }
    out.mesh.localBounds = bounds;
    if (cameraAt.has_value()) {
        const Math::Vector3UVE d = *cameraAt - bounds.GetCenterUVE();
        const float length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (length > 1.0e-4F) {
            out.viewDirection = d * (1.0F / length);
        }
    }
    return true;
}

} // namespace UVE::Editor
