// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "asset_guid.h"
#include "mesh_asset.h"
#include "mesh_thumbnail_renderer.h"
#include "vector3.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

namespace UVE::Asset {
class IAssetDatabaseUVE;
}

namespace UVE::Editor {

/// What an entity file looks like, as one mesh for the thumbnail renderer: every MeshInstance3D
/// (its model, posed by `poseMesh`) and every primitive (Cube, Sphere, Plane, Capsule, Cylinder) in
/// the entity, each where its transform puts it, with a look per surface (`lookOf` for a material,
/// a primitive's own colour). Read from the file alone; nothing is placed in a scene.
struct EntityThumbnailSceneUVE final {
    Asset::MeshAssetUVE mesh;
    std::vector<MeshThumbnailLookUVE> looks;
    /// The files it came from (the entity, its models), to draw it again when one changes.
    std::vector<std::filesystem::path> sources;
    /// From the centre toward the entity's "Thumbnail Camera" (a Camera3D so named), when it has one.
    std::optional<Math::Vector3UVE> viewDirection;
};

[[nodiscard]] bool BuildEntityThumbnailSceneUVE(const std::filesystem::path& entityFile, Asset::IAssetDatabaseUVE& database,
                                                const std::function<MeshThumbnailLookUVE(Asset::AssetGuidUVE material)>& lookOf,
                                                const std::function<void(Asset::MeshAssetUVE& mesh, const std::filesystem::path& file)>& poseMesh,
                                                EntityThumbnailSceneUVE& out);

} // namespace UVE::Editor
