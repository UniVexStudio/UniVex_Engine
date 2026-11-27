// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "gl_command_buffer.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "gl_error_check.h"
#include "engine_assert.h"
#include "logging_macros.h"

namespace UVE::Render {

namespace {

[[nodiscard]] bool ValidateUniformType(
    const Detail::GlDeviceState::PipelineRecord::UniformRecord& uniform,
    ShaderDataType expectedType, std::string_view name) noexcept {
    if (uniform.type == expectedType) {
        return true;
    }
    UVE_ERROR("GlCommandBuffer: uniform type mismatch for '{}'", name);
    return false;
}

[[nodiscard]] bool RequireInsideRenderPass(bool insideRenderPass, std::string_view operation) noexcept {
    UVE_ASSERT(insideRenderPass);
    if (!insideRenderPass) {
        UVE_ERROR("GlCommandBuffer: {} must be called inside a render pass", operation);
        return false;
    }
    return true;
}

[[nodiscard]] bool RequireOutsideRenderPass(bool insideRenderPass) noexcept {
    UVE_ASSERT(!insideRenderPass);
    if (insideRenderPass) {
        UVE_ERROR("GlCommandBuffer: BeginRenderPass does not support nested render passes");
        return false;
    }
    return true;
}

[[nodiscard]] GLenum ToGlCompare(const DepthCompare compare) noexcept {
    switch (compare) {
        case DepthCompare::Never: return GL_NEVER;
        case DepthCompare::Less: return GL_LESS;
        case DepthCompare::Equal: return GL_EQUAL;
        case DepthCompare::LessOrEqual: return GL_LEQUAL;
        case DepthCompare::Greater: return GL_GREATER;
        case DepthCompare::NotEqual: return GL_NOTEQUAL;
        case DepthCompare::GreaterOrEqual: return GL_GEQUAL;
        case DepthCompare::Always: return GL_ALWAYS;
    }
    return GL_ALWAYS;
}

[[nodiscard]] GLenum ToGlStencilOp(const StencilOp op) noexcept {
    switch (op) {
        case StencilOp::Keep: return GL_KEEP;
        case StencilOp::Zero: return GL_ZERO;
        case StencilOp::Replace: return GL_REPLACE;
        case StencilOp::IncrementClamp: return GL_INCR;
        case StencilOp::DecrementClamp: return GL_DECR;
        case StencilOp::Invert: return GL_INVERT;
        case StencilOp::IncrementWrap: return GL_INCR_WRAP;
        case StencilOp::DecrementWrap: return GL_DECR_WRAP;
    }
    return GL_KEEP;
}

void ApplyStencilFace(const Detail::GlFunctions& gl, const GLenum face,
                         const StencilFaceState& state) noexcept {
    gl.glStencilFuncSeparate(face, ToGlCompare(state.compare), static_cast<GLint>(state.reference),
                          static_cast<GLuint>(state.compareMask));
    gl.glStencilOpSeparate(face, ToGlStencilOp(state.stencilFail), ToGlStencilOp(state.depthFail),
                        ToGlStencilOp(state.depthPass));
    gl.glStencilMaskSeparate(face, static_cast<GLuint>(state.writeMask));
}

[[nodiscard]] GLenum ToGlDrawMode(PrimitiveTopology topology) noexcept {
    switch (topology) {
        case PrimitiveTopology::Triangles:
            return GL_TRIANGLES;
        case PrimitiveTopology::Lines:
            return GL_LINES;
        case PrimitiveTopology::Points:
            return GL_POINTS;
        case PrimitiveTopology::LineStrip:
            return GL_LINE_STRIP;
        case PrimitiveTopology::TriangleStrip:
            return GL_TRIANGLE_STRIP;
    }
    return GL_TRIANGLES;
}

/// The glVertexAttrib(I)Pointer arguments for one RHI attribute format.
struct GlVertexAttributeFetch final {
    GLint componentCount = 3;
    GLenum type = GL_FLOAT;
    GLboolean normalized = GL_FALSE;
    bool integer = false; ///< glVertexAttribIPointer instead of glVertexAttribPointer
};

[[nodiscard]] GlVertexAttributeFetch VertexAttributeFetch(VertexAttributeFormat format) noexcept {
    switch (format) {
        case VertexAttributeFormat::Float2:
            return {2, GL_FLOAT, GL_FALSE, false};
        case VertexAttributeFormat::Float3:
            return {3, GL_FLOAT, GL_FALSE, false};
        case VertexAttributeFormat::Float4:
            return {4, GL_FLOAT, GL_FALSE, false};
        case VertexAttributeFormat::UByte4Norm:
            return {4, GL_UNSIGNED_BYTE, GL_TRUE, false};
        case VertexAttributeFormat::UShort4:
            return {4, GL_UNSIGNED_SHORT, GL_FALSE, true};
        case VertexAttributeFormat::UInt4:
            return {4, GL_UNSIGNED_INT, GL_FALSE, true};
        case VertexAttributeFormat::Short2Norm:
            return {2, GL_SHORT, GL_TRUE, false};
        case VertexAttributeFormat::Half2:
            return {2, GL_HALF_FLOAT, GL_FALSE, false};
        case VertexAttributeFormat::Half4:
            return {4, GL_HALF_FLOAT, GL_FALSE, false};
    }
    return {};
}

[[nodiscard]] bool IsFiniteVector2(const Math::Vector2& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool IsFiniteVector3(const Math::Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool IsFiniteVector4(const Math::Vector4& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

[[nodiscard]] bool IsFiniteMatrix4x4(const Math::Matrix4x4& value) noexcept {
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
/// or `renderPassDesc.viewportOverride` if set (Phase 3's ViewportManager split-view support -
/// see RenderPassDesc's doc comment). Returns false without calling glViewport() if an override
/// is present but does not fit within the target, matching this function's existing "reject a
/// malformed pass descriptor" convention rather than silently clamping it.
///
/// Also arms (or disarms) GL_SCISSOR_TEST to match: glViewport alone does not confine a clear -
/// glClear ignores the viewport and always affects the whole framebuffer unless the scissor test
/// is also enabled with a matching rect. Without this, a second pane's Clear-load-op pass would
/// wipe out a first pane already drawn into the same default framebuffer. Explicitly setting this
/// on every call (both branches) rather than only when an override is present means no scissor
/// state can leak from a previous pass into one that doesn't ask for it.
[[nodiscard]] bool ApplyViewport(const RenderPassDesc& renderPassDesc, const std::uint32_t targetWidth,
                                     const std::uint32_t targetHeight) noexcept {
    if (!renderPassDesc.viewportOverride.has_value()) {
        glViewport(0, 0, static_cast<GLsizei>(targetWidth), static_cast<GLsizei>(targetHeight));
        glDisable(GL_SCISSOR_TEST);
        return true;
    }
    const ViewportRect& rect = *renderPassDesc.viewportOverride;
    const ViewportRect targetBounds{Math::Vector2i{0, 0},
                                        Math::Vector2i{static_cast<std::int32_t>(targetWidth),
                                                          static_cast<std::int32_t>(targetHeight)}};
    if (rect.size.x <= 0 || rect.size.y <= 0 || !Math::Contains(targetBounds, rect)) {
        UVE_ERROR("GlCommandBuffer: BeginRenderPass viewportOverride ({}, {}, {}x{}) does not fit within "
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

GlCommandBuffer::GlCommandBuffer(Detail::GlDeviceState& state) : m_state(&state) {}

void GlCommandBuffer::BeginRenderPass(const RenderPassDesc& renderPassDesc) {
    ++m_passEpoch;
    if (!RequireOutsideRenderPass(m_insideRenderPass)) {
        return;
    }
    if (!IsLoadOpValid(renderPassDesc.colorLoadOp) || !IsLoadOpValid(renderPassDesc.depthLoadOp)) {
        UVE_ERROR("GlCommandBuffer: BeginRenderPass received an unknown load operation");
        return;
    }
    // Tier 2.5: store ops validate like load ops (garbage in = error, even on passes whose
    // store is later ignored — the default framebuffer never invalidates).
    m_passDiscardCount = 0U;
    if (!IsStoreOpValid(renderPassDesc.colorStoreOp) || !IsStoreOpValid(renderPassDesc.depthStoreOp)) {
        UVE_ERROR("GlCommandBuffer: BeginRenderPass received an unknown store operation");
        return;
    }
    if (renderPassDesc.colorLoadOp == LoadOp::Clear) {
        for (const float clearChannel : renderPassDesc.clearColor) {
            if (!std::isfinite(clearChannel)) {
                UVE_ERROR("GlCommandBuffer: color clear value must be finite");
                return;
            }
        }
    }
    if (renderPassDesc.depthLoadOp == LoadOp::Clear && !std::isfinite(renderPassDesc.clearDepth)) {
        UVE_ERROR("GlCommandBuffer: depth clear value must be finite");
        return;
    }

    // Tier 2.4: extras alone make a pass offscreen (they ride the offscreen branch's
    // validation — a default-framebuffer pass must not silently drop them).
    bool hasExtraAttachments = false;
    for (const ColorAttachment& extra : renderPassDesc.extraColorAttachments) {
        if (extra.target != kInvalidTextureHandle) {
            hasExtraAttachments = true;
            break;
        }
    }
    std::uint32_t extraCount = 0U; // resolved by the offscreen branch below
    if (renderPassDesc.colorAttachment == kInvalidTextureHandle &&
        renderPassDesc.depthAttachment == kInvalidTextureHandle && !hasExtraAttachments) {
        if (m_state->windowManager == nullptr || !m_state->windowManager->IsValid()) {
            UVE_ERROR("GlCommandBuffer: default framebuffer render pass requires a valid window surface");
            return;
        }
        const std::uint32_t width = m_state->windowManager->GetWidth();
        const std::uint32_t height = m_state->windowManager->GetHeight();
        if (width == 0U || height == 0U ||
            width > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()) ||
            height > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())) {
            UVE_ERROR("GlCommandBuffer: default framebuffer dimensions are invalid ({}x{})", width, height);
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
        if (!ApplyViewport(renderPassDesc, width, height)) {
            return;
        }
    } else {
        const auto colorIt = renderPassDesc.colorAttachment == kInvalidTextureHandle
                                 ? m_state->textures.end()
                                 : m_state->textures.find(renderPassDesc.colorAttachment.value);
        if (renderPassDesc.colorAttachment != kInvalidTextureHandle && colorIt == m_state->textures.end()) {
            UVE_ERROR("GlCommandBuffer: BeginRenderPass referenced an unknown colorAttachment handle");
            return;
        }
        const auto depthIt = renderPassDesc.depthAttachment == kInvalidTextureHandle
                                 ? m_state->textures.end()
                                 : m_state->textures.find(renderPassDesc.depthAttachment.value);
        if (renderPassDesc.depthAttachment != kInvalidTextureHandle && depthIt == m_state->textures.end()) {
            UVE_ERROR("GlCommandBuffer: BeginRenderPass referenced an unknown depthAttachment handle");
            return;
        }
        if (colorIt != m_state->textures.end() && IsDepthFormat(colorIt->second.desc.format)) {
            UVE_ERROR("GlCommandBuffer: colorAttachment must use a color texture format");
            return;
        }
        if (colorIt != m_state->textures.end() &&
            IsTextureFormatCompressed(colorIt->second.desc.format)) {
            UVE_ERROR("GlCommandBuffer: block-compressed textures are sampled-only and cannot be color attachments");
            return;
        }
        if (depthIt != m_state->textures.end() && !IsDepthFormat(depthIt->second.desc.format)) {
            UVE_ERROR("GlCommandBuffer: depthAttachment must use a depth texture format");
            return;
        }
        // Usage set: a texture created without RenderTarget (a sampled-only asset, a volume)
        // is refused here, before any FBO state changes, instead of failing completeness later.
        if ((colorIt != m_state->textures.end() &&
             !HasTextureUsage(GetEffectiveTextureUsage(colorIt->second.desc), TextureUsage::RenderTarget)) ||
            (depthIt != m_state->textures.end() &&
             !HasTextureUsage(GetEffectiveTextureUsage(depthIt->second.desc), TextureUsage::RenderTarget))) {
            UVE_ERROR("GlCommandBuffer: BeginRenderPass attachment was created without "
                      "TextureUsage::RenderTarget");
            return;
        }
        // Tier 2.4: extras need location 0 (a pass with extras but no colorAttachment is
        // malformed — the swapchain/default framebuffer never takes extras).
        if (hasExtraAttachments && colorIt == m_state->textures.end()) {
            UVE_ERROR("GlCommandBuffer: extra color attachments need location 0 "
                      "(a pass with extras but no colorAttachment is malformed)");
            return;
        }
        if (colorIt != m_state->textures.end() && depthIt != m_state->textures.end() &&
            (colorIt->second.desc.width != depthIt->second.desc.width ||
             colorIt->second.desc.height != depthIt->second.desc.height)) {
            UVE_ERROR("GlCommandBuffer: colorAttachment and depthAttachment dimensions must match");
            return;
        }
        // Tier 2.3: layer selection must sit inside the attachment's layer count (2D textures
        // carry exactly one layer, so any nonzero layer on them fails here too).
        if (colorIt != m_state->textures.end() &&
            renderPassDesc.colorLayer >= colorIt->second.desc.arrayLayers) {
            UVE_ERROR("GlCommandBuffer: colorLayer exceeds the color attachment's layer count");
            return;
        }
        if (depthIt != m_state->textures.end() &&
            renderPassDesc.depthLayer >= depthIt->second.desc.arrayLayers) {
            UVE_ERROR("GlCommandBuffer: depthLayer exceeds the depth attachment's layer count");
            return;
        }

        // Tier 2.4: locations 1..3 — a contiguous prefix under the same contract as location 0
        // (known color-format handle, matching dims, layer in range), pairwise distinct from
        // location 0, the depth, and each other (one texture may not feed two attachments).
        // Loader entries are checked here too so an MRT pass fails BEFORE allocating its FBO.
        std::array<std::uint32_t, kExtraColorAttachmentCount> extraHandles{};
        std::array<std::uint32_t, kExtraColorAttachmentCount> extraLayers{};
        {
            bool gapSeen = false;
            bool anyColorClear = renderPassDesc.colorLoadOp == LoadOp::Clear;
            for (const ColorAttachment& slot : renderPassDesc.extraColorAttachments) {
                if (slot.target == kInvalidTextureHandle) {
                    gapSeen = true;
                    continue;
                }
                if (gapSeen) {
                    UVE_ERROR("GlCommandBuffer: extra color attachments must be a contiguous "
                              "prefix (a gap is malformed)");
                    return;
                }
                if (!IsLoadOpValid(slot.loadOp) || !IsStoreOpValid(slot.storeOp)) {
                    UVE_ERROR("GlCommandBuffer: BeginRenderPass received an unknown extra "
                              "attachment load or store operation");
                    return;
                }
                if (slot.loadOp == LoadOp::Clear) {
                    anyColorClear = true;
                    for (const float clearChannel : slot.clearColor) {
                        if (!std::isfinite(clearChannel)) {
                            UVE_ERROR("GlCommandBuffer: extra color clear value must be finite");
                            return;
                        }
                    }
                }
                const auto extraIt = m_state->textures.find(slot.target.value);
                if (extraIt == m_state->textures.end()) {
                    UVE_ERROR("GlCommandBuffer: BeginRenderPass referenced an unknown extra "
                              "color attachment handle");
                    return;
                }
                if (IsDepthFormat(extraIt->second.desc.format) ||
                    IsTextureFormatCompressed(extraIt->second.desc.format)) {
                    UVE_ERROR("GlCommandBuffer: extra color attachments must use a color texture "
                              "format (block-compressed textures are sampled-only)");
                    return;
                }
                if (!HasTextureUsage(GetEffectiveTextureUsage(extraIt->second.desc),
                                        TextureUsage::RenderTarget)) {
                    UVE_ERROR("GlCommandBuffer: extra color attachment was created without "
                              "TextureUsage::RenderTarget");
                    return;
                }
                if (extraIt->second.desc.width != colorIt->second.desc.width ||
                    extraIt->second.desc.height != colorIt->second.desc.height) {
                    UVE_ERROR("GlCommandBuffer: every color attachment must match location 0's "
                              "dimensions (FBO completeness)");
                    return;
                }
                if (slot.layer >= extraIt->second.desc.arrayLayers) {
                    UVE_ERROR("GlCommandBuffer: an extra color layer exceeds its attachment's "
                              "layer count");
                    return;
                }
                if (extraIt->second.desc.type != TextureType::Texture2D &&
                    extraIt->second.desc.type != TextureType::Cubemap &&
                    m_state->gl.glFramebufferTextureLayer == nullptr) {
                    UVE_ERROR("GlCommandBuffer: layered render targets need "
                              "glFramebufferTextureLayer (GL 3.2+)");
                    return;
                }
                if (slot.target.value == renderPassDesc.colorAttachment.value ||
                    slot.target.value == renderPassDesc.depthAttachment.value) {
                    UVE_ERROR("GlCommandBuffer: one texture may not feed two attachments of the "
                              "same pass");
                    return;
                }
                for (std::uint32_t j = 0U; j < extraCount; ++j) {
                    if (extraHandles[j] == slot.target.value) {
                        UVE_ERROR("GlCommandBuffer: one texture may not feed two attachments of "
                                  "the same pass");
                        return;
                    }
                }
                extraHandles[extraCount] = slot.target.value;
                extraLayers[extraCount] = slot.layer;
                ++extraCount;
            }
            if (extraCount > 0U && m_state->gl.glDrawBuffers == nullptr) {
                UVE_ERROR("GlCommandBuffer: multi-target passes need glDrawBuffers");
                return;
            }
            if (extraCount > 0U && anyColorClear && m_state->gl.glClearBufferfv == nullptr) {
                UVE_ERROR("GlCommandBuffer: multi-target clears need glClearBufferfv");
                return;
            }
        }

        const Detail::GlDeviceState::FramebufferKey framebufferKey{
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
                    UVE_ERROR("GlCommandBuffer: the pass names more color attachments than the "
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
                // Cubemap faces must attach via glFramebufferTexture2D with the face target;
                // glFramebufferTextureLayer is invalid on GL_TEXTURE_CUBE_MAP in GL/GLES3.
                if (colorIt->second.desc.type == TextureType::Cubemap) {
                    const GLenum faceTarget = static_cast<GLenum>(
                        GL_TEXTURE_CUBE_MAP_POSITIVE_X + renderPassDesc.colorLayer);
                    m_state->gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                                       faceTarget, colorIt->second.glTexture, 0);
                } else if (colorIt->second.desc.type != TextureType::Texture2D) {
                    if (m_state->gl.glFramebufferTextureLayer == nullptr) {
                        // No layered attach on this context (pre-3.2): skip the attachment so the
                        // status check below tears the FBO down through the standard path.
                        UVE_ERROR("GlCommandBuffer: layered render targets need "
                                  "glFramebufferTextureLayer (GL 3.2+)");
                    } else {
                        m_state->gl.glFramebufferTextureLayer(
                            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, colorIt->second.glTexture, 0,
                            static_cast<GLint>(renderPassDesc.colorLayer));
                    }
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
                    if (extraIt->second.desc.type == TextureType::Cubemap) {
                        const GLenum faceTarget =
                            static_cast<GLenum>(GL_TEXTURE_CUBE_MAP_POSITIVE_X + extraLayers[i]);
                        m_state->gl.glFramebufferTexture2D(GL_FRAMEBUFFER, extraPoint, faceTarget,
                                                           extraIt->second.glTexture, 0);
                    } else if (extraIt->second.desc.type != TextureType::Texture2D) {
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
                    std::array<GLenum, 1U + kExtraColorAttachmentCount> drawBuffers{};
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
                    IsDepthStencilFormat(depthIt->second.desc.format) ? GL_DEPTH_STENCIL_ATTACHMENT
                                                                          : GL_DEPTH_ATTACHMENT;
                if (depthIt->second.desc.type == TextureType::Cubemap) {
                    const GLenum faceTarget = static_cast<GLenum>(
                        GL_TEXTURE_CUBE_MAP_POSITIVE_X + renderPassDesc.depthLayer);
                    m_state->gl.glFramebufferTexture2D(GL_FRAMEBUFFER, depthAttachmentPoint,
                                                       faceTarget, depthIt->second.glTexture, 0);
                } else if (depthIt->second.desc.type != TextureType::Texture2D) {
                    if (m_state->gl.glFramebufferTextureLayer == nullptr) {
                        UVE_ERROR("GlCommandBuffer: layered render targets need "
                                  "glFramebufferTextureLayer (GL 3.2+)");
                    } else {
                        m_state->gl.glFramebufferTextureLayer(
                            GL_FRAMEBUFFER, depthAttachmentPoint, depthIt->second.glTexture, 0,
                            static_cast<GLint>(renderPassDesc.depthLayer));
                    }
                } else {
                    m_state->gl.glFramebufferTexture2D(GL_FRAMEBUFFER, depthAttachmentPoint,
                                                       GL_TEXTURE_2D, depthIt->second.glTexture, 0);
                }
            }
            const GLenum framebufferStatus = m_state->gl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
                UVE_ERROR("GlCommandBuffer: BeginRenderPass built an incomplete framebuffer");
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

        if (!ApplyViewport(renderPassDesc, attachmentWidth, attachmentHeight)) {
            if (framebufferCreated) {
                m_state->gl.glDeleteFramebuffers(1, &framebuffer);
                m_state->framebufferCache.erase(framebufferKey);
                m_state->gl.glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
            }
            return;
        }
        // Tier 2.5: remember the DontCare attachments — EndRenderPass invalidates them while
        // the FBO is still bound (a discard hint, never a correctness requirement).
        m_passDiscardCount = 0U;
        if (renderPassDesc.colorStoreOp == StoreOp::DontCare &&
            colorIt != m_state->textures.end()) {
            m_passDiscardAttachments[m_passDiscardCount++] = GL_COLOR_ATTACHMENT0;
        }
        for (std::uint32_t i = 0U; i < extraCount; ++i) {
            if (renderPassDesc.extraColorAttachments[i].storeOp == StoreOp::DontCare) {
                m_passDiscardAttachments[m_passDiscardCount++] =
                    static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + 1U + i);
            }
        }
        if (renderPassDesc.depthStoreOp == StoreOp::DontCare &&
            depthIt != m_state->textures.end()) {
            // Invalidation takes individual aspects, not the combined FBO attachment token.
            m_passDiscardAttachments[m_passDiscardCount++] = GL_DEPTH_ATTACHMENT;
            if (IsDepthStencilFormat(depthIt->second.desc.format)) {
                m_passDiscardAttachments[m_passDiscardCount++] = GL_STENCIL_ATTACHMENT;
            }
        }
        m_tempFramebuffer = framebuffer;
    }

    GLbitfield clearMask = 0;
    if (extraCount == 0U && renderPassDesc.colorLoadOp == LoadOp::Clear) {
        glClearColor(renderPassDesc.clearColor[0], renderPassDesc.clearColor[1], renderPassDesc.clearColor[2],
                     renderPassDesc.clearColor[3]);
        clearMask |= GL_COLOR_BUFFER_BIT;
    }
    // Tier 2.4: glClear would paint every draw buffer one color — multi-target passes clear
    // per slot instead (glClearBufferfv was validated non-null above when any color clears).
    if (extraCount > 0U && renderPassDesc.colorLoadOp == LoadOp::Clear) {
        m_state->gl.glClearBufferfv(GL_COLOR, 0, renderPassDesc.clearColor.data());
    }
    if (extraCount > 0U) {
        for (std::uint32_t i = 0U; i < extraCount; ++i) {
            const ColorAttachment& slot = renderPassDesc.extraColorAttachments[i];
            if (slot.loadOp == LoadOp::Clear) {
                m_state->gl.glClearBufferfv(GL_COLOR, static_cast<GLint>(1U + i),
                                             slot.clearColor.data());
            }
        }
    }
    if (renderPassDesc.depthLoadOp == LoadOp::Clear) {
        // glClear(GL_DEPTH_BUFFER_BIT) respects GL_DEPTH_WRITEMASK. A prior fullscreen pass
        // deliberately disables depth writes, so a render pass that explicitly requests a depth
        // clear must restore the write mask first or its clear becomes a silent no-op and all
        // same-depth geometry in subsequent frames can fail GL_LESS.
        glDepthMask(GL_TRUE);
        m_currentPipeline = kInvalidPipelineHandle;
#if defined(__ANDROID__)
        glClearDepthf(renderPassDesc.clearDepth);
#else
        glClearDepth(static_cast<GLdouble>(renderPassDesc.clearDepth));
#endif
        clearMask |= GL_DEPTH_BUFFER_BIT;
        const auto clearDepthIt = m_state->textures.find(renderPassDesc.depthAttachment.value);
        if (clearDepthIt != m_state->textures.end() &&
            IsDepthStencilFormat(clearDepthIt->second.desc.format)) {
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

void GlCommandBuffer::EndRenderPass() {
    ++m_passEpoch;
    if (m_activeQuery != 0U && m_state != nullptr && m_state->activeOcclusion == m_activeQuery) {
        // A bracket may not span the pass boundary: close it loudly so the GL query object is
        // left in a valid state; the caller's late EndQuery reports InvalidState.
        UVE_ERROR("GlCommandBuffer: EndRenderPass closed an occlusion query that was still open");
        m_state->gl.glEndQuery(GL_ANY_SAMPLES_PASSED);
        const auto found = m_state->queries.find(m_activeQuery);
        if (found != m_state->queries.end())
            found->second.issued = true;
        m_state->activeOcclusion = 0U;
        m_activeQuery = 0U;
    }
    if (!RequireInsideRenderPass(m_insideRenderPass, "EndRenderPass")) {
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
    if (m_state != nullptr && m_state->gl.glBindSampler != nullptr && !m_boundSamplers.empty()) {
        for (const auto& [slot, samplerHandle] : m_boundSamplers) {
            static_cast<void>(samplerHandle);
            m_state->gl.glBindSampler(slot, 0U);
        }
        m_boundSamplers.clear();
    }
    m_insideRenderPass = false;
}

void GlCommandBuffer::BindPipeline(PipelineHandle pipeline) {
    // M5a: no inside-pass gate here anymore. Compute pipelines MUST bind outside render-pass
    // markers (mirroring Vulkan, where binding COMPUTE inside a pass instance is illegal), and
    // glUseProgram/glBindVertexArray are equally legal outside a pass. The real structural gates
    // live on the consuming commands: Draw (inside) and Dispatch (outside).
    if (pipeline == kInvalidPipelineHandle) {
        UVE_ERROR("GlCommandBuffer: BindPipeline referenced an invalid pipeline handle");
        return;
    }
    if (m_currentPipeline == pipeline) {
        return;
    }
    const auto pipelineIt = m_state->pipelines.find(pipeline.value);
    if (pipelineIt == m_state->pipelines.end()) {
        UVE_ERROR("GlCommandBuffer: BindPipeline referenced an unknown pipeline handle");
        return;
    }
    m_currentProgram = pipelineIt->second.glProgram;
    m_currentVao = pipelineIt->second.glVao;
    m_currentPipeline = pipeline;
    m_boundVertexBuffer = kInvalidBufferHandle;
    m_boundIndexBuffer = kInvalidBufferHandle;
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
    // Every mode sets its own equation, because a pipeline switch can leave the equation of the
    // previous material behind: GL has no per-fragment state, so a batch of Additive draws followed
    // by a plain SourceAlphaOver one would keep GL_FUNC_REVERSE_SUBTRACT if the equation were only
    // ever set by the mode that needed it.
    switch (pipelineIt->second.blendMode) {
        case PipelineBlendMode::SourceAlphaOver:
            glEnable(GL_BLEND);
            m_state->gl.glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case PipelineBlendMode::Additive:
            glEnable(GL_BLEND);
            m_state->gl.glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_ONE, GL_ONE);
            break;
        case PipelineBlendMode::Multiply:
            glEnable(GL_BLEND);
            m_state->gl.glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
            break;
        case PipelineBlendMode::PremultipliedAlpha:
            // The source baked its alpha into rgb, so it is added as-is and only the destination is
            // attenuated.
            glEnable(GL_BLEND);
            m_state->gl.glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case PipelineBlendMode::Subtractive:
            // dst - src*srcA. Reverse subtract, because GL's FUNC_SUBTRACT is src - dst.
            glEnable(GL_BLEND);
            m_state->gl.glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case PipelineBlendMode::Opaque:
            glDisable(GL_BLEND);
            break;
    }
    // Tier 2.1: rasterizer state from the pipeline record. Defaults reproduce pre-2.1 behavior
    // exactly (culling off, CCW, fill, no polygon offset, LESS compare — the compare call is
    // new but LESS is what the untouched context default always gave us).
    switch (pipelineIt->second.cullMode) {
        case CullMode::None:
            glDisable(GL_CULL_FACE);
            break;
        case CullMode::Front:
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);
            break;
        case CullMode::Back:
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            break;
        case CullMode::FrontAndBack:
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT_AND_BACK);
            break;
    }
    switch (pipelineIt->second.frontFace) {
        case FrontFace::CounterClockwise:
            glFrontFace(GL_CCW);
            break;
        case FrontFace::Clockwise:
            glFrontFace(GL_CW);
            break;
    }
#if !defined(__ANDROID__)
    switch (pipelineIt->second.fillMode) {
        case FillMode::Fill:
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            break;
        case FillMode::Wireframe:
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            break;
    }
#endif
#if !defined(__ANDROID__)
    // Points take their size from the vertex shader's gl_PointSize, which desktop GL ignores
    // unless program point size is enabled (GLES always honours it). Toggled per pipeline so a
    // Points pipeline never leaks the mode into the next triangle pipeline's state.
    if (pipelineIt->second.topology == PrimitiveTopology::Points) {
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
        case DepthCompare::Never:          glDepthFunc(GL_NEVER);   break;
        case DepthCompare::Less:           glDepthFunc(GL_LESS);    break;
        case DepthCompare::Equal:          glDepthFunc(GL_EQUAL);   break;
        case DepthCompare::LessOrEqual:    glDepthFunc(GL_LEQUAL);  break;
        case DepthCompare::Greater:        glDepthFunc(GL_GREATER); break;
        case DepthCompare::NotEqual:       glDepthFunc(GL_NOTEQUAL); break;
        case DepthCompare::GreaterOrEqual: glDepthFunc(GL_GEQUAL);  break;
        case DepthCompare::Always:         glDepthFunc(GL_ALWAYS);  break;
    }
    if (pipelineIt->second.stencil.enabled) {
        glEnable(GL_STENCIL_TEST);
        ApplyStencilFace(m_state->gl, GL_FRONT, pipelineIt->second.stencil.front);
        ApplyStencilFace(m_state->gl, GL_BACK, pipelineIt->second.stencil.back);
    } else {
        glDisable(GL_STENCIL_TEST);
        glStencilMask(0U);
    }
}

void GlCommandBuffer::BindVertexBuffer(BufferHandle buffer, std::uint32_t slot) {
    if (!RequireInsideRenderPass(m_insideRenderPass, "BindVertexBuffer")) {
        return;
    }
    static_cast<void>(slot); // This minimal RHI describes one interleaved vertex layout per
                              // pipeline, not a per-slot binding table — every attribute in
                              // vertexLayout is configured against whichever buffer is bound here.
    if (buffer == kInvalidBufferHandle) {
        UVE_ERROR("GlCommandBuffer: BindVertexBuffer referenced an invalid buffer handle");
        return;
    }
    if (m_boundVertexBuffer == buffer) {
        return;
    }
    const auto bufferIt = m_state->buffers.find(buffer.value);
    if (bufferIt == m_state->buffers.end()) {
        UVE_ERROR("GlCommandBuffer: BindVertexBuffer referenced an unknown buffer handle");
        return;
    }
    if (bufferIt->second.target != GL_ARRAY_BUFFER) {
        UVE_ERROR("GlCommandBuffer: BindVertexBuffer requires a vertex buffer");
        return;
    }
    const auto* const pipelineRecord = FindCurrentPipeline();
    if (pipelineRecord == nullptr) {
        UVE_ERROR("GlCommandBuffer: BindVertexBuffer called without a live pipeline");
        return;
    }
    m_state->gl.glBindBuffer(GL_ARRAY_BUFFER, bufferIt->second.glBuffer);

    for (std::size_t index = 0; index < pipelineRecord->vertexLayout.size(); ++index) {
        const VertexAttribute& attribute = pipelineRecord->vertexLayout[index];
        const auto attributeIndex = static_cast<GLuint>(index);
        const GlVertexAttributeFetch fetch = VertexAttributeFetch(attribute.format);
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

void GlCommandBuffer::BindIndexBuffer(BufferHandle buffer) {
    if (!RequireInsideRenderPass(m_insideRenderPass, "BindIndexBuffer")) {
        return;
    }
    if (buffer == kInvalidBufferHandle) {
        UVE_ERROR("GlCommandBuffer: BindIndexBuffer referenced an invalid buffer handle");
        return;
    }
    if (m_boundIndexBuffer == buffer) {
        return;
    }
    const auto bufferIt = m_state->buffers.find(buffer.value);
    if (bufferIt == m_state->buffers.end()) {
        UVE_ERROR("GlCommandBuffer: BindIndexBuffer referenced an unknown buffer handle");
        return;
    }
    if (bufferIt->second.target != GL_ELEMENT_ARRAY_BUFFER) {
        UVE_ERROR("GlCommandBuffer: BindIndexBuffer requires an index buffer");
        return;
    }
    m_state->gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bufferIt->second.glBuffer);
    m_boundIndexBuffer = buffer;
}

void GlCommandBuffer::BindTexture(TextureHandle texture, std::uint32_t slot) {
    // M5b: BindTexture now also feeds STORAGE-image slots (the unified texture-slot
    // space), and the compute flow (bind compute pipeline → bind its textures → dispatch)
    // lives entirely OUTSIDE pass markers — so while a compute pipeline is the bound one,
    // this call passes the gate; graphics-side misuse keeps the strict inside-pass rule
    // (the same M5a relaxation BindStorageBuffer and the SetUniform* calls got).
    if (!m_insideRenderPass && !ActivePipelineIsCompute()) {
        (void)RequireInsideRenderPass(m_insideRenderPass, "BindTexture");
        return;
    }
    if (m_state->maxCombinedTextureImageUnits <= 0 ||
        slot >= static_cast<std::uint32_t>(m_state->maxCombinedTextureImageUnits)) {
        UVE_ERROR("GlCommandBuffer: BindTexture texture slot exceeds GL texture-unit limits");
        return;
    }
    const auto textureIt = m_state->textures.find(texture.value);
    if (textureIt == m_state->textures.end()) {
        UVE_ERROR("GlCommandBuffer: BindTexture referenced an unknown texture handle");
        return;
    }
    const TextureUsage textureUsage = GetEffectiveTextureUsage(textureIt->second.desc);
    if (!HasTextureUsage(textureUsage, TextureUsage::Sampled)) {
        UVE_ERROR("GlCommandBuffer: BindTexture texture was created without TextureUsage::Sampled; "
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
        const GLenum bindTarget = textureIt->second.desc.type == TextureType::Cubemap
                                      ? GL_TEXTURE_CUBE_MAP
                                  : textureIt->second.desc.type == TextureType::Texture2DArray
                                      ? GL_TEXTURE_2D_ARRAY
                                  : textureIt->second.desc.type == TextureType::Texture3D
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
        const auto* const pipelineRecord = FindCurrentPipeline();
        if (pipelineRecord != nullptr && pipelineRecord->hasImageUniforms) {
            GLenum imageFormat = 0;
            switch (textureIt->second.desc.format) {
                case TextureFormat::RGBA8Unorm:
                    // OpenGL does not provide storage-image access to sRGB texture storage.
                    // Keep sRGB textures available to samplers, but refuse their image-unit
                    // binding instead of issuing writes that drivers may silently discard.
                    imageFormat = textureIt->second.desc.colorSpace == TextureColorSpace::Srgb
                                      ? 0U
                                      : GL_RGBA8;
                    break;
                case TextureFormat::RGBA16Float:
                    imageFormat = GL_RGBA16F;
                    break;
                case TextureFormat::R8Unorm:
                    imageFormat = GL_R8;
                    break;
                case TextureFormat::RG8Unorm:
                    imageFormat = GL_RG8;
                    break;
                case TextureFormat::R16Float:
                    imageFormat = GL_R16F;
                    break;
                case TextureFormat::RG16Float:
                    imageFormat = GL_RG16F;
                    break;
                case TextureFormat::R32Float:
                    imageFormat = GL_R32F;
                    break;
                case TextureFormat::RG32Float:
                    imageFormat = GL_RG32F;
                    break;
                case TextureFormat::RGBA32Float:
                    imageFormat = GL_RGBA32F;
                    break;
                case TextureFormat::Depth32Float:
                case TextureFormat::Depth24Stencil8:
                case TextureFormat::BC1RGB:
                case TextureFormat::BC3RGBA:
                case TextureFormat::BC7RGBA:
                case TextureFormat::ETC2RGB8:
                case TextureFormat::ETC2RGBA8:
                case TextureFormat::ASTC4x4RGBA:
                    imageFormat = 0;
                    break;
            }
            // Tier 2.3: layered storage images are out of scope — arrays/cubes stay
            // sampler-only, matching the Vulkan backend's black-sink fallback.
            const bool storageAllowed = HasTextureUsage(textureUsage, TextureUsage::Storage);
            if (imageFormat != 0 && textureIt->second.desc.arrayLayers <= 1U && storageAllowed) {
                m_state->gl.glBindImageTexture(slot, textureIt->second.glTexture, 0, GL_FALSE,
                                               0, GL_READ_WRITE, imageFormat);
            } else {
                // Do not leave a prior command's image binding live when the new sampled
                // texture cannot legally serve as a storage image.
                m_state->gl.glBindImageTexture(slot, 0U, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
                if (!storageAllowed) {
                    UVE_ERROR("GlCommandBuffer: texture was created without TextureUsage::Storage; "
                              "the image unit is left unbound");
                } else if (textureIt->second.desc.arrayLayers > 1U) {
                    UVE_ERROR("GlCommandBuffer: array/cubemap textures cannot be bound as "
                              "storage images in Tier 2.3; the image unit is left unbound");
                } else if (IsTextureFormatCompressed(textureIt->second.desc.format)) {
                    UVE_ERROR("GlCommandBuffer: block-compressed textures cannot be bound as storage "
                              "images; the image unit is left unbound");
                } else if (textureIt->second.desc.colorSpace == TextureColorSpace::Srgb) {
                    UVE_ERROR("GlCommandBuffer: sRGB textures cannot be bound as storage images; "
                              "the image unit is left unbound");
                } else {
                    UVE_ERROR("GlCommandBuffer: depth textures cannot be bound as storage "
                              "images; the image unit is left unbound");
                }
            }
        }
    }
}

void GlCommandBuffer::BindSampler(SamplerHandle sampler, std::uint32_t slot) {
    // Same compute-aware gate as BindTexture: sampler binds pair with texture binds.
    if (!m_insideRenderPass && !ActivePipelineIsCompute()) {
        (void)RequireInsideRenderPass(m_insideRenderPass, "BindSampler");
        return;
    }
    if (m_state->maxCombinedTextureImageUnits <= 0 ||
        slot >= static_cast<std::uint32_t>(m_state->maxCombinedTextureImageUnits)) {
        UVE_ERROR("GlCommandBuffer: BindSampler sampler slot exceeds GL texture-unit limits");
        return;
    }
    const auto samplerIt = m_state->samplers.find(sampler.value);
    if (samplerIt == m_state->samplers.end()) {
        UVE_ERROR("GlCommandBuffer: BindSampler referenced an unknown sampler handle");
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

void GlCommandBuffer::BindUniformBuffer(BufferHandle buffer, std::uint32_t slot) {
    if (!RequireInsideRenderPass(m_insideRenderPass, "BindUniformBuffer")) {
        return;
    }
    if (m_state->maxUniformBufferBindings <= 0 ||
        slot >= static_cast<std::uint32_t>(m_state->maxUniformBufferBindings)) {
        UVE_ERROR("GlCommandBuffer: BindUniformBuffer slot exceeds GL uniform-buffer limits");
        return;
    }
    const auto bufferIt = m_state->buffers.find(buffer.value);
    if (bufferIt == m_state->buffers.end()) {
        UVE_ERROR("GlCommandBuffer: BindUniformBuffer referenced an unknown buffer handle");
        return;
    }
    if (bufferIt->second.target != GL_UNIFORM_BUFFER) {
        UVE_ERROR("GlCommandBuffer: BindUniformBuffer requires a uniform buffer");
        return;
    }
    m_state->gl.glBindBufferBase(GL_UNIFORM_BUFFER, slot, bufferIt->second.glBuffer);
}

void GlCommandBuffer::BindStorageBuffer(BufferHandle buffer, std::uint32_t slot) {
    // M5a: the compute flow (bind compute pipeline, bind its SSBOs, set its uniforms,
    // dispatch) lives entirely OUTSIDE pass markers — so while a compute pipeline is the
    // bound one, these calls pass the gate; graphics-side misuse keeps the strict inside-pass
    // rule (GL knows the bound pipeline's kind, unlike NullCommandBuffer).
    if (!m_insideRenderPass && !ActivePipelineIsCompute()) {
        (void)RequireInsideRenderPass(m_insideRenderPass, "BindStorageBuffer");
        return;
    }
    // SSBOs are core in desktop GL 4.3 (the same floor as compute shaders); the cached gate
    // doubles as the "this context knows GL_SHADER_STORAGE_BUFFER at all" answer - on older
    // or GLES contexts the enum itself doesn't exist, so refuse before naming it.
    if (!m_state->supportsComputeShaders || m_state->maxShaderStorageBindings <= 0) {
        UVE_ERROR("GlCommandBuffer: BindStorageBuffer needs a desktop GL 4.3+ context "
                  "(shader storage buffers); this context does not offer them");
        return;
    }
    if (slot >= static_cast<std::uint32_t>(m_state->maxShaderStorageBindings)) {
        UVE_ERROR("GlCommandBuffer: BindStorageBuffer slot exceeds GL shader-storage limits");
        return;
    }
    const auto bufferIt = m_state->buffers.find(buffer.value);
    if (bufferIt == m_state->buffers.end()) {
        UVE_ERROR("GlCommandBuffer: BindStorageBuffer referenced an unknown buffer handle");
        return;
    }
#if !defined(__ANDROID__)
    if (bufferIt->second.target != GL_SHADER_STORAGE_BUFFER) {
        UVE_ERROR("GlCommandBuffer: BindStorageBuffer requires a Storage-usage buffer");
        return;
    }
    // Whole-buffer base binding, matching the Vulkan backend's whole-buffer STORAGE_BUFFER
    // descriptor (offset 0, range = the buffer's full size) byte for byte in semantics.
    m_state->gl.glBindBufferBase(GL_SHADER_STORAGE_BUFFER, slot, bufferIt->second.glBuffer);
#endif
}

bool GlCommandBuffer::ActivePipelineIsCompute() const noexcept {
    const auto* const record = FindCurrentPipeline();
    return record != nullptr && record->isCompute;
}

const Detail::GlDeviceState::PipelineRecord* GlCommandBuffer::FindCurrentPipeline() const {
    if (m_currentPipeline == kInvalidPipelineHandle) {
        return nullptr;
    }
    const auto pipelineIt = m_state->pipelines.find(m_currentPipeline.value);
    if (pipelineIt == m_state->pipelines.end()) {
        return nullptr;
    }
    return &pipelineIt->second;
}

const GlCommandBuffer::UniformRecord* GlCommandBuffer::FindUniform(std::string_view name) const {
    const auto* const pipelineRecord = FindCurrentPipeline();
    if (pipelineRecord == nullptr) {
        UVE_WARNING("GlCommandBuffer: SetUniform*UVE called without a live pipeline (uniform \"{}\")", name);
        return nullptr;
    }
    const auto it = pipelineRecord->uniforms.find(name);
    if (it == pipelineRecord->uniforms.end()) {
        const auto element = pipelineRecord->uniformElements.find(name);
        if (element != pipelineRecord->uniformElements.end()) return &element->second;
        if (pipelineRecord->warnedUnknownUniforms.emplace(name).second) {
            UVE_WARNING("GlCommandBuffer: SetUniform*UVE - \"{}\" is not an active uniform on the bound pipeline",
                         name);
        }
        return nullptr;
    }
    return &it->second;
}

void GlCommandBuffer::SetUniformFloat(std::string_view name, float value) {
    if (!m_insideRenderPass && !ActivePipelineIsCompute()) { // M5a compute flow
        (void)RequireInsideRenderPass(m_insideRenderPass, "SetUniformFloat");
        return;
    }
    const UniformRecord* const uniform = FindUniform(name);
    if (uniform == nullptr || !ValidateUniformType(*uniform, ShaderDataType::Float, name)) {
        return;
    }
    if (!std::isfinite(value)) {
        UVE_ERROR("GlCommandBuffer: float uniform '{}' must be finite", name);
        return;
    }
    m_state->gl.glUniform1f(uniform->location, value);
}

void GlCommandBuffer::SetUniformInt(std::string_view name, std::int32_t value) {
    if (!m_insideRenderPass && !ActivePipelineIsCompute()) { // M5a compute flow
        (void)RequireInsideRenderPass(m_insideRenderPass, "SetUniformInt");
        return;
    }
    const UniformRecord* const uniform = FindUniform(name);
    if (uniform == nullptr || !ValidateUniformType(*uniform, ShaderDataType::Int, name)) {
        return;
    }
    m_state->gl.glUniform1i(uniform->location, static_cast<GLint>(value));
}

void GlCommandBuffer::SetUniformBool(std::string_view name, bool value) {
    if (!m_insideRenderPass && !ActivePipelineIsCompute()) { // M5a compute flow
        (void)RequireInsideRenderPass(m_insideRenderPass, "SetUniformBool");
        return;
    }
    const UniformRecord* const uniform = FindUniform(name);
    if (uniform == nullptr || !ValidateUniformType(*uniform, ShaderDataType::Bool, name)) {
        return;
    }
    m_state->gl.glUniform1i(uniform->location, value ? 1 : 0);
}

void GlCommandBuffer::SetUniformVector2(std::string_view name, const Math::Vector2& value) {
    if (!m_insideRenderPass && !ActivePipelineIsCompute()) { // M5a compute flow
        (void)RequireInsideRenderPass(m_insideRenderPass, "SetUniformVector2");
        return;
    }
    const UniformRecord* const uniform = FindUniform(name);
    if (uniform == nullptr || !ValidateUniformType(*uniform, ShaderDataType::Vec2, name)) {
        return;
    }
    if (!IsFiniteVector2(value)) {
        UVE_ERROR("GlCommandBuffer: vector uniform '{}' must be finite", name);
        return;
    }
    m_state->gl.glUniform2fv(uniform->location, 1, &value.x);
}

void GlCommandBuffer::SetUniformVector4(std::string_view name, const Math::Vector4& value) {
    if (!m_insideRenderPass && !ActivePipelineIsCompute()) { // M5a compute flow
        (void)RequireInsideRenderPass(m_insideRenderPass, "SetUniformVector4");
        return;
    }
    const UniformRecord* const uniform = FindUniform(name);
    if (uniform == nullptr || !ValidateUniformType(*uniform, ShaderDataType::Vec4, name)) {
        return;
    }
    if (!IsFiniteVector4(value)) {
        UVE_ERROR("GlCommandBuffer: vector uniform '{}' must be finite", name);
        return;
    }
    m_state->gl.glUniform4fv(uniform->location, 1, &value.x);
}

void GlCommandBuffer::SetUniformVector3(std::string_view name, const Math::Vector3& value) {
    if (!m_insideRenderPass && !ActivePipelineIsCompute()) { // M5a compute flow
        (void)RequireInsideRenderPass(m_insideRenderPass, "SetUniformVector3");
        return;
    }
    const UniformRecord* const uniform = FindUniform(name);
    if (uniform == nullptr || !ValidateUniformType(*uniform, ShaderDataType::Vec3, name)) {
        return;
    }
    if (!IsFiniteVector3(value)) {
        UVE_ERROR("GlCommandBuffer: vector uniform '{}' must be finite", name);
        return;
    }
    m_state->gl.glUniform3fv(uniform->location, 1, &value.x);
}

void GlCommandBuffer::SetUniformMatrix4x4(std::string_view name, const Math::Matrix4x4& value) {
    if (!m_insideRenderPass && !ActivePipelineIsCompute()) { // M5a compute flow
        (void)RequireInsideRenderPass(m_insideRenderPass, "SetUniformMatrix4x4");
        return;
    }
    const UniformRecord* const uniform = FindUniform(name);
    if (uniform == nullptr || !ValidateUniformType(*uniform, ShaderDataType::Mat4, name)) {
        return;
    }
    if (!IsFiniteMatrix4x4(value)) {
        UVE_ERROR("GlCommandBuffer: matrix uniform '{}' must be finite", name);
        return;
    }
    // Matrix4x4 is row-major storage (docs/CODING_STANDARDS.md, "Matrix convention"). Desktop
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

void GlCommandBuffer::DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount,
                                        std::uint32_t firstIndex) {
    if (!RequireInsideRenderPass(m_insideRenderPass, "DrawIndexed")) {
        return;
    }
    const auto* const pipelineRecord = FindCurrentPipeline();
    if (pipelineRecord == nullptr) {
        UVE_ERROR("GlCommandBuffer: DrawIndexed called without a live pipeline");
        return;
    }
    if (m_boundIndexBuffer == kInvalidBufferHandle) {
        UVE_ERROR("GlCommandBuffer: DrawIndexed called without a bound index buffer");
        return;
    }
    const auto indexBufferIt = m_state->buffers.find(m_boundIndexBuffer.value);
    if (indexBufferIt == m_state->buffers.end() || indexBufferIt->second.target != GL_ELEMENT_ARRAY_BUFFER) {
        UVE_ERROR("GlCommandBuffer: DrawIndexed has no valid bound index buffer");
        return;
    }
    const std::uint64_t indexCapacity = indexBufferIt->second.sizeBytes / sizeof(std::uint32_t);
    // The whole range, not just its length: a surface at the tail of a large index buffer passes
    // an indexCount check on its own and still reads past the binding.
    const std::uint64_t rangeEnd = static_cast<std::uint64_t>(firstIndex) + indexCount;
    if (rangeEnd > indexCapacity) {
        UVE_ERROR("GlCommandBuffer: DrawIndexed range [{}, {}) exceeds the bound index buffer "
                  "of {} indices",
                  firstIndex, rangeEnd, indexCapacity);
        return;
    }
    if (indexCount == 0U || instanceCount == 0U) {
        return;
    }
    if (indexCount > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()) ||
        instanceCount > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())) {
        UVE_ERROR("GlCommandBuffer: DrawIndexed count exceeds the GLsizei range");
        return;
    }
    // GL takes a byte offset here, unlike every other API in this engine which speaks indices.
    const auto indexOffsetBytes =
        static_cast<std::uintptr_t>(firstIndex) * static_cast<std::uintptr_t>(sizeof(std::uint32_t));
    const GLenum drawMode = ToGlDrawMode(pipelineRecord->topology);
    if (instanceCount > 1U) {
        if (m_state->gl.glDrawElementsInstanced != nullptr) {
            m_state->gl.glDrawElementsInstanced(drawMode, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT,
                                                reinterpret_cast<const void*>(indexOffsetBytes),
                                                static_cast<GLsizei>(instanceCount));
        } else {
            UVE_WARNING("GlCommandBuffer: DrawIndexed instanceCount > 1 requires "
                        "glDrawElementsInstanced - drawing once");
            glDrawElements(drawMode, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(indexOffsetBytes));
        }
    } else {
        glDrawElements(drawMode, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(indexOffsetBytes));
    }
    UVE_GL_CHECK_ERROR_UVE("DrawIndexed");
}

void GlCommandBuffer::DrawIndexedIndirect(const BufferHandle buffer,
                                                const std::uint64_t offsetBytes) {
    if (!RequireInsideRenderPass(m_insideRenderPass, "DrawIndexedIndirect")) {
        return;
    }
    const auto* const pipelineRecord = FindCurrentPipeline();
    if (pipelineRecord == nullptr) {
        UVE_ERROR("GlCommandBuffer: DrawIndexedIndirect called without a live pipeline");
        return;
    }
    if (m_boundIndexBuffer == kInvalidBufferHandle) {
        UVE_ERROR("GlCommandBuffer: DrawIndexedIndirect called without a bound index buffer");
        return;
    }
    if (m_state->gl.glDrawElementsIndirect == nullptr) {
        // GL below 4.0, or a driver that did not export it. Warn and skip rather than call
        // through null - the same degradation the compute paths use.
        UVE_WARNING("GlCommandBuffer: DrawIndexedIndirect needs GL 4.0+ indirect draw, "
                    "which this context does not provide; the draw is skipped");
        return;
    }
    const auto indirectIt = m_state->buffers.find(buffer.value);
    if (indirectIt == m_state->buffers.end()) {
        UVE_ERROR("GlCommandBuffer: DrawIndexedIndirect was given an unknown buffer handle");
        return;
    }
    if (!IsStorageBindableUsage(indirectIt->second.usage)) {
        UVE_ERROR("GlCommandBuffer: DrawIndexedIndirect needs an IndirectStorage buffer");
        return;
    }
    // The whole command must lie inside the buffer. Reading parameters off the end would produce
    // a draw with garbage counts, which is precisely the failure indirect draw makes invisible -
    // nothing on the CPU would ever see the numbers.
    if (offsetBytes > indirectIt->second.sizeBytes ||
        indirectIt->second.sizeBytes - offsetBytes < sizeof(DrawIndexedIndirectCommand)) {
        UVE_ERROR("GlCommandBuffer: DrawIndexedIndirect offset leaves no whole command "
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
        ToGlDrawMode(pipelineRecord->topology), GL_UNSIGNED_INT,
        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(offsetBytes)));
    UVE_GL_CHECK_ERROR_UVE("DrawIndexedIndirect");
    // Leave no lingering indirect binding: the buffer's home target is the SSBO one, and a stale
    // GL_DRAW_INDIRECT_BUFFER binding would silently feed the next indirect draw.
    m_state->gl.glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0U);
}

void GlCommandBuffer::Draw(std::uint32_t vertexCount, std::uint32_t instanceCount) {
    DrawRange(vertexCount, instanceCount, 0U);
}

void GlCommandBuffer::DrawRange(std::uint32_t vertexCount, std::uint32_t instanceCount,
                                std::uint32_t firstVertex) {
    if (!RequireInsideRenderPass(m_insideRenderPass, "Draw")) {
        return;
    }
    if (m_currentPipeline != kInvalidPipelineHandle && FindCurrentPipeline() == nullptr) {
        UVE_ERROR("GlCommandBuffer: Draw called with a destroyed pipeline");
        return;
    }
    if (m_boundVertexBuffer != kInvalidBufferHandle) {
        const auto vertexBufferIt = m_state->buffers.find(m_boundVertexBuffer.value);
        if (vertexBufferIt == m_state->buffers.end() || vertexBufferIt->second.target != GL_ARRAY_BUFFER) {
            UVE_ERROR("GlCommandBuffer: Draw has no valid bound vertex buffer");
            return;
        }
        const std::uint64_t rangeEnd =
            static_cast<std::uint64_t>(firstVertex) + static_cast<std::uint64_t>(vertexCount);
        if (m_currentVertexStride > 0U &&
            rangeEnd >
                vertexBufferIt->second.sizeBytes / static_cast<std::uint64_t>(m_currentVertexStride)) {
            UVE_ERROR("GlCommandBuffer: Draw range [{}, {}) exceeds the bound vertex buffer",
                      firstVertex, rangeEnd);
            return;
        }
    }
    if (vertexCount == 0U || instanceCount == 0U) {
        return;
    }
    if (firstVertex > static_cast<std::uint32_t>(std::numeric_limits<GLint>::max()) ||
        vertexCount > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()) ||
        instanceCount > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())) {
        UVE_ERROR("GlCommandBuffer: Draw count or offset exceeds the GL range");
        return;
    }
    const auto* const drawPipelineRecord = FindCurrentPipeline();
    const GLenum drawMode = drawPipelineRecord != nullptr
                                ? ToGlDrawMode(drawPipelineRecord->topology)
                                : GL_TRIANGLES;
    const auto glFirst = static_cast<GLint>(firstVertex);
    if (instanceCount > 1U) {
        if (m_state->gl.glDrawArraysInstanced != nullptr) {
            m_state->gl.glDrawArraysInstanced(drawMode, glFirst, static_cast<GLsizei>(vertexCount),
                                              static_cast<GLsizei>(instanceCount));
        } else {
            UVE_WARNING("GlCommandBuffer: Draw instanceCount > 1 requires glDrawArraysInstanced "
                        "- drawing once");
            glDrawArrays(drawMode, glFirst, static_cast<GLsizei>(vertexCount));
        }
    } else {
        glDrawArrays(drawMode, glFirst, static_cast<GLsizei>(vertexCount));
    }
    UVE_GL_CHECK_ERROR_UVE("Draw");
}

void GlCommandBuffer::Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY,
                                     std::uint32_t groupCountZ) {
    // M5a: dispatch belongs OUTSIDE render-pass markers — the mirror image of Draw's inside-pass
    // gate (Vulkan forbids compute inside a render-pass instance, so the portable flow binds the
    // compute pipeline and dispatches before/after pass markers).
    UVE_ASSERT(!m_insideRenderPass);
    if (m_insideRenderPass) {
        UVE_ERROR("GlCommandBuffer: Dispatch must be called outside a render pass");
        return;
    }
    const auto* const pipelineRecord = FindCurrentPipeline();
    if (pipelineRecord == nullptr) {
        UVE_ERROR("GlCommandBuffer: Dispatch requires a live compute pipeline to be bound");
        return;
    }
    if (!pipelineRecord->isCompute) {
        UVE_ERROR("GlCommandBuffer: Dispatch requires a compute pipeline (a graphics pipeline is bound)");
        return;
    }
    if (m_state->gl.glDispatchCompute == nullptr || m_state->gl.glMemoryBarrier == nullptr) {
        UVE_ERROR("GlCommandBuffer: Dispatch requires an OpenGL 4.3+ context");
        return;
    }
    // GL executes at record time: BindPipeline already ran glUseProgram for this program.
    m_state->gl.glDispatchCompute(groupCountX, groupCountY, groupCountZ);
    // Conservative global barrier — makes the compute writes visible to every later reader
    // (glGetBufferSubData readback and the fragment palette sampling in the M5a proof).
    m_state->gl.glMemoryBarrier(GL_ALL_BARRIER_BITS);
    UVE_GL_CHECK_ERROR_UVE("Dispatch");
}

} // namespace UVE::Render

namespace UVE::Render {
RenderFeatureSupport GlCommandBuffer::GetMemoryBarrierSupport() const noexcept {
#if defined(__ANDROID__)
    return RenderFeatureSupport::Unsupported;
#else
    return m_state->supportsComputeShaders && m_state->gl.glMemoryBarrier
        ? RenderFeatureSupport::Native : RenderFeatureSupport::Unsupported;
#endif
}
RenderOperationResult GlCommandBuffer::MemoryBarrier(MemoryBarrierMask mask) {
    if (!IsMemoryBarrierMaskValid(mask))
        return RenderOperationResult::InvalidArgument;
    if (m_insideRenderPass)
        return RenderOperationResult::InvalidState;
#if defined(__ANDROID__)
    return RenderOperationResult::Unsupported;
#else
    if (!m_state->supportsComputeShaders || !m_state->gl.glMemoryBarrier)
        return RenderOperationResult::Unsupported;
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
#if !defined(NDEBUG)
    return glGetError() == GL_NO_ERROR ? RenderOperationResult::Success
                                       : RenderOperationResult::SystemError;
#else
    return RenderOperationResult::Success;
#endif
#endif
}
} // namespace UVE::Render

namespace UVE::Render {
RenderOperationResult GlCommandBuffer::BeginQuery(const QueryHandle query) {
    if (m_state == nullptr)
        return RenderOperationResult::DeviceUnavailable;
    auto& state = *m_state;
    if (state.gl.glBeginQuery == nullptr || state.gl.glEndQuery == nullptr)
        return RenderOperationResult::Unsupported;
    const auto found = state.queries.find(query.value);
    if (found == state.queries.end())
        return RenderOperationResult::InvalidHandle;
    if (found->second.kind != RenderQueryKind::AnySamplesPassed)
        return RenderOperationResult::InvalidArgument;
    if (state.activeOcclusion != 0U || m_activeQuery != 0U)
        return RenderOperationResult::InvalidState;
    state.gl.glBeginQuery(GL_ANY_SAMPLES_PASSED, found->second.native);
    state.activeOcclusion = query.value;
    found->second.issued = false;
    m_activeQuery = query.value;
    m_activeQueryPassEpoch = m_passEpoch;
    return RenderOperationResult::Recorded;
}

RenderOperationResult GlCommandBuffer::EndQuery(const QueryHandle query) {
    if (m_state == nullptr)
        return RenderOperationResult::DeviceUnavailable;
    auto& state = *m_state;
    const auto found = state.queries.find(query.value);
    if (found == state.queries.end())
        return RenderOperationResult::InvalidHandle;
    if (m_activeQuery != query.value || state.activeOcclusion != query.value)
        return RenderOperationResult::InvalidState;
    if (m_activeQueryPassEpoch != m_passEpoch)
        return RenderOperationResult::InvalidState;
    state.gl.glEndQuery(GL_ANY_SAMPLES_PASSED);
    state.activeOcclusion = 0U;
    m_activeQuery = 0U;
    found->second.issued = true;
    return RenderOperationResult::Recorded;
}

RenderOperationResult GlCommandBuffer::WriteTimestamp(const QueryHandle query) {
    if (m_state == nullptr)
        return RenderOperationResult::DeviceUnavailable;
    auto& state = *m_state;
    const auto found = state.queries.find(query.value);
    if (found == state.queries.end())
        return RenderOperationResult::InvalidHandle;
    if (found->second.kind != RenderQueryKind::Timestamp)
        return RenderOperationResult::InvalidArgument;
#if defined(__ANDROID__)
    return RenderOperationResult::Unsupported;
#else
    if (state.gl.glQueryCounter == nullptr)
        return RenderOperationResult::Unsupported;
    state.gl.glQueryCounter(found->second.native, GL_TIMESTAMP);
    found->second.issued = true;
    return RenderOperationResult::Recorded;
#endif
}
} // namespace UVE::Render
