// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "univex/render/GlApi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "Viewport2D/DrawUtil.h"
#include "Viewport2D/IViewportRenderer.h"
#include "Viewport2D/Viewport2D.h"
#include "univex/render/ShaderProgram.h"

#include "uve/asset/asset_guid_uve.h"
#include "uve/asset/bmp_metadata_uve.h"
#include "uve/asset/file_system_uve.h"
#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/jpeg_metadata_uve.h"
#include "uve/asset/png_metadata_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/asset/tga_metadata_uve.h"
#include "uve/component/camera_2d_component_uve.h"
#include "uve/component/disabled_component_uve.h"
#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/sprite_2d_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/core/engine_core_uve.h"
#include "uve/editor/editor_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/2d/all_objects_2d_uve.h"
#include "uve/scene/i_scene_graph_uve.h"

namespace UVE::App::Viewport2DIntegration {

// 5x7 ASCII bitmap font (codes 32..126), 5 columns per glyph, LSB = top row (row 0..6).
inline const std::array<std::uint8_t, 5>& Glyph5x7UVE(const char ch) noexcept {
    static constexpr std::array<std::array<std::uint8_t, 5>, 95> kFont5x7 = {{
        {0x00, 0x00, 0x00, 0x00, 0x00}, // ' '
        {0x00, 0x00, 0x5F, 0x00, 0x00}, // '!'
        {0x00, 0x07, 0x00, 0x07, 0x00}, // '"'
        {0x14, 0x7F, 0x14, 0x7F, 0x14}, // '#'
        {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // '$'
        {0x23, 0x13, 0x08, 0x64, 0x62}, // '%'
        {0x36, 0x49, 0x55, 0x22, 0x50}, // '&'
        {0x00, 0x05, 0x03, 0x00, 0x00}, // '\''
        {0x00, 0x1C, 0x22, 0x41, 0x00}, // '('
        {0x00, 0x41, 0x22, 0x1C, 0x00}, // ')'
        {0x14, 0x08, 0x3E, 0x08, 0x14}, // '*'
        {0x08, 0x08, 0x3E, 0x08, 0x08}, // '+'
        {0x00, 0x50, 0x30, 0x00, 0x00}, // ','
        {0x08, 0x08, 0x08, 0x08, 0x08}, // '-'
        {0x00, 0x60, 0x60, 0x00, 0x00}, // '.'
        {0x20, 0x10, 0x08, 0x04, 0x02}, // '/'
        {0x3E, 0x51, 0x49, 0x45, 0x3E}, // '0'
        {0x00, 0x42, 0x7F, 0x40, 0x00}, // '1'
        {0x42, 0x61, 0x51, 0x49, 0x46}, // '2'
        {0x21, 0x41, 0x45, 0x4B, 0x31}, // '3'
        {0x18, 0x14, 0x12, 0x7F, 0x10}, // '4'
        {0x27, 0x45, 0x45, 0x45, 0x39}, // '5'
        {0x3C, 0x4A, 0x49, 0x49, 0x30}, // '6'
        {0x01, 0x71, 0x09, 0x05, 0x03}, // '7'
        {0x36, 0x49, 0x49, 0x49, 0x36}, // '8'
        {0x06, 0x49, 0x49, 0x29, 0x1E}, // '9'
        {0x00, 0x36, 0x36, 0x00, 0x00}, // ':'
        {0x00, 0x56, 0x36, 0x00, 0x00}, // ';'
        {0x08, 0x14, 0x22, 0x41, 0x00}, // '<'
        {0x14, 0x14, 0x14, 0x14, 0x14}, // '='
        {0x00, 0x41, 0x22, 0x14, 0x08}, // '>'
        {0x02, 0x01, 0x51, 0x09, 0x06}, // '?'
        {0x32, 0x49, 0x79, 0x41, 0x3E}, // '@'
        {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 'A'
        {0x7F, 0x49, 0x49, 0x49, 0x36}, // 'B'
        {0x3E, 0x41, 0x41, 0x41, 0x22}, // 'C'
        {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 'D'
        {0x7F, 0x49, 0x49, 0x49, 0x41}, // 'E'
        {0x7F, 0x09, 0x09, 0x09, 0x01}, // 'F'
        {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 'G'
        {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 'H'
        {0x00, 0x41, 0x7F, 0x41, 0x00}, // 'I'
        {0x20, 0x40, 0x41, 0x3F, 0x01}, // 'J'
        {0x7F, 0x08, 0x14, 0x22, 0x41}, // 'K'
        {0x7F, 0x40, 0x40, 0x40, 0x40}, // 'L'
        {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 'M'
        {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 'N'
        {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 'O'
        {0x7F, 0x09, 0x09, 0x09, 0x06}, // 'P'
        {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 'Q'
        {0x7F, 0x09, 0x19, 0x29, 0x46}, // 'R'
        {0x46, 0x49, 0x49, 0x49, 0x31}, // 'S'
        {0x01, 0x01, 0x7F, 0x01, 0x01}, // 'T'
        {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 'U'
        {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 'V'
        {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 'W'
        {0x63, 0x14, 0x08, 0x14, 0x63}, // 'X'
        {0x07, 0x08, 0x70, 0x08, 0x07}, // 'Y'
        {0x61, 0x51, 0x49, 0x45, 0x43}, // 'Z'
        {0x00, 0x7F, 0x41, 0x41, 0x00}, // '['
        {0x02, 0x04, 0x08, 0x10, 0x20}, // '\\'
        {0x00, 0x41, 0x41, 0x7F, 0x00}, // ']'
        {0x04, 0x02, 0x01, 0x02, 0x04}, // '^'
        {0x40, 0x40, 0x40, 0x40, 0x40}, // '_'
        {0x00, 0x01, 0x02, 0x04, 0x00}, // '`'
        {0x20, 0x54, 0x54, 0x54, 0x78}, // 'a'
        {0x7F, 0x48, 0x44, 0x44, 0x38}, // 'b'
        {0x38, 0x44, 0x44, 0x44, 0x20}, // 'c'
        {0x38, 0x44, 0x44, 0x48, 0x7F}, // 'd'
        {0x38, 0x54, 0x54, 0x54, 0x18}, // 'e'
        {0x08, 0x7E, 0x09, 0x01, 0x02}, // 'f'
        {0x0C, 0x52, 0x52, 0x52, 0x3E}, // 'g'
        {0x7F, 0x08, 0x04, 0x04, 0x78}, // 'h'
        {0x00, 0x44, 0x7D, 0x40, 0x00}, // 'i'
        {0x20, 0x40, 0x44, 0x3D, 0x00}, // 'j'
        {0x7F, 0x10, 0x28, 0x44, 0x00}, // 'k'
        {0x00, 0x41, 0x7F, 0x40, 0x00}, // 'l'
        {0x7C, 0x04, 0x18, 0x04, 0x78}, // 'm'
        {0x7C, 0x08, 0x04, 0x04, 0x78}, // 'n'
        {0x38, 0x44, 0x44, 0x44, 0x38}, // 'o'
        {0x7C, 0x14, 0x14, 0x14, 0x08}, // 'p'
        {0x08, 0x14, 0x14, 0x18, 0x7C}, // 'q'
        {0x7C, 0x08, 0x04, 0x04, 0x08}, // 'r'
        {0x48, 0x54, 0x54, 0x54, 0x20}, // 's'
        {0x04, 0x3F, 0x44, 0x40, 0x20}, // 't'
        {0x3C, 0x40, 0x40, 0x20, 0x7C}, // 'u'
        {0x1C, 0x20, 0x40, 0x20, 0x1C}, // 'v'
        {0x3C, 0x40, 0x30, 0x40, 0x3C}, // 'w'
        {0x44, 0x28, 0x10, 0x28, 0x44}, // 'x'
        {0x0C, 0x50, 0x50, 0x50, 0x3C}, // 'y'
        {0x44, 0x64, 0x54, 0x4C, 0x44}, // 'z'
        {0x00, 0x08, 0x36, 0x41, 0x00}, // '{'
        {0x00, 0x00, 0x7F, 0x00, 0x00}, // '|'
        {0x00, 0x41, 0x36, 0x08, 0x00}, // '}'
        {0x08, 0x04, 0x08, 0x10, 0x08}, // '~'
    }};
    const int code = static_cast<unsigned char>(ch);
    if (code < 32 || code > 126) {
        return kFont5x7[0U];
    }
    return kFont5x7[static_cast<std::size_t>(code - 32)];
}

// OpenGL 3.3 Core implementation of Claude's univex::viewport::IViewportRenderer, plus textured
// quad support for rendering Sprite2D / AnimatedSprite2D / MeshInstance2D / TileMapLayer assets.
class GlViewport2DRendererUVE final : public univex::viewport::IViewportRenderer {
public:
    GlViewport2DRendererUVE() = default;
    ~GlViewport2DRendererUVE() override {
        DestroyResourcesUVE();
    }

    GlViewport2DRendererUVE(const GlViewport2DRendererUVE&) = delete;
    GlViewport2DRendererUVE& operator=(const GlViewport2DRendererUVE&) = delete;

    void beginFrame(const int widthPx, const int heightPx, const univex::viewport::Color clear) override {
        widthPx_ = std::max(1, widthPx);
        heightPx_ = std::max(1, heightPx);
        EnsureResourcesUVE();
        vertices_.clear();
        currentTex_ = whiteTex_;

        glViewport(0, 0, widthPx_, heightPx_);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glClearColor(static_cast<float>(clear.r) / 255.0F,
                     static_cast<float>(clear.g) / 255.0F,
                     static_cast<float>(clear.b) / 255.0F,
                     static_cast<float>(clear.a) / 255.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void drawLine(const univex::viewport::Vec2 a, const univex::viewport::Vec2 b,
                  const univex::viewport::Color c, const float thicknessPx) override {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len <= 1.0e-5F) {
            return;
        }
        const float halfW = std::max(0.5F, thicknessPx * 0.5F);
        const float nx = (-dy / len) * halfW;
        const float ny = (dx / len) * halfW;
        fillQuad(univex::viewport::Vec2{a.x + nx, a.y + ny},
                 univex::viewport::Vec2{b.x + nx, b.y + ny},
                 univex::viewport::Vec2{b.x - nx, b.y - ny},
                 univex::viewport::Vec2{a.x - nx, a.y - ny}, c);
    }

    void fillRect(const univex::viewport::Vec2 topLeft, const univex::viewport::Vec2 sizePx,
                  const univex::viewport::Color c) override {
        if (sizePx.x <= 0.0F || sizePx.y <= 0.0F) {
            return;
        }
        const univex::viewport::Vec2 p0 = topLeft;
        const univex::viewport::Vec2 p1{topLeft.x + sizePx.x, topLeft.y};
        const univex::viewport::Vec2 p2{topLeft.x + sizePx.x, topLeft.y + sizePx.y};
        const univex::viewport::Vec2 p3{topLeft.x, topLeft.y + sizePx.y};
        fillQuad(p0, p1, p2, p3, c);
    }

    void fillQuad(const univex::viewport::Vec2 a, const univex::viewport::Vec2 b,
                  const univex::viewport::Vec2 c, const univex::viewport::Vec2 d,
                  const univex::viewport::Color col) override {
        fillTexturedQuadUVE(a, b, c, d,
                            univex::viewport::Vec2{0.5F, 0.5F},
                            univex::viewport::Vec2{0.5F, 0.5F},
                            univex::viewport::Vec2{0.5F, 0.5F},
                            univex::viewport::Vec2{0.5F, 0.5F},
                            whiteTex_, col);
    }

    void fillTexturedQuadUVE(const univex::viewport::Vec2 a, const univex::viewport::Vec2 b,
                             const univex::viewport::Vec2 c, const univex::viewport::Vec2 d,
                             const univex::viewport::Vec2 uvA, const univex::viewport::Vec2 uvB,
                             const univex::viewport::Vec2 uvC, const univex::viewport::Vec2 uvD,
                             const GLuint texId, const univex::viewport::Color col) {
        if (col.a == 0U) {
            return;
        }
        const GLuint resolvedTex = (texId != 0U) ? texId : whiteTex_;
        if (resolvedTex != currentTex_) {
            FlushBatchUVE();
            currentTex_ = resolvedTex;
        }
        const float rf = static_cast<float>(col.r) / 255.0F;
        const float gf = static_cast<float>(col.g) / 255.0F;
        const float bf = static_cast<float>(col.b) / 255.0F;
        const float af = static_cast<float>(col.a) / 255.0F;

        const Vertex2DUVE vA{a.x, a.y, uvA.x, uvA.y, rf, gf, bf, af};
        const Vertex2DUVE vB{b.x, b.y, uvB.x, uvB.y, rf, gf, bf, af};
        const Vertex2DUVE vC{c.x, c.y, uvC.x, uvC.y, rf, gf, bf, af};
        const Vertex2DUVE vD{d.x, d.y, uvD.x, uvD.y, rf, gf, bf, af};
        vertices_.push_back(vA);
        vertices_.push_back(vB);
        vertices_.push_back(vC);
        vertices_.push_back(vA);
        vertices_.push_back(vC);
        vertices_.push_back(vD);
    }

    void drawText(const univex::viewport::Vec2 pos, const char* const text,
                  const univex::viewport::Color c, const float sizePx,
                  const univex::viewport::TextOrient orient) override {
        if (text == nullptr || c.a == 0U) {
            return;
        }
        const float scale = std::max(1.0F, sizePx / 7.0F);
        std::size_t charIndex = 0U;
        for (const char* p = text; *p != '\0'; ++p, ++charIndex) {
            const auto& cols = Glyph5x7UVE(*p);
            const float charAdvance = static_cast<float>(charIndex * 6U) * scale;
            for (std::size_t col = 0U; col < 5U; ++col) {
                const std::uint8_t bits = cols[col];
                if (bits == 0U) {
                    continue;
                }
                for (std::size_t row = 0U; row < 7U; ++row) {
                    if ((static_cast<unsigned int>(bits) & (1U << row)) == 0U) {
                        continue;
                    }
                    if (orient == univex::viewport::TextOrient::Horizontal) {
                        const float x = pos.x + charAdvance + static_cast<float>(col) * scale;
                        const float y = pos.y + static_cast<float>(row) * scale;
                        fillRect(univex::viewport::Vec2{x, y}, univex::viewport::Vec2{scale, scale}, c);
                    } else {
                        // VerticalUp: text reads bottom->top, pos is bottom-left of rotated text
                        const float x = pos.x + static_cast<float>(row) * scale;
                        const float y = pos.y - (charAdvance + static_cast<float>(col + 1U) * scale);
                        fillRect(univex::viewport::Vec2{x, y}, univex::viewport::Vec2{scale, scale}, c);
                    }
                }
            }
        }
    }

    void endFrame() override {
        FlushBatchUVE();
    }

private:
    struct Vertex2DUVE final {
        float x = 0.0F;
        float y = 0.0F;
        float u = 0.0F;
        float v = 0.0F;
        float r = 1.0F;
        float g = 1.0F;
        float b = 1.0F;
        float a = 1.0F;
    };

    void EnsureResourcesUVE() {
        if (program_.has_value()) {
            return;
        }
        static constexpr std::string_view kVertSrc = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uViewportSize;
out vec2 vUV;
out vec4 vColor;
void main() {
    float ndcX = (aPos.x / uViewportSize.x) * 2.0 - 1.0;
    float ndcY = 1.0 - (aPos.y / uViewportSize.y) * 2.0;
    gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
)";
        static constexpr std::string_view kFragSrc = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
out vec4 FragColor;
void main() {
    FragColor = texture(uTex, vUV) * vColor;
}
)";
        std::string error;
        program_ = univex::render::ShaderProgram::Build(kVertSrc, kFragSrc, error);
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glEnableVertexAttribArray(0U);
        glVertexAttribPointer(0U, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(Vertex2DUVE)),
                              reinterpret_cast<const void*>(offsetof(Vertex2DUVE, x)));
        glEnableVertexAttribArray(1U);
        glVertexAttribPointer(1U, 2, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(Vertex2DUVE)),
                              reinterpret_cast<const void*>(offsetof(Vertex2DUVE, u)));
        glEnableVertexAttribArray(2U);
        glVertexAttribPointer(2U, 4, GL_FLOAT, GL_FALSE, static_cast<GLsizei>(sizeof(Vertex2DUVE)),
                              reinterpret_cast<const void*>(offsetof(Vertex2DUVE, r)));
        glBindVertexArray(0U);
        glBindBuffer(GL_ARRAY_BUFFER, 0U);

        const std::array<std::uint8_t, 4> whitePixel{255U, 255U, 255U, 255U};
        glGenTextures(1, &whiteTex_);
        glBindTexture(GL_TEXTURE_2D, whiteTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0U);
    }

    void FlushBatchUVE() {
        if (vertices_.empty() || !program_.has_value()) {
            vertices_.clear();
            return;
        }
        program_->Use();
        glUniform2f(program_->UniformLocation("uViewportSize"),
                    static_cast<float>(widthPx_), static_cast<float>(heightPx_));
        glUniform1i(program_->UniformLocation("uTex"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, currentTex_ != 0U ? currentTex_ : whiteTex_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(vertices_.size() * sizeof(Vertex2DUVE)),
                     vertices_.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices_.size()));
        glBindBuffer(GL_ARRAY_BUFFER, 0U);
        glBindVertexArray(0U);
        glBindTexture(GL_TEXTURE_2D, 0U);
        vertices_.clear();
    }

    void DestroyResourcesUVE() {
        if (whiteTex_ != 0U) {
            glDeleteTextures(1, &whiteTex_);
            whiteTex_ = 0U;
        }
        if (vbo_ != 0U) {
            glDeleteBuffers(1, &vbo_);
            vbo_ = 0U;
        }
        if (vao_ != 0U) {
            glDeleteVertexArrays(1, &vao_);
            vao_ = 0U;
        }
        program_.reset();
    }

    int widthPx_ = 1;
    int heightPx_ = 1;
    std::optional<univex::render::ShaderProgram> program_;
    GLuint vao_ = 0U;
    GLuint vbo_ = 0U;
    GLuint whiteTex_ = 0U;
    GLuint currentTex_ = 0U;
    std::vector<Vertex2DUVE> vertices_;
};

// Complete 2D Viewport host that connects Claude's univex::viewport::Viewport2D to UniVex Engine's
// ECS 2D entities, textures, gizmos, picking, marquee, and undoable transform editing.
class Viewport2DHostUVE final {
public:
    Viewport2DHostUVE(Editor::EditorUVE& editor, Core::EngineCoreUVE& engine)
        : editor_(editor),
          engine_(engine),
          entityManager_(engine.GetServicesUVE().GetEntityManagerUVE()) {
        viewport_.setCanvasRect(univex::viewport::Rect{{0.0F, 0.0F}, {1280.0F, 720.0F}});
        viewport_.camera().setCenter(univex::viewport::Vec2{640.0F, 360.0F});
        viewport_.camera().setZoom(0.75F);

        viewport_.setDrawScene([this](univex::viewport::IViewportRenderer& r,
                                      const univex::viewport::Camera2D& cam) {
            DrawScene2DUVE(r, cam);
        });
        viewport_.setOnPick([this](const univex::viewport::Vec2 world,
                                   const univex::viewport::Vec2 /*screen*/,
                                   const bool additive) {
            HandlePick2DUVE(world, additive);
        });
        viewport_.setOnMarquee([this](const univex::viewport::Rect& worldRect,
                                      const bool additive,
                                      const univex::viewport::Phase phase) {
            HandleMarquee2DUVE(worldRect, additive, phase);
        });
        viewport_.setOnTransform([this](const univex::viewport::Box& proposed,
                                        const univex::viewport::Phase phase) {
            HandleTransform2DUVE(proposed, phase);
        });
    }

    ~Viewport2DHostUVE() {
        for (auto& [guid, entry] : textureCache_) {
            (void)guid;
            if (entry.glTexId != 0U) {
                glDeleteTextures(1, &entry.glTexId);
            }
        }
    }

    Viewport2DHostUVE(const Viewport2DHostUVE&) = delete;
    Viewport2DHostUVE& operator=(const Viewport2DHostUVE&) = delete;

    void RenderToBoundFramebufferUVE(const int width, const int height,
                                     const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
        viewport_.setSize(width, height);
        if (!initialCentered_ && width > 64 && height > 64) {
            initialCentered_ = true;
            viewport_.zoomToFit(univex::viewport::Rect{{0.0F, 0.0F}, {1280.0F, 720.0F}}, false);
        }

        SyncOverlaySettingsUVE(overlayState);
        SyncSelectionToGizmoUVE();
        if (!overlayState.gameWorkspaceActive && !overlayState.studioView) {
            PumpImGuiInputUVE(width, height, overlayState.pointerOverOverlay);
        }
        viewport_.update(ImGui::GetIO().DeltaTime);

        const univex::viewport::Vec2 camCenter = viewport_.camera().center();
        editor_.SetViewportCameraFocusUVE(Math::Vector3UVE{camCenter.x, camCenter.y, 0.0F});

        viewport_.render(renderer_);
    }

private:
    struct CachedTexture2DUVE final {
        GLuint glTexId = 0U;
        float width = 64.0F;
        float height = 64.0F;
        bool attempted = false;
    };

    struct Pose2DUVE final {
        univex::viewport::Vec2 position{0.0F, 0.0F};
        float rotationRadians = 0.0F;
        univex::viewport::Vec2 scale{1.0F, 1.0F};
    };

    [[nodiscard]] static std::uint8_t FloatToByteUVE(const float v) noexcept {
        return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F));
    }

    [[nodiscard]] static univex::viewport::Vec2 RotateVec2UVE(const univex::viewport::Vec2 v,
                                                              const float angle) noexcept {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        return univex::viewport::Vec2{v.x * c - v.y * s, v.x * s + v.y * c};
    }

    [[nodiscard]] static univex::viewport::Vec2 TransformPoint2DUVE(const Pose2DUVE& pose,
                                                                    const univex::viewport::Vec2 local) noexcept {
        const univex::viewport::Vec2 scaled{local.x * pose.scale.x, local.y * pose.scale.y};
        return pose.position + RotateVec2UVE(scaled, pose.rotationRadians);
    }

    [[nodiscard]] static float ExtractRotationZRadiansUVE(const Math::QuaternionUVE& q) noexcept {
        Math::Vector3UVE euler{};
        if (Math::TryToEulerUVE(q, euler)) {
            return euler.z;
        }
        return std::atan2(2.0F * (q.w * q.z + q.x * q.y), 1.0F - 2.0F * (q.y * q.y + q.z * q.z));
    }

    [[nodiscard]] Pose2DUVE ResolveEntityPose2DUVE(const Scene::EntityUVE entity) const {
        Pose2DUVE pose{};
        if (entityManager_.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
            const auto& wt = entityManager_.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
            if (Scene::IsWorldTransformComponentValidUVE(wt)) {
                pose.position = univex::viewport::Vec2{wt.worldPosition.x, wt.worldPosition.y};
                pose.rotationRadians = ExtractRotationZRadiansUVE(wt.worldRotation);
                pose.scale = univex::viewport::Vec2{wt.worldScale.x, wt.worldScale.y};
            }
        } else if (entityManager_.HasComponentUVE<Scene::TransformComponentUVE>(entity)) {
            const auto& lt = entityManager_.GetComponentUVE<Scene::TransformComponentUVE>(entity);
            pose.position = univex::viewport::Vec2{lt.localPosition.x, lt.localPosition.y};
            Math::Vector3UVE euler{};
            if (Scene::TryGetDisplayEulerUVE(lt, euler)) {
                pose.rotationRadians = euler.z;
            } else {
                pose.rotationRadians = ExtractRotationZRadiansUVE(lt.localRotation);
            }
            pose.scale = univex::viewport::Vec2{lt.localScale.x, lt.localScale.y};
        }
        if (entityManager_.HasComponentUVE<Scene::Parallax2DComponentUVE>(entity)) {
            const auto& parallax = entityManager_.GetComponentUVE<Scene::Parallax2DComponentUVE>(entity);
            if (parallax.enabled) {
                const univex::viewport::Vec2 camCenter = viewport_.camera().center();
                const Math::Vector2UVE offset = Scene::Parallax2DUVE::ComputeEffectiveOffsetUVE(
                    parallax, Math::Vector2UVE{camCenter.x, camCenter.y});
                pose.position = pose.position + univex::viewport::Vec2{offset.x, offset.y};
            }
        }
        return pose;
    }

    [[nodiscard]] bool IsEntityVisibleIn2DUVE(const Scene::EntityUVE entity) const {
        if (!entityManager_.IsAliveUVE(entity) ||
            entityManager_.HasComponentUVE<Scene::EditorInternalEntityComponentUVE>(entity) ||
            entityManager_.HasComponentUVE<Scene::DisabledComponentUVE>(entity)) {
            return false;
        }
        if (entityManager_.HasComponentUVE<Scene::VisibilityComponentUVE>(entity)) {
            const auto& vis = entityManager_.GetComponentUVE<Scene::VisibilityComponentUVE>(entity);
            if (!vis.visible || !vis.visibleInHierarchy) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] CachedTexture2DUVE ResolveTexture2DUVE(const Asset::AssetGuidUVE guid) {
        if (guid == Asset::kInvalidAssetGuidUVE) {
            return CachedTexture2DUVE{};
        }
        if (const auto it = textureCache_.find(guid); it != textureCache_.end() && it->second.attempted) {
            return it->second;
        }
        CachedTexture2DUVE entry{};
        entry.attempted = true;

        const std::filesystem::path registeredPath =
            engine_.GetServicesUVE().GetAssetDatabaseUVE().ResolveUVE(guid);
        if (registeredPath.empty()) {
            textureCache_[guid] = entry;
            return entry;
        }
        std::filesystem::path resolvedPath = registeredPath;
        std::error_code ec;
        if (!std::filesystem::exists(resolvedPath, ec)) {
            const std::filesystem::path contentCandidate =
                engine_.GetServicesUVE().GetFileSystemUVE().ResolveRealPathUVE(
                    "content://" + registeredPath.generic_string());
            if (!contentCandidate.empty() && std::filesystem::exists(contentCandidate, ec)) {
                resolvedPath = contentCandidate;
            }
        }

        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        std::vector<std::byte> rgbaPixels;

        Asset::TextureAssetUVE texAsset{};
        if (Asset::LoadTextureAssetUVE(resolvedPath, texAsset) &&
            texAsset.format == Asset::TextureAssetFormatUVE::RGBA8Unorm &&
            texAsset.width > 0U && texAsset.height > 0U &&
            texAsset.pixels.size() == static_cast<std::size_t>(texAsset.width) * static_cast<std::size_t>(texAsset.height) * 4U) {
            width = texAsset.width;
            height = texAsset.height;
            rgbaPixels = std::move(texAsset.pixels);
        } else {
            std::vector<std::byte> rawBytes;
            if (const auto vfsBytes = engine_.GetServicesUVE().GetFileSystemUVE().ReadFileUVE(
                    "content://" + registeredPath.generic_string());
                vfsBytes.has_value()) {
                rawBytes = *vfsBytes;
            } else {
                std::ifstream file(resolvedPath, std::ios::binary | std::ios::ate);
                if (file.is_open()) {
                    const std::streamsize sz = file.tellg();
                    if (sz > 0) {
                        rawBytes.resize(static_cast<std::size_t>(sz));
                        file.seekg(0, std::ios::beg);
                        file.read(reinterpret_cast<char*>(rawBytes.data()), sz);
                    }
                }
            }
            if (!rawBytes.empty()) {
                Asset::PngRgba8ImageUVE pngImg{};
                Asset::JpegRgba8ImageUVE jpgImg{};
                Asset::BmpRgba8ImageUVE bmpImg{};
                Asset::TgaRgba8ImageUVE tgaImg{};
                if (Asset::DecodePngRgba8ImageUVE(rawBytes, pngImg)) {
                    width = pngImg.width;
                    height = pngImg.height;
                    rgbaPixels = std::move(pngImg.pixels);
                } else if (Asset::DecodeJpegRgba8ImageUVE(rawBytes, jpgImg)) {
                    width = jpgImg.width;
                    height = jpgImg.height;
                    rgbaPixels = std::move(jpgImg.pixels);
                } else if (Asset::DecodeBmpRgba8ImageUVE(rawBytes, bmpImg)) {
                    width = bmpImg.width;
                    height = bmpImg.height;
                    rgbaPixels = std::move(bmpImg.pixels);
                } else if (Asset::DecodeTgaRgba8ImageUVE(rawBytes, tgaImg)) {
                    width = tgaImg.width;
                    height = tgaImg.height;
                    rgbaPixels = std::move(tgaImg.pixels);
                }
            }
        }

        if (width > 0U && height > 0U && !rgbaPixels.empty()) {
            glGenTextures(1, &entry.glTexId);
            glBindTexture(GL_TEXTURE_2D, entry.glTexId);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                         static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                         0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0U);
            entry.width = static_cast<float>(width);
            entry.height = static_cast<float>(height);
        }

        textureCache_[guid] = entry;
        return entry;
    }

    void SyncOverlaySettingsUVE(const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
        gameWorkspaceActive_ = overlayState.gameWorkspaceActive;
        showCollisionGizmos_ = overlayState.viewport2DShowCollision && !overlayState.gameWorkspaceActive;

        auto& st = viewport_.style();
        st.showGrid = overlayState.gridVisible && !overlayState.gameWorkspaceActive;
        st.showAxes = overlayState.gridVisible && !overlayState.gameWorkspaceActive;
        st.showRulers = overlayState.viewport2DShowRulers && !overlayState.gameWorkspaceActive;
        st.showGuides = overlayState.viewport2DShowGuides && !overlayState.gameWorkspaceActive;
        st.showHud = overlayState.viewport2DShowHud && !overlayState.gameWorkspaceActive;
        st.gridMinor.a = static_cast<std::uint8_t>(std::lround(18.0F * std::clamp(overlayState.gridOpacity, 0.1F, 1.0F)));
        st.gridMajor.a = static_cast<std::uint8_t>(std::lround(44.0F * std::clamp(overlayState.gridOpacity, 0.1F, 1.0F)));
        if (overlayState.axisColorsValid) {
            st.axisX = univex::viewport::Color{FloatToByteUVE(overlayState.axisColorX.r),
                                               FloatToByteUVE(overlayState.axisColorX.g),
                                               FloatToByteUVE(overlayState.axisColorX.b), 210U};
            st.axisY = univex::viewport::Color{FloatToByteUVE(overlayState.axisColorY.r),
                                               FloatToByteUVE(overlayState.axisColorY.g),
                                               FloatToByteUVE(overlayState.axisColorY.b), 210U};
        }

        viewport_.setTool(overlayState.viewport2DPanTool ? univex::viewport::Tool::Hand
                                                         : univex::viewport::Tool::Select);
        viewport_.setPivotEditing(overlayState.viewport2DPivotEditing);

        auto& sn = viewport_.snap();
        sn.pixel = overlayState.viewport2DPixelSnap;
        sn.grid = overlayState.viewport2DGridSnap || overlayState.snapEnabled;
        sn.gridStep = std::max(1.0F, overlayState.viewport2DGridStep);
        sn.guides = overlayState.viewport2DGuideSnap;
        sn.rotation = overlayState.viewport2DRotationSnap;
        sn.rotationStepDeg = std::max(1.0F, overlayState.viewport2DRotationStepDeg);

        if (overlayState.viewport2DZoom100Serial != appliedZoom100Serial_) {
            appliedZoom100Serial_ = overlayState.viewport2DZoom100Serial;
            viewport_.zoomTo100(true);
        }
        if (overlayState.viewport2DZoomFitSerial != appliedZoomFitSerial_) {
            appliedZoomFitSerial_ = overlayState.viewport2DZoomFitSerial;
            viewport_.zoomToFit(univex::viewport::Rect{{0.0F, 0.0F}, {1280.0F, 720.0F}}, true);
        }
        if (overlayState.viewport2DClearGuidesSerial != appliedClearGuidesSerial_) {
            appliedClearGuidesSerial_ = overlayState.viewport2DClearGuidesSerial;
            viewport_.guides().horizontal.clear();
            viewport_.guides().vertical.clear();
        }
        if (overlayState.focusRequestSerial != appliedFocusSerial_) {
            appliedFocusSerial_ = overlayState.focusRequestSerial;
            if (overlayState.focusEntity != Scene::kInvalidEntityUVE &&
                entityManager_.IsAliveUVE(overlayState.focusEntity)) {
                const Pose2DUVE pose = ResolveEntityPose2DUVE(overlayState.focusEntity);
                viewport_.focusOn(pose.position, true);
            }
        }
    }

    [[nodiscard]] bool HasAny2DComponentUVE(const Scene::EntityUVE entity) const {
        return entityManager_.HasComponentUVE<Scene::Sprite2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::AnimatedSprite2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::Polygon2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::Line2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::TileMapLayerComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::MeshInstance2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::CollisionShape2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::CollisionPolygon2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::Area2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::PhysicsBody2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::RayCast2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::ShapeCast2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::Camera2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::Light2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::LightOccluder2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::Marker2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::Path2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::PathFollow2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::Bone2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::Skeleton2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::NavigationRegion2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::NavigationAgent2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::NavigationObstacle2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::NavigationLink2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::Particles2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::TouchScreenButtonComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::VisibleOnScreenNotifier2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::AudioListener2DComponentUVE>(entity) ||
               entityManager_.HasComponentUVE<Scene::AudioStreamPlayer2DComponentUVE>(entity);
    }

    [[nodiscard]] univex::viewport::Vec2 ResolveEntityBaseSize2DUVE(const Scene::EntityUVE entity) {
        if (entityManager_.HasComponentUVE<Scene::Sprite2DComponentUVE>(entity)) {
            const auto& sp = entityManager_.GetComponentUVE<Scene::Sprite2DComponentUVE>(entity);
            const CachedTexture2DUVE tex = ResolveTexture2DUVE(sp.textureAssetGuid);
            const Math::Vector2UVE ext = Scene::Sprite2DUVE::ResolveDrawExtentPixelsUVE(
                sp, Math::Vector2UVE{tex.width, tex.height});
            return univex::viewport::Vec2{std::max(1.0F, ext.x), std::max(1.0F, ext.y)};
        }
        if (entityManager_.HasComponentUVE<Scene::AnimatedSprite2DComponentUVE>(entity)) {
            const auto& asp = entityManager_.GetComponentUVE<Scene::AnimatedSprite2DComponentUVE>(entity);
            const CachedTexture2DUVE tex = ResolveTexture2DUVE(asp.textureAssetGuid);
            const float fw = tex.width / static_cast<float>(std::max(1U, asp.hframes));
            const float fh = tex.height / static_cast<float>(std::max(1U, asp.vframes));
            return univex::viewport::Vec2{std::max(1.0F, fw), std::max(1.0F, fh)};
        }
        if (entityManager_.HasComponentUVE<Scene::CollisionShape2DComponentUVE>(entity)) {
            const auto& cs = entityManager_.GetComponentUVE<Scene::CollisionShape2DComponentUVE>(entity);
            if (cs.shapeKind == Scene::CollisionShape2DKindUVE::Circle) {
                const float d = std::max(4.0F, cs.radius * 2.0F);
                return univex::viewport::Vec2{d, d};
            }
            return univex::viewport::Vec2{std::max(4.0F, cs.halfExtents.x * 2.0F),
                                          std::max(4.0F, cs.halfExtents.y * 2.0F)};
        }
        if (entityManager_.HasComponentUVE<Scene::Area2DComponentUVE>(entity)) {
            const auto& ar = entityManager_.GetComponentUVE<Scene::Area2DComponentUVE>(entity);
            return univex::viewport::Vec2{std::max(4.0F, ar.halfExtents.x * 2.0F),
                                          std::max(4.0F, ar.halfExtents.y * 2.0F)};
        }
        if (entityManager_.HasComponentUVE<Scene::Polygon2DComponentUVE>(entity)) {
            const auto& poly = entityManager_.GetComponentUVE<Scene::Polygon2DComponentUVE>(entity);
            if (!poly.vertices.empty()) {
                float minX = poly.vertices[0].x;
                float maxX = poly.vertices[0].x;
                float minY = poly.vertices[0].y;
                float maxY = poly.vertices[0].y;
                for (const auto& v : poly.vertices) {
                    minX = std::min(minX, v.x);
                    maxX = std::max(maxX, v.x);
                    minY = std::min(minY, v.y);
                    maxY = std::max(maxY, v.y);
                }
                return univex::viewport::Vec2{std::max(8.0F, maxX - minX), std::max(8.0F, maxY - minY)};
            }
        }
        if (entityManager_.HasComponentUVE<Scene::Line2DComponentUVE>(entity)) {
            const Math::RectUVE r = Scene::Line2DUVE::ComputeLocalBoundingRectUVE(
                entityManager_.GetComponentUVE<Scene::Line2DComponentUVE>(entity));
            if (r.size.x > 0.0F && r.size.y > 0.0F) {
                return univex::viewport::Vec2{std::max(8.0F, r.size.x), std::max(8.0F, r.size.y)};
            }
        }
        if (entityManager_.HasComponentUVE<Scene::TouchScreenButtonComponentUVE>(entity)) {
            const auto& btn = entityManager_.GetComponentUVE<Scene::TouchScreenButtonComponentUVE>(entity);
            return univex::viewport::Vec2{std::max(8.0F, btn.bounds.size.x),
                                          std::max(8.0F, btn.bounds.size.y)};
        }
        if (entityManager_.HasComponentUVE<Scene::VisibleOnScreenNotifier2DComponentUVE>(entity)) {
            const auto& vn = entityManager_.GetComponentUVE<Scene::VisibleOnScreenNotifier2DComponentUVE>(entity);
            return univex::viewport::Vec2{std::max(8.0F, vn.localRect.size.x),
                                          std::max(8.0F, vn.localRect.size.y)};
        }
        return univex::viewport::Vec2{48.0F, 48.0F};
    }

    [[nodiscard]] univex::viewport::Box BuildEntityBox2DUVE(const Scene::EntityUVE entity) {
        const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
        const univex::viewport::Vec2 baseSize = ResolveEntityBaseSize2DUVE(entity);
        univex::viewport::Box box{};
        box.size = univex::viewport::Vec2{
            std::max(2.0F, baseSize.x * std::fabs(pose.scale.x)),
            std::max(2.0F, baseSize.y * std::fabs(pose.scale.y))};
        box.rotation = pose.rotationRadians;
        if (entityManager_.HasComponentUVE<Scene::Sprite2DComponentUVE>(entity)) {
            const auto& sp = entityManager_.GetComponentUVE<Scene::Sprite2DComponentUVE>(entity);
            const univex::viewport::Vec2 scaledOffset{sp.offset.x * pose.scale.x,
                                                      sp.offset.y * pose.scale.y};
            box.center = pose.position + RotateVec2UVE(scaledOffset, pose.rotationRadians);
            box.pivotOffset = univex::viewport::Vec2{-scaledOffset.x, -scaledOffset.y};
        } else {
            box.center = pose.position;
            box.pivotOffset = univex::viewport::Vec2{0.0F, 0.0F};
        }
        return box;
    }

    void SyncSelectionToGizmoUVE() {
        if (viewport_.gizmo().dragging()) {
            return;
        }
        if (gameWorkspaceActive_) {
            viewport_.clearSelection();
            return;
        }
        const Scene::EntityUVE selected = editor_.GetSelectedEntityUVE();
        if (selected == Scene::kInvalidEntityUVE || !entityManager_.IsAliveUVE(selected) ||
            !entityManager_.HasComponentUVE<Scene::TransformComponentUVE>(selected)) {
            viewport_.clearSelection();
            return;
        }
        viewport_.setSelectionBox(BuildEntityBox2DUVE(selected));
    }

    void PumpImGuiInputUVE(const int width, const int height, const bool pointerOverOverlay) {
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 imageOrigin = ImGui::GetCursorScreenPos();
        const univex::viewport::Vec2 localMouse{io.MousePos.x - imageOrigin.x,
                                                io.MousePos.y - imageOrigin.y};
        const bool hovered = ImGui::IsWindowHovered() && !pointerOverOverlay &&
                             localMouse.x >= 0.0F && localMouse.y >= 0.0F &&
                             localMouse.x <= static_cast<float>(width) &&
                             localMouse.y <= static_cast<float>(height);

        viewport_.setModifiers(univex::viewport::Modifiers{io.KeyShift, io.KeyCtrl, io.KeyAlt});
        viewport_.setPanModifier(ImGui::IsKeyDown(ImGuiKey_Space));

        if (hovered || mouseButtonDown_[0] || mouseButtonDown_[1] || mouseButtonDown_[2]) {
            viewport_.onMouseMove(localMouse);
        }

        const auto pumpButton = [&](const int idx, const ImGuiMouseButton imguiBtn,
                                    const univex::viewport::MouseButton vpBtn) {
            if (hovered && ImGui::IsMouseClicked(imguiBtn)) {
                mouseButtonDown_[static_cast<std::size_t>(idx)] = true;
                viewport_.onMouseDown(vpBtn, localMouse);
            }
            if (mouseButtonDown_[static_cast<std::size_t>(idx)] && ImGui::IsMouseReleased(imguiBtn)) {
                mouseButtonDown_[static_cast<std::size_t>(idx)] = false;
                viewport_.onMouseUp(vpBtn, localMouse);
            }
        };
        pumpButton(0, ImGuiMouseButton_Left, univex::viewport::MouseButton::Left);
        pumpButton(1, ImGuiMouseButton_Middle, univex::viewport::MouseButton::Middle);
        // Right-button drag also pans smoothly in 2D
        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 2.0F)) {
            viewport_.onScrollPan(univex::viewport::Vec2{io.MouseDelta.x, io.MouseDelta.y});
        }

        if (hovered && io.MouseWheel != 0.0F) {
            viewport_.onWheel(io.MouseWheel, localMouse);
        }

        if (hovered && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                const Scene::EntityUVE sel = editor_.GetSelectedEntityUVE();
                if (sel != Scene::kInvalidEntityUVE && entityManager_.IsAliveUVE(sel)) {
                    viewport_.focusOn(ResolveEntityPose2DUVE(sel).position, true);
                } else {
                    viewport_.zoomToFit(univex::viewport::Rect{{0.0F, 0.0F}, {1280.0F, 720.0F}}, true);
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
                viewport_.zoomToFit(univex::viewport::Rect{{0.0F, 0.0F}, {1280.0F, 720.0F}}, true);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_1, false) && io.KeyAlt) {
                viewport_.zoomTo100(true);
            }
        }

        if (hovered) {
            using univex::viewport::CursorHint;
            switch (viewport_.cursorHint()) {
                case CursorHint::Pan:
                case CursorHint::MoveAll:
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
                    break;
                case CursorHint::ResizeNS:
                case CursorHint::GuideH:
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                    break;
                case CursorHint::ResizeEW:
                case CursorHint::GuideV:
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    break;
                case CursorHint::ResizeNWSE:
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                    break;
                case CursorHint::ResizeNESW:
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
                    break;
                case CursorHint::Rotate:
                case CursorHint::Crosshair:
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    break;
                case CursorHint::Default:
                    break;
            }
        }
    }

    void HandlePick2DUVE(const univex::viewport::Vec2 worldPt, const bool additive) {
        Scene::EntityUVE bestEntity = Scene::kInvalidEntityUVE;
        float bestArea = 1.0e30F;

        entityManager_.ForEachUVE<Scene::TransformComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::TransformComponentUVE&) {
                if (!IsEntityVisibleIn2DUVE(entity) || !HasAny2DComponentUVE(entity)) {
                    return;
                }
                const univex::viewport::Box box = BuildEntityBox2DUVE(entity);
                const univex::viewport::Vec2 local = box.worldToLocal(worldPt);
                const float hx = std::max(8.0F, box.size.x * 0.5F);
                const float hy = std::max(8.0F, box.size.y * 0.5F);
                if (std::fabs(local.x) <= hx && std::fabs(local.y) <= hy) {
                    const float area = hx * hy;
                    if (area <= bestArea) {
                        bestArea = area;
                        bestEntity = entity;
                    }
                }
            });

        if (bestEntity == Scene::kInvalidEntityUVE) {
            if (!additive) {
                editor_.ClearSelectionUVE();
            }
            return;
        }
        if (additive) {
            editor_.ToggleEntitySelectionUVE(bestEntity);
        } else {
            editor_.SelectEntityUVE(bestEntity);
        }
    }

    void HandleMarquee2DUVE(const univex::viewport::Rect& worldRect, const bool additive,
                            const univex::viewport::Phase phase) {
        if (phase != univex::viewport::Phase::End) {
            return;
        }
        if (!additive) {
            editor_.ClearSelectionUVE();
        }
        entityManager_.ForEachUVE<Scene::TransformComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::TransformComponentUVE&) {
                if (!IsEntityVisibleIn2DUVE(entity) || !HasAny2DComponentUVE(entity)) {
                    return;
                }
                const univex::viewport::Box box = BuildEntityBox2DUVE(entity);
                if (box.center.x >= worldRect.min.x && box.center.x <= worldRect.max.x &&
                    box.center.y >= worldRect.min.y && box.center.y <= worldRect.max.y) {
                    editor_.ToggleEntitySelectionUVE(entity);
                }
            });
    }

    void HandleTransform2DUVE(const univex::viewport::Box& proposed,
                              const univex::viewport::Phase phase) {
        const Scene::EntityUVE selected = editor_.GetSelectedEntityUVE();
        if (selected == Scene::kInvalidEntityUVE || !entityManager_.IsAliveUVE(selected) ||
            !entityManager_.HasComponentUVE<Scene::TransformComponentUVE>(selected)) {
            return;
        }
        if (phase == univex::viewport::Phase::Begin) {
            dragEntity_ = selected;
            dragStartTransform_ = entityManager_.GetComponentUVE<Scene::TransformComponentUVE>(selected);
            dragStartBox_ = BuildEntityBox2DUVE(selected);
            return;
        }
        if (dragEntity_ != selected || !entityManager_.IsAliveUVE(dragEntity_)) {
            return;
        }

        Scene::TransformComponentUVE updated = dragStartTransform_;
        const float scaleRatioX = (dragStartBox_.size.x > 1.0e-4F)
                                      ? (proposed.size.x / dragStartBox_.size.x)
                                      : 1.0F;
        const float scaleRatioY = (dragStartBox_.size.y > 1.0e-4F)
                                      ? (proposed.size.y / dragStartBox_.size.y)
                                      : 1.0F;
        updated.localScale.x = dragStartTransform_.localScale.x * scaleRatioX;
        updated.localScale.y = dragStartTransform_.localScale.y * scaleRatioY;

        updated.rotationEditMode = Scene::RotationEditModeUVE::Euler;
        updated.localEulerRadians.z = proposed.rotation;
        static_cast<void>(Scene::TrySyncRotationFromEulerUVE(updated));

        if (entityManager_.HasComponentUVE<Scene::Sprite2DComponentUVE>(selected)) {
            auto& sp = entityManager_.GetComponentUVE<Scene::Sprite2DComponentUVE>(selected);
            if (std::fabs(updated.localScale.x) > 1.0e-4F && std::fabs(updated.localScale.y) > 1.0e-4F) {
                sp.offset.x = -proposed.pivotOffset.x / updated.localScale.x;
                sp.offset.y = -proposed.pivotOffset.y / updated.localScale.y;
            }
            const univex::viewport::Vec2 scaledOffset{sp.offset.x * updated.localScale.x,
                                                      sp.offset.y * updated.localScale.y};
            const univex::viewport::Vec2 nodeOrigin =
                proposed.center - RotateVec2UVE(scaledOffset, proposed.rotation);
            updated.localPosition.x = nodeOrigin.x;
            updated.localPosition.y = nodeOrigin.y;
        } else {
            updated.localPosition.x = proposed.center.x;
            updated.localPosition.y = proposed.center.y;
        }

        if (phase == univex::viewport::Phase::End) {
            // Restore start transform briefly so SetSelectedLocalTransformUVE records a single clean undo command
            engine_.GetServicesUVE().GetSceneGraphUVE().SetLocalTransformUVE(
                entityManager_, selected, dragStartTransform_);
            static_cast<void>(editor_.SetSelectedLocalTransformUVE(updated));
            dragEntity_ = Scene::kInvalidEntityUVE;
        } else {
            engine_.GetServicesUVE().GetSceneGraphUVE().SetLocalTransformUVE(
                entityManager_, selected, updated);
        }
    }

    void DrawScene2DUVE(univex::viewport::IViewportRenderer& r,
                        const univex::viewport::Camera2D& cam) {
        const Scene::CanvasModulateComponentUVE canvasMod =
            Scene::ResolveActiveCanvasModulateUVE(entityManager_);

        // 1. TileMapLayer entities
        entityManager_.ForEachUVE<Scene::TileMapLayerComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::TileMapLayerComponentUVE& layer) {
                if (!layer.enabled || !IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const float tw = std::max(1.0F, layer.tileSizePixels.x);
                const float th = std::max(1.0F, layer.tileSizePixels.y);
                for (const Scene::TileMapCell2DUVE& cell : layer.cells) {
                    if (cell.tileId == Scene::kTileMapInvalidTileUVE) {
                        continue;
                    }
                    const Math::Vector2UVE lp = Scene::TileMapLayerUVE::MapToLocalPositionUVE(layer, cell.coord);
                    const univex::viewport::Vec2 p0 = cam.worldToScreen(TransformPoint2DUVE(pose, {lp.x, lp.y}));
                    const univex::viewport::Vec2 p1 = cam.worldToScreen(TransformPoint2DUVE(pose, {lp.x + tw, lp.y}));
                    const univex::viewport::Vec2 p2 = cam.worldToScreen(TransformPoint2DUVE(pose, {lp.x + tw, lp.y + th}));
                    const univex::viewport::Vec2 p3 = cam.worldToScreen(TransformPoint2DUVE(pose, {lp.x, lp.y + th}));
                    const unsigned int hue =
                        80U + (static_cast<unsigned int>(std::abs(cell.tileId)) * 37U) % 120U;
                    const univex::viewport::Color fillCol{
                        static_cast<std::uint8_t>(
                            (hue * static_cast<unsigned int>(FloatToByteUVE(canvasMod.color.r))) / 255U),
                        static_cast<std::uint8_t>(
                            ((hue + 30U) * static_cast<unsigned int>(FloatToByteUVE(canvasMod.color.g))) / 255U),
                        static_cast<std::uint8_t>(
                            ((hue + 55U) * static_cast<unsigned int>(FloatToByteUVE(canvasMod.color.b))) / 255U),
                        FloatToByteUVE(0.85F * canvasMod.opacity)};
                    r.fillQuad(p0, p1, p2, p3, fillCol);
                    const std::array<univex::viewport::Vec2, 4> corners{p0, p1, p2, p3};
                    univex::viewport::strokePolygon(r, corners.data(), 4,
                                                    univex::viewport::Color{220U, 235U, 255U, 95U}, 1.0F);
                }
            });

        // 2. Polygon2D entities
        entityManager_.ForEachUVE<Scene::Polygon2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::Polygon2DComponentUVE& poly) {
                if (!IsEntityVisibleIn2DUVE(entity) || poly.vertices.size() < 3U) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                std::vector<univex::viewport::Vec2> screenPts;
                screenPts.reserve(poly.vertices.size());
                for (const Math::Vector2UVE& v : poly.vertices) {
                    screenPts.push_back(cam.worldToScreen(TransformPoint2DUVE(pose, {v.x, v.y})));
                }
                const univex::viewport::Color col{
                    FloatToByteUVE(poly.color.r * canvasMod.color.r),
                    FloatToByteUVE(poly.color.g * canvasMod.color.g),
                    FloatToByteUVE(poly.color.b * canvasMod.color.b),
                    FloatToByteUVE(poly.opacity * canvasMod.opacity)};
                for (std::size_t i = 1U; i + 1U < screenPts.size(); ++i) {
                    r.fillQuad(screenPts[0], screenPts[i], screenPts[i + 1U], screenPts[i + 1U], col);
                }
                univex::viewport::strokePolygon(r, screenPts.data(), static_cast<int>(screenPts.size()),
                                                univex::viewport::withAlpha(col, 1.0F), 1.5F);
            });

        // 3. Sprite2D entities
        entityManager_.ForEachUVE<Scene::Sprite2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::Sprite2DComponentUVE& sprite) {
                if (!IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const CachedTexture2DUVE tex = ResolveTexture2DUVE(sprite.textureAssetGuid);
                const std::optional<Scene::Sprite2DQuadUVE> quadOpt =
                    Scene::Sprite2DUVE::ComposeQuadUVE(sprite, Math::Vector2UVE{tex.width, tex.height});
                if (!quadOpt.has_value()) {
                    return;
                }
                const auto& q = *quadOpt;
                const univex::viewport::Vec2 s0 = cam.worldToScreen(TransformPoint2DUVE(pose, {q.corners[0].x, q.corners[0].y}));
                const univex::viewport::Vec2 s1 = cam.worldToScreen(TransformPoint2DUVE(pose, {q.corners[1].x, q.corners[1].y}));
                const univex::viewport::Vec2 s2 = cam.worldToScreen(TransformPoint2DUVE(pose, {q.corners[2].x, q.corners[2].y}));
                const univex::viewport::Vec2 s3 = cam.worldToScreen(TransformPoint2DUVE(pose, {q.corners[3].x, q.corners[3].y}));
                const univex::viewport::Color tint{
                    FloatToByteUVE(sprite.modulate.x * canvasMod.color.r),
                    FloatToByteUVE(sprite.modulate.y * canvasMod.color.g),
                    FloatToByteUVE(sprite.modulate.z * canvasMod.color.b),
                    FloatToByteUVE(sprite.alpha * canvasMod.opacity)};
                renderer_.fillTexturedQuadUVE(
                    s0, s1, s2, s3,
                    univex::viewport::Vec2{q.uvs[0].x, q.uvs[0].y},
                    univex::viewport::Vec2{q.uvs[1].x, q.uvs[1].y},
                    univex::viewport::Vec2{q.uvs[2].x, q.uvs[2].y},
                    univex::viewport::Vec2{q.uvs[3].x, q.uvs[3].y},
                    tex.glTexId, tint);
            });

        // 4. AnimatedSprite2D entities
        entityManager_.ForEachUVE<Scene::AnimatedSprite2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::AnimatedSprite2DComponentUVE& anim) {
                if (!IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const CachedTexture2DUVE tex = ResolveTexture2DUVE(anim.textureAssetGuid);
                const std::uint32_t hf = std::max(1U, anim.hframes);
                const std::uint32_t vf = std::max(1U, anim.vframes);
                const std::uint32_t total = hf * vf;
                const std::uint32_t f = anim.frame % total;
                const std::uint32_t colIdx = f % hf;
                const std::uint32_t rowIdx = f / hf;
                const float fw = tex.width / static_cast<float>(hf);
                const float fh = tex.height / static_cast<float>(vf);
                const float hx = fw * 0.5F;
                const float hy = fh * 0.5F;
                const float u0 = static_cast<float>(colIdx) / static_cast<float>(hf);
                const float u1 = static_cast<float>(colIdx + 1U) / static_cast<float>(hf);
                const float v0 = static_cast<float>(rowIdx) / static_cast<float>(vf);
                const float v1 = static_cast<float>(rowIdx + 1U) / static_cast<float>(vf);

                const univex::viewport::Vec2 s0 = cam.worldToScreen(TransformPoint2DUVE(pose, {-hx, -hy}));
                const univex::viewport::Vec2 s1 = cam.worldToScreen(TransformPoint2DUVE(pose, {hx, -hy}));
                const univex::viewport::Vec2 s2 = cam.worldToScreen(TransformPoint2DUVE(pose, {hx, hy}));
                const univex::viewport::Vec2 s3 = cam.worldToScreen(TransformPoint2DUVE(pose, {-hx, hy}));
                const univex::viewport::Color tint{
                    FloatToByteUVE(canvasMod.color.r),
                    FloatToByteUVE(canvasMod.color.g),
                    FloatToByteUVE(canvasMod.color.b),
                    FloatToByteUVE(canvasMod.opacity)};
                renderer_.fillTexturedQuadUVE(
                    s0, s1, s2, s3,
                    univex::viewport::Vec2{u0, v0},
                    univex::viewport::Vec2{u1, v0},
                    univex::viewport::Vec2{u1, v1},
                    univex::viewport::Vec2{u0, v1},
                    tex.glTexId, tint);
            });

        // 5. MeshInstance2D & MultiMeshInstance2D entities
        entityManager_.ForEachUVE<Scene::MeshInstance2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::MeshInstance2DComponentUVE& mesh2d) {
                if (!mesh2d.enabled || !IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const CachedTexture2DUVE tex = ResolveTexture2DUVE(mesh2d.textureAssetGuid);
                const float hx = tex.width * 0.5F;
                const float hy = tex.height * 0.5F;
                const univex::viewport::Color tint{
                    FloatToByteUVE(canvasMod.color.r),
                    FloatToByteUVE(canvasMod.color.g),
                    FloatToByteUVE(canvasMod.color.b),
                    FloatToByteUVE(canvasMod.opacity)};
                const auto drawInstance = [&](const univex::viewport::Vec2 instOffset) {
                    const univex::viewport::Vec2 s0 = cam.worldToScreen(TransformPoint2DUVE(pose, {instOffset.x - hx, instOffset.y - hy}));
                    const univex::viewport::Vec2 s1 = cam.worldToScreen(TransformPoint2DUVE(pose, {instOffset.x + hx, instOffset.y - hy}));
                    const univex::viewport::Vec2 s2 = cam.worldToScreen(TransformPoint2DUVE(pose, {instOffset.x + hx, instOffset.y + hy}));
                    const univex::viewport::Vec2 s3 = cam.worldToScreen(TransformPoint2DUVE(pose, {instOffset.x - hx, instOffset.y + hy}));
                    renderer_.fillTexturedQuadUVE(
                        s0, s1, s2, s3,
                        univex::viewport::Vec2{0.0F, 0.0F},
                        univex::viewport::Vec2{1.0F, 0.0F},
                        univex::viewport::Vec2{1.0F, 1.0F},
                        univex::viewport::Vec2{0.0F, 1.0F},
                        tex.glTexId, tint);
                };
                if (mesh2d.instanced && !mesh2d.instanceOffsets2D.empty()) {
                    for (const Math::Vector2UVE& off : mesh2d.instanceOffsets2D) {
                        drawInstance(univex::viewport::Vec2{off.x, off.y});
                    }
                } else {
                    drawInstance(univex::viewport::Vec2{0.0F, 0.0F});
                }
            });

        // 6. Line2D entities
        entityManager_.ForEachUVE<Scene::Line2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::Line2DComponentUVE& line) {
                if (!IsEntityVisibleIn2DUVE(entity) || line.points.size() < 2U) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const univex::viewport::Color col{
                    FloatToByteUVE(line.defaultColor.r * canvasMod.color.r),
                    FloatToByteUVE(line.defaultColor.g * canvasMod.color.g),
                    FloatToByteUVE(line.defaultColor.b * canvasMod.color.b),
                    FloatToByteUVE(line.opacity * canvasMod.opacity)};
                const float thickness = std::max(1.0F, line.widthPixels * cam.zoom());
                for (std::size_t i = 0U; i + 1U < line.points.size(); ++i) {
                    const univex::viewport::Vec2 a = cam.worldToScreen(
                        TransformPoint2DUVE(pose, {line.points[i].x, line.points[i].y}));
                    const univex::viewport::Vec2 b = cam.worldToScreen(
                        TransformPoint2DUVE(pose, {line.points[i + 1U].x, line.points[i + 1U].y}));
                    r.drawLine(a, b, col, thickness);
                }
                if (line.closed && line.points.size() >= 3U) {
                    const univex::viewport::Vec2 a = cam.worldToScreen(
                        TransformPoint2DUVE(pose, {line.points.back().x, line.points.back().y}));
                    const univex::viewport::Vec2 b = cam.worldToScreen(
                        TransformPoint2DUVE(pose, {line.points.front().x, line.points.front().y}));
                    r.drawLine(a, b, col, thickness);
                }
            });

        if (gameWorkspaceActive_ || !showCollisionGizmos_) {
            return;
        }

        // 7. Editor Authoring Overlays & Gizmos (Camera2D, Colliders, Areas, RayCasts, Lights, Paths, Bones, Nav, Markers)
        DrawAuthoringGizmos2DUVE(r, cam);
    }

    void DrawAuthoringGizmos2DUVE(univex::viewport::IViewportRenderer& r,
                                  const univex::viewport::Camera2D& cam) {
        // Camera2D viewport frames
        entityManager_.ForEachUVE<Scene::Camera2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::Camera2DComponentUVE& cam2d) {
                if (!IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const Math::RectUVE worldRect = Scene::ResolveCamera2DVisibleWorldRectUVE(
                    cam2d, Math::Vector2UVE{pose.position.x, pose.position.y},
                    Math::Vector2UVE{1280.0F, 720.0F});
                const univex::viewport::Vec2 sMin = cam.worldToScreen(
                    univex::viewport::Vec2{worldRect.position.x, worldRect.position.y});
                const univex::viewport::Vec2 sMax = cam.worldToScreen(
                    univex::viewport::Vec2{worldRect.position.x + worldRect.size.x,
                                           worldRect.position.y + worldRect.size.y});
                const univex::viewport::Color frameCol =
                    cam2d.current ? univex::viewport::Color{195U, 125U, 255U, 230U}
                                  : univex::viewport::Color{140U, 110U, 200U, 165U};
                univex::viewport::strokeRect(r, sMin, sMax, frameCol, cam2d.current ? 2.0F : 1.2F);
                r.drawText(univex::viewport::Vec2{std::min(sMin.x, sMax.x) + 6.0F,
                                                  std::min(sMin.y, sMax.y) + 6.0F},
                           cam2d.current ? "Camera2D (Current)" : "Camera2D",
                           frameCol, 7.0F, univex::viewport::TextOrient::Horizontal);
            });

        // CollisionShape2D wireframes (Circle, Rectangle, Capsule)
        constexpr univex::viewport::Color kColliderStroke{0U, 205U, 255U, 220U};
        constexpr univex::viewport::Color kColliderFill{0U, 180U, 240U, 36U};
        entityManager_.ForEachUVE<Scene::CollisionShape2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::CollisionShape2DComponentUVE& shape) {
                if (!shape.enabled || !IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                if (shape.shapeKind == Scene::CollisionShape2DKindUVE::Circle) {
                    const univex::viewport::Vec2 centerScreen = cam.worldToScreen(pose.position);
                    const float radiusPx =
                        std::max(1.0F, shape.radius * std::max(std::fabs(pose.scale.x), std::fabs(pose.scale.y)) * cam.zoom());
                    univex::viewport::strokeCircle(r, centerScreen, radiusPx, kColliderStroke, 1.5F, 32);
                } else {
                    const float hx = shape.halfExtents.x;
                    const float hy = shape.halfExtents.y;
                    const std::array<univex::viewport::Vec2, 4> corners{
                        cam.worldToScreen(TransformPoint2DUVE(pose, {-hx, -hy})),
                        cam.worldToScreen(TransformPoint2DUVE(pose, {hx, -hy})),
                        cam.worldToScreen(TransformPoint2DUVE(pose, {hx, hy})),
                        cam.worldToScreen(TransformPoint2DUVE(pose, {-hx, hy}))};
                    r.fillQuad(corners[0], corners[1], corners[2], corners[3], kColliderFill);
                    univex::viewport::strokePolygon(r, corners.data(), 4, kColliderStroke, 1.5F);
                }
            });

        // CollisionPolygon2D wireframes
        entityManager_.ForEachUVE<Scene::CollisionPolygon2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::CollisionPolygon2DComponentUVE& poly) {
                if (!poly.enabled || !IsEntityVisibleIn2DUVE(entity) || poly.vertices.size() < 3U) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                std::vector<univex::viewport::Vec2> pts;
                pts.reserve(poly.vertices.size());
                for (const Math::Vector2UVE& v : poly.vertices) {
                    pts.push_back(cam.worldToScreen(TransformPoint2DUVE(pose, {v.x, v.y})));
                }
                univex::viewport::strokePolygon(r, pts.data(), static_cast<int>(pts.size()), kColliderStroke, 1.5F);
            });

        // Area2D monitoring bounds
        constexpr univex::viewport::Color kAreaStroke{90U, 230U, 155U, 210U};
        constexpr univex::viewport::Color kAreaFill{90U, 230U, 155U, 28U};
        entityManager_.ForEachUVE<Scene::Area2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::Area2DComponentUVE& area) {
                if (!IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const float hx = area.halfExtents.x;
                const float hy = area.halfExtents.y;
                const std::array<univex::viewport::Vec2, 4> corners{
                    cam.worldToScreen(TransformPoint2DUVE(pose, {-hx, -hy})),
                    cam.worldToScreen(TransformPoint2DUVE(pose, {hx, -hy})),
                    cam.worldToScreen(TransformPoint2DUVE(pose, {hx, hy})),
                    cam.worldToScreen(TransformPoint2DUVE(pose, {-hx, hy}))};
                r.fillQuad(corners[0], corners[1], corners[2], corners[3], kAreaFill);
                univex::viewport::strokePolygon(r, corners.data(), 4, kAreaStroke, 1.5F);
            });

        // RayCast2D & ShapeCast2D arrows
        constexpr univex::viewport::Color kRayColor{255U, 105U, 95U, 235U};
        entityManager_.ForEachUVE<Scene::RayCast2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::RayCast2DComponentUVE& rc) {
                if (!rc.enabled || !IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const univex::viewport::Vec2 a = cam.worldToScreen(pose.position);
                const univex::viewport::Vec2 b = cam.worldToScreen(
                    TransformPoint2DUVE(pose, {rc.targetPosition.x, rc.targetPosition.y}));
                r.drawLine(a, b, kRayColor, 2.0F);
                univex::viewport::strokeCircle(r, b, 4.0F, kRayColor, 1.5F, 12);
            });
        entityManager_.ForEachUVE<Scene::ShapeCast2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::ShapeCast2DComponentUVE& sc) {
                if (!sc.enabled || !IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const univex::viewport::Vec2 a = cam.worldToScreen(pose.position);
                const univex::viewport::Vec2 b = cam.worldToScreen(
                    TransformPoint2DUVE(pose, {sc.targetPosition.x, sc.targetPosition.y}));
                r.drawLine(a, b, kRayColor, 2.0F);
                univex::viewport::strokeCircle(r, b, std::max(3.0F, sc.radius * cam.zoom()), kRayColor, 1.5F, 24);
            });

        // Light2D & LightOccluder2D
        entityManager_.ForEachUVE<Scene::Light2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::Light2DComponentUVE& light) {
                if (!light.enabled || !IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const univex::viewport::Vec2 s = cam.worldToScreen(pose.position);
                const univex::viewport::Color col{
                    FloatToByteUVE(light.color.r),
                    FloatToByteUVE(light.color.g),
                    FloatToByteUVE(light.color.b),
                    200U};
                univex::viewport::strokeCircle(r, s, 6.0F, col, 2.0F, 16);
                if (light.kind == Scene::Light2DKindUVE::Point) {
                    univex::viewport::strokeCircle(r, s, std::max(8.0F, light.rangePixels * cam.zoom()),
                                                   univex::viewport::withAlpha(col, 0.45F), 1.2F, 36);
                } else {
                    const univex::viewport::Vec2 tip = cam.worldToScreen(
                        pose.position + univex::viewport::Vec2{light.direction.x * 64.0F, light.direction.y * 64.0F});
                    r.drawLine(s, tip, col, 2.0F);
                }
            });
        entityManager_.ForEachUVE<Scene::LightOccluder2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::LightOccluder2DComponentUVE& occ) {
                if (!occ.enabled || !IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const univex::viewport::Vec2 a = cam.worldToScreen(
                    TransformPoint2DUVE(pose, {occ.boundingRect.position.x, occ.boundingRect.position.y}));
                const univex::viewport::Vec2 b = cam.worldToScreen(
                    TransformPoint2DUVE(pose, {occ.boundingRect.position.x + occ.boundingRect.size.x,
                                               occ.boundingRect.position.y + occ.boundingRect.size.y}));
                univex::viewport::strokeRect(r, a, b, univex::viewport::Color{255U, 195U, 80U, 200U}, 1.5F);
            });

        // Marker2D crosshairs
        entityManager_.ForEachUVE<Scene::Marker2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::Marker2DComponentUVE& marker) {
                if (!marker.enabled || !IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const Math::Vector2UVE wp = Scene::Marker2DUVE::ComposeWorldPositionUVE(
                    marker, Math::Vector2UVE{pose.position.x, pose.position.y}, pose.rotationRadians);
                const univex::viewport::Vec2 s = cam.worldToScreen(univex::viewport::Vec2{wp.x, wp.y});
                const float ext = std::max(6.0F, marker.gizmoExtents * cam.zoom());
                constexpr univex::viewport::Color kMarkerCol{255U, 165U, 60U, 230U};
                r.drawLine({s.x - ext, s.y}, {s.x + ext, s.y}, kMarkerCol, 1.5F);
                r.drawLine({s.x, s.y - ext}, {s.x, s.y + ext}, kMarkerCol, 1.5F);
            });

        // Path2D curves
        entityManager_.ForEachUVE<Scene::Path2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::Path2DComponentUVE& path) {
                if (!IsEntityVisibleIn2DUVE(entity) || path.points.size() < 2U) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                constexpr univex::viewport::Color kPathCol{255U, 140U, 220U, 220U};
                for (std::size_t i = 0U; i + 1U < path.points.size(); ++i) {
                    const univex::viewport::Vec2 a = cam.worldToScreen(
                        TransformPoint2DUVE(pose, {path.points[i].x, path.points[i].y}));
                    const univex::viewport::Vec2 b = cam.worldToScreen(
                        TransformPoint2DUVE(pose, {path.points[i + 1U].x, path.points[i + 1U].y}));
                    r.drawLine(a, b, kPathCol, 1.8F);
                    r.fillRect(a - univex::viewport::Vec2{3.0F, 3.0F}, {6.0F, 6.0F}, kPathCol);
                }
                const univex::viewport::Vec2 lastPt = cam.worldToScreen(
                    TransformPoint2DUVE(pose, {path.points.back().x, path.points.back().y}));
                r.fillRect(lastPt - univex::viewport::Vec2{3.0F, 3.0F}, {6.0F, 6.0F}, kPathCol);
                if (path.closed && path.points.size() >= 3U) {
                    const univex::viewport::Vec2 firstPt = cam.worldToScreen(
                        TransformPoint2DUVE(pose, {path.points.front().x, path.points.front().y}));
                    r.drawLine(lastPt, firstPt, kPathCol, 1.8F);
                }
            });

        // Bone2D chains
        entityManager_.ForEachUVE<Scene::Bone2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::Bone2DComponentUVE& bone) {
                if (!IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const float angle = pose.rotationRadians + bone.restRotationRadians + bone.poseRotationRadians;
                const univex::viewport::Vec2 head = cam.worldToScreen(pose.position);
                const univex::viewport::Vec2 tail = cam.worldToScreen(
                    pose.position + RotateVec2UVE(univex::viewport::Vec2{bone.lengthPixels, 0.0F}, angle));
                constexpr univex::viewport::Color kBoneCol{255U, 210U, 90U, 230U};
                r.drawLine(head, tail, kBoneCol, 2.5F);
                univex::viewport::strokeCircle(r, head, 4.0F, kBoneCol, 1.5F, 12);
                univex::viewport::strokeCircle(r, tail, 3.0F, kBoneCol, 1.5F, 12);
            });

        // NavigationRegion2D, NavigationObstacle2D, NavigationLink2D
        entityManager_.ForEachUVE<Scene::NavigationRegion2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::NavigationRegion2DComponentUVE& nav) {
                if (!nav.enabled || !IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const univex::viewport::Vec2 a = cam.worldToScreen(
                    TransformPoint2DUVE(pose, {nav.bounds.position.x, nav.bounds.position.y}));
                const univex::viewport::Vec2 b = cam.worldToScreen(
                    TransformPoint2DUVE(pose, {nav.bounds.position.x + nav.bounds.size.x,
                                               nav.bounds.position.y + nav.bounds.size.y}));
                const univex::viewport::Vec2 tl{std::min(a.x, b.x), std::min(a.y, b.y)};
                r.fillRect(tl, {std::fabs(b.x - a.x), std::fabs(b.y - a.y)},
                           univex::viewport::Color{70U, 210U, 190U, 22U});
                univex::viewport::strokeRect(r, a, b, univex::viewport::Color{70U, 210U, 190U, 180U}, 1.2F);
            });
        entityManager_.ForEachUVE<Scene::NavigationObstacle2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::NavigationObstacle2DComponentUVE& obs) {
                if (!obs.enabled || !IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                univex::viewport::strokeCircle(r, cam.worldToScreen(pose.position),
                                               std::max(4.0F, obs.radiusPixels * cam.zoom()),
                                               univex::viewport::Color{255U, 145U, 65U, 210U}, 1.5F, 24);
            });
        entityManager_.ForEachUVE<Scene::NavigationLink2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::NavigationLink2DComponentUVE& link) {
                if (!link.enabled || !IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const univex::viewport::Vec2 a = cam.worldToScreen(
                    TransformPoint2DUVE(pose, {link.startPosition.x, link.startPosition.y}));
                const univex::viewport::Vec2 b = cam.worldToScreen(
                    TransformPoint2DUVE(pose, {link.endPosition.x, link.endPosition.y}));
                constexpr univex::viewport::Color kLinkCol{120U, 240U, 140U, 220U};
                r.drawLine(a, b, kLinkCol, 1.8F);
                univex::viewport::strokeCircle(r, a, 4.0F, kLinkCol, 1.5F, 12);
                univex::viewport::strokeCircle(r, b, 4.0F, kLinkCol, 1.5F, 12);
            });

        // VisibleOnScreenNotifier2D & TouchScreenButton
        entityManager_.ForEachUVE<Scene::VisibleOnScreenNotifier2DComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::VisibleOnScreenNotifier2DComponentUVE& vn) {
                if (!vn.enabled || !IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const univex::viewport::Vec2 a = cam.worldToScreen(
                    TransformPoint2DUVE(pose, {vn.localRect.position.x, vn.localRect.position.y}));
                const univex::viewport::Vec2 b = cam.worldToScreen(
                    TransformPoint2DUVE(pose, {vn.localRect.position.x + vn.localRect.size.x,
                                               vn.localRect.position.y + vn.localRect.size.y}));
                univex::viewport::strokeRect(r, a, b, univex::viewport::Color{245U, 110U, 185U, 195U}, 1.2F);
            });
        entityManager_.ForEachUVE<Scene::TouchScreenButtonComponentUVE>(
            [&](const Scene::EntityUVE entity, const Scene::TouchScreenButtonComponentUVE& btn) {
                if (!IsEntityVisibleIn2DUVE(entity)) {
                    return;
                }
                const Pose2DUVE pose = ResolveEntityPose2DUVE(entity);
                const univex::viewport::Vec2 a = cam.worldToScreen(
                    TransformPoint2DUVE(pose, {btn.bounds.position.x, btn.bounds.position.y}));
                const univex::viewport::Vec2 b = cam.worldToScreen(
                    TransformPoint2DUVE(pose, {btn.bounds.position.x + btn.bounds.size.x,
                                               btn.bounds.position.y + btn.bounds.size.y}));
                univex::viewport::strokeRect(r, a, b, univex::viewport::Color{255U, 200U, 90U, 210U}, 1.5F);
            });
    }

    Editor::EditorUVE& editor_;
    Core::EngineCoreUVE& engine_;
    Scene::IEntityManagerUVE& entityManager_;
    univex::viewport::Viewport2D viewport_;
    GlViewport2DRendererUVE renderer_;
    std::unordered_map<Asset::AssetGuidUVE, CachedTexture2DUVE> textureCache_;
    bool initialCentered_ = false;
    bool gameWorkspaceActive_ = false;
    bool showCollisionGizmos_ = true;
    std::array<bool, 3> mouseButtonDown_{false, false, false};
    std::uint32_t appliedZoom100Serial_ = 0U;
    std::uint32_t appliedZoomFitSerial_ = 0U;
    std::uint32_t appliedClearGuidesSerial_ = 0U;
    std::uint32_t appliedFocusSerial_ = 0U;
    Scene::EntityUVE dragEntity_ = Scene::kInvalidEntityUVE;
    Scene::TransformComponentUVE dragStartTransform_{};
    univex::viewport::Box dragStartBox_{};
};

} // namespace UVE::App::Viewport2DIntegration
