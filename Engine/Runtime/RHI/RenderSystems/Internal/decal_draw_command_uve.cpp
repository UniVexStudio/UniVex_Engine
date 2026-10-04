// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/render_systems/decal_draw_command_uve.h"

#include <algorithm>

namespace UVE::Render {

namespace {

/// The material a draw paints with, or nullptr when the handle is gone or not loaded. Both are
/// ordinary outcomes between the projection pass and the plan: the asset manager collects handles
/// it no longer sees, and a decal spawned this frame names a material that may still be in flight.
[[nodiscard]] const Asset::MaterialAssetUVE* TryResolveMaterialUVE(const DecalDrawListUVE& drawList,
                                                                   const std::size_t materialIndex) noexcept {
    const Asset::AssetHandleUVE<Asset::MaterialAssetUVE>* const handle =
        drawList.TryGetMaterialHandleUVE(materialIndex);
    return handle != nullptr ? handle->TryGetUVE() : nullptr;
}

} // namespace

void DecalDrawPlanUVE::ClearUVE() noexcept {
    vertices.clear();
    indices.clear();
    commands.clear();
    drawsTruncated = 0U;
    drawsWithoutMaterial = 0U;
    drawsWithoutGeometry = 0U;
    drawsWithoutInverse = 0U;
    drawsWithoutPaint = 0U;
}

void BuildDecalDrawPlanUVE(const DecalDrawListUVE& drawList, DecalDrawPlanUVE& outPlan,
                           const std::size_t maximumCommands, const std::size_t maximumVertices) {
    outPlan.ClearUVE();
    // The plan's own capacity follows the cap, so a frame that hits it has already stopped growing
    // rather than allocating up to the cap and discarding the overflow afterwards.
    outPlan.commands.reserve(std::min(maximumCommands, drawList.draws.size()));

    for (const DecalDrawUVE& draw : drawList.draws) {
        if (outPlan.commands.size() >= maximumCommands) {
            ++outPlan.drawsTruncated;
            continue;
        }

        const Asset::MaterialAssetUVE* const material = TryResolveMaterialUVE(drawList, draw.materialIndex);
        if (material == nullptr) {
            ++outPlan.drawsWithoutMaterial;
            continue;
        }

        // A decal whose blend alpha is zero cannot change a pixel of the surface under it, so the
        // draw is one the GPU would run and the frame would not show. `albedoMix` 0 is exactly that
        // state: this forward pass can only replace the receiving surface's colour by blending over
        // it, so a decal that replaces none of it has nothing left to paint with.
        if (!(draw.albedoMix > 0.0F)) {
            ++outPlan.drawsWithoutPaint;
            continue;
        }

        // The volume's own unit space, inverted once per decal rather than per vertex. This is the
        // same transform Scene::Decal3DWorldToUnitUVE() applies, built from the same three pieces
        // (position, rotation, half extents) that function uses, so the CPU's clipping space and the
        // fragment program's coordinate space are one space and not two that happen to agree today.
        const Math::Matrix4x4UVE unitToWorld =
            Math::Matrix4x4UVE::ComposeTrsUVE(draw.projection.worldPosition, draw.projection.worldRotation,
                                              draw.projection.halfExtents);
        Math::Matrix4x4UVE worldToUnit{};
        if (!Math::TryInverseUVE(unitToWorld, worldToUnit)) {
            // `TryMakeDecal3DProjectionUVE()` already refused a degenerate volume, so this is the
            // float-precision edge of a volume a thousand units wide and a thousandth across; a
            // smear mapped through a singular basis is worse than no decal.
            ++outPlan.drawsWithoutInverse;
            continue;
        }

        // Counted before a vertex is written: a command the cap refuses must not leave half of
        // itself in the streams for the next decal to be drawn with.
        std::size_t vertexCount = 0U;
        std::size_t indexCount = 0U;
        for (const DecalPatchUVE& patch : draw.patches) {
            if (!patch.IsValidUVE()) {
                continue;
            }
            vertexCount += patch.vertexCount;
            indexCount += (patch.vertexCount - 2U) * 3U;
        }
        if (vertexCount < 3U) {
            ++outPlan.drawsWithoutGeometry;
            continue;
        }
        if (outPlan.vertices.size() + vertexCount > maximumVertices) {
            ++outPlan.drawsTruncated;
            continue;
        }

        const std::size_t firstVertex = outPlan.vertices.size();
        const std::size_t firstIndex = outPlan.indices.size();
        for (const DecalPatchUVE& patch : draw.patches) {
            AppendDecalPatchUVE(patch, draw.projection.projectionDirection, outPlan.vertices, outPlan.indices);
        }

        DecalDrawCommandUVE command{};
        command.decal = draw.decal;
        command.firstVertex = static_cast<std::uint32_t>(firstVertex);
        command.vertexCount = static_cast<std::uint32_t>(vertexCount);
        command.firstIndex = static_cast<std::uint32_t>(firstIndex);
        command.indexCount = static_cast<std::uint32_t>(indexCount);
        command.worldToUnit = worldToUnit;
        command.projectionDirection = draw.projection.projectionDirection;
        command.albedoTextureGuid = material->albedoTexture;
        command.baseColor = material->albedoColor * draw.modulate;
        command.emissionColor = material->emissiveColor * draw.emissionEnergy;
        command.alphaScale = draw.albedoMix;
        command.normalFade = draw.projection.normalFade;
        command.upperFade = draw.projection.upperFade;
        command.lowerFade = draw.projection.lowerFade;
        command.distanceFadeEnabled = draw.projection.distanceFadeEnabled;
        command.distanceFadeBegin = draw.projection.distanceFadeBegin;
        command.distanceFadeLength = draw.projection.distanceFadeLength;
        outPlan.commands.push_back(command);
    }
}

} // namespace UVE::Render
