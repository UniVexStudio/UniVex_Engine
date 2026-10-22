// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string_view>

namespace UVE::Render::Shader::BuiltIn {

/// The built-in shaders (4 from Increment 21, plus shadow_depth from Increment 26), each a single
/// physical `.glsl` file under
/// engine/render/shader/built_in/ containing both stages, split via `#ifdef VERTEX_SHADER` /
/// `#ifdef FRAGMENT_SHADER` (ShaderManagerUVE::CreateProgramUVE() compiles the same resolved
/// source twice, injecting the matching macro each time). Every constant here is the embedded
/// fallback ShaderManagerUVE transparently uses when the corresponding virtual path isn't
/// reachable (see ShaderProgramDescUVE's doc comment) — kept byte-identical to its `.glsl` file
/// by convention, enforced by tests/render/shader/built_in_shaders_parity_uve_tests.cpp.

inline constexpr std::string_view kBasic2DVirtualPath = "shaders/basic_2d.glsl";
extern const std::string_view kBasic2DSource;

inline constexpr std::string_view kBasic3DVirtualPath = "shaders/basic_3d.glsl";
extern const std::string_view kBasic3DSource;

inline constexpr std::string_view kBasic3DTexturedVirtualPath = "shaders/basic_3d_textured.glsl";
extern const std::string_view kBasic3DTexturedSource;

inline constexpr std::string_view kFullscreenQuadVirtualPath = "shaders/fullscreen_quad.glsl";
extern const std::string_view kFullscreenQuadSource;

inline constexpr std::string_view kProceduralSkyVirtualPath = "shaders/procedural_sky.glsl";
extern const std::string_view kProceduralSkySource;

inline constexpr std::string_view kShadowDepthVirtualPath = "shaders/shadow_depth.glsl";
extern const std::string_view kShadowDepthSource;

/// Renderer3DUVE's occlusion proxy (Foundation WP3): a position-only box drawn depth-tested with
/// depth writes off and an all-zero additive colour, so it changes neither attachment and only
/// feeds the AnySamplesPassed bracket it is recorded inside.
inline constexpr std::string_view kOcclusionProxyVirtualPath = "shaders/occlusion_proxy.glsl";
extern const std::string_view kOcclusionProxySource;

inline constexpr std::string_view kLitShadowed3DVirtualPath = "shaders/lit_shadowed_3d.glsl";
extern const std::string_view kLitShadowed3DSource;

/// Lambert-only shading for PrimitiveMeshComponentUVE geometry, which has an authored base colour
/// but no material and so cannot use kLitShadowed3DSource's PBR/texture/shadow path.
inline constexpr std::string_view kLitPrimitive3DVirtualPath = "shaders/lit_primitive_3d.glsl";
extern const std::string_view kLitPrimitive3DSource;

inline constexpr std::string_view kParticleVirtualPath = "shaders/particle.glsl";
extern const std::string_view kParticleSource;

/// Decal3D's paint pass: world-space, CPU-clipped patches projected through uViewProjection, with
/// the authored fades evaluated per pixel from the same unit-space matrix the CPU clipped in.
inline constexpr std::string_view kDecalVirtualPath = "shaders/decal.glsl";
extern const std::string_view kDecalSource;

/// DebugRendererUVE's line pass: world-space positions with per-vertex colors through
/// uViewProjection, so one batch carries every debug line of the frame in a single draw.
inline constexpr std::string_view kDebugLineVirtualPath = "shaders/debug_line.glsl";
/// DebugRendererUVE's point pass: the line pass's vertex format drawn as a Points pipeline, with
/// one batch-wide uPointSize (pixels) written to gl_PointSize.
inline constexpr std::string_view kDebugPointVirtualPath = "shaders/debug_point.glsl";
extern const std::string_view kDebugPointSource;
/// Renderer2DUVE's sprite pass: world-space textured quads with per-vertex modulate
/// through uViewProjection, sampled from uTexture (slot 0) - one upload + draw per sprite.
inline constexpr std::string_view kSprite2DVirtualPath = "shaders/sprite_2d.glsl";
extern const std::string_view kDebugLineSource;
extern const std::string_view kSprite2DSource;

/// The compute kernel Render::ParticleComputeSimulationUVE dispatches (CS4) - the GPU twin of
/// Scene::ParticleRuntimeUVE's per-particle integration. Unlike every other entry here this file
/// holds a single COMPUTE stage, so it carries no VERTEX_SHADER/FRAGMENT_SHADER split and is
/// compiled once, through IComputeSystemUVE::CreateProgramUVE() rather than ShaderManagerUVE.
inline constexpr std::string_view kParticleSimulateVirtualPath = "shaders/particle_simulate.glsl";
extern const std::string_view kParticleSimulateSource;

/// The compute kernel Render::FrustumCullComputeUVE dispatches (CS5) - the GPU twin of
/// Math::FrustumUVE::IntersectsUVE over many boxes at once. COMPUTE-only, like its
/// particle_simulate sibling, so it carries no VERTEX/FRAGMENT split.
inline constexpr std::string_view kFrustumCullVirtualPath = "shaders/frustum_cull.glsl";
extern const std::string_view kFrustumCullSource;

/// CS8's variant of the cull kernel: same test, but instead of a visibility array the host reads
/// back, it writes an indirect draw's instanceCount and a compacted list of surviving indices
/// straight into device memory. COMPUTE-only, like its two siblings above.
inline constexpr std::string_view kFrustumCullIndirectVirtualPath =
    "shaders/frustum_cull_indirect.glsl";
extern const std::string_view kFrustumCullIndirectSource;

/// CS10's skinning kernel: the GPU twin of Asset::TrySkinMeshUVE. COMPUTE-only.
inline constexpr std::string_view kMeshSkinVirtualPath = "shaders/mesh_skin.glsl";
extern const std::string_view kMeshSkinSource;

inline constexpr std::string_view kBloomBrightPassVirtualPath = "shaders/bloom_bright_pass.glsl";
extern const std::string_view kBloomBrightPassSource;

inline constexpr std::string_view kBloomDownsampleVirtualPath = "shaders/bloom_downsample.glsl";
extern const std::string_view kBloomDownsampleSource;

inline constexpr std::string_view kBloomBlurVirtualPath = "shaders/bloom_blur.glsl";
extern const std::string_view kBloomBlurSource;

inline constexpr std::string_view kFullscreenCopyVirtualPath = "shaders/fullscreen_copy.glsl";
extern const std::string_view kFullscreenCopySource;

inline constexpr std::string_view kSsaoVirtualPath = "shaders/ssao.glsl";
extern const std::string_view kSsaoSource;

inline constexpr std::string_view kUIOverlayVirtualPath = "shaders/ui_overlay.glsl";
extern const std::string_view kUIOverlaySource;

inline constexpr std::string_view kContactShadowVirtualPath = "shaders/contact_shadow.glsl";
extern const std::string_view kContactShadowSource;

} // namespace UVE::Render::Shader::BuiltIn
