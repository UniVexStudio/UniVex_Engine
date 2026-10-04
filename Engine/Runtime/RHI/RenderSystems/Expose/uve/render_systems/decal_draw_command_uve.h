// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/render_systems/decal_draw_data_uve.h"

namespace UVE::Render {

/// The most decals one frame submits. A projection pass is bounded by the receivers it walks, not
/// by a scene's lifetime, so a pathological frame (hundreds of overlapping decals on a dense mesh)
/// has to stop somewhere rather than allocate a draw per decal until memory runs out. Reaching it
/// is reported (`DecalDrawPlanUVE::drawsTruncated`), never silent.
inline constexpr std::size_t kMaximumDecalDrawCommandsUVE = 4096U;

/// The most vertices one frame's decal geometry may hold, across every decal in it. A patch is at
/// most eight vertices, and a decal that lands on a dense mesh can hold one patch per receiving
/// face, so this is the second half of the same bound as the command cap.
inline constexpr std::size_t kMaximumDecalVerticesUVE = 262144U;

/// One decal's GPU work: the slice of the plan's vertex/index streams that belongs to it, and the
/// uniform values that slice is drawn with.
///
/// WHY THE UNIFORMS TRAVEL WITH THE DRAW. The CPU pass decides WHICH patches exist by clipping the
/// receiving surfaces against the volume, and evaluates the authored fades at each patch's centre
/// to reject the ones that paint nothing. What it cannot decide on the CPU is how much of the decal
/// reaches each PIXEL of a surviving patch: a patch is one receiving face, and a wall's face is
/// large enough that the fade across it is the difference between a decal that blends into the
/// surface and one with a hard rectangular edge. So the draw carries the volume and the authored
/// fades, and the fragment program evaluates the same rule per pixel.
struct DecalDrawCommandUVE final {
    Scene::EntityUVE decal = Scene::kInvalidEntityUVE;

    /// This decal's window in DecalDrawPlanUVE::vertices / ::indices. Indices are already absolute
    /// into the plan's vertex stream, so a command is drawn by binding the slice's own buffers.
    std::uint32_t firstVertex = 0U;
    std::uint32_t vertexCount = 0U;
    std::uint32_t firstIndex = 0U;
    std::uint32_t indexCount = 0U;

    /// World space to the volume's unit space: the inverse of
    /// `ComposeTrsUVE(projection.worldPosition, projection.worldRotation, projection.halfExtents)`,
    /// which is exactly what `Scene::Decal3DUnitToWorldUVE()` maps with. Handing the shader this
    /// one matrix is what keeps the unit coordinates the fragment program works in identical to the
    /// ones the CPU clipped in - a second, hand-written reconstruction of the same transform in
    /// GLSL is a second thing that can disagree with `Scene::Decal3DWorldToUnitUVE()`.
    Math::Matrix4x4UVE worldToUnit{};
    /// Unit direction the decal projects along, for the per-pixel normal fade.
    Math::Vector3UVE projectionDirection{};

    /// The material's albedo texture, or `kInvalidAssetGuidUVE` for a flat-coloured decal. The
    /// renderer resolves it to a GPU handle the same way the mesh path does, with the same fallback
    /// texture when it is unset or still loading - and the patch's unit coordinates are already the
    /// [0, 1] UVs that texture is sampled with, so a decal material's texture lands on the volume
    /// exactly as the artist authored it.
    Asset::AssetGuidUVE albedoTextureGuid;
    /// The material's albedo, tinted by the decal's `modulate`.
    Math::Vector3UVE baseColor{1.0F, 1.0F, 1.0F};
    /// The material's emissive, scaled by the decal's `emissionEnergy`.
    Math::Vector3UVE emissionColor{0.0F, 0.0F, 0.0F};
    /// How much of the receiving surface's own colour the decal replaces - the decal's `albedoMix`,
    /// carried as the blend's alpha scale.
    float alphaScale = 1.0F;

    float normalFade = 0.0F;
    float upperFade = 0.0F;
    float lowerFade = 0.0F;
    bool distanceFadeEnabled = false;
    float distanceFadeBegin = 0.0F;
    float distanceFadeLength = 0.0F;
};

/// This frame's decal geometry and the commands that draw it, in the order they must be drawn
/// (the draw list is already back-to-front, and the plan keeps that order).
///
/// The streams are one flat pair for the whole frame: each decal's vertices are appended as its
/// command is built, and the command's firstVertex/firstIndex are its window into them. The
/// renderer uploads each window into that decal's own GPU buffers because this engine's command
/// buffer has no first-index offset on a draw - the same reason a skinned mesh owns its vertex
/// buffer - so the split belongs here, where the geometry is already grouped.
struct DecalDrawPlanUVE final {
    std::vector<Asset::MeshVertexUVE> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<DecalDrawCommandUVE> commands;

    /// Draws the caps refused, and why a draw that reached this point produced no command. Kept
    /// apart for the reason the extractor's own counters are: "nothing was drawn" and "the frame
    /// hit its bound" are different answers, and the second one is the only one that says the scene
    /// needs its decals budgeted rather than its decals fixed.
    std::size_t drawsTruncated = 0U;
    std::size_t drawsWithoutMaterial = 0U;
    std::size_t drawsWithoutGeometry = 0U;
    std::size_t drawsWithoutInverse = 0U;
    /// Draws that resolve and have geometry but cannot change a pixel: `albedoMix` 0 means the decal
    /// replaces none of the receiving surface's colour, which in a forward alpha-blended pass is the
    /// same thing as painting nothing.
    std::size_t drawsWithoutPaint = 0U;

    void ClearUVE() noexcept;
};

/// Fills `outPlan` from this frame's draw list, clearing it first. `maximumCommands` and
/// `maximumVertices` default to the caps above; a caller that reaches them gets a truncated plan
/// and the counters that say so, never a partially-built command.
///
/// The material each draw names is resolved here rather than at extraction time because the asset
/// can be collected between the two: a handle that resolved when the pass ran can be gone by the
/// time the plan is built, and a draw whose material is gone is skipped and counted instead of
/// being drawn with whatever the last material happened to leave behind.
void BuildDecalDrawPlanUVE(const DecalDrawListUVE& drawList, DecalDrawPlanUVE& outPlan,
                           std::size_t maximumCommands = kMaximumDecalDrawCommandsUVE,
                           std::size_t maximumVertices = kMaximumDecalVerticesUVE);

} // namespace UVE::Render
