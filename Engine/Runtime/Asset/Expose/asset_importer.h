// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <memory>

#include "i_asset_importer.h"

namespace UVE::Asset {

inline constexpr std::size_t kMaximumTextImportBytes = 16U * 1024U * 1024U;
inline constexpr std::size_t kMaximumShaderSourceBytes = 16U * 1024U * 1024U;

/// AssetImporter is the concrete, engine-standard implementation of IAssetImporter. Its
/// constructor registers built-in importers for plain project documents, the existing typed UVE
/// envelopes including `.uvanim`, and bounded raw BMP/PNG/TGA-to-`.uvtex`, OBJ-to-`.uvmodel`, MTL-to-`.uvmat`,
/// glTF/GLB-to-`.uvmodel`, FBX-to-`.uvmodel` (static mesh), JPEG-to-`.uvtex`, and explicit `.vert`/`.frag`/`.comp` shader-source-
/// to-`.uvshader` bridges.
/// Envelope imports are deterministic reimport/copy contracts; the source importers decode/convert
/// only their documented bounded forms, while raw audio, FBX skins/materials/animation, ambiguous combined `.glsl`, broader glTF
/// scene/material/image conversion, raw animation decoding, MTL texture/shader resolution, and other audio conversion remains independently deferred;
/// EngineCore composes the cycle-safe WAV-to-`.uvaudio` bridge after constructing this importer.
class AssetImporter final : public IAssetImporter {
public:
    AssetImporter();
    ~AssetImporter() override;

    AssetImporter(const AssetImporter&) = delete;
    AssetImporter& operator=(const AssetImporter&) = delete;

    void RegisterImporter(
        std::string sourceExtension,
        std::function<bool(const std::filesystem::path&, const std::filesystem::path&,
                            const AssetImportSettings&)>
            importFunc) override;
    void RegisterDatabaseAwareImporter(std::string sourceExtension,
                                          DatabaseAwareImportFunc importFunc) override;
    [[nodiscard]] AssetImportSourceClassification ClassifySource(
        const std::filesystem::path& sourcePath) const override;
    [[nodiscard]] AssetGuid Import(const std::filesystem::path& sourcePath,
                                          const std::filesystem::path& destinationPath,
                                          IAssetDatabase& assetDatabase,
                                          const AssetImportSettings& settings = {}) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Asset
