// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "decal_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <string>

#include "render_instance_component.h"
#include "world_transform_component.h"
#include "decal_3d.h"
#include "occluder_3d.h"
#include "visibility_region_3d.h"
#include "world_partition_3d.h"

namespace UVE::Render {

namespace {

/// A patch polygon area below this many square metres is treated as nothing. A decal clipped to a
/// sliver a thousandth of a millimetre across costs a draw call and paints less than one pixel of
/// it, and the sliver's own floating-point noise is a larger quantity than its area.
constexpr float kMinimumPatchAreaSquareMetres = 1.0e-6F;

/// The polygon a single receiving face can contribute, in world space while it is built and in unit
/// space while it is clipped. Fixed capacity rather than a vector: a rectangle clipped by a box or
/// by a cylinder can only grow to eight vertices, and a decal pass that allocated per face per
/// decal per frame would spend more time in the allocator than in the clipping it is doing.
struct ClipPolygon final {
    static constexpr std::size_t kCapacity = DecalPatch::kMaximumVertices;

    std::array<Math::Vector3, kCapacity> points{};
    std::size_t count = 0U;

    void Push(const Math::Vector3& point) noexcept {
        if (count < kCapacity) {
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
void ClipAgainstAxisBound(const ClipPolygon& polygon, const std::size_t axisIndex, const float sign,
                             const float bound, ClipPolygon& outPolygon) noexcept {
    outPolygon.count = 0U;
    if (polygon.count == 0U) {
        return;
    }
    for (std::size_t index = 0U; index < polygon.count; ++index) {
        const Math::Vector3& current = polygon.points[index];
        const Math::Vector3& previous = polygon.points[(index + polygon.count - 1U) % polygon.count];
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
                outPolygon.Push(previous + (current - previous) * interpolation);
            }
        }
        if (currentInside) {
            outPolygon.Push(current);
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
void ClipAgainstUnitCylinderWall(const ClipPolygon& polygon, ClipPolygon& outPolygon) noexcept {
    outPolygon.count = 0U;
    if (polygon.count == 0U) {
        return;
    }

    for (std::size_t index = 0U; index < polygon.count; ++index) {
        const Math::Vector3& entry = polygon.points[(index + polygon.count - 1U) % polygon.count];
        const Math::Vector3& exit = polygon.points[index];
        const Math::Vector3 delta = exit - entry;
        const float quadratic = delta.x * delta.x + delta.z * delta.z;
        const float constant = entry.x * entry.x + entry.z * entry.z - 1.0F;
        if (quadratic <= 0.0F) {
            // The edge runs along the projection axis, so x and z are constant along it and the
            // whole edge is on one side of the wall.
            if (constant <= 0.0F) {
                outPolygon.Push(exit);
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
            outPolygon.Push(entry + delta * enterT);
        }
        if (leaveT < 1.0F) {
            outPolygon.Push(entry + delta * leaveT);
        } else {
            // The edge ends inside the disc, so its endpoint is part of the patch.
            outPolygon.Push(exit);
        }
    }
}

/// The world-space quad of one face of an axis-aligned box, wound counter-clockwise seen from
/// outside, and its outward normal.
void BuildBoxFaceQuad(const Math::Aabb& bounds, const std::size_t faceIndex,
                         std::array<Math::Vector3, 4U>& outCorners, Math::Vector3& outNormal) noexcept {
    const float minX = bounds.min.x;
    const float minY = bounds.min.y;
    const float minZ = bounds.min.z;
    const float maxX = bounds.max.x;
    const float maxY = bounds.max.y;
    const float maxZ = bounds.max.z;
    switch (faceIndex) {
        case 0U: // +X
            outCorners = {Math::Vector3{maxX, minY, maxZ}, Math::Vector3{maxX, minY, minZ},
                          Math::Vector3{maxX, maxY, minZ}, Math::Vector3{maxX, maxY, maxZ}};
            outNormal = Math::Vector3{1.0F, 0.0F, 0.0F};
            break;
        case 1U: // -X
            outCorners = {Math::Vector3{minX, minY, minZ}, Math::Vector3{minX, minY, maxZ},
                          Math::Vector3{minX, maxY, maxZ}, Math::Vector3{minX, maxY, minZ}};
            outNormal = Math::Vector3{-1.0F, 0.0F, 0.0F};
            break;
        case 2U: // +Y
            outCorners = {Math::Vector3{minX, maxY, maxZ}, Math::Vector3{maxX, maxY, maxZ},
                          Math::Vector3{maxX, maxY, minZ}, Math::Vector3{minX, maxY, minZ}};
            outNormal = Math::Vector3{0.0F, 1.0F, 0.0F};
            break;
        case 3U: // -Y
            outCorners = {Math::Vector3{minX, minY, minZ}, Math::Vector3{maxX, minY, minZ},
                          Math::Vector3{maxX, minY, maxZ}, Math::Vector3{minX, minY, maxZ}};
            outNormal = Math::Vector3{0.0F, -1.0F, 0.0F};
            break;
        case 4U: // +Z
            outCorners = {Math::Vector3{minX, minY, maxZ}, Math::Vector3{minX, maxY, maxZ},
                          Math::Vector3{maxX, maxY, maxZ}, Math::Vector3{maxX, minY, maxZ}};
            outNormal = Math::Vector3{0.0F, 0.0F, 1.0F};
            break;
        default: // -Z
            outCorners = {Math::Vector3{maxX, minY, minZ}, Math::Vector3{maxX, maxY, minZ},
                          Math::Vector3{minX, maxY, minZ}, Math::Vector3{minX, minY, minZ}};
            outNormal = Math::Vector3{0.0F, 0.0F, -1.0F};
            break;
    }
}

/// Newell's area of a world-space polygon, which is correct for any planar convex polygon rather
/// than only for triangles.
[[nodiscard]] float PolygonArea(const std::array<Math::Vector3, DecalPatch::kMaximumVertices>& points,
                                   const std::size_t count) noexcept {
    if (count < 3U) {
        return 0.0F;
    }
    Math::Vector3 accumulated{};
    for (std::size_t index = 0U; index < count; ++index) {
        const Math::Vector3& current = points[index];
        const Math::Vector3& next = points[(index + 1U) % count];
        accumulated = accumulated + Math::Cross(current, next);
    }
    return 0.5F * Math::Length(accumulated);
}

/// Builds this frame's receiver list out of the visibility set. Entities without a render instance
/// component carry the documented layer-1 default rather than being skipped, which is what makes a
/// decal whose cullMask includes layer 1 project onto ordinary scene meshes.
void BuildReceivers(const MeshVisibilitySet& visibilitySet, std::vector<DecalReceiver>& outReceivers) {
    outReceivers.clear();
    outReceivers.reserve(visibilitySet.candidates.size());
    for (const MeshVisibilityCandidate& candidate : visibilitySet.candidates) {
        DecalReceiver receiver{};
        receiver.entity = candidate.entity;
        receiver.worldBounds = candidate.placement.worldBounds;
        receiver.renderLayers = candidate.renderLayers;
        outReceivers.push_back(receiver);
    }
}

/// The same index-and-side-table idiom the mesh visibility set uses: one reference per DISTINCT
/// material, not one per decal that names it.
[[nodiscard]] std::size_t ResolveMaterialIndex(std::unordered_map<std::uint64_t, std::size_t>& slots,
                                                  std::vector<Asset::AssetHandle<Asset::MaterialAsset>>& handles,
                                                  const Asset::AssetGuid guid,
                                                  const Asset::AssetHandle<Asset::MaterialAsset>& handle) {
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
[[nodiscard]] bool TryResolveDecalMaterial(const std::string& materialAssetPath,
                                              Asset::IAssetDatabase& assetDatabase,
                                              std::unordered_map<std::string, Asset::AssetGuid>& pathToGuidIndex,
                                              bool& indexBuilt, Asset::AssetGuid& outGuid) noexcept {
    if (materialAssetPath.empty()) {
        return false;
    }
    if (!indexBuilt) {
        pathToGuidIndex.clear();
        for (const Asset::AssetRecord& record : assetDatabase.GetRegisteredAssets()) {
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

void DecalRenderer::BuildDrawList(Scene::IEntityManager& entityManager,
                                        Asset::IAssetManager& assetManager,
                                        Asset::IAssetDatabase& assetDatabase,
                                        const MeshVisibilitySet& visibilitySet,
                                        const Math::Vector3& cameraWorldPosition,
                                        const Math::Frustum& viewFrustum,
                                        const std::uint32_t receiverLayerMask,
                                        DecalDrawList& outDrawList) const {
    outDrawList.Clear();

    std::vector<DecalReceiver> receivers;
    BuildReceivers(visibilitySet, receivers);

    // Built lazily, on the first decal that gets far enough to need a material, and shared by every
    // decal after it.
    std::unordered_map<std::string, Asset::AssetGuid> pathToGuidIndex;
    bool pathIndexBuilt = false;
    std::unordered_map<std::uint64_t, std::size_t> materialSlots;
    std::vector<Scene::Occluder3DSnapshot> occluders;
    Scene::CollectOccluder3DSnapshots(entityManager, occluders);

    entityManager.ForEach<Scene::Decal3DComponent, Scene::WorldTransformComponent>(
        [&](const Scene::Entity entity, const Scene::Decal3DComponent& decal,
            const Scene::WorldTransformComponent& worldTransform) {
            if (Scene::IsWorldPartition3DDrawHidden(entityManager, entity) ||
                Scene::IsVisibilityRegion3DDrawHidden(entityManager, entity)) {
                return;
            }
            ++outDrawList.decalsConsidered;

            // The one gate that answers "should this decal paint at all": enabled, unexpired and
            // non-degenerate. Everything after this point is about where and how strongly.
            if (!Scene::IsDecal3DPainting(decal)) {
                ++outDrawList.decalsSkippedNotPainting;
                return;
            }

            Scene::Decal3DProjection projection{};
            if (!Scene::TryMakeDecal3DProjection(decal, worldTransform.worldPosition,
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

            const Math::Aabb projectionBounds =
                Math::Aabb::FromCenterExtents(projection.worldPosition, projection.halfExtents);
            // The frustum-culling debug freeze covers decals too: a decal is painted by the same
            // main view whose meshes stopped being rejected, and leaving it culled would make the
            // frozen frame disagree with itself about what is on screen. Occlusion, layer and
            // distance-fade verdicts are separate systems and keep running.
            if (!visibilitySet.frustumTestsDisabled && !viewFrustum.Intersects(projectionBounds)) {
                ++outDrawList.decalsOutsideView;
                return;
            }
            if (Scene::IsOccluder3DAabbDrawHidden(occluders, cameraWorldPosition, projectionBounds)) {
                return;
            }

            // Distance is measured to the decal's own origin, which is where its projection volume
            // is centred, and the fade is asked through the same sampler the per-patch weights use:
            // one rule, evaluated at the centre with a surface facing squarely back at the decal, so
            // the decal-level cull and the patch-level weight can never disagree.
            const float distanceToCamera = Math::Length(projection.worldPosition - cameraWorldPosition);
            const Scene::Decal3DSample centreSample =
                Scene::SampleDecal3D(projection, projection.worldPosition, -projection.projectionDirection,
                                        distanceToCamera);
            if (centreSample.distanceFadeWeight <= 0.0F) {
                ++outDrawList.decalsFadedOut;
                return;
            }

            Asset::AssetGuid materialGuid = Asset::kInvalidAssetGuid;
            if (!TryResolveDecalMaterial(decal.materialAssetPath, assetDatabase, pathToGuidIndex,
                                            pathIndexBuilt, materialGuid)) {
                ++outDrawList.decalsWithoutMaterial;
                return;
            }
            Asset::AssetHandle<Asset::MaterialAsset> materialHandle =
                assetManager.Load<Asset::MaterialAsset>(materialGuid, assetDatabase);
            if (!materialHandle.IsReady() || materialHandle.TryGet() == nullptr) {
                // Pending or failed. Either way there is nothing to paint with this frame, and the
                // decal appears on its own once the load lands - the same non-blocking contract the
                // mesh path has.
                ++outDrawList.decalsWithoutMaterial;
                return;
            }

            DecalDraw draw{};
            draw.decal = entity;
            draw.materialIndex = ResolveMaterialIndex(materialSlots, outDrawList.materialHandles,
                                                          materialGuid, materialHandle);
            draw.projection = projection;
            draw.modulate = decal.modulate;
            draw.emissionEnergy = decal.emissionEnergy;
            draw.albedoMix = decal.albedoMix;
            draw.sortDepth = distanceToCamera;

            for (const DecalReceiver& receiver : receivers) {
                if ((decal.cullMask & receiver.renderLayers) == 0U) {
                    continue;
                }
                ++outDrawList.receiversTested;

                // The cheap rejection, in unit space: the receiver's world bounds, expressed as the
                // axis-aligned box that contains them in the decal's own frame, against the unit
                // volume. A receiver the decal cannot reach never reaches the per-face clipping,
                // which is where the real work is.
                Math::Aabb unitBounds{};
                {
                    const std::array<Math::Vector3, 8U> corners = {
                        Math::Vector3{receiver.worldBounds.min.x, receiver.worldBounds.min.y, receiver.worldBounds.min.z},
                        Math::Vector3{receiver.worldBounds.min.x, receiver.worldBounds.min.y, receiver.worldBounds.max.z},
                        Math::Vector3{receiver.worldBounds.min.x, receiver.worldBounds.max.y, receiver.worldBounds.min.z},
                        Math::Vector3{receiver.worldBounds.min.x, receiver.worldBounds.max.y, receiver.worldBounds.max.z},
                        Math::Vector3{receiver.worldBounds.max.x, receiver.worldBounds.min.y, receiver.worldBounds.min.z},
                        Math::Vector3{receiver.worldBounds.max.x, receiver.worldBounds.min.y, receiver.worldBounds.max.z},
                        Math::Vector3{receiver.worldBounds.max.x, receiver.worldBounds.max.y, receiver.worldBounds.min.z},
                        Math::Vector3{receiver.worldBounds.max.x, receiver.worldBounds.max.y, receiver.worldBounds.max.z},
                    };
                    const Math::Vector3 first = Scene::Decal3DWorldToUnit(projection, corners[0]);
                    unitBounds = Math::Aabb{first, first};
                    for (std::size_t cornerIndex = 1U; cornerIndex < corners.size(); ++cornerIndex) {
                        const Math::Vector3 unitCorner = Scene::Decal3DWorldToUnit(projection, corners[cornerIndex]);
                        unitBounds.min = Math::Vector3{std::min(unitBounds.min.x, unitCorner.x),
                                                          std::min(unitBounds.min.y, unitCorner.y),
                                                          std::min(unitBounds.min.z, unitCorner.z)};
                        unitBounds.max = Math::Vector3{std::max(unitBounds.max.x, unitCorner.x),
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
                    std::array<Math::Vector3, 4U> worldCorners{};
                    Math::Vector3 faceNormal{};
                    BuildBoxFaceQuad(receiver.worldBounds, faceIndex, worldCorners, faceNormal);

                    // A receiving face turned away from the camera cannot be seen, so a decal patch
                    // painted on it would cost a draw and paint nothing that is not already behind
                    // the face in front of it.
                    const Math::Vector3 faceCentre = (worldCorners[0] + worldCorners[1] + worldCorners[2] +
                                                         worldCorners[3]) *
                                                        0.25F;
                    if (Math::Dot(faceNormal, cameraWorldPosition - faceCentre) <= 0.0F) {
                        ++outDrawList.patchesDiscardedBackFacing;
                        continue;
                    }

                    ClipPolygon unitPolygon{};
                    for (const Math::Vector3& corner : worldCorners) {
                        unitPolygon.Push(Scene::Decal3DWorldToUnit(projection, corner));
                    }

                    // Clip in unit space, where the volume is exactly the unit box or the unit
                    // cylinder. The box is six half-spaces; the cylinder is the same six, except
                    // that its four side planes are one round wall.
                    // Each step writes into the polygon the previous one read, never into itself:
                    // a clip that cleared its own output before reading its input would clip
                    // nothing at all.
                    ClipPolygon clipped = unitPolygon;
                    ClipPolygon scratch{};
                    ClipAgainstAxisBound(clipped, 1U, 1.0F, 1.0F, scratch);
                    ClipAgainstAxisBound(scratch, 1U, -1.0F, 1.0F, clipped);
                    ClipAgainstAxisBound(clipped, 0U, 1.0F, 1.0F, scratch);
                    ClipAgainstAxisBound(scratch, 0U, -1.0F, 1.0F, clipped);
                    if (projection.mode == Scene::DecalProjectionMode::Cylinder) {
                        ClipAgainstUnitCylinderWall(clipped, scratch);
                        clipped = scratch;
                    } else {
                        ClipAgainstAxisBound(clipped, 2U, 1.0F, 1.0F, scratch);
                        ClipAgainstAxisBound(scratch, 2U, -1.0F, 1.0F, clipped);
                    }

                    if (clipped.count < 3U) {
                        // Wholly outside the volume; not a defect, so it is not counted as one.
                        continue;
                    }

                    // Back to the world, and weight the patch where it actually sits rather than
                    // where the face's centre would have been before clipping.
                    DecalPatch patch{};
                    patch.vertexCount = clipped.count;
                    patch.normal = faceNormal;
                    Math::Vector3 unitCentroid{};
                    for (std::size_t vertexIndex = 0U; vertexIndex < clipped.count; ++vertexIndex) {
                        patch.unitCoords[vertexIndex] =
                            Math::Vector2{clipped.points[vertexIndex].x, clipped.points[vertexIndex].z};
                        patch.worldPositions[vertexIndex] =
                            Scene::Decal3DUnitToWorld(projection, clipped.points[vertexIndex]);
                        unitCentroid = unitCentroid + clipped.points[vertexIndex];
                    }
                    unitCentroid = unitCentroid * (1.0F / static_cast<float>(clipped.count));

                    const Math::Vector3 worldCentroid =
                        Scene::Decal3DUnitToWorld(projection, unitCentroid);
                    const Scene::Decal3DSample sample = Scene::SampleDecal3D(
                        projection, worldCentroid, faceNormal,
                        Math::Length(worldCentroid - cameraWorldPosition));
                    patch.weight = sample.combinedWeight;
                    if (patch.weight <= 0.0F) {
                        ++outDrawList.patchesDiscardedByFade;
                        continue;
                    }

                    if (PolygonArea(patch.worldPositions, patch.vertexCount) < kMinimumPatchAreaSquareMetres) {
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

    outDrawList.SortBackToFront(cameraWorldPosition);
}

} // namespace UVE::Render
