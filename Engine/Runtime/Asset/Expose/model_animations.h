// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "animation_clip_asset.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace UVE::Asset {

/// A model's own animations: the takes its source carried, kept inside its .uvmodel (a tagged
/// section after the mesh data, found from a small footer at the end of the payload), so a model
/// with a mesh never scatters its animations as loose files. Each take is addressed as
/// "<model>.uvmodel#<take>", and LoadAnimationClipAssetUVE reads one from there.
struct ModelAnimationEntryUVE final {
    std::string name;
    double durationSeconds = 0.0;
};

/// The takes kept in `model` (names and lengths only; their keys are not read). Empty when it has none.
[[nodiscard]] std::vector<ModelAnimationEntryUVE> ListModelAnimationsUVE(const std::filesystem::path& model);

/// Reads take `name` of `model`.
[[nodiscard]] bool LoadModelAnimationUVE(const std::filesystem::path& model, const std::string& name, AnimationClipAssetUVE& outClip);

/// Replaces every take kept in `model` (an existing .uvmodel) with `clips`, each under its clipId.
/// The mesh data is left as it is. An empty list removes them.
/// `sourceStamp` records which version of the model's source the takes came from (0: unknown).
[[nodiscard]] bool SaveModelAnimationsUVE(const std::filesystem::path& model, const std::vector<AnimationClipAssetUVE>& clips,
                                          std::uint64_t sourceStamp = 0U);

/// The stamp the takes were saved with (0 when there are none or it was not given).
[[nodiscard]] std::uint64_t ReadModelAnimationStampUVE(const std::filesystem::path& model);

/// The bytes of the takes section at the end of a mesh payload (footer included), or empty: what a
/// mesh saved again carries over so its takes are not lost.
[[nodiscard]] std::vector<std::byte> ExtractModelAnimationSectionUVE(const std::vector<std::byte>& meshPayload);

/// "<model>.uvmodel#<take>" split into the model file and the take; false for an ordinary path.
[[nodiscard]] bool SplitEmbeddedClipPathUVE(const std::filesystem::path& path, std::filesystem::path& outModel, std::string& outTake);
[[nodiscard]] std::filesystem::path MakeEmbeddedClipPathUVE(const std::filesystem::path& model, const std::string& take);

/// The name a person reads for an asset path: its stem, and for a take inside a model
/// "Model / Take" (just "Model" when the take is named after it).
[[nodiscard]] std::string AssetDisplayNameUVE(const std::filesystem::path& path);

/// The tag that opens the takes section ('UVAN' little-endian); the mesh reader knows it so a
/// static mesh followed by takes is not misread.
inline constexpr std::uint32_t kModelAnimationSectionTagUVE = 0x4E415655U;

} // namespace UVE::Asset
