// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/i_asset_manager_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/frustum_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/render_systems/decal_draw_data_uve.h"
#include "uve/render_systems/mesh_visibility_set_uve.h"

namespace UVE::Render {

/// One surface a decal can land on, resolved from this frame's visibility set.
///
/// The pass takes the set rather than walking the entity manager again: the set has already
/// resolved every mesh's assets, composed its world matrix and transformed its bounds, and doing
/// that a second time for decals would double the most expensive part of the frame to answer a
/// question the first pass already answered.
struct DecalReceiverUVE final {
    Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
    Math::AabbUVE worldBounds{};
    /// The render layers this receiver is on - RenderInstanceComponentUVE::renderLayers, or the
    /// layer-1 default for an entity that has no render instance component.
    std::uint32_t renderLayers = 1U;
};

/// Builds this frame's projected decals: every enabled, unexpired, in-range decal is clipped onto
/// the receiving surfaces its layers reach, and the surviving polygons are published as a
/// back-to-front draw list.
///
/// This is the consumer Decal3DComponentUVE never had. The component had an authored projection, a
/// lifetime and a layer mask, a validator, an inspector and a serializer - and nothing in the
/// engine that ever read any of it.
///
/// WHERE THE CLIPPING HAPPENS, AND WHY. Every patch is built in the decal's UNIT space - the space
/// where the volume is exactly the unit box or the unit cylinder - because that is the only space
/// where "inside the decal" is one comparison. The world positions of the surviving vertices are
/// mapped back through Decal3DUnitToWorldUVE once per vertex, so the clipping arithmetic never has
/// to divide by an extent that might be a thousand units or a hundredth of one.
///
/// Thread-safety: stateless and const; call once per frame from the main thread after
/// MeshRendererUVE::BuildVisibilitySetUVE() has placed this frame's receivers.
class DecalRendererUVE final {
public:
    /// Publishes this frame's decal draws into `outDrawList`, which is cleared first.
    ///
    /// `viewFrustum` and `cameraWorldPosition` come from CameraSystemUVE - the same pair the main
    /// view culls meshes with. `receiverLayerMask` is the layer filter the view is rendering (a
    /// camera that renders only layer 1 passes 1U); a decal's cullMask is tested against the
    /// RECEIVER's layers, so a decal can project onto some of what the camera sees and not all of
    /// it.
    void BuildDrawListUVE(Scene::IEntityManagerUVE& entityManager,
                          Asset::IAssetManagerUVE& assetManager,
                          Asset::IAssetDatabaseUVE& assetDatabase,
                          const MeshVisibilitySetUVE& visibilitySet,
                          const Math::Vector3UVE& cameraWorldPosition,
                          const Math::FrustumUVE& viewFrustum,
                          std::uint32_t receiverLayerMask,
                          DecalDrawListUVE& outDrawList) const;
};

} // namespace UVE::Render
