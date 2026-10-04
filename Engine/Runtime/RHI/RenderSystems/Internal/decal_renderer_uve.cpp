// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/render_systems/decal_renderer_uve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>

#include "uve/component/render_instance_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/objects/3d/decal_3d_uve.h"

namespace UVE::Render {

namespace {

/// A patch polygon area below this many square metres is treated as nothing. A decal clipped to a
/// sliver a thousandth of a millimetre across costs a draw call and paints less than one pixel of
/// it, and the sliver's own floating-point noise is a larger quantity than its area.
constexpr float kMinimumPatchAreaSquareMetresUVE = 1.0e-6F;

/// The polygon a single receiving face can contribute, in world space while it is built and in unit
/// space while it is clipped. Fixed capacity rather than a vector: a rectangle clipped by a box or
/// by a cylinder can only grow to eight vertices, and a decal pass that allocated per face per
/// decal per frame would spend more time in the allocator than in the clipping it is doing.
struct ClipPolygonUVE final {
    static constexpr std::size_t kCapacityUVE = DecalPatchUVE::kMaximumVerticesUVE;

    std::array<Math::Vector3UVE, kCapacityUVE> points{};
    std::size_t count = 0U;

    void PushUVE(const Math::Vector3UVE& point) noexcept {
        if (count < kCapacityUVE) {
            points[count] = point;
            ++count;
        }
    }
};

/// Clips `polygon` against the half-space `sign * axis[axisIndex] <= bound`, into `outPolygon`.
///
/// The sign is a parameter rather than a second family of functions because a lower bound IS an
/// upper bound on the negated axis: `y >= -1` is exactly `-y <= 1`. Clipping against `y <= -1`
/// instead - the mistake this parameter exists to prevent - keeps the part of the polygon OUTSIDE
/// the volume and throws away the part inside it, which is the opposite of what was asked for.
///
/// One Sutherland-Hodgman step. Vertices are walked as edges rather than as points because a
/// crossing is a property of a pair: an edge leaving the half-space contributes its intersection
/// with the boundary, and the edge's own start contributes nothing.
void ClipAgainstAxisBoundUVE(const ClipPolygonUVE& polygon, const std::size_t axisIndex, const float sign,
                             const float bound, ClipPolygonUVE& outPolygon) noexcept {
    outPolygon.count = 0U;
    if (polygon.count == 0U) {
        return;
    }
    for (std::size_t index = 0U; index < polygon.count; ++index) {
        const Math::Vector3UVE& current = polygon.points[index];
        const Math::Vector3UVE& previous = polygon.points[(index + polygon.count - 1U) % polygon.count];
        const float* const currentComponents = &current.x;
        const float* const previousComponents = &previous.x;
        const float currentValue = sign * currentComponents[axisIndex];
        const float previousValue = sign * previousComponents[axisIndex];
        const bool currentInside = currentValue <= bound;
        const bool previousInside = previousValue <= bound;
        if (currentInside != previousInside) {
            const float denominator = currentValue - previousValue;
            if (std::fabs(denominator) > 0.0F) {
                const float interpolation = (bound - previousValue) / denominator;
                outPolygon.PushUVE(previous + (current - previous) * interpolation);
            }
        }
        if (currentInside) {
            outPolygon.PushUVE(current);
        }
    }
}

/// Clips `polygon` against the unit cylinder's wall, x^2 + z^2 <= 1, into `outPolygon`.
///
/// A circle is not a half-space, and the difference is not cosmetic: an edge whose BOTH endpoints
/// lie outside the disc can still cross straight through it, so a step that only looks at the two
/// endpoint flags would drop that piece and leave a hole in the patch. This walks each edge's own
/// span instead - the quadratic's two roots are where the edge enters and leaves the disc, and the
/// part of that interval inside the segment is what survives. The segment is scanned in traversal
/// order, so the output keeps the polygon's winding.
void ClipAgainstUnitCylinderWallUVE(const ClipPolygonUVE& polygon, ClipPolygonUVE& outPolygon) noexcept {
    outPolygon.count = 0U;
    if (polygon.count == 0U) {
        return;
    }

    for (std::size_t index = 0U; index < polygon.count; ++index) {
        const Math::Vector3UVE& entry = polygon.points[(index + polygon.count - 1U) % polygon.count];
        const Math::Vector3UVE& exit = polygon.points[index];
        const Math::Vector3UVE delta = exit - entry;
        const float quadratic = delta.x * delta.x + delta.z * delta.z;
        const float constant = entry.x * entry.x + entry.z * entry.z - 1.0F;
        if (quadratic <= 0.0F) {
            // The edge runs along the projection axis, so x and z are constant along it and the
            // whole edge is on one side of the wall.
            if (constant <= 0.0F) {
                outPolygon.PushUVE(exit);
            }
            continue;
        }

        const float linear = 2.0F * (entry.x * delta.x + entry.z * delta.z);
        const float discriminant = linear * linear - 4.0F * quadratic * constant;
        if (discriminant <= 0.0F) {
            // The edge never reaches the wall. With a positive leading coefficient that means the
            // quadratic is positive everywhere - outside the disc - so nothing of it survives.
            continue;
        }

        const float root = std::sqrt(discriminant);
        const float enterT = std::max(0.0F, (-linear - root) / (2.0F * quadratic));
        const float leaveT = std::min(1.0F, (-linear + root) / (2.0F * quadratic));
        if (enterT > leaveT) {
            // The disc is crossed before or after this segment's span.
            continue;
        }
        if (enterT > 0.0F) {
            // The edge crosses INTO the disc inside the segment; the start point is outside and the
            // previous edge already accounted for it.
            outPolygon.PushUVE(entry + delta * enterT);
        }
        if (leaveT < 1.0F) {
            outPolygon.PushUVE(entry + delta * leaveT);
        } else {
            // The edge ends inside the disc, so its endpoint is part of the patch.
            outPolygon.PushUVE(exit);
        }
    }
}

/// The world-space quad of one face of an axis-aligned box, wound counter-clockwise seen from
/// outside, and its outward normal.
void BuildBoxFaceQuadUVE(const Math::AabbUVE& bounds, const std::size_t faceIndex,
                         std::array<Math::Vector3UVE, 4U>& outCorners, Math::Vector3UVE& outNormal) noexcept {
    const float minX = bounds.min.x;
    const float minY = bounds.min.y;
    const float minZ = bounds.min.z;
    const float maxX = bounds.max.x;
    const float maxY = bounds.max.y;
    const float maxZ = bounds.max.z;
    switch (faceIndex) {
        case 0U: // +X
            outCorners = {Math::Vector3UVE{maxX, minY, maxZ}, Math::Vector3UVE{maxX, minY, minZ},
                          Math::Vector3UVE{maxX, maxY, minZ}, Math::Vector3UVE{maxX, maxY, maxZ}};
            outNormal = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
            break;
        case 1U: // -X
            outCorners = {Math::Vector3UVE{minX, minY, minZ}, Math::Vector3UVE{minX, minY, maxZ},
                          Math::Vector3UVE{minX, maxY, maxZ}, Math::Vector3UVE{minX, maxY, minZ}};
            outNormal = Math::Vector3UVE{-1.0F, 0.0F, 0.0F};
            break;
        case 2U: // +Y
            outCorners = {Math::Vector3UVE{minX, maxY, maxZ}, Math::Vector3UVE{maxX, maxY, maxZ},
                          Math::Vector3UVE{maxX, maxY, minZ}, Math::Vector3UVE{minX, maxY, minZ}};
            outNormal = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
            break;
        case 3U: // -Y
            outCorners = {Math::Vector3UVE{minX, minY, minZ}, Math::Vector3UVE{maxX, minY, minZ},
                          Math::Vector3UVE{maxX, minY, maxZ}, Math::Vector3UVE{minX, minY, maxZ}};
            outNormal = Math::Vector3UVE{0.0F, -1.0F, 0.0F};
            break;
        case 4U: // +Z
            outCorners = {Math::Vector3UVE{minX, minY, maxZ}, Math::Vector3UVE{minX, maxY, maxZ},
                          Math::Vector3UVE{maxX, maxY, maxZ}, Math::Vector3UVE{maxX, minY, maxZ}};
            outNormal = Math::Vector3UVE{0.0F, 0.0F, 1.0F};
            break;
        default: // -Z
            outCorners = {Math::Vector3UVE{maxX, minY, minZ}, Math::Vector3UVE{maxX, maxY, minZ},
                          Math::Vector3UVE{minX, maxY, minZ}, Math::Vector3UVE{minX, minY, minZ}};
            outNormal = Math::Vector3UVE{0.0F, 0.0F, -1.0F};
            break;
    }
}

/// Newell's area of a world-space polygon, which is correct for any planar convex polygon rather
/// than only for triangles.
[[nodiscard]] float PolygonAreaUVE(const std::array<Math::Vector3UVE, DecalPatchUVE::kMaximumVerticesUVE>& points,
                                   const std::size_t count) noexcept {
    if (count < 3U) {
        return 0.0F;
    }
    Math::Vector3UVE accumulated{};
    for (std::size_t index = 0U; index < count; ++index) {
        const Math::Vector3UVE& current = points[index];
        const Math::Vector3UVE& next = points[(index + 1U) % count];
        accumulated = accumulated + Math::CrossUVE(current, next);
    }
    return 0.5F * Math::LengthUVE(accumulated);
}

/// Builds this frame's receiver list out of the visibility set. Entities without a render instance
/// component carry the documented layer-1 default rather than being skipped, which is what makes a
/// decal whose cullMask includes layer 1 project onto ordinary scene meshes.
void BuildReceiversUVE(Scene::IEntityManagerUVE& entityManager, const MeshVisibilitySetUVE& visibilitySet,
                       std::vector<DecalReceiverUVE>& outReceivers) {
    outReceivers.clear();
    outReceivers.reserve(visibilitySet.candidates.size());
    for (const MeshVisibilityCandidateUVE& candidate : visibilitySet.candidates) {
        DecalReceiverUVE receiver{};
        receiver.entity = candidate.entity;
        receiver.worldBounds = candidate.placement.worldBounds;
        if (candidate.entity != Scene::kInvalidEntityUVE &&
            entityManager.HasComponentUVE<Scene::RenderInstanceComponentUVE>(candidate.entity)) {
            receiver.renderLayers =
                entityManager.GetComponentUVE<Scene::RenderInstanceComponentUVE>(candidate.entity).renderLayers;
        }
        outReceivers.push_back(receiver);
    }
}

/// The same index-and-side-table idiom the mesh visibility set uses: one reference per DISTINCT
/// material, not one per decal that names it.
[[nodiscard]] std::size_t ResolveMaterialIndexUVE(std::unordered_map<std::uint64_t, std::size_t>& slots,
                                                  std::vector<Asset::AssetHandleUVE<Asset::MaterialAssetUVE>>& handles,
                                                  const Asset::AssetGuidUVE guid,
                                                  const Asset::AssetHandleUVE<Asset::MaterialAssetUVE>& handle) {
    const auto existing = slots.find(guid.value);
    if (existing != slots.end()) {
        return existing->second;
    }
    const std::size_t index = handles.size();
    // A copy, not a move: the reference is shared with every other decal naming this material.
    handles.push_back(handle);
    slots.emplace(guid.value, index);
    return index;
}

/// Resolves the decal's authored material path to a loadable material.
///
/// The Decal3D component authors its material as a PATH, not a GUID - it predates the GUID
/// discipline the mesh path uses - while the asset manager loads by GUID. The bridge is the
/// database's own registry, looked up once per frame and only when the frame actually has a decal
/// to paint: a scene with no decals pays nothing, and a scene with a hundred decals pays one
/// registry walk rather than a hundred.
[[nodiscard]] bool TryResolveDecalMaterialUVE(const std::string& materialAssetPath,
                                              Asset::IAssetDatabaseUVE& assetDatabase,
                                              std::unordered_map<std::string, Asset::AssetGuidUVE>& pathToGuidIndex,
                                              bool& indexBuilt, Asset::AssetGuidUVE& outGuid) noexcept {
    if (materialAssetPath.empty()) {
        return false;
    }
    if (!indexBuilt) {
        pathToGuidIndex.clear();
        for (const Asset::AssetRecordUVE& record : assetDatabase.GetRegisteredAssetsUVE()) {
            pathToGuidIndex.emplace(record.path.lexically_normal().generic_string(), record.guid);
        }
        indexBuilt = true;
    }
    const std::string wanted = std::filesystem::path(materialAssetPath).lexically_normal().generic_string();
    const auto found = pathToGuidIndex.find(wanted);
    if (found == pathToGuidIndex.end()) {
        return false;
    }
    outGuid = found->second;
    return true;
}

} // namespace

void DecalRendererUVE::BuildDrawListUVE(Scene::IEntityManagerUVE& entityManager,
                                        Asset::IAssetManagerUVE& assetManager,
                                        Asset::IAssetDatabaseUVE& assetDatabase,
                                        const MeshVisibilitySetUVE& visibilitySet,
                                        const Math::Vector3UVE& cameraWorldPosition,
                                        const Math::FrustumUVE& viewFrustum,
                                        const std::uint32_t receiverLayerMask,
                                        DecalDrawListUVE& outDrawList) const {
    outDrawList.ClearUVE();

    std::vector<DecalReceiverUVE> receivers;
    BuildReceiversUVE(entityManager, visibilitySet, receivers);

    // Built lazily, on the first decal that gets far enough to need a material, and shared by every
    // decal after it.
    std::unordered_map<std::string, Asset::AssetGuidUVE> pathToGuidIndex;
    bool pathIndexBuilt = false;
    std::unordered_map<std::uint64_t, std::size_t> materialSlots;

    entityManager.ForEachUVE<Scene::Decal3DComponentUVE, Scene::WorldTransformComponentUVE>(
        [&](const Scene::EntityUVE entity, const Scene::Decal3DComponentUVE& decal,
            const Scene::WorldTransformComponentUVE& worldTransform) {
            ++outDrawList.decalsConsidered;

            // The one gate that answers "should this decal paint at all": enabled, unexpired and
            // non-degenerate. Everything after this point is about where and how strongly.
            if (!Scene::IsDecal3DPaintingUVE(decal)) {
                ++outDrawList.decalsSkippedNotPainting;
                return;
            }

            Scene::Decal3DProjectionUVE projection{};
            if (!Scene::TryMakeDecal3DProjectionUVE(decal, worldTransform.worldPosition,
                                                    worldTransform.worldRotation, worldTransform.worldScale,
                                                    projection)) {
                // A decal with a degenerate pose is not painting anything either - it is the same
                // answer for a different reason, and pretending it was in range would put a draw
                // call in the frame that can never contribute a pixel.
                ++outDrawList.decalsSkippedNotPainting;
                return;
            }

            // A decal that only paints layer 4 is invisible to a view rendering layer 1, and
            // deciding that before the volume is tested against every receiver is the difference
            // between one comparison and several hundred.
            if ((decal.cullMask & receiverLayerMask) == 0U) {
                ++outDrawList.decalsCulledByLayer;
                return;
            }

            if (!viewFrustum.IntersectsUVE(Math::AabbUVE::FromCenterExtentsUVE(projection.worldPosition,
                                                                                projection.halfExtents))) {
                ++outDrawList.decalsOutsideView;
                return;
            }

            // Distance is measured to the decal's own origin, which is where its projection volume
            // is centred, and the fade is asked through the same sampler the per-patch weights use:
            // one rule, evaluated at the centre with a surface facing squarely back at the decal, so
            // the decal-level cull and the patch-level weight can never disagree.
            const float distanceToCamera = Math::LengthUVE(projection.worldPosition - cameraWorldPosition);
            const Scene::Decal3DSampleUVE centreSample =
                Scene::SampleDecal3DUVE(projection, projection.worldPosition, -projection.projectionDirection,
                                        distanceToCamera);
            if (centreSample.distanceFadeWeight <= 0.0F) {
                ++outDrawList.decalsFadedOut;
                return;
            }

            Asset::AssetGuidUVE materialGuid = Asset::kInvalidAssetGuidUVE;
            if (!TryResolveDecalMaterialUVE(decal.materialAssetPath, assetDatabase, pathToGuidIndex,
                                            pathIndexBuilt, materialGuid)) {
                ++outDrawList.decalsWithoutMaterial;
                return;
            }
            Asset::AssetHandleUVE<Asset::MaterialAssetUVE> materialHandle =
                assetManager.LoadUVE<Asset::MaterialAssetUVE>(materialGuid, assetDatabase);
            if (!materialHandle.IsReadyUVE() || materialHandle.TryGetUVE() == nullptr) {
                // Pending or failed. Either way there is nothing to paint with this frame, and the
                // decal appears on its own once the load lands - the same non-blocking contract the
                // mesh path has.
                ++outDrawList.decalsWithoutMaterial;
                return;
            }

            DecalDrawUVE draw{};
            draw.decal = entity;
            draw.materialIndex = ResolveMaterialIndexUVE(materialSlots, outDrawList.materialHandles,
                                                          materialGuid, materialHandle);
            draw.projectionDirection = projection.projectionDirection;
            draw.sortDepth = distanceToCamera;

            for (const DecalReceiverUVE& receiver : receivers) {
                if ((decal.cullMask & receiver.renderLayers) == 0U) {
                    continue;
                }
                ++outDrawList.receiversTested;

                // The cheap rejection, in unit space: the receiver's world bounds, expressed as the
                // axis-aligned box that contains them in the decal's own frame, against the unit
                // volume. A receiver the decal cannot reach never reaches the per-face clipping,
                // which is where the real work is.
                Math::AabbUVE unitBounds{};
                {
                    const std::array<Math::Vector3UVE, 8U> corners = {
                        Math::Vector3UVE{receiver.worldBounds.min.x, receiver.worldBounds.min.y, receiver.worldBounds.min.z},
                        Math::Vector3UVE{receiver.worldBounds.min.x, receiver.worldBounds.min.y, receiver.worldBounds.max.z},
                        Math::Vector3UVE{receiver.worldBounds.min.x, receiver.worldBounds.max.y, receiver.worldBounds.min.z},
                        Math::Vector3UVE{receiver.worldBounds.min.x, receiver.worldBounds.max.y, receiver.worldBounds.max.z},
                        Math::Vector3UVE{receiver.worldBounds.max.x, receiver.worldBounds.min.y, receiver.worldBounds.min.z},
                        Math::Vector3UVE{receiver.worldBounds.max.x, receiver.worldBounds.min.y, receiver.worldBounds.max.z},
                        Math::Vector3UVE{receiver.worldBounds.max.x, receiver.worldBounds.max.y, receiver.worldBounds.min.z},
                        Math::Vector3UVE{receiver.worldBounds.max.x, receiver.worldBounds.max.y, receiver.worldBounds.max.z},
                    };
                    const Math::Vector3UVE first = Scene::Decal3DWorldToUnitUVE(projection, corners[0]);
                    unitBounds = Math::AabbUVE{first, first};
                    for (std::size_t cornerIndex = 1U; cornerIndex < corners.size(); ++cornerIndex) {
                        const Math::Vector3UVE unitCorner = Scene::Decal3DWorldToUnitUVE(projection, corners[cornerIndex]);
                        unitBounds.min = Math::Vector3UVE{std::min(unitBounds.min.x, unitCorner.x),
                                                          std::min(unitBounds.min.y, unitCorner.y),
                                                          std::min(unitBounds.min.z, unitCorner.z)};
                        unitBounds.max = Math::Vector3UVE{std::max(unitBounds.max.x, unitCorner.x),
                                                          std::max(unitBounds.max.y, unitCorner.y),
                                                          std::max(unitBounds.max.z, unitCorner.z)};
                    }
                }
                const bool overlapsOnAxis = [&unitBounds]() noexcept {
                    return unitBounds.min.x <= 1.0F && unitBounds.max.x >= -1.0F &&
                           unitBounds.min.y <= 1.0F && unitBounds.max.y >= -1.0F &&
                           unitBounds.min.z <= 1.0F && unitBounds.max.z >= -1.0F;
                }();
                if (!overlapsOnAxis) {
                    continue;
                }
                ++outDrawList.receiversOverlapped;

                for (std::size_t faceIndex = 0U; faceIndex < 6U; ++faceIndex) {
                    std::array<Math::Vector3UVE, 4U> worldCorners{};
                    Math::Vector3UVE faceNormal{};
                    BuildBoxFaceQuadUVE(receiver.worldBounds, faceIndex, worldCorners, faceNormal);

                    // A receiving face turned away from the camera cannot be seen, so a decal patch
                    // painted on it would cost a draw and paint nothing that is not already behind
                    // the face in front of it.
                    const Math::Vector3UVE faceCentre = (worldCorners[0] + worldCorners[1] + worldCorners[2] +
                                                         worldCorners[3]) *
                                                        0.25F;
                    if (Math::DotUVE(faceNormal, cameraWorldPosition - faceCentre) <= 0.0F) {
                        ++outDrawList.patchesDiscardedBackFacing;
                        continue;
                    }

                    ClipPolygonUVE unitPolygon{};
                    for (const Math::Vector3UVE& corner : worldCorners) {
                        unitPolygon.PushUVE(Scene::Decal3DWorldToUnitUVE(projection, corner));
                    }

                    // Clip in unit space, where the volume is exactly the unit box or the unit
                    // cylinder. The box is six half-spaces; the cylinder is the same six, except
                    // that its four side planes are one round wall.
                    // Each step writes into the polygon the previous one read, never into itself:
                    // a clip that cleared its own output before reading its input would clip
                    // nothing at all.
                    ClipPolygonUVE clipped = unitPolygon;
                    ClipPolygonUVE scratch{};
                    ClipAgainstAxisBoundUVE(clipped, 1U, 1.0F, 1.0F, scratch);
                    ClipAgainstAxisBoundUVE(scratch, 1U, -1.0F, 1.0F, clipped);
                    ClipAgainstAxisBoundUVE(clipped, 0U, 1.0F, 1.0F, scratch);
                    ClipAgainstAxisBoundUVE(scratch, 0U, -1.0F, 1.0F, clipped);
                    if (projection.mode == Scene::DecalProjectionModeUVE::Cylinder) {
                        ClipAgainstUnitCylinderWallUVE(clipped, scratch);
                        clipped = scratch;
                    } else {
                        ClipAgainstAxisBoundUVE(clipped, 2U, 1.0F, 1.0F, scratch);
                        ClipAgainstAxisBoundUVE(scratch, 2U, -1.0F, 1.0F, clipped);
                    }

                    if (clipped.count < 3U) {
                        // Wholly outside the volume; not a defect, so it is not counted as one.
                        continue;
                    }

                    // Back to the world, and weight the patch where it actually sits rather than
                    // where the face's centre would have been before clipping.
                    DecalPatchUVE patch{};
                    patch.vertexCount = clipped.count;
                    patch.normal = faceNormal;
                    Math::Vector3UVE unitCentroid{};
                    for (std::size_t vertexIndex = 0U; vertexIndex < clipped.count; ++vertexIndex) {
                        patch.unitCoords[vertexIndex] =
                            Math::Vector2UVE{clipped.points[vertexIndex].x, clipped.points[vertexIndex].z};
                        patch.worldPositions[vertexIndex] =
                            Scene::Decal3DUnitToWorldUVE(projection, clipped.points[vertexIndex]);
                        unitCentroid = unitCentroid + clipped.points[vertexIndex];
                    }
                    unitCentroid = unitCentroid * (1.0F / static_cast<float>(clipped.count));

                    const Math::Vector3UVE worldCentroid =
                        Scene::Decal3DUnitToWorldUVE(projection, unitCentroid);
                    const Scene::Decal3DSampleUVE sample = Scene::SampleDecal3DUVE(
                        projection, worldCentroid, faceNormal,
                        Math::LengthUVE(worldCentroid - cameraWorldPosition));
                    patch.weight = sample.combinedWeight;
                    if (patch.weight <= 0.0F) {
                        ++outDrawList.patchesDiscardedByFade;
                        continue;
                    }

                    if (PolygonAreaUVE(patch.worldPositions, patch.vertexCount) < kMinimumPatchAreaSquareMetresUVE) {
                        ++outDrawList.patchesDiscardedDegenerate;
                        continue;
                    }

                    ++outDrawList.patchesEmitted;
                    draw.patches.push_back(patch);
                }
            }

            if (draw.patches.empty()) {
                // In range, resolved and facing receivers, but nothing it can actually land on -
                // a different answer from every reason above, which is why it is its own counter.
                ++outDrawList.decalsWithoutReceivers;
                return;
            }
            outDrawList.draws.push_back(std::move(draw));
        });

    outDrawList.SortBackToFrontUVE(cameraWorldPosition);
}

} // namespace UVE::Render
