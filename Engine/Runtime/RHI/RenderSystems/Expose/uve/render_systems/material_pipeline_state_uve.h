// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>

#include "uve/asset/material_asset_uve.h"
#include "uve/rhi/render_resource_descs_uve.h"

namespace UVE::Render {

/// The material's blend mode as the pipeline expresses it.
///
/// Every authored mode has a home here. A value that fell through to a default would be an
/// authoring control that silently composites as something else, which is exactly the failure this
/// header exists to make impossible - the mapping is total, and the tests enumerate all of it.
[[nodiscard]] constexpr PipelineBlendModeUVE ToPipelineBlendModeUVE(
    const Asset::MaterialBlendModeUVE mode) noexcept {
    switch (mode) {
        case Asset::MaterialBlendModeUVE::Opaque:
            return PipelineBlendModeUVE::Opaque;
        case Asset::MaterialBlendModeUVE::AlphaBlend:
            return PipelineBlendModeUVE::SourceAlphaOver;
        case Asset::MaterialBlendModeUVE::PremultipliedAlpha:
            return PipelineBlendModeUVE::PremultipliedAlpha;
        case Asset::MaterialBlendModeUVE::Additive:
            return PipelineBlendModeUVE::Additive;
        case Asset::MaterialBlendModeUVE::Subtractive:
            return PipelineBlendModeUVE::Subtractive;
        case Asset::MaterialBlendModeUVE::Multiply:
            return PipelineBlendModeUVE::Multiply;
    }
    return PipelineBlendModeUVE::Opaque;
}

/// Which triangle faces the rasterizer discards, from the material's own choice.
[[nodiscard]] constexpr CullModeUVE ToCullModeUVE(const Asset::MaterialCullModeUVE mode) noexcept {
    switch (mode) {
        case Asset::MaterialCullModeUVE::Back:
            return CullModeUVE::Back;
        case Asset::MaterialCullModeUVE::Front:
            return CullModeUVE::Front;
        case Asset::MaterialCullModeUVE::Disabled:
            return CullModeUVE::None;
    }
    return CullModeUVE::Back;
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
[[nodiscard]] constexpr PipelineBlendModeUVE EffectiveBlendModeUVE(
    const Asset::MaterialAssetUVE& material) noexcept {
    if (material.blendMode != Asset::MaterialBlendModeUVE::Opaque) {
        return ToPipelineBlendModeUVE(material.blendMode);
    }
    if (material.isTransparent || material.albedoAlpha < 1.0F) {
        return PipelineBlendModeUVE::SourceAlphaOver;
    }
    return PipelineBlendModeUVE::Opaque;
}

/// Whether this material writes depth.
///
/// `depthDrawMode` is the authored rule and `depthWriteEnabled` is the capability underneath it, so
/// neither alone answers the question: `OpaqueOnly` with the switch off means a translucent AND an
/// opaque material both skip the write, while `Always` with it off still means never. The default
/// pair (`OpaqueOnly`, enabled) reproduces the pre-existing rule exactly - an opaque material writes
/// depth, an alpha-blended one does not.
[[nodiscard]] constexpr bool MaterialWritesDepthUVE(const Asset::MaterialAssetUVE& material) noexcept {
    if (!material.depthWriteEnabled) {
        return false;
    }
    switch (material.depthDrawMode) {
        case Asset::MaterialDepthDrawModeUVE::Never:
            return false;
        case Asset::MaterialDepthDrawModeUVE::Always:
        case Asset::MaterialDepthDrawModeUVE::DepthPrePass:
            return true;
        case Asset::MaterialDepthDrawModeUVE::OpaqueOnly:
            return EffectiveBlendModeUVE(material) == PipelineBlendModeUVE::Opaque;
    }
    return EffectiveBlendModeUVE(material) == PipelineBlendModeUVE::Opaque;
}

/// The rasterizer/depth/blend state a material draws with, packed into one comparable integer so a
/// shared program can be cached per COMBINATION rather than per material. A project uses a handful
/// of these however many materials it has, so the cache stays small; keying by blend mode alone
/// would be wrong the moment one material asked for a different depth rule than another with the
/// same mode.
struct MaterialPipelineStateUVE final {
    PipelineBlendModeUVE blendMode = PipelineBlendModeUVE::Opaque;
    CullModeUVE cullMode = CullModeUVE::None;
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;

    [[nodiscard]] constexpr std::uint32_t PackUVE() const noexcept {
        return static_cast<std::uint32_t>(blendMode) | (static_cast<std::uint32_t>(cullMode) << 3U) |
               (depthTestEnabled ? (1U << 5U) : 0U) | (depthWriteEnabled ? (1U << 6U) : 0U);
    }

    [[nodiscard]] friend constexpr bool operator==(const MaterialPipelineStateUVE& lhs,
                                                   const MaterialPipelineStateUVE& rhs) noexcept {
        return lhs.PackUVE() == rhs.PackUVE();
    }
};

/// The pipeline state a material asks for, resolved in one place so the built-in lit path, the
/// project-authored-shader path and the tests cannot disagree about what a field means.
[[nodiscard]] constexpr MaterialPipelineStateUVE ResolveMaterialPipelineStateUVE(
    const Asset::MaterialAssetUVE& material) noexcept {
    MaterialPipelineStateUVE state;
    state.blendMode = EffectiveBlendModeUVE(material);
    state.cullMode = ToCullModeUVE(material.cullMode);
    state.depthTestEnabled = material.depthTestEnabled;
    state.depthWriteEnabled = MaterialWritesDepthUVE(material);
    return state;
}

/// True when the material needs its rgb premultiplied by alpha before the pipeline composites it.
/// Only the premultiplied mode does: its source factor is ONE, so an un-premultiplied colour would
/// be added at full strength.
[[nodiscard]] constexpr bool MaterialPremultipliesAlphaUVE(
    const Asset::MaterialAssetUVE& material) noexcept {
    return material.blendMode == Asset::MaterialBlendModeUVE::PremultipliedAlpha;
}

} // namespace UVE::Render
