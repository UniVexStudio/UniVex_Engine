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

[[nodiscard]] Window::WindowDesc MakeThumbnailGlWindowDesc() {
    Window::WindowDesc desc;
    desc.title = "uve_pbr_thumbnail_renderer_tests";
    desc.width = 64;
    desc.height = 64;
    // Strict OpenGL 3.3 Core context ensures GLSL #version 330 core shaders compile without
    // relying on 4.20+ relaxations (such as runtime-initialized const variables).
    desc.glVersionMajor = 3;
    desc.glVersionMinor = 3;
    return desc;
}

[[nodiscard]] std::vector<std::uint8_t> ReadTextureRgba8(const std::uintptr_t textureId, const int width,
                                                            const int height) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 0U);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(textureId));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, 0U);
    return pixels;
}

[[nodiscard]] Asset::MeshAsset BuildUnitQuadMesh() {
    Asset::MeshAsset mesh{};
    const auto makeVertex = [](const float x, const float y, const float z, const float u, const float v) {
        Asset::MeshVertex vert{};
        vert.position = Math::Vector3{x, y, z};
        vert.normal = Math::Vector3{0.0F, 0.0F, 1.0F};
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
    mesh.surfaces = {Asset::MeshSurface{0U, 6U, 0U}};
    mesh.localBounds = Math::Aabb{Math::Vector3{-0.5F, -0.5F, -0.05F}, Math::Vector3{0.5F, 0.5F, 0.05F}};
    return mesh;
}

TEST(PbrThumbnailRendererTest, MaterialAndMeshThumbnailsCompileAndRenderOnRealOpenGL33Core) {
    Events::EventSystem eventSystem;
    Window::WindowManager windowManager(eventSystem, MakeThumbnailGlWindowDesc());
    if (!windowManager.IsValid()) {
        GTEST_SKIP() << "No display available for OpenGL 3.3 thumbnail verification - skipping";
    }

    MaterialThumbnailRenderer materialRenderer;
    materialRenderer.Initialize();

    MeshThumbnailRenderer meshRenderer;
    meshRenderer.Initialize();

    // Seed a stale GL error before rendering to prove both thumbnail renderers drain prior errors.
    glEnable(static_cast<GLenum>(0xFFFFU));

    Asset::MaterialAsset material{};
    material.albedoColor = Math::Color{0.85F, 0.35F, 0.15F};
    material.metallic = 0.4F;
    material.roughness = 0.35F;

    constexpr int kSize = 128;
    const std::uintptr_t matTex = materialRenderer.RenderThumbnail(material, MaterialThumbnailTextures{}, kSize, kSize);
    ASSERT_NE(matTex, 0U) << "MaterialThumbnailRenderer failed to compile or render on OpenGL 3.3 Core";
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));

    const std::vector<std::uint8_t> matPixels = ReadTextureRgba8(matTex, kSize, kSize);
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

    const Asset::MeshAsset quad = BuildUnitQuadMesh();
    MeshThumbnailLook look{};
    look.colour = {0.25F, 0.65F, 0.90F};
    look.metallic = 0.2F;
    look.roughness = 0.4F;
    const std::uintptr_t meshTex = meshRenderer.RenderThumbnail(quad, kSize, kSize, {look});
    ASSERT_NE(meshTex, 0U) << "MeshThumbnailRenderer failed to compile or render on OpenGL 3.3 Core";
    EXPECT_EQ(glGetError(), static_cast<GLenum>(GL_NO_ERROR));

    const std::vector<std::uint8_t> meshPixels = ReadTextureRgba8(meshTex, kSize, kSize);
    EXPECT_EQ(meshPixels[centerIdx + 3U], 255U);
    EXPECT_GT(static_cast<int>(meshPixels[centerIdx + 2U]), 40);

    const GLuint meshTexGl = static_cast<GLuint>(meshTex);
    glDeleteTextures(1, &meshTexGl);

    meshRenderer.Shutdown();
    materialRenderer.Shutdown();
}

} // namespace
} // namespace UVE::Editor
