// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "gl_command_buffer_uve.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "gl_error_check_uve.h"
#include "uve/logging/assert_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Render {

namespace {

[[nodiscard]] bool ValidateUniformTypeUVE(
    const Detail::GlDeviceStateUVE::PipelineRecordUVE::UniformRecordUVE& uniform,
    ShaderDataTypeUVE expectedType, std::string_view name) noexcept {
    if (uniform.type == expectedType) {
        return true;
    }
    UVE_ERROR("GlCommandBufferUVE: uniform type mismatch for '{}'", name);
    return false;
}

[[nodiscard]] bool RequireInsideRenderPassUVE(bool insideRenderPass, std::string_view operation) noexcept {
    UVE_ASSERT(insideRenderPass);
    if (!insideRenderPass) {
        UVE_ERROR("GlCommandBufferUVE: {} must be called inside a render pass", operation);
        return false;
    }
    return true;
}

[[nodiscard]] bool RequireOutsideRenderPassUVE(bool insideRenderPass) noexcept {
    UVE_ASSERT(!insideRenderPass);
    if (insideRenderPass) {
        UVE_ERROR("GlCommandBufferUVE: BeginRenderPassUVE does not support nested render passes");
        return false;
    }
    return true;
}

[[nodiscard]] GLenum ToGlCompareUVE(const DepthCompareUVE compare) noexcept {
    switch (compare) {
        case DepthCompareUVE::Never: return GL_NEVER;
        case DepthCompareUVE::Less: return GL_LESS;
        case DepthCompareUVE::Equal: return GL_EQUAL;
        case DepthCompareUVE::LessOrEqual: return GL_LEQUAL;
        case DepthCompareUVE::Greater: return GL_GREATER;
        case DepthCompareUVE::NotEqual: return GL_NOTEQUAL;
        case DepthCompareUVE::GreaterOrEqual: return GL_GEQUAL;
        case DepthCompareUVE::Always: return GL_ALWAYS;
    }
    return GL_ALWAYS;
}

[[nodiscard]] GLenum ToGlStencilOpUVE(const StencilOpUVE op) noexcept {
    switch (op) {
        case StencilOpUVE::Keep: return GL_KEEP;
        case StencilOpUVE::Zero: return GL_ZERO;
        case StencilOpUVE::Replace: return GL_REPLACE;
        case StencilOpUVE::IncrementClamp: return GL_INCR;
        case StencilOpUVE::DecrementClamp: return GL_DECR;
        case StencilOpUVE::Invert: return GL_INVERT;
        case StencilOpUVE::IncrementWrap: return GL_INCR_WRAP;
        case StencilOpUVE::DecrementWrap: return GL_DECR_WRAP;
    }
    return GL_KEEP;
}

void ApplyStencilFaceUVE(const Detail::GlFunctionsUVE& gl, const GLenum face,
                         const StencilFaceStateUVE& state) noexcept {
    gl.glStencilFuncSeparate(face, ToGlCompareUVE(state.compare), static_cast<GLint>(state.reference),
                          static_cast<GLuint>(state.compareMask));
    gl.glStencilOpSeparate(face, ToGlStencilOpUVE(state.stencilFail), ToGlStencilOpUVE(state.depthFail),
                        ToGlStencilOpUVE(state.depthPass));
    gl.glStencilMaskSeparate(face, static_cast<GLuint>(state.writeMask));
}

[[nodiscard]] GLenum ToGlDrawModeUVE(PrimitiveTopologyUVE topology) noexcept {
    switch (topology) {
        case PrimitiveTopologyUVE::Triangles:
            return GL_TRIANGLES;
        case PrimitiveTopologyUVE::Lines:
            return GL_LINES;
        case PrimitiveTopologyUVE::Points:
            return GL_POINTS;
        case PrimitiveTopologyUVE::LineStrip:
            return GL_LINE_STRIP;
        case PrimitiveTopologyUVE::TriangleStrip:
            return GL_TRIANGLE_STRIP;
    }
    return GL_TRIANGLES;
}

/// The glVertexAttrib(I)Pointer arguments for one RHI attribute format.
struct GlVertexAttributeFetchUVE final {
    GLint componentCount = 3;
    GLenum type = GL_FLOAT;
    GLboolean normalized = GL_FALSE;
    bool integer = false; ///< glVertexAttribIPointer instead of glVertexAttribPointer
};

[[nodiscard]] GlVertexAttributeFetchUVE VertexAttributeFetchUVE(VertexAttributeFormatUVE format) noexcept {
    switch (format) {
        case VertexAttributeFormatUVE::Float2:
            return {2, GL_FLOAT, GL_FALSE, false};
        case VertexAttributeFormatUVE::Float3:
            return {3, GL_FLOAT, GL_FALSE, false};
        case VertexAttributeFormatUVE::Float4:
            return {4, GL_FLOAT, GL_FALSE, false};
        case VertexAttributeFormatUVE::UByte4Norm:
            return {4, GL_UNSIGNED_BYTE, GL_TRUE, false};
        case VertexAttributeFormatUVE::UShort4:
            return {4, GL_UNSIGNED_SHORT, GL_FALSE, true};
        case VertexAttributeFormatUVE::UInt4:
            return {4, GL_UNSIGNED_INT, GL_FALSE, true};
        case VertexAttributeFormatUVE::Short2Norm:
            return {2, GL_SHORT, GL_TRUE, false};
        case VertexAttributeFormatUVE::Half2:
            return {2, GL_HALF_FLOAT, GL_FALSE, false};
        case VertexAttributeFormatUVE::Half4:
            return {4, GL_HALF_FLOAT, GL_FALSE, false};
    }
    return {};
}

[[nodiscard]] bool IsFiniteVector2UVE(const Math::Vector2UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool IsFiniteVector3UVE(const Math::Vector3UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool IsFiniteVector4UVE(const Math::Vector4UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] bool IsFiniteMatrix4x4UVE(const Math::Matrix4x4UVE& value) noexcept {
    for (const auto& row : value.m) {
        for (const float component : row) {
            if (!std::isfinite(component)) {
                return false;
            }
        }
    }
    return true;
}

/// Resolves and applies the pass's GL viewport: the full `targetWidth`x`targetHeight` by default,
/// or `renderPassDesc.viewportOverride` if set (Phase 3's ViewportManagerUVE split-view support -
/// see RenderPassDescUVE's doc comment). Returns false without calling glViewport() if an override
/// is present but does not fit within the target, matching this function's existing "reject a
/// malformed pass descriptor" convention rather than silently clamping it.
///
/// Also arms (or disarms) GL_SCISSOR_TEST to match: glViewport alone does not confine a clear -
/// glClear ignores the viewport and always affects the whole framebuffer unless the scissor test
/// is also enabled with a matching rect. Without this, a second pane's Clear-load-op pass would
/// wipe out a first pane already drawn into the same default framebuffer. Explicitly setting this
/// on every call (both branches) rather than only when an override is present means no scissor
/// state can leak from a previous pass into one that doesn't ask for it.
[[nodiscard]] bool ApplyViewportUVE(const RenderPassDescUVE& renderPassDesc, const std::uint32_t targetWidth,
                                     const std::uint32_t targetHeight) noexcept {
    if (!renderPassDesc.viewportOverride.has_value()) {
        glViewport(0, 0, static_cast<GLsizei>(targetWidth), static_cast<GLsizei>(targetHeight));
        glDisable(GL_SCISSOR_TEST);
        return true;
    }
    const ViewportRectUVE& rect = *renderPassDesc.viewportOverride;
    const ViewportRectUVE targetBounds{Math::Vector2iUVE{0, 0},
                                        Math::Vector2iUVE{static_cast<std::int32_t>(targetWidth),
                                                          static_cast<std::int32_t>(targetHeight)}};
    if (rect.size.x <= 0 || rect.size.y <= 0 || !Math::ContainsUVE(targetBounds, rect)) {
        UVE_ERROR("GlCommandBufferUVE: BeginRenderPassUVE viewportOverride ({}, {}, {}x{}) does not fit within "
                  "the {}x{} target",
                  rect.position.x, rect.position.y, rect.size.x, rect.size.y, targetWidth, targetHeight);
        return false;
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(rect.position.x, rect.position.y, rect.size.x, rect.size.y);
    glViewport(rect.position.x, rect.position.y, rect.size.x, rect.size.y);
    return true;
}

} // namespace

GlCommandBufferUVE::GlCommandBufferUVE(Detail::GlDeviceStateUVE& state) : m_state(&state) {}

void GlCommandBufferUVE::BeginRenderPassUVE(const RenderPassDescUVE& renderPassDesc) {
    ++m_passEpoch;
    if (!RequireOutsideRenderPassUVE(m_insideRenderPass)) {
        return;
    }
    if (!IsLoadOpValidUVE(renderPassDesc.colorLoadOp) || !IsLoadOpValidUVE(renderPassDesc.depthLoadOp)) {
        UVE_ERROR("GlCommandBufferUVE: BeginRenderPassUVE received an unknown load operation");
        return;
    }
    // Tier 2.5: store ops validate like load ops (garbage in = error, even on passes whose
    // store is later ignored — the default framebuffer never invalidates).
    m_passDiscardCount = 0U;
    if (!IsStoreOpValidUVE(renderPassDesc.colorStoreOp) || !IsStoreOpValidUVE(renderPassDesc.depthStoreOp)) {
        UVE_ERROR("GlCommandBufferUVE: BeginRenderPassUVE received an unknown store operation");
        return;
    }
    if (renderPassDesc.colorLoadOp == LoadOpUVE::Clear) {
        for (const float clearChannel : renderPassDesc.clearColor) {
            if (!std::isfinite(clearChannel)) {
                UVE_ERROR("GlCommandBufferUVE: color clear value must be finite");
                return;
            }
        }
    }
    if (renderPassDesc.depthLoadOp == LoadOpUVE::Clear && !std::isfinite(renderPassDesc.clearDepth)) {
        UVE_ERROR("GlCommandBufferUVE: depth clear value must be finite");
        return;
    }

    // Tier 2.4: extras alone make a pass offscreen (they ride the offscreen branch's
    // validation — a default-framebuffer pass must not silently drop them).
    bool hasExtraAttachments = false;
    for (const ColorAttachmentUVE& extra : renderPassDesc.extraColorAttachments) {
        if (extra.target != kInvalidTextureHandleUVE) {
            hasExtraAttachments = true;
            break;
        }
    }
    std::uint32_t extraCount = 0U; // resolved by the offscreen branch below
    if (renderPassDesc.colorAttachment == kInvalidTextureHandleUVE &&
        renderPassDesc.depthAttachment == kInvalidTextureHandleUVE && !hasExtraAttachments) {
        if (m_state->windowManager == nullptr || !m_state->windowManager->IsValidUVE()) {
            UVE_ERROR("GlCommandBufferUVE: default framebuffer render pass requires a valid window surface");
            return;
        }
        const std::uint32_t width = m_state->windowManager->GetWidthUVE();
        const std::uint32_t height = m_state->windowManager->GetHeightUVE();
        if (width == 0U || height == 0U ||
            width > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()) ||
            height > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())) {
            UVE_ERROR("GlCommandBufferUVE: default framebuffer dimensions are invalid ({}x{})", width, height);
            return;
        }
        m_state->gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        m_tempFramebuffer = 0;
#if !defined(__ANDROID__)
        // A preceding depth-only FBO explicitly uses GL_NONE for both buffers. Explicitly route
        // every default-framebuffer pass to the window back buffer; using the loaded entry also
        // avoids relying on the driver's legacy glDrawBuffer dispatch after an FBO transition.
        const GLenum defaultDrawBuffer = GL_BACK_LEFT;
        if (m_state->gl.glDrawBuffers != nullptr) {
            m_state->gl.glDrawBuffers(1, &defaultDrawBuffer);
        } else {
            glDrawBuffer(GL_BACK);
        }
        glReadBuffer(GL_BACK);
#endif
        if (!ApplyViewportUVE(renderPassDesc, width, height)) {
            return;
        }
    } else {
        const auto colorIt = renderPassDesc.colorAttachment == kInvalidTextureHandleUVE
                                 ? m_state->textures.end()
                                 : m_state->textures.find(renderPassDesc.colorAttachment.value);
        if (renderPassDesc.colorAttachment != kInvalidTextureHandleUVE && colorIt == m_state->textures.end()) {
            UVE_ERROR("GlCommandBufferUVE: BeginRenderPassUVE referenced an unknown colorAttachment handle");
            return;
        }
        const auto depthIt = renderPassDesc.depthAttachment == kInvalidTextureHandleUVE
                                 ? m_state->textures.end()
                                 : m_state->textures.find(renderPassDesc.depthAttachment.value);
        if (renderPassDesc.depthAttachment != kInvalidTextureHandleUVE && depthIt == m_state->textures.end()) {
            UVE_ERROR("GlCommandBufferUVE: BeginRenderPassUVE referenced an unknown depthAttachment handle");
            return;
        }
        if (colorIt != m_state->textures.end() && IsDepthFormatUVE(colorIt->second.desc.format)) {
            UVE_ERROR("GlCommandBufferUVE: colorAttachment must use a color texture format");
            return;
        }
        if (colorIt != m_state->textures.end() &&
            IsTextureFormatCompressedUVE(colorIt->second.desc.format)) {
            UVE_ERROR("GlCommandBufferUVE: block-compressed textures are sampled-only and cannot be color attachments");
            return;
        }
        if (depthIt != m_state->textures.end() && !IsDepthFormatUVE(depthIt->second.desc.format)) {
            UVE_ERROR("GlCommandBufferUVE: depthAttachment must use a depth texture format");
            return;
        }
        // Usage set: a texture created without RenderTarget (a sampled-only asset, a volume)
        // is refused here, before any FBO state changes, instead of failing completeness later.
        if ((colorIt != m_state->textures.end() &&
             !HasTextureUsageUVE(GetEffectiveTextureUsageUVE(colorIt->second.desc), TextureUsageUVE::RenderTarget)) ||
            (depthIt != m_state->textures.end() &&
             !HasTextureUsageUVE(GetEffectiveTextureUsageUVE(depthIt->second.desc), TextureUsageUVE::RenderTarget))) {
            UVE_ERROR("GlCommandBufferUVE: BeginRenderPassUVE attachment was created without "
                      "TextureUsageUVE::RenderTarget");
            return;
        }
        // Tier 2.4: extras need location 0 (a pass with extras but no colorAttachment is
        // malformed — the swapchain/default framebuffer never takes extras).
        if (hasExtraAttachments && colorIt == m_state->textures.end()) {
            UVE_ERROR("GlCommandBufferUVE: extra color attachments need location 0 "
                      "(a pass with extras but no colorAttachment is malformed)");
            return;
        }
        if (colorIt != m_state->textures.end() && depthIt != m_state->textures.end() &&
            (colorIt->second.desc.width != depthIt->second.desc.width ||
             colorIt->second.desc.height != depthIt->second.desc.height)) {
            UVE_ERROR("GlCommandBufferUVE: colorAttachment and depthAttachment dimensions must match");
            return;
        }
        // Tier 2.3: layer selection must sit inside the attachment's layer count (2D textures
        // carry exactly one layer, so any nonzero layer on them fails here too).
        if (colorIt != m_state->textures.end() &&
            renderPassDesc.colorLayer >= colorIt->second.desc.arrayLayers) {
            UVE_ERROR("GlCommandBufferUVE: colorLayer exceeds the color attachment's layer count");
            return;
        }
        if (depthIt != m_state->textures.end() &&
            renderPassDesc.depthLayer >= depthIt->second.desc.arrayLayers) {
            UVE_ERROR("GlCommandBufferUVE: depthLayer exceeds the depth attachment's layer count");
            return;
        }

        // Tier 2.4: locations 1..3 — a contiguous prefix under the same contract as location 0
        // (known color-format handle, matching dims, layer in range), pairwise distinct from
        // location 0, the depth, and each other (one texture may not feed two attachments).
        // Loader entries are checked here too so an MRT pass fails BEFORE allocating its FBO.
        std::array<std::uint32_t, kExtraColorAttachmentCountUVE> extraHandles{};
        std::array<std::uint32_t, kExtraColorAttachmentCountUVE> extraLayers{};
        {
            bool gapSeen = false;
            bool anyColorClear = renderPassDesc.colorLoadOp == LoadOpUVE::Clear;
            for (const ColorAttachmentUVE& slot : renderPassDesc.extraColorAttachments) {
                if (slot.target == kInvalidTextureHandleUVE) {
                    gapSeen = true;
                    continue;
                }
                if (gapSeen) {
                    UVE_ERROR("GlCommandBufferUVE: extra color attachments must be a contiguous "
                              "prefix (a gap is malformed)");
                    return;
                }
                if (!IsLoadOpValidUVE(slot.loadOp) || !IsStoreOpValidUVE(slot.storeOp)) {
                    UVE_ERROR("GlCommandBufferUVE: BeginRenderPassUVE received an unknown extra "
                              "attachment load or store operation");
                    return;
                }
                if (slot.loadOp == LoadOpUVE::Clear) {
                    anyColorClear = true;
                    for (const float clearChannel : slot.clearColor) {
                        if (!std::isfinite(clearChannel)) {
                            UVE_ERROR("GlCommandBufferUVE: extra color clear value must be finite");
                            return;
                        }
                    }
                }
                const auto extraIt = m_state->textures.find(slot.target.value);
                if (extraIt == m_state->textures.end()) {
                    UVE_ERROR("GlCommandBufferUVE: BeginRenderPassUVE referenced an unknown extra "
                              "color attachment handle");
                    return;
                }
                if (IsDepthFormatUVE(extraIt->second.desc.format) ||
                    IsTextureFormatCompressedUVE(extraIt->second.desc.format)) {
                    UVE_ERROR("GlCommandBufferUVE: extra color attachments must use a color texture "
                              "format (block-compressed textures are sampled-only)");
                    return;
                }
                if (!HasTextureUsageUVE(GetEffectiveTextureUsageUVE(extraIt->second.desc),
                                        TextureUsageUVE::RenderTarget)) {
                    UVE_ERROR("GlCommandBufferUVE: extra color attachment was created without "
                              "TextureUsageUVE::RenderTarget");
                    return;
                }
                if (extraIt->second.desc.width != colorIt->second.desc.width ||
                    extraIt->second.desc.height != colorIt->second.desc.height) {
                    UVE_ERROR("GlCommandBufferUVE: every color attachment must match location 0's "
                              "dimensions (FBO completeness)");
                    return;
                }
                if (slot.layer >= extraIt->second.desc.arrayLayers) {
                    UVE_ERROR("GlCommandBufferUVE: an extra color layer exceeds its attachment's "
                              "layer count");
                    return;
                }
                if (extraIt->second.desc.type != TextureTypeUVE::Texture2D &&
                    m_state->gl.glFramebufferTextureLayer == nullptr) {
                    UVE_ERROR("GlCommandBufferUVE: layered render targets need "
                              "glFramebufferTextureLayer (GL 3.2+)");
                    return;
                }
                if (slot.target.value == renderPassDesc.colorAttachment.value ||
                    slot.target.value == renderPassDesc.depthAttachment.value) {
                    UVE_ERROR("GlCommandBufferUVE: one texture may not feed two attachments of the "
                              "same pass");
                    return;
                }
                for (std::uint32_t j = 0U; j < extraCount; ++j) {
                    if (extraHandles[j] == slot.target.value) {
                        UVE_ERROR("GlCommandBufferUVE: one texture may not feed two attachments of "
                                  "the same pass");
                        return;
                    }
                }
                extraHandles[extraCount] = slot.target.value;
                extraLayers[extraCount] = slot.layer;
                ++extraCount;
            }
            if (extraCount > 0U && m_state->gl.glDrawBuffers == nullptr) {
                UVE_ERROR("GlCommandBufferUVE: multi-target passes need glDrawBuffers");
                return;
            }
            if (extraCount > 0U && anyColorClear && m_state->gl.glClearBufferfv == nullptr) {
                UVE_ERROR("GlCommandBufferUVE: multi-target clears need glClearBufferfv");
                return;
            }
        }

        const Detail::GlDeviceStateUVE::FramebufferKeyUVE framebufferKey{
            renderPassDesc.colorAttachment.value, renderPassDesc.depthAttachment.value,
            renderPassDesc.colorLayer, renderPassDesc.depthLayer, extraHandles, extraLayers};
        GLuint framebuffer = 0;
        const auto cachedFramebufferIt = m_state->framebufferCache.find(framebufferKey);
        const bool framebufferCreated = cachedFramebufferIt == m_state->framebufferCache.end();
        if (framebufferCreated) {
            m_state->gl.glGenFramebuffers(1, &framebuffer);
            m_state->framebufferCache.emplace(framebufferKey, framebuffer);
        } else {
            framebuffer = cachedFramebufferIt->second;
        }
        GLint previousFramebuffer = 0;
        if (framebufferCreated) {
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
        }
        m_state->gl.glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

        std::uint32_t attachmentWidth = 0;
        std::uint32_t attachmentHeight = 0;
        if (colorIt != m_state->textures.end()) {
            attachmentWidth = colorIt->second.desc.width;
            attachmentHeight = colorIt->second.desc.height;
        }
        if (depthIt != m_state->textures.end() && attachmentWidth == 0) {
            attachmentWidth = depthIt->second.desc.width;
            attachmentHeight = depthIt->second.desc.height;
        }

        if (framebufferCreated) {
            // Tier 2.4: fail closed past the context's draw-buffer limit (queried once per FBO
            // creation, not per pass — the cached FBO bakes the routing).
            if (extraCount > 0U) {
                GLint maxDrawBuffers = 0;
                glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
                if (maxDrawBuffers < 1 ||
                    1U + extraCount > static_cast<std::uint32_t>(maxDrawBuffers)) {
                    UVE_ERROR("GlCommandBufferUVE: the pass names more color attachments than the "
                              "context offers ({} > GL_MAX_DRAW_BUFFERS {})",
                              1U + extraCount, maxDrawBuffers);
                    m_state->gl.glDeleteFramebuffers(1, &framebuffer);
                    m_state->framebufferCache.erase(framebufferKey);
                    m_state->gl.glBindFramebuffer(GL_FRAMEBUFFER,
                                                  static_cast<GLuint>(previousFramebuffer));
                    return;
                }
            }
            if (colorIt != m_state->textures.end()) {
                // Tier 2.3: arrays/cubes attach one layer (cube faces ARE layers 0..5).
                const bool colorIsLayered =
                    colorIt->second.desc.type != TextureTypeUVE::Texture2D;
                if (colorIsLayered && m_state->gl.glFramebufferTextureLayer == nullptr) {
                    // No layered attach on this context (pre-3.2): skip the attachment so the
                    // status check below tears the FBO down through the standard path.
                    UVE_ERROR("GlCommandBufferUVE: layered render targets need "
                              "glFramebufferTextureLayer (GL 3.2+)");
                } else if (colorIsLayered) {
                    m_state->gl.glFramebufferTextureLayer(
                        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, colorIt->second.glTexture, 0,
                        static_cast<GLint>(renderPassDesc.colorLayer));
                } else {
                    m_state->gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                       GL_TEXTURE_2D, colorIt->second.glTexture, 0);
                }
                // Tier 2.4: locations 1..3 attach exactly like location 0 (layered extras ride
                // glFramebufferTextureLayer — validated available above when layered).
                for (std::uint32_t i = 0U; i < extraCount; ++i) {
                    const auto extraIt =
                        m_state->textures.find(extraHandles[i]); // validated known above
                    const GLenum extraPoint =
                        static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + 1U + i);
                    if (extraIt->second.desc.type != TextureTypeUVE::Texture2D) {
                        m_state->gl.glFramebufferTextureLayer(
                            GL_FRAMEBUFFER, extraPoint, extraIt->second.glTexture, 0,
                            static_cast<GLint>(extraLayers[i]));
                    } else {
                        m_state->gl.glFramebufferTexture2D(GL_FRAMEBUFFER, extraPoint,
                                                           GL_TEXTURE_2D, extraIt->second.glTexture,
                                                           0);
                    }
                }
                if (extraCount > 0U) {
                    // Draw-buffer routing is per-FBO state: set once at creation, baked into the
                    // cached FBO. (glDrawBuffers was validated non-null above.)
                    std::array<GLenum, 1U + kExtraColorAttachmentCountUVE> drawBuffers{};
                    for (std::uint32_t i = 0U; i < 1U + extraCount; ++i) {
                        drawBuffers[i] = static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i);
                    }
                    m_state->gl.glDrawBuffers(static_cast<GLsizei>(1U + extraCount),
                                                  drawBuffers.data());
                }
            } else {
                // Depth-only pass (e.g. a shadow map's depth pre-pass, Increment 26): a core-profile
                // FBO with no color attachment must explicitly declare it has none, or
                // glCheckFramebufferStatus reports GL_FRAMEBUFFER_INCOMPLETE_DRAW/READ_BUFFER.
                // Desktop core OpenGL requires an explicit no-color draw/read buffer for a depth-only
                // FBO. GLES3 has no glDrawBuffer/glReadBuffer entry points; its framebuffer contract
                // already treats a depth-only FBO as having no color target.
#if !defined(__ANDROID__)
                glDrawBuffer(GL_NONE);
                glReadBuffer(GL_NONE);
#endif
            }
            if (depthIt != m_state->textures.end()) {
                const GLenum depthAttachmentPoint =
                    IsDepthStencilFormatUVE(depthIt->second.desc.format) ? GL_DEPTH_STENCIL_ATTACHMENT
                                                                          : GL_DEPTH_ATTACHMENT;
                const bool depthIsLayered =
                    depthIt->second.desc.type != TextureTypeUVE::Texture2D;
                if (depthIsLayered && m_state->gl.glFramebufferTextureLayer == nullptr) {
                    UVE_ERROR("GlCommandBufferUVE: layered render targets need "
                              "glFramebufferTextureLayer (GL 3.2+)");
                } else if (depthIsLayered) {
                    m_state->gl.glFramebufferTextureLayer(
                        GL_FRAMEBUFFER, depthAttachmentPoint, depthIt->second.glTexture, 0,
                        static_cast<GLint>(renderPassDesc.depthLayer));
                } else {
                    m_state->gl.glFramebufferTexture2D(GL_FRAMEBUFFER, depthAttachmentPoint,
                                                       GL_TEXTURE_2D, depthIt->second.glTexture, 0);
                }
            }
            const GLenum framebufferStatus = m_state->gl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
                UVE_ERROR("GlCommandBufferUVE: BeginRenderPassUVE built an incomplete framebuffer");
#if defined(__ANDROID__)
                __android_log_print(ANDROID_LOG_ERROR, "UVEAndroidGLES",
                                    "FBO incomplete: status=0x%04x color=%u depth=%u",
                                    static_cast<unsigned int>(framebufferStatus),
                                    colorIt != m_state->textures.end() ? colorIt->second.glTexture : 0U,
                                    depthIt != m_state->textures.end() ? depthIt->second.glTexture : 0U);
#endif
                m_state->gl.glDeleteFramebuffers(1, &framebuffer);
                m_state->framebufferCache.erase(framebufferKey);
                m_state->gl.glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
                return;
            }
        }

        if (!ApplyViewportUVE(renderPassDesc, attachmentWidth, attachmentHeight)) {
            if (framebufferCreated) {
                m_state->gl.glDeleteFramebuffers(1, &framebuffer);
                m_state->framebufferCache.erase(framebufferKey);
                m_state->gl.glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
            }
            return;
        }
        // Tier 2.5: remember the DontCare attachments — EndRenderPassUVE invalidates them while
        // the FBO is still bound (a discard hint, never a correctness requirement).
        m_passDiscardCount = 0U;
        if (renderPassDesc.colorStoreOp == StoreOpUVE::DontCare &&
            colorIt != m_state->textures.end()) {
            m_passDiscardAttachments[m_passDiscardCount++] = GL_COLOR_ATTACHMENT0;
        }
        for (std::uint32_t i = 0U; i < extraCount; ++i) {
            if (renderPassDesc.extraColorAttachments[i].storeOp == StoreOpUVE::DontCare) {
                m_passDiscardAttachments[m_passDiscardCount++] =
                    static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + 1U + i);
            }
        }
        if (renderPassDesc.depthStoreOp == StoreOpUVE::DontCare &&
            depthIt != m_state->textures.end()) {
            // Invalidation takes individual aspects, not the combined FBO attachment token.
            m_passDiscardAttachments[m_passDiscardCount++] = GL_DEPTH_ATTACHMENT;
            if (IsDepthStencilFormatUVE(depthIt->second.desc.format)) {
                m_passDiscardAttachments[m_passDiscardCount++] = GL_STENCIL_ATTACHMENT;
            }
        }
        m_tempFramebuffer = framebuffer;
    }

    GLbitfield clearMask = 0;
    if (extraCount == 0U && renderPassDesc.colorLoadOp == LoadOpUVE::Clear) {
        glClearColor(renderPassDesc.clearColor[0], renderPassDesc.clearColor[1], renderPassDesc.clearColor[2],
                     renderPassDesc.clearColor[3]);
        clearMask |= GL_COLOR_BUFFER_BIT;
    }
    // Tier 2.4: glClear would paint every draw buffer one color — multi-target passes clear
    // per slot instead (glClearBufferfv was validated non-null above when any color clears).
    if (extraCount > 0U && renderPassDesc.colorLoadOp == LoadOpUVE::Clear) {
        m_state->gl.glClearBufferfv(GL_COLOR, 0, renderPassDesc.clearColor.data());
    }
    if (extraCount > 0U) {
        for (std::uint32_t i = 0U; i < extraCount; ++i) {
            const ColorAttachmentUVE& slot = renderPassDesc.extraColorAttachments[i];
            if (slot.loadOp == LoadOpUVE::Clear) {
                m_state->gl.glClearBufferfv(GL_COLOR, static_cast<GLint>(1U + i),
                                             slot.clearColor.data());
            }
        }
    }
    if (renderPassDesc.depthLoadOp == LoadOpUVE::Clear) {
        // glClear(GL_DEPTH_BUFFER_BIT) respects GL_DEPTH_WRITEMASK. A prior fullscreen pass
        // deliberately disables depth writes, so a render pass that explicitly requests a depth
        // clear must restore the write mask first or its clear becomes a silent no-op and all
        // same-depth geometry in subsequent frames can fail GL_LESS.
        glDepthMask(GL_TRUE);
#if defined(__ANDROID__)
        glClearDepthf(renderPassDesc.clearDepth);
#else
        glClearDepth(static_cast<GLdouble>(renderPassDesc.clearDepth));
#endif
        clearMask |= GL_DEPTH_BUFFER_BIT;
        const auto clearDepthIt = m_state->textures.find(renderPassDesc.depthAttachment.value);
        if (clearDepthIt != m_state->textures.end() &&
            IsDepthStencilFormatUVE(clearDepthIt->second.desc.format)) {
            glStencilMask(0xFFU);
            glClearStencil(static_cast<GLint>(renderPassDesc.clearStencil & 0xFFU));
            clearMask |= GL_STENCIL_BUFFER_BIT;
        }
    }
    if (clearMask != 0) {
        glClear(clearMask);
    }

    m_insideRenderPass = true;
}

void GlCommandBufferUVE::EndRenderPassUVE() {
    ++m_passEpoch;
    if (m_activeQuery != 0U && m_state != nullptr && m_state->activeOcclusion == m_activeQuery) {
        // A bracket may not span the pass boundary: close it loudly so the GL query object is
        // left in a valid state; the caller's late EndQueryUVE reports InvalidState.
        UVE_ERROR("GlCommandBufferUVE: EndRenderPassUVE closed an occlusion query that was still open");
        m_state->gl.glEndQuery(GL_ANY_SAMPLES_PASSED);
        const auto found = m_state->queries.find(m_activeQuery);
        if (found != m_state->queries.end())
            found->second.issued = true;
        m_state->activeOcclusion = 0U;
        m_activeQuery = 0U;
    }
    if (!RequireInsideRenderPassUVE(m_insideRenderPass, "EndRenderPassUVE")) {
        return;
    }
    if (m_tempFramebuffer != 0) {
        // Tier 2.5: discard DontCare attachments while the FBO is still bound (a missing loader
        // entry just skips the hint — contents survive, which is always safe).
        if (m_passDiscardCount > 0U && m_state->gl.glInvalidateFramebuffer != nullptr) {
            m_state->gl.glInvalidateFramebuffer(GL_FRAMEBUFFER,
                                                 static_cast<GLsizei>(m_passDiscardCount),
                                                 m_passDiscardAttachments.data());
        }
        m_passDiscardCount = 0U;
        m_state->gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        m_tempFramebuffer = 0;
    }
    m_insideRenderPass = false;
}

void GlCommandBufferUVE::BindPipelineUVE(PipelineHandleUVE pipeline) {
    // M5a: no inside-pass gate here anymore. Compute pipelines MUST bind outside render-pass
    // markers (mirroring Vulkan, where binding COMPUTE inside a pass instance is illegal), and
    // glUseProgram/glBindVertexArray are equally legal outside a pass. The real structural gates
    // live on the consuming commands: DrawUVE (inside) and DispatchUVE (outside).
    if (pipeline == kInvalidPipelineHandleUVE) {
        UVE_ERROR("GlCommandBufferUVE: BindPipelineUVE referenced an invalid pipeline handle");
        return;
    }
    if (m_currentPipeline == pipeline) {
        return;
    }
    const auto pipelineIt = m_state->pipelines.find(pipeline.value);
    if (pipelineIt == m_state->pipelines.end()) {
        UVE_ERROR("GlCommandBufferUVE: BindPipelineUVE referenced an unknown pipeline handle");
        return;
    }
    m_currentProgram = pipelineIt->second.glProgram;
    m_currentVao = pipelineIt->second.glVao;
    m_currentPipeline = pipeline;
    m_boundVertexBuffer = kInvalidBufferHandleUVE;
    m_boundIndexBuffer = kInvalidBufferHandleUVE;
    m_currentVertexStride = pipelineIt->second.vertexStride;
    m_state->gl.glUseProgram(m_currentProgram);
    if (pipelineIt->second.isCompute) {
        return; // M5a: no VAO, no depth/blend state — a compute program carries none of it.
    }
    m_state->gl.glBindVertexArray(m_currentVao);

    if (pipelineIt->second.depthTestEnabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(pipelineIt->second.depthWriteEnabled ? GL_TRUE : GL_FALSE);
    switch (pipelineIt->second.blendMode) {
        case PipelineBlendModeUVE::SourceAlphaOver:
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case PipelineBlendModeUVE::Additive:
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
            break;
        case PipelineBlendModeUVE::Multiply:
            glEnable(GL_BLEND);
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
            break;
        case PipelineBlendModeUVE::Opaque:
            glDisable(GL_BLEND);
            break;
    }
    // Tier 2.1: rasterizer state from the pipeline record. Defaults reproduce pre-2.1 behavior
    // exactly (culling off, CCW, fill, no polygon offset, LESS compare — the compare call is
    // new but LESS is what the untouched context default always gave us).
    switch (pipelineIt->second.cullMode) {
        case CullModeUVE::None:
            glDisable(GL_CULL_FACE);
            break;
        case CullModeUVE::Front:
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);
            break;
        case CullModeUVE::Back:
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            break;
        case CullModeUVE::FrontAndBack:
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT_AND_BACK);
            break;
    }
    switch (pipelineIt->second.frontFace) {
        case FrontFaceUVE::CounterClockwise:
            glFrontFace(GL_CCW);
            break;
        case FrontFaceUVE::Clockwise:
            glFrontFace(GL_CW);
            break;
    }
    switch (pipelineIt->second.fillMode) {
        case FillModeUVE::Fill:
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            break;
        case FillModeUVE::Wireframe:
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            break;
    }
#if !defined(__ANDROID__)
    // Points take their size from the vertex shader's gl_PointSize, which desktop GL ignores
    // unless program point size is enabled (GLES always honours it). Toggled per pipeline so a
    // Points pipeline never leaks the mode into the next triangle pipeline's state.
    if (pipelineIt->second.topology == PrimitiveTopologyUVE::Points) {
        glEnable(GL_PROGRAM_POINT_SIZE);
    } else {
        glDisable(GL_PROGRAM_POINT_SIZE);
    }
#endif
    if (pipelineIt->second.depthBiasEnabled) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        // NOTE the argument order: glPolygonOffset takes (slopeFactor, constantUnits).
        glPolygonOffset(pipelineIt->second.depthBiasSlopeFactor, pipelineIt->second.depthBiasConstantFactor);
    } else {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    switch (pipelineIt->second.depthCompare) {
        case DepthCompareUVE::Never:          glDepthFunc(GL_NEVER);   break;
        case DepthCompareUVE::Less:           glDepthFunc(GL_LESS);    break;
        case DepthCompareUVE::Equal:          glDepthFunc(GL_EQUAL);   break;
        case DepthCompareUVE::LessOrEqual:    glDepthFunc(GL_LEQUAL);  break;
        case DepthCompareUVE::Greater:        glDepthFunc(GL_GREATER); break;
        case DepthCompareUVE::NotEqual:       glDepthFunc(GL_NOTEQUAL); break;
        case DepthCompareUVE::GreaterOrEqual: glDepthFunc(GL_GEQUAL);  break;
        case DepthCompareUVE::Always:         glDepthFunc(GL_ALWAYS);  break;
    }
    if (pipelineIt->second.stencil.enabled) {
        glEnable(GL_STENCIL_TEST);
        ApplyStencilFaceUVE(m_state->gl, GL_FRONT, pipelineIt->second.stencil.front);
        ApplyStencilFaceUVE(m_state->gl, GL_BACK, pipelineIt->second.stencil.back);
    } else {
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0U);
    }
}

void GlCommandBufferUVE::BindVertexBufferUVE(BufferHandleUVE buffer, std::uint32_t slot) {
    if (!RequireInsideRenderPassUVE(m_insideRenderPass, "BindVertexBufferUVE")) {
        return;
    }
    static_cast<void>(slot); // This minimal RHI describes one interleaved vertex layout per
                              // pipeline, not a per-slot binding table — every attribute in
                              // vertexLayout is configured against whichever buffer is bound here.
    if (buffer == kInvalidBufferHandleUVE) {
        UVE_ERROR("GlCommandBufferUVE: BindVertexBufferUVE referenced an invalid buffer handle");
        return;
    }
    if (m_boundVertexBuffer == buffer) {
        return;
    }
    const auto bufferIt = m_state->buffers.find(buffer.value);
    if (bufferIt == m_state->buffers.end()) {
        UVE_ERROR("GlCommandBufferUVE: BindVertexBufferUVE referenced an unknown buffer handle");
        return;
    }
    if (bufferIt->second.target != GL_ARRAY_BUFFER) {
        UVE_ERROR("GlCommandBufferUVE: BindVertexBufferUVE requires a vertex buffer");
        return;
    }
    const auto* const pipelineRecord = FindCurrentPipelineUVE();
    if (pipelineRecord == nullptr) {
        UVE_ERROR("GlCommandBufferUVE: BindVertexBufferUVE called without a live pipeline");
        return;
    }
    m_state->gl.glBindBuffer(GL_ARRAY_BUFFER, bufferIt->second.glBuffer);

    for (std::size_t index = 0; index < pipelineRecord->vertexLayout.size(); ++index) {
        const VertexAttributeUVE& attribute = pipelineRecord->vertexLayout[index];
        const auto attributeIndex = static_cast<GLuint>(index);
        const GlVertexAttributeFetchUVE fetch = VertexAttributeFetchUVE(attribute.format);
        const auto* const pointer = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(attribute.offset));
        if (fetch.integer) {
            // Pipeline creation refused integer layouts when this entry point is absent.
            UVE_ASSERT(m_state->gl.glVertexAttribIPointer != nullptr);
            m_state->gl.glVertexAttribIPointer(attributeIndex, fetch.componentCount, fetch.type,
                                               static_cast<GLsizei>(m_currentVertexStride), pointer);
        } else {
            m_state->gl.glVertexAttribPointer(attributeIndex, fetch.componentCount, fetch.type, fetch.normalized,
                                              static_cast<GLsizei>(m_currentVertexStride), pointer);
        }
        m_state->gl.glEnableVertexAttribArray(attributeIndex);
    }
    m_boundVertexBuffer = buffer;
}

void GlCommandBufferUVE::BindIndexBufferUVE(BufferHandleUVE buffer) {
    if (!RequireInsideRenderPassUVE(m_insideRenderPass, "BindIndexBufferUVE")) {
        return;
    }
    if (buffer == kInvalidBufferHandleUVE) {
        UVE_ERROR("GlCommandBufferUVE: BindIndexBufferUVE referenced an invalid buffer handle");
        return;
    }
    if (m_boundIndexBuffer == buffer) {
        return;
    }
    const auto bufferIt = m_state->buffers.find(buffer.value);
    if (bufferIt == m_state->buffers.end()) {
        UVE_ERROR("GlCommandBufferUVE: BindIndexBufferUVE referenced an unknown buffer handle");
        return;
    }
    if (bufferIt->second.target != GL_ELEMENT_ARRAY_BUFFER) {
        UVE_ERROR("GlCommandBufferUVE: BindIndexBufferUVE requires an index buffer");
        return;
    }
    m_state->gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bufferIt->second.glBuffer);
    m_boundIndexBuffer = buffer;
}

void GlCommandBufferUVE::BindTextureUVE(TextureHandleUVE texture, std::uint32_t slot) {
    // M5b: BindTextureUVE now also feeds STORAGE-image slots (the unified texture-slot
    // space), and the compute flow (bind compute pipeline → bind its textures → dispatch)
    // lives entirely OUTSIDE pass markers — so while a compute pipeline is the bound one,
    // this call passes the gate; graphics-side misuse keeps the strict inside-pass rule
    // (the same M5a relaxation BindStorageBufferUVE and the SetUniform* calls got).
    if (!m_insideRenderPass && !ActivePipelineIsComputeUVE()) {
        (void)RequireInsideRenderPassUVE(m_insideRenderPass, "BindTextureUVE");
        return;
    }
    if (m_state->maxCombinedTextureImageUnits <= 0 ||
        slot >= static_cast<std::uint32_t>(m_state->maxCombinedTextureImageUnits)) {
        UVE_ERROR("GlCommandBufferUVE: BindTextureUVE texture slot exceeds GL texture-unit limits");
        return;
    }
    const auto textureIt = m_state->textures.find(texture.value);
    if (textureIt == m_state->textures.end()) {
        UVE_ERROR("GlCommandBufferUVE: BindTextureUVE referenced an unknown texture handle");
        return;
    }
    const TextureUsageUVE textureUsage = GetEffectiveTextureUsageUVE(textureIt->second.desc);
    if (!HasTextureUsageUVE(textureUsage, TextureUsageUVE::Sampled)) {
        UVE_ERROR("GlCommandBufferUVE: BindTextureUVE texture was created without TextureUsageUVE::Sampled; "
                  "the slot keeps its previous binding");
        return;
    }
    const auto boundTextureIt = m_boundTextures.find(slot);
    const bool alreadyBoundToSlot =
        boundTextureIt != m_boundTextures.end() && boundTextureIt->second == texture;
    if (!alreadyBoundToSlot) {
        m_state->gl.glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + slot));
        // Tier 2.3: bind under the texture's own target (targets coexist per unit in GL, so
        // a cube bound after a 2D on the same slot cannot disturb the earlier binding).
        const GLenum bindTarget = textureIt->second.desc.type == TextureTypeUVE::Cubemap
                                      ? GL_TEXTURE_CUBE_MAP
                                  : textureIt->second.desc.type == TextureTypeUVE::Texture2DArray
                                      ? GL_TEXTURE_2D_ARRAY
                                  : textureIt->second.desc.type == TextureTypeUVE::Texture3D
                                      ? GL_TEXTURE_3D
                                      : GL_TEXTURE_2D;
        glBindTexture(bindTarget, textureIt->second.glTexture);
        m_boundTextures[slot] = texture;
    }

    // M5b: when the CURRENT program declares image uniforms (GL_IMAGE_2D), also bind the
    // texture to the image unit of the same index so imageLoad/imageStore see it. Image units
    // follow the same slot==unit convention samplers do (callers point an image uniform at the
    // slot they bound, exactly like a sampler uniform; the reflected default is unit 0).
    // Only supported linear, uncompressed color formats are image-bound here; depth,
    // block-compressed and sRGB textures remain available to samplers only.
    if (m_state->gl.glBindImageTexture != nullptr) {
        const auto* const pipelineRecord = FindCurrentPipelineUVE();
        if (pipelineRecord != nullptr) {
            bool programHasImageUniform = false;
            for (const auto& uniformEntry : pipelineRecord->uniforms) {
                if (uniformEntry.second.isImageUniform) {
                    programHasImageUniform = true;
                    break;
                }
            }
            if (programHasImageUniform) {
                GLenum imageFormat = 0;
                switch (textureIt->second.desc.format) {
                    case TextureFormatUVE::RGBA8Unorm:
                        // OpenGL does not provide storage-image access to sRGB texture storage.
                        // Keep sRGB textures available to samplers, but refuse their image-unit
                        // binding instead of issuing writes that drivers may silently discard.
                        imageFormat = textureIt->second.desc.colorSpace == TextureColorSpaceUVE::Srgb
                                          ? 0U
                                          : GL_RGBA8;
                        break;
                    case TextureFormatUVE::RGBA16Float:
                        imageFormat = GL_RGBA16F;
                        break;
                    case TextureFormatUVE::R8Unorm:
                        imageFormat = GL_R8;
                        break;
                    case TextureFormatUVE::RG8Unorm:
                        imageFormat = GL_RG8;
                        break;
                    case TextureFormatUVE::R16Float:
                        imageFormat = GL_R16F;
                        break;
                    case TextureFormatUVE::RG16Float:
                        imageFormat = GL_RG16F;
                        break;
                    case TextureFormatUVE::R32Float:
                        imageFormat = GL_R32F;
                        break;
                    case TextureFormatUVE::RG32Float:
                        imageFormat = GL_RG32F;
                        break;
                    case TextureFormatUVE::RGBA32Float:
                        imageFormat = GL_RGBA32F;
                        break;
                    case TextureFormatUVE::Depth32Float:
                    case TextureFormatUVE::Depth24Stencil8:
                    case TextureFormatUVE::BC1RGB:
                    case TextureFormatUVE::BC3RGBA:
                    case TextureFormatUVE::BC7RGBA:
                    case TextureFormatUVE::ETC2RGB8:
                    case TextureFormatUVE::ETC2RGBA8:
                    case TextureFormatUVE::ASTC4x4RGBA:
                        imageFormat = 0;
                        break;
                }
                // Tier 2.3: layered storage images are out of scope — arrays/cubes stay
                // sampler-only, matching the Vulkan backend's black-sink fallback.
                const bool storageAllowed = HasTextureUsageUVE(textureUsage, TextureUsageUVE::Storage);
                if (imageFormat != 0 && textureIt->second.desc.arrayLayers <= 1U && storageAllowed) {
                    m_state->gl.glBindImageTexture(slot, textureIt->second.glTexture, 0, GL_FALSE,
                                                   0, GL_READ_WRITE, imageFormat);
                } else {
                    // Do not leave a prior command's image binding live when the new sampled
                    // texture cannot legally serve as a storage image.
                    m_state->gl.glBindImageTexture(slot, 0U, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
                    if (!storageAllowed) {
                        UVE_ERROR("GlCommandBufferUVE: texture was created without TextureUsageUVE::Storage; "
                                  "the image unit is left unbound");
                    } else if (textureIt->second.desc.arrayLayers > 1U) {
                        UVE_ERROR("GlCommandBufferUVE: array/cubemap textures cannot be bound as "
                                  "storage images in Tier 2.3; the image unit is left unbound");
                    } else if (IsTextureFormatCompressedUVE(textureIt->second.desc.format)) {
                        UVE_ERROR("GlCommandBufferUVE: block-compressed textures cannot be bound as storage "
                                  "images; the image unit is left unbound");
                    } else if (textureIt->second.desc.colorSpace == TextureColorSpaceUVE::Srgb) {
                        UVE_ERROR("GlCommandBufferUVE: sRGB textures cannot be bound as storage images; "
                                  "the image unit is left unbound");
                    } else {
                        UVE_ERROR("GlCommandBufferUVE: depth textures cannot be bound as storage "
                                  "images; the image unit is left unbound");
                    }
                }
            }
        }
    }
}

void GlCommandBufferUVE::BindSamplerUVE(SamplerHandleUVE sampler, std::uint32_t slot) {
    // Same compute-aware gate as BindTextureUVE: sampler binds pair with texture binds.
    if (!m_insideRenderPass && !ActivePipelineIsComputeUVE()) {
        (void)RequireInsideRenderPassUVE(m_insideRenderPass, "BindSamplerUVE");
        return;
    }
    if (m_state->maxCombinedTextureImageUnits <= 0 ||
        slot >= static_cast<std::uint32_t>(m_state->maxCombinedTextureImageUnits)) {
        UVE_ERROR("GlCommandBufferUVE: BindSamplerUVE sampler slot exceeds GL texture-unit limits");
        return;
    }
    const auto samplerIt = m_state->samplers.find(sampler.value);
    if (samplerIt == m_state->samplers.end()) {
        UVE_ERROR("GlCommandBufferUVE: BindSamplerUVE referenced an unknown sampler handle");
        return;
    }
    const auto boundSamplerIt = m_boundSamplers.find(slot);
    const bool alreadyBoundToSlot =
        boundSamplerIt != m_boundSamplers.end() && boundSamplerIt->second == sampler;
    if (!alreadyBoundToSlot) {
        // NOTE: glBindSampler takes the unit INDEX (unlike glActiveTexture's GL_TEXTURE0+n).
        m_state->gl.glBindSampler(slot, samplerIt->second.glSampler);
        m_boundSamplers[slot] = sampler;
    }
}

void GlCommandBufferUVE::BindUniformBufferUVE(BufferHandleUVE buffer, std::uint32_t slot) {
    if (!RequireInsideRenderPassUVE(m_insideRenderPass, "BindUniformBufferUVE")) {
        return;
    }
    if (m_state->maxUniformBufferBindings <= 0 ||
        slot >= static_cast<std::uint32_t>(m_state->maxUniformBufferBindings)) {
        UVE_ERROR("GlCommandBufferUVE: BindUniformBufferUVE slot exceeds GL uniform-buffer limits");
        return;
    }
    const auto bufferIt = m_state->buffers.find(buffer.value);
    if (bufferIt == m_state->buffers.end()) {
        UVE_ERROR("GlCommandBufferUVE: BindUniformBufferUVE referenced an unknown buffer handle");
        return;
    }
    if (bufferIt->second.target != GL_UNIFORM_BUFFER) {
        UVE_ERROR("GlCommandBufferUVE: BindUniformBufferUVE requires a uniform buffer");
        return;
    }
    m_state->gl.glBindBufferBase(GL_UNIFORM_BUFFER, slot, bufferIt->second.glBuffer);
}

void GlCommandBufferUVE::BindStorageBufferUVE(BufferHandleUVE buffer, std::uint32_t slot) {
    // M5a: the compute flow (bind compute pipeline, bind its SSBOs, set its uniforms,
    // dispatch) lives entirely OUTSIDE pass markers — so while a compute pipeline is the
    // bound one, these calls pass the gate; graphics-side misuse keeps the strict inside-pass
    // rule (GL knows the bound pipeline's kind, unlike NullCommandBufferUVE).
    if (!m_insideRenderPass && !ActivePipelineIsComputeUVE()) {
        (void)RequireInsideRenderPassUVE(m_insideRenderPass, "BindStorageBufferUVE");
        return;
    }
    // SSBOs are core in desktop GL 4.3 (the same floor as compute shaders); the cached gate
    // doubles as the "this context knows GL_SHADER_STORAGE_BUFFER at all" answer - on older
    // or GLES contexts the enum itself doesn't exist, so refuse before naming it.
    if (!m_state->supportsComputeShadersUVE || m_state->maxShaderStorageBindings <= 0) {
        UVE_ERROR("GlCommandBufferUVE: BindStorageBufferUVE needs a desktop GL 4.3+ context "
                  "(shader storage buffers); this context does not offer them");
        return;
    }
    if (slot >= static_cast<std::uint32_t>(m_state->maxShaderStorageBindings)) {
        UVE_ERROR("GlCommandBufferUVE: BindStorageBufferUVE slot exceeds GL shader-storage limits");
        return;
    }
    const auto bufferIt = m_state->buffers.find(buffer.value);
    if (bufferIt == m_state->buffers.end()) {
        UVE_ERROR("GlCommandBufferUVE: BindStorageBufferUVE referenced an unknown buffer handle");
        return;
    }
#if !defined(__ANDROID__)
    if (bufferIt->second.target != GL_SHADER_STORAGE_BUFFER) {
        UVE_ERROR("GlCommandBufferUVE: BindStorageBufferUVE requires a Storage-usage buffer");
        return;
    }
    // Whole-buffer base binding, matching the Vulkan backend's whole-buffer STORAGE_BUFFER
    // descriptor (offset 0, range = the buffer's full size) byte for byte in semantics.
    m_state->gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, bufferIt->second.glBuffer);
#endif
}

bool GlCommandBufferUVE::ActivePipelineIsComputeUVE() const noexcept {
    const auto* const record = FindCurrentPipelineUVE();
    return record != nullptr && record->isCompute;
}

const Detail::GlDeviceStateUVE::PipelineRecordUVE* GlCommandBufferUVE::FindCurrentPipelineUVE() const {
    if (m_currentPipeline == kInvalidPipelineHandleUVE) {
        return nullptr;
    }
    const auto pipelineIt = m_state->pipelines.find(m_currentPipeline.value);
    if (pipelineIt == m_state->pipelines.end()) {
        return nullptr;
    }
    return &pipelineIt->second;
}

const GlCommandBufferUVE::UniformRecordUVE* GlCommandBufferUVE::FindUniformUVE(std::string_view name) const {
    const auto* const pipelineRecord = FindCurrentPipelineUVE();
    if (pipelineRecord == nullptr) {
        UVE_WARNING("GlCommandBufferUVE: SetUniform*UVE called without a live pipeline (uniform \"{}\")", name);
        return nullptr;
    }
    const auto it = pipelineRecord->uniforms.find(name);
    if (it == pipelineRecord->uniforms.end()) {
        UVE_WARNING("GlCommandBufferUVE: SetUniform*UVE - \"{}\" is not an active uniform on the bound pipeline",
                     name);
        return nullptr;
    }
    return &it->second;
}

void GlCommandBufferUVE::SetUniformFloatUVE(std::string_view name, float value) {
    if (!m_insideRenderPass && !ActivePipelineIsComputeUVE()) { // M5a compute flow
        (void)RequireInsideRenderPassUVE(m_insideRenderPass, "SetUniformFloatUVE");
        return;
    }
    const UniformRecordUVE* const uniform = FindUniformUVE(name);
    if (uniform == nullptr || !ValidateUniformTypeUVE(*uniform, ShaderDataTypeUVE::Float, name)) {
        return;
    }
    if (!std::isfinite(value)) {
        UVE_ERROR("GlCommandBufferUVE: float uniform '{}' must be finite", name);
        return;
    }
    m_state->gl.glUniform1f(uniform->location, value);
}

void GlCommandBufferUVE::SetUniformIntUVE(std::string_view name, std::int32_t value) {
    if (!m_insideRenderPass && !ActivePipelineIsComputeUVE()) { // M5a compute flow
        (void)RequireInsideRenderPassUVE(m_insideRenderPass, "SetUniformIntUVE");
        return;
    }
    const UniformRecordUVE* const uniform = FindUniformUVE(name);
    if (uniform == nullptr || !ValidateUniformTypeUVE(*uniform, ShaderDataTypeUVE::Int, name)) {
        return;
    }
    m_state->gl.glUniform1i(uniform->location, static_cast<GLint>(value));
}

void GlCommandBufferUVE::SetUniformBoolUVE(std::string_view name, bool value) {
    if (!m_insideRenderPass && !ActivePipelineIsComputeUVE()) { // M5a compute flow
        (void)RequireInsideRenderPassUVE(m_insideRenderPass, "SetUniformBoolUVE");
        return;
    }
    const UniformRecordUVE* const uniform = FindUniformUVE(name);
    if (uniform == nullptr || !ValidateUniformTypeUVE(*uniform, ShaderDataTypeUVE::Bool, name)) {
        return;
    }
    m_state->gl.glUniform1i(uniform->location, value ? 1 : 0);
}

void GlCommandBufferUVE::SetUniformVector2UVE(std::string_view name, const Math::Vector2UVE& value) {
    if (!m_insideRenderPass && !ActivePipelineIsComputeUVE()) { // M5a compute flow
        (void)RequireInsideRenderPassUVE(m_insideRenderPass, "SetUniformVector2UVE");
        return;
    }
    const UniformRecordUVE* const uniform = FindUniformUVE(name);
    if (uniform == nullptr || !ValidateUniformTypeUVE(*uniform, ShaderDataTypeUVE::Vec2, name)) {
        return;
    }
    if (!IsFiniteVector2UVE(value)) {
        UVE_ERROR("GlCommandBufferUVE: vector uniform '{}' must be finite", name);
        return;
    }
    m_state->gl.glUniform2fv(uniform->location, 1, &value.x);
}

void GlCommandBufferUVE::SetUniformVector4UVE(std::string_view name, const Math::Vector4UVE& value) {
    if (!m_insideRenderPass && !ActivePipelineIsComputeUVE()) { // M5a compute flow
        (void)RequireInsideRenderPassUVE(m_insideRenderPass, "SetUniformVector4UVE");
        return;
    }
    const UniformRecordUVE* const uniform = FindUniformUVE(name);
    if (uniform == nullptr || !ValidateUniformTypeUVE(*uniform, ShaderDataTypeUVE::Vec4, name)) {
        return;
    }
    if (!IsFiniteVector4UVE(value)) {
        UVE_ERROR("GlCommandBufferUVE: vector uniform '{}' must be finite", name);
        return;
    }
    m_state->gl.glUniform4fv(uniform->location, 1, &value.x);
}

void GlCommandBufferUVE::SetUniformVector3UVE(std::string_view name, const Math::Vector3UVE& value) {
    if (!m_insideRenderPass && !ActivePipelineIsComputeUVE()) { // M5a compute flow
        (void)RequireInsideRenderPassUVE(m_insideRenderPass, "SetUniformVector3UVE");
        return;
    }
    const UniformRecordUVE* const uniform = FindUniformUVE(name);
    if (uniform == nullptr || !ValidateUniformTypeUVE(*uniform, ShaderDataTypeUVE::Vec3, name)) {
        return;
    }
    if (!IsFiniteVector3UVE(value)) {
        UVE_ERROR("GlCommandBufferUVE: vector uniform '{}' must be finite", name);
        return;
    }
    m_state->gl.glUniform3fv(uniform->location, 1, &value.x);
}

void GlCommandBufferUVE::SetUniformMatrix4x4UVE(std::string_view name, const Math::Matrix4x4UVE& value) {
    if (!m_insideRenderPass && !ActivePipelineIsComputeUVE()) { // M5a compute flow
        (void)RequireInsideRenderPassUVE(m_insideRenderPass, "SetUniformMatrix4x4UVE");
        return;
    }
    const UniformRecordUVE* const uniform = FindUniformUVE(name);
    if (uniform == nullptr || !ValidateUniformTypeUVE(*uniform, ShaderDataTypeUVE::Mat4, name)) {
        return;
    }
    if (!IsFiniteMatrix4x4UVE(value)) {
        UVE_ERROR("GlCommandBufferUVE: matrix uniform '{}' must be finite", name);
        return;
    }
    // Matrix4x4UVE is row-major storage (docs/CODING_STANDARDS.md, "Matrix convention"). Desktop
    // GL accepts GL_TRUE and performs the transpose, but GLES3 requires transpose == GL_FALSE, so
    // Android uploads an explicit column-major stack copy instead of issuing an invalid call.
#if defined(__ANDROID__)
    std::array<float, 16> columnMajor{};
    for (std::size_t row = 0; row < 4U; ++row) {
        for (std::size_t column = 0; column < 4U; ++column) {
            columnMajor[column * 4U + row] = value.m[row][column];
        }
    }
    m_state->gl.glUniformMatrix4fv(uniform->location, 1, GL_FALSE, columnMajor.data());
#else
    m_state->gl.glUniformMatrix4fv(uniform->location, 1, GL_TRUE, &value.m[0][0]);
#endif
}

void GlCommandBufferUVE::DrawIndexedUVE(std::uint32_t indexCount, std::uint32_t instanceCount,
                                        std::uint32_t firstIndex) {
    if (!RequireInsideRenderPassUVE(m_insideRenderPass, "DrawIndexedUVE")) {
        return;
    }
    const auto* const pipelineRecord = FindCurrentPipelineUVE();
    if (pipelineRecord == nullptr) {
        UVE_ERROR("GlCommandBufferUVE: DrawIndexedUVE called without a live pipeline");
        return;
    }
    if (m_boundIndexBuffer == kInvalidBufferHandleUVE) {
        UVE_ERROR("GlCommandBufferUVE: DrawIndexedUVE called without a bound index buffer");
        return;
    }
    const auto indexBufferIt = m_state->buffers.find(m_boundIndexBuffer.value);
    if (indexBufferIt == m_state->buffers.end() || indexBufferIt->second.target != GL_ELEMENT_ARRAY_BUFFER) {
        UVE_ERROR("GlCommandBufferUVE: DrawIndexedUVE has no valid bound index buffer");
        return;
    }
    const std::uint64_t indexCapacity = indexBufferIt->second.sizeBytes / sizeof(std::uint32_t);
    // The whole range, not just its length: a surface at the tail of a large index buffer passes
    // an indexCount check on its own and still reads past the binding.
    const std::uint64_t rangeEnd = static_cast<std::uint64_t>(firstIndex) + indexCount;
    if (rangeEnd > indexCapacity) {
        UVE_ERROR("GlCommandBufferUVE: DrawIndexedUVE range [{}, {}) exceeds the bound index buffer "
                  "of {} indices",
                  firstIndex, rangeEnd, indexCapacity);
        return;
    }
    if (indexCount > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())) {
        UVE_ERROR("GlCommandBufferUVE: DrawIndexedUVE indexCount exceeds the GLsizei range");
        return;
    }
    if (instanceCount > 1) {
        UVE_WARNING("GlCommandBufferUVE: DrawIndexedUVE instanceCount > 1 is not yet supported - drawing once");
    }
    // GL takes a byte offset here, unlike every other API in this engine which speaks indices.
    const auto indexOffsetBytes =
        static_cast<std::uintptr_t>(firstIndex) * static_cast<std::uintptr_t>(sizeof(std::uint32_t));
    glDrawElements(ToGlDrawModeUVE(pipelineRecord->topology), static_cast<GLsizei>(indexCount),
                     GL_UNSIGNED_INT, reinterpret_cast<const void*>(indexOffsetBytes));
    UVE_GL_CHECK_ERROR_UVE("DrawIndexedUVE");
}

void GlCommandBufferUVE::DrawIndexedIndirectUVE(const BufferHandleUVE buffer,
                                                const std::uint64_t offsetBytes) {
    if (!RequireInsideRenderPassUVE(m_insideRenderPass, "DrawIndexedIndirectUVE")) {
        return;
    }
    const auto* const pipelineRecord = FindCurrentPipelineUVE();
    if (pipelineRecord == nullptr) {
        UVE_ERROR("GlCommandBufferUVE: DrawIndexedIndirectUVE called without a live pipeline");
        return;
    }
    if (m_boundIndexBuffer == kInvalidBufferHandleUVE) {
        UVE_ERROR("GlCommandBufferUVE: DrawIndexedIndirectUVE called without a bound index buffer");
        return;
    }
    if (m_state->gl.glDrawElementsIndirect == nullptr) {
        // GL below 4.0, or a driver that did not export it. Warn and skip rather than call
        // through null - the same degradation the compute paths use.
        UVE_WARNING("GlCommandBufferUVE: DrawIndexedIndirectUVE needs GL 4.0+ indirect draw, "
                    "which this context does not provide; the draw is skipped");
        return;
    }
    const auto indirectIt = m_state->buffers.find(buffer.value);
    if (indirectIt == m_state->buffers.end()) {
        UVE_ERROR("GlCommandBufferUVE: DrawIndexedIndirectUVE was given an unknown buffer handle");
        return;
    }
    if (!IsStorageBindableUsageUVE(indirectIt->second.usage)) {
        UVE_ERROR("GlCommandBufferUVE: DrawIndexedIndirectUVE needs an IndirectStorage buffer");
        return;
    }
    // The whole command must lie inside the buffer. Reading parameters off the end would produce
    // a draw with garbage counts, which is precisely the failure indirect draw makes invisible -
    // nothing on the CPU would ever see the numbers.
    if (offsetBytes > indirectIt->second.sizeBytes ||
        indirectIt->second.sizeBytes - offsetBytes < sizeof(DrawIndexedIndirectCommandUVE)) {
        UVE_ERROR("GlCommandBufferUVE: DrawIndexedIndirectUVE offset leaves no whole command "
                  "inside the buffer");
        return;
    }

    // A compute dispatch may have just written these parameters; without this barrier the
    // indirect read is not guaranteed to see them (GL_COMMAND_BARRIER_BIT is the one that orders
    // shader writes against indirect-draw parameter fetches specifically).
    if (m_state->gl.glMemoryBarrier != nullptr) {
        m_state->gl.glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
    }
    m_state->gl.glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectIt->second.glBuffer);
    m_state->gl.glDrawElementsIndirect(
        ToGlDrawModeUVE(pipelineRecord->topology), GL_UNSIGNED_INT,
        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(offsetBytes)));
    UVE_GL_CHECK_ERROR_UVE("DrawIndexedIndirectUVE");
    // Leave no lingering indirect binding: the buffer's home target is the SSBO one, and a stale
    // GL_DRAW_INDIRECT_BUFFER binding would silently feed the next indirect draw.
    m_state->gl.glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0U);
}

void GlCommandBufferUVE::DrawUVE(std::uint32_t vertexCount, std::uint32_t instanceCount) {
    if (!RequireInsideRenderPassUVE(m_insideRenderPass, "DrawUVE")) {
        return;
    }
    if (m_currentPipeline != kInvalidPipelineHandleUVE && FindCurrentPipelineUVE() == nullptr) {
        UVE_ERROR("GlCommandBufferUVE: DrawUVE called with a destroyed pipeline");
        return;
    }
    if (m_boundVertexBuffer != kInvalidBufferHandleUVE) {
        const auto vertexBufferIt = m_state->buffers.find(m_boundVertexBuffer.value);
        if (vertexBufferIt == m_state->buffers.end() || vertexBufferIt->second.target != GL_ARRAY_BUFFER) {
            UVE_ERROR("GlCommandBufferUVE: DrawUVE has no valid bound vertex buffer");
            return;
        }
        if (m_currentVertexStride == 0U ||
            static_cast<std::uint64_t>(vertexCount) >
                vertexBufferIt->second.sizeBytes / static_cast<std::uint64_t>(m_currentVertexStride)) {
            UVE_ERROR("GlCommandBufferUVE: DrawUVE vertexCount exceeds the bound vertex buffer");
            return;
        }
    }
    if (vertexCount > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())) {
        UVE_ERROR("GlCommandBufferUVE: DrawUVE vertexCount exceeds the GLsizei range");
        return;
    }
    if (instanceCount > 1) {
        UVE_WARNING("GlCommandBufferUVE: DrawUVE instanceCount > 1 is not yet supported - drawing once");
    }
    const auto* const drawPipelineRecord = FindCurrentPipelineUVE();
    const GLenum drawMode = drawPipelineRecord != nullptr
                                ? ToGlDrawModeUVE(drawPipelineRecord->topology)
                                : GL_TRIANGLES;
    glDrawArrays(drawMode, 0, static_cast<GLsizei>(vertexCount));
    UVE_GL_CHECK_ERROR_UVE("DrawUVE");
}

void GlCommandBufferUVE::DispatchUVE(std::uint32_t groupCountX, std::uint32_t groupCountY,
                                     std::uint32_t groupCountZ) {
    // M5a: dispatch belongs OUTSIDE render-pass markers — the mirror image of DrawUVE's inside-pass
    // gate (Vulkan forbids compute inside a render-pass instance, so the portable flow binds the
    // compute pipeline and dispatches before/after pass markers).
    UVE_ASSERT(!m_insideRenderPass);
    if (m_insideRenderPass) {
        UVE_ERROR("GlCommandBufferUVE: DispatchUVE must be called outside a render pass");
        return;
    }
    const auto* const pipelineRecord = FindCurrentPipelineUVE();
    if (pipelineRecord == nullptr) {
        UVE_ERROR("GlCommandBufferUVE: DispatchUVE requires a live compute pipeline to be bound");
        return;
    }
    if (!pipelineRecord->isCompute) {
        UVE_ERROR("GlCommandBufferUVE: DispatchUVE requires a compute pipeline (a graphics pipeline is bound)");
        return;
    }
    if (m_state->gl.glDispatchCompute == nullptr || m_state->gl.glMemoryBarrier == nullptr) {
        UVE_ERROR("GlCommandBufferUVE: DispatchUVE requires an OpenGL 4.3+ context");
        return;
    }
    // GL executes at record time: BindPipelineUVE already ran glUseProgram for this program.
    m_state->gl.glDispatchCompute(groupCountX, groupCountY, groupCountZ);
    // Conservative global barrier — makes the compute writes visible to every later reader
    // (glGetBufferSubData readback and the fragment palette sampling in the M5a proof).
    m_state->gl.glMemoryBarrier(GL_ALL_BARRIER_BITS);
    UVE_GL_CHECK_ERROR_UVE("DispatchUVE");
}

} // namespace UVE::Render

namespace UVE::Render {
RenderFeatureSupportUVE GlCommandBufferUVE::GetMemoryBarrierSupportUVE() const noexcept {
#if defined(__ANDROID__)
    return RenderFeatureSupportUVE::Unsupported;
#else
    return m_state->supportsComputeShadersUVE && m_state->gl.glMemoryBarrier
        ? RenderFeatureSupportUVE::Native : RenderFeatureSupportUVE::Unsupported;
#endif
}
RenderOperationResultUVE GlCommandBufferUVE::MemoryBarrierUVE(MemoryBarrierMaskUVE mask) {
    if (!IsMemoryBarrierMaskValidUVE(mask))
        return RenderOperationResultUVE::InvalidArgument;
    if (m_insideRenderPass)
        return RenderOperationResultUVE::InvalidState;
#if defined(__ANDROID__)
    return RenderOperationResultUVE::Unsupported;
#else
    if (!m_state->supportsComputeShadersUVE || !m_state->gl.glMemoryBarrier)
        return RenderOperationResultUVE::Unsupported;
    const auto bits = static_cast<std::uint32_t>(mask);
    GLbitfield native = 0U;
    if ((bits & 1U) != 0U)
        native |= GL_SHADER_STORAGE_BARRIER_BIT;
    if ((bits & 2U) != 0U)
        native |= GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
    if ((bits & 4U) != 0U)
        native |= GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT | GL_ELEMENT_ARRAY_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT;
    if ((bits & 8U) != 0U)
        native |= GL_COMMAND_BARRIER_BIT;
    if ((bits & 16U) != 0U)
        native |= GL_BUFFER_UPDATE_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT;
    if ((bits & 32U) != 0U)
        native |= GL_FRAMEBUFFER_BARRIER_BIT;
    m_state->gl.glMemoryBarrier(native);
    return glGetError() == GL_NO_ERROR ? RenderOperationResultUVE::Success
                                       : RenderOperationResultUVE::SystemError;
#endif
}
} // namespace UVE::Render

namespace UVE::Render {
RenderOperationResultUVE GlCommandBufferUVE::BeginQueryUVE(const QueryHandleUVE query) {
    if (m_state == nullptr)
        return RenderOperationResultUVE::DeviceUnavailable;
    auto& state = *m_state;
    if (state.gl.glBeginQuery == nullptr || state.gl.glEndQuery == nullptr)
        return RenderOperationResultUVE::Unsupported;
    const auto found = state.queries.find(query.value);
    if (found == state.queries.end())
        return RenderOperationResultUVE::InvalidHandle;
    if (found->second.kind != RenderQueryKindUVE::AnySamplesPassed)
        return RenderOperationResultUVE::InvalidArgument;
    if (state.activeOcclusion != 0U || m_activeQuery != 0U)
        return RenderOperationResultUVE::InvalidState;
    state.gl.glBeginQuery(GL_ANY_SAMPLES_PASSED, found->second.native);
    state.activeOcclusion = query.value;
    found->second.issued = false;
    m_activeQuery = query.value;
    m_activeQueryPassEpoch = m_passEpoch;
    return RenderOperationResultUVE::Recorded;
}

RenderOperationResultUVE GlCommandBufferUVE::EndQueryUVE(const QueryHandleUVE query) {
    if (m_state == nullptr)
        return RenderOperationResultUVE::DeviceUnavailable;
    auto& state = *m_state;
    const auto found = state.queries.find(query.value);
    if (found == state.queries.end())
        return RenderOperationResultUVE::InvalidHandle;
    if (m_activeQuery != query.value || state.activeOcclusion != query.value)
        return RenderOperationResultUVE::InvalidState;
    if (m_activeQueryPassEpoch != m_passEpoch)
        return RenderOperationResultUVE::InvalidState;
    state.gl.glEndQuery(GL_ANY_SAMPLES_PASSED);
    state.activeOcclusion = 0U;
    m_activeQuery = 0U;
    found->second.issued = true;
    return RenderOperationResultUVE::Recorded;
}

RenderOperationResultUVE GlCommandBufferUVE::WriteTimestampUVE(const QueryHandleUVE query) {
    if (m_state == nullptr)
        return RenderOperationResultUVE::DeviceUnavailable;
    auto& state = *m_state;
    const auto found = state.queries.find(query.value);
    if (found == state.queries.end())
        return RenderOperationResultUVE::InvalidHandle;
    if (found->second.kind != RenderQueryKindUVE::Timestamp)
        return RenderOperationResultUVE::InvalidArgument;
#if defined(__ANDROID__)
    return RenderOperationResultUVE::Unsupported;
#else
    if (state.gl.glQueryCounter == nullptr)
        return RenderOperationResultUVE::Unsupported;
    state.gl.glQueryCounter(found->second.native, GL_TIMESTAMP);
    found->second.issued = true;
    return RenderOperationResultUVE::Recorded;
#endif
}
} // namespace UVE::Render
