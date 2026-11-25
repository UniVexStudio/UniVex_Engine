// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "i_asset_database.h"
#include "i_asset_manager.h"
#include "aabb.h"
#include "frustum.h"
#include "vector3.h"
#include "decal_draw_data.h"
#include "mesh_visibility_set.h"

namespace UVE::Render {

/// One surface a decal can land on, resolved from this frame's visibility set.
///
/// The pass takes the set rather than walking the entity manager again: the set has already
/// resolved every mesh's assets, composed its world matrix and transformed its bounds, and doing
/// that a second time for decals would double the most expensive part of the frame to answer a
/// question the first pass already answered.
struct DecalReceiver final {
    Scene::Entity entity = Scene::kInvalidEntity;
    Math::Aabb worldBounds{};
    /// The render layers this receiver is on - RenderInstanceComponent::renderLayers, or the
    /// layer-1 default for an entity that has no render instance component.
    std::uint32_t renderLayers = 1U;
};

/// Builds this frame's projected decals: every enabled, unexpired, in-range decal is clipped onto
/// the receiving surfaces its layers reach, and the surviving polygons are published as a
/// back-to-front draw list.
///
/// This is the consumer Decal3DComponent never had. The component had an authored projection, a
/// lifetime and a layer mask, a validator, an inspector and a serializer - and nothing in the
/// engine that ever read any of it.
///
/// WHERE THE CLIPPING HAPPENS, AND WHY. Every patch is built in the decal's UNIT space - the space
/// where the volume is exactly the unit box or the unit cylinder - because that is the only space
/// where "inside the decal" is one comparison. The world positions of the surviving vertices are
/// mapped back through Decal3DUnitToWorld once per vertex, so the clipping arithmetic never has
/// to divide by an extent that might be a thousand units or a hundredth of one.
///
/// Thread-safety: stateless and const; call once per frame from the main thread after
/// MeshRenderer::BuildVisibilitySet() has placed this frame's receivers.
class DecalRenderer final {
public:
    /// Publishes this frame's decal draws into `outDrawList`, which is cleared first.
    ///
    /// `viewFrustum` and `cameraWorldPosition` come from CameraSystem - the same pair the main
    /// view culls meshes with. `receiverLayerMask` is the layer filter the view is rendering (a
    /// camera that renders only layer 1 passes 1U); a decal's cullMask is tested against the
    /// RECEIVER's layers, so a decal can project onto some of what the camera sees and not all of
    /// it.
    void BuildDrawList(Scene::IEntityManager& entityManager,
                          Asset::IAssetManager& assetManager,
                          Asset::IAssetDatabase& assetDatabase,
                          const MeshVisibilitySet& visibilitySet,
                          const Math::Vector3& cameraWorldPosition,
                          const Math::Frustum& viewFrustum,
                          std::uint32_t receiverLayerMask,
                          DecalDrawList& outDrawList) const;
};

} // namespace UVE::Render
