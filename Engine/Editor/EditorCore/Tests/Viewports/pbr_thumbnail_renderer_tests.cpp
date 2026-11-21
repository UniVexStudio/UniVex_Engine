// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include <GL/gl.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "event_system.h"
#include "material_asset.h"
#include "material_thumbnail_renderer.h"
#include "mesh_asset.h"
#include "mesh_thumbnail_renderer.h"
#include "window_manager.h"

namespace UVE::Editor {
namespace {

[[nodiscard]] Window::WindowDescUVE MakeThumbnailGlWindowDescUVE() {
    Window::WindowDescUVE desc;
    desc.title = "uve_pbr_thumbnail_renderer_tests";
    desc.width = 64;
    desc.height = 64;
    // Strict OpenGL 3.3 Core context ensures GLSL #version 330 core shaders compile without
    // relying on 4.20+ relaxations (such as runtime-initialized const variables).
    desc.glVersionMajor = 3;
    desc.glVersionMinor = 3;
    return desc;
}

[[nodiscard]] std::vector<std::uint8_t> ReadTextureRgba8UVE(const std::uintptr_t textureId, const int width,
                                                            const int height) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 0U);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textureId));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0U);
    return pixels;
}

[[nodiscard]] Asset::MeshAssetUVE BuildUnitQuadMeshUVE() {
    Asset::MeshAssetUVE mesh{};
    const auto makeVertex = [](const float x, const float y, const float z, const float u, const float v) {
        Asset::MeshVertexUVE vert{};
        vert.position = Math::Vector3UVE{x, y, z};
        vert.normal = Math::Vector3UVE{0.0F, 0.0F, 1.0F};
        vert.u = u;
        vert.v = v;
        return vert;
    };
    mesh.vertices = {
        makeVertex(-0.5F, -0.5F, 0.0F, 0.0F, 0.0F),
        makeVertex(0.5F, -0.5F, 0.0F, 1.0F, 0.0F),
        makeVertex(0.5F, 0.5F, 0.0F, 1.0F, 1.0F),
        makeVertex(-0.5F, 0.5F, 0.0F, 0.0F, 1.0F),
    };
    mesh.indices = {0U, 1U, 2U, 0U, 2U, 3U};
    mesh.surfaces = {Asset::MeshSurfaceUVE{0U, 6U, 0U}};
    mesh.localBounds = Math::AabbUVE{Math::Vector3UVE{-0.5F, -0.5F, -0.05F}, Math::Vector3UVE{0.5F, 0.5F, 0.05F}};
    return mesh;
}

TEST(PbrThumbnailRendererUVETest, MaterialAndMeshThumbnailsCompileAndRenderOnRealOpenGL33Core) {
    Events::EventSystemUVE eventSystem;
    Window::WindowManagerUVE windowManager(eventSystem, MakeThumbnailGlWindowDescUVE());
    if (!windowManager.IsValidUVE()) {
        GTEST_SKIP() << "No display available for OpenGL 3.3 thumbnail verification - skipping";
    }

    MaterialThumbnailRendererUVE materialRenderer;
    materialRenderer.InitializeUVE();

    MeshThumbnailRendererUVE meshRenderer;
    meshRenderer.InitializeUVE();

    // Seed a stale GL error before rendering to prove both thumbnail renderers drain prior errors.
    glEnable(static_cast<GLenum>(0xFFFFU));

    Asset::MaterialAssetUVE material{};
    material.albedoColor = Math::ColorUVE{0.85F, 0.35F, 0.15F};
    material.metallic = 0.4F;
    material.roughness = 0.35F;

    constexpr int kSize = 128;
    const std::uintptr_t matTex = materialRenderer.RenderThumbnailUVE(material, MaterialThumbnailTexturesUVE{}, kSize, kSize);
    ASSERT_NE(matTex, 0U) << "MaterialThumbnailRendererUVE failed to compile or render on OpenGL 3.3 Core";

    const std::vector<std::uint8_t> matPixels = ReadTextureRgba8UVE(matTex, kSize, kSize);
    const std::size_t centerIdx = (static_cast<std::size_t>(kSize / 2) * static_cast<std::size_t>(kSize) +
                                   static_cast<std::size_t>(kSize / 2)) *
                                  4U;
    EXPECT_EQ(matPixels[centerIdx + 3U], 255U);
    EXPECT_GT(static_cast<int>(matPixels[centerIdx + 0U]), 40);
    EXPECT_EQ(matPixels[3U], 0U); // transparent corner outside the sphere

    const GLuint matTexGl = static_cast<GLuint>(matTex);
    glDeleteTextures(1, &matTexGl);

    // Seed another stale error before mesh thumbnail rendering.
    glEnable(static_cast<GLenum>(0xFFFFU));

    const Asset::MeshAssetUVE quad = BuildUnitQuadMeshUVE();
    MeshThumbnailLookUVE look{};
    look.colour = {0.25F, 0.65F, 0.90F};
    look.metallic = 0.2F;
    look.roughness = 0.4F;
    const std::uintptr_t meshTex = meshRenderer.RenderThumbnailUVE(quad, kSize, kSize, {look});
    ASSERT_NE(meshTex, 0U) << "MeshThumbnailRendererUVE failed to compile or render on OpenGL 3.3 Core";

    const std::vector<std::uint8_t> meshPixels = ReadTextureRgba8UVE(meshTex, kSize, kSize);
    EXPECT_EQ(meshPixels[centerIdx + 3U], 255U);
    EXPECT_GT(static_cast<int>(meshPixels[centerIdx + 2U]), 40);

    const GLuint meshTexGl = static_cast<GLuint>(meshTex);
    glDeleteTextures(1, &meshTexGl);

    meshRenderer.ShutdownUVE();
    materialRenderer.ShutdownUVE();
}

} // namespace
} // namespace UVE::Editor
