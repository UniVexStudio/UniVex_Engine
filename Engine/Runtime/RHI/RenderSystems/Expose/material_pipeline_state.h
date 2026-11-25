// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>

#include "material_asset.h"
#include "render_resource_descs.h"

namespace UVE::Render {

/// The material's blend mode as the pipeline expresses it.
///
/// Every authored mode has a home here. A value that fell through to a default would be an
/// authoring control that silently composites as something else, which is exactly the failure this
/// header exists to make impossible - the mapping is total, and the tests enumerate all of it.
[[nodiscard]] constexpr PipelineBlendMode ToPipelineBlendMode(
    const Asset::MaterialBlendMode mode) noexcept {
    switch (mode) {
        case Asset::MaterialBlendMode::Opaque:
            return PipelineBlendMode::Opaque;
        case Asset::MaterialBlendMode::AlphaBlend:
            return PipelineBlendMode::SourceAlphaOver;
        case Asset::MaterialBlendMode::PremultipliedAlpha:
            return PipelineBlendMode::PremultipliedAlpha;
        case Asset::MaterialBlendMode::Additive:
            return PipelineBlendMode::Additive;
        case Asset::MaterialBlendMode::Subtractive:
            return PipelineBlendMode::Subtractive;
        case Asset::MaterialBlendMode::Multiply:
            return PipelineBlendMode::Multiply;
    }
    return PipelineBlendMode::Opaque;
}

/// Which triangle faces the rasterizer discards, from the material's own choice.
[[nodiscard]] constexpr CullMode ToCullMode(const Asset::MaterialCullMode mode) noexcept {
    switch (mode) {
        case Asset::MaterialCullMode::Back:
            return CullMode::Back;
        case Asset::MaterialCullMode::Front:
            return CullMode::Front;
        case Asset::MaterialCullMode::Disabled:
            return CullMode::None;
    }
    return CullMode::Back;
}

/// The blend mode the pipeline will actually use, with the two older spellings of "translucent"
/// folded in.
///
/// A material saved before blend modes were honoured carries the default `Opaque` plus either
/// `isTransparent` or an `albedoAlpha` below one, and both of those make the fragment shader emit
/// alpha below one. Leaving the pipeline `Opaque` for them would ignore that alpha completely: the
/// surface would be bucketed into the transparent pass, sort back to front, and then composite as
/// if it were solid. Reading the raw field anywhere would also make one material answer the depth
/// rule and the blend rule differently, which is the defect the depth test below caught.
[[nodiscard]] constexpr PipelineBlendMode EffectiveBlendMode(
    const Asset::MaterialAsset& material) noexcept {
    if (material.blendMode != Asset::MaterialBlendMode::Opaque) {
        return ToPipelineBlendMode(material.blendMode);
    }
    if (material.isTransparent || material.albedoAlpha < 1.0F) {
        return PipelineBlendMode::SourceAlphaOver;
    }
    return PipelineBlendMode::Opaque;
}

/// Whether this material writes depth.
///
/// `depthDrawMode` is the authored rule and `depthWriteEnabled` is the capability underneath it, so
/// neither alone answers the question: `OpaqueOnly` with the switch off means a translucent AND an
/// opaque material both skip the write, while `Always` with it off still means never. The default
/// pair (`OpaqueOnly`, enabled) reproduces the pre-existing rule exactly - an opaque material writes
/// depth, an alpha-blended one does not.
[[nodiscard]] constexpr bool MaterialWritesDepth(const Asset::MaterialAsset& material) noexcept {
    if (!material.depthWriteEnabled) {
        return false;
    }
    switch (material.depthDrawMode) {
        case Asset::MaterialDepthDrawMode::Never:
            return false;
        case Asset::MaterialDepthDrawMode::Always:
        case Asset::MaterialDepthDrawMode::DepthPrePass:
            return true;
        case Asset::MaterialDepthDrawMode::OpaqueOnly:
            return EffectiveBlendMode(material) == PipelineBlendMode::Opaque;
    }
    return EffectiveBlendMode(material) == PipelineBlendMode::Opaque;
}

/// The rasterizer/depth/blend state a material draws with, packed into one comparable integer so a
/// shared program can be cached per COMBINATION rather than per material. A project uses a handful
/// of these however many materials it has, so the cache stays small; keying by blend mode alone
/// would be wrong the moment one material asked for a different depth rule than another with the
/// same mode.
struct MaterialPipelineState final {
    PipelineBlendMode blendMode = PipelineBlendMode::Opaque;
    CullMode cullMode = CullMode::None;
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;

    [[nodiscard]] constexpr std::uint32_t Pack() const noexcept {
        return static_cast<std::uint32_t>(blendMode) | (static_cast<std::uint32_t>(cullMode) << 3U) |
               (depthTestEnabled ? (1U << 5U) : 0U) | (depthWriteEnabled ? (1U << 6U) : 0U);
    }

    [[nodiscard]] friend constexpr bool operator==(const MaterialPipelineState& lhs,
                                                   const MaterialPipelineState& rhs) noexcept {
        return lhs.Pack() == rhs.Pack();
    }
};

/// The pipeline state a material asks for, resolved in one place so the built-in lit path, the
/// project-authored-shader path and the tests cannot disagree about what a field means.
[[nodiscard]] constexpr MaterialPipelineState ResolveMaterialPipelineState(
    const Asset::MaterialAsset& material) noexcept {
    MaterialPipelineState state;
    state.blendMode = EffectiveBlendMode(material);
    state.cullMode = ToCullMode(material.cullMode);
    state.depthTestEnabled = material.depthTestEnabled;
    state.depthWriteEnabled = MaterialWritesDepth(material);
    return state;
}

/// True when the material needs its rgb premultiplied by alpha before the pipeline composites it.
/// Only the premultiplied mode does: its source factor is ONE, so an un-premultiplied colour would
/// be added at full strength.
[[nodiscard]] constexpr bool MaterialPremultipliesAlpha(
    const Asset::MaterialAsset& material) noexcept {
    return material.blendMode == Asset::MaterialBlendMode::PremultipliedAlpha;
}

} // namespace UVE::Render
