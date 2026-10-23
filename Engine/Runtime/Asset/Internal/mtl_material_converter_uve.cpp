// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/mtl_material_converter_uve.h"

#include "uve/math/color_uve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "uve/asset/mtl_metadata_uve.h"

namespace UVE::Asset {
namespace {

[[nodiscard]] std::string_view NextTokenUVE(std::string_view& rest) noexcept {
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) {
        rest.remove_prefix(1U);
    }
    const std::size_t end = rest.find_first_of(" \t");
    const std::string_view token = rest.substr(0U, end);
    rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end);
    return token;
}

[[nodiscard]] bool ParseDirectiveUVE(std::string_view line, std::string_view& outDirective) noexcept {
    const std::size_t commentStart = line.find('#');
    if (commentStart != std::string_view::npos) {
        line = line.substr(0U, commentStart);
    }
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
        line.remove_prefix(1U);
    }
    std::string_view rest = line;
    outDirective = NextTokenUVE(rest);
    return !outDirective.empty();
}

[[nodiscard]] float AverageVectorUVE(const std::array<float, 3U>& value) noexcept {
    return (value[0] + value[1] + value[2]) / 3.0F;
}

[[nodiscard]] bool IsUnitIntervalUVE(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

} // namespace

bool ConvertMtlMaterialUVE(const std::string_view source, MaterialAssetUVE& outMaterial) {
    if (source.empty() || source.size() > kMaximumMtlMaterialSourceBytesUVE ||
        source.contains('\0')) {
        return false;
    }

    try {
        MaterialAssetUVE candidate;
        bool hasMaterialName = false;
        std::size_t lineStart = 0U;
        while (lineStart <= source.size()) {
            const std::size_t lineEnd = source.find('\n', lineStart);
            std::string_view line = source.substr(lineStart, lineEnd == std::string_view::npos
                                                        ? std::string_view::npos
                                                        : lineEnd - lineStart);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1U);
            }
            const std::size_t commentStart = line.find('#');
            if (commentStart != std::string_view::npos) {
                line = line.substr(0U, commentStart);
            }
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
                line.remove_prefix(1U);
            }

            if (!line.empty()) {
                std::string_view directive;
                if (!ParseDirectiveUVE(line, directive)) {
                    return false;
                }
                if (directive == "newmtl") {
                    std::string_view rest = line;
                    static_cast<void>(NextTokenUVE(rest));
                    if (hasMaterialName) {
                        return false;
                    }
                    if (NextTokenUVE(rest).empty() || !NextTokenUVE(rest).empty()) {
                        return false;
                    }
                    hasMaterialName = true;
                } else if (!hasMaterialName) {
                    return false;
                } else if (directive.rfind("map_", 0U) == 0U) {
                    MtlTextureMapUVE map;
                    if (!ParseMtlTextureMapUVE(line, map)) {
                        return false;
                    }
                    // MaterialAssetUVE stores only AssetGuidUVE slots. The validated source path
                    // remains syntax-only until an explicit texture resolver/import policy exists.
                } else {
                    MtlMaterialPropertyUVE property;
                    if (!ParseMtlMaterialPropertyUVE(line, property)) {
                        return false;
                    }
                    if (directive == "Kd") {
                        // MTL colors are display-authored (sRGB); the material stores linear.
                        candidate.albedoColor = Math::ColorFromDisplayUVE(Math::Vector3UVE{
                            property.vectorValue[0], property.vectorValue[1], property.vectorValue[2]});
                    } else if (directive == "Ke") {
                        candidate.emissiveColor = Math::ColorFromDisplayUVE(Math::Vector3UVE{
                            property.vectorValue[0], property.vectorValue[1], property.vectorValue[2]});
                    } else if (directive == "Ks") {
                        candidate.metallic = std::clamp(AverageVectorUVE(property.vectorValue), 0.0F, 1.0F);
                    } else if (directive == "Ns") {
                        const float normalized = std::clamp(property.scalarValue, 0.0F, 1000.0F) / 1000.0F;
                        candidate.roughness = std::clamp(1.0F - normalized, 0.04F, 1.0F);
                    } else if (directive == "d") {
                        if (!IsUnitIntervalUVE(property.scalarValue)) {
                            return false;
                        }
                        candidate.isTransparent = property.scalarValue < 1.0F;
                    } else if (directive == "Tr") {
                        if (!IsUnitIntervalUVE(property.scalarValue)) {
                            return false;
                        }
                        candidate.isTransparent = property.scalarValue > 0.0F;
                    }
                }
            }

            if (lineEnd == std::string_view::npos) {
                break;
            }
            lineStart = lineEnd + 1U;
        }

        if (!hasMaterialName) {
            return false;
        }
        outMaterial = std::move(candidate);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

namespace {

/// Reads the single token after `directive` on a comment-stripped MTL line. Used for `newmtl`
/// names and map directives, which both take one required argument.
[[nodiscard]] std::string_view TrimmedLineUVE(std::string_view line) noexcept {
    const std::size_t commentStart = line.find('#');
    if (commentStart != std::string_view::npos) {
        line = line.substr(0U, commentStart);
    }
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
        line.remove_prefix(1U);
    }
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) {
        line.remove_suffix(1U);
    }
    return line;
}

/// `map_Bump`, `map_bump`, `bump`, `norm` and `map_normal` all describe the tangent-space normal in
/// the wild; anything else is matched by name. The alias table is deliberately small and explicit
/// rather than a fuzzy match, so a typo cannot silently claim a slot.
[[nodiscard]] std::string* TextureFileSlotUVE(MtlLibraryMaterialUVE& material, const std::string_view directive) {
    if (directive == "map_Kd") {
        return &material.albedoTextureFile;
    }
    if (directive == "map_Ks") {
        return &material.metallicTextureFile;
    }
    if (directive == "map_Ke") {
        return &material.emissiveTextureFile;
    }
    if (directive == "map_Ka" || directive == "map_ao") {
        return &material.occlusionTextureFile;
    }
    if (directive == "map_Bump" || directive == "map_bump" || directive == "bump" || directive == "norm" ||
        directive == "map_normal") {
        return &material.normalTextureFile;
    }
    return nullptr;
}

} // namespace

bool ConvertMtlLibraryUVE(const std::string_view source, MtlLibraryUVE& outLibrary) {
    outLibrary = MtlLibraryUVE{};
    if (source.empty() || source.size() > kMaximumMtlLibrarySourceBytesUVE || source.contains('\0')) {
        return false;
    }

    try {
        std::string block;
        std::vector<std::pair<std::string, std::string>> textureFiles;
        std::string materialName;
        bool inBlock = false;

        const auto flushBlock = [&]() {
            if (!inBlock) {
                return;
            }
            MaterialAssetUVE converted;
            const bool convertedOk = ConvertMtlMaterialUVE(block, converted);
            if (!convertedOk) {
                ++outLibrary.skippedMaterials;
            } else {
                MtlLibraryMaterialUVE entry;
                entry.name = materialName;
                entry.material = std::move(converted);
                for (const auto& [slotDirective, file] : textureFiles) {
                    if (std::string* const slot = TextureFileSlotUVE(entry, slotDirective); slot != nullptr) {
                        // First occurrence wins, matching how a texture-map list is authored: the
                        // later duplicate of one slot is an authoring mistake, not an override.
                        if (slot->empty()) {
                            *slot = file;
                        }
                    }
                }
                outLibrary.materials.push_back(std::move(entry));
            }
            textureFiles.clear();
            block.clear();
        };

        std::size_t lineStart = 0U;
        while (lineStart <= source.size()) {
            const std::size_t lineEnd = source.find('\n', lineStart);
            const std::string_view rawLine =
                source.substr(lineStart, lineEnd == std::string_view::npos ? std::string_view::npos
                                                                           : lineEnd - lineStart);
            const std::string_view line = TrimmedLineUVE(rawLine);
            if (!line.empty()) {
                std::string_view rest = line;
                const std::string_view directive = NextTokenUVE(rest);
                if (directive == "newmtl") {
                    if (outLibrary.materials.size() + outLibrary.skippedMaterials >=
                        kMaximumMtlLibraryMaterialsUVE) {
                        // Bounded work: the blocks past the cap are not read into a material at all.
                        // Stopping here keeps every material before the cap, which is strictly more
                        // useful than dropping the whole library because it was large.
                        flushBlock();
                        outLibrary.truncated = true;
                        return true;
                    }
                    flushBlock();
                    materialName = std::string(TrimmedLineUVE(rest));
                    if (materialName.empty()) {
                        return false;
                    }
                    inBlock = true;
                }
                if (inBlock) {
                    // `bump`/`norm` are the same directive under older spellings, and the shared
                    // map parser only knows the `map_` forms. Rewriting the spelling here keeps one
                    // bounded parser instead of a second, subtly different one.
                    std::string remapped;
                    std::string_view blockLine = line;
                    std::string_view blockDirective = directive;
                    if (directive == "bump" || directive == "norm") {
                        remapped = "map_Bump " + std::string(TrimmedLineUVE(rest));
                        blockLine = remapped;
                        blockDirective = "map_Bump";
                    }
                    block.append(blockLine.data(), blockLine.size());
                    block.push_back('\n');
                    if (blockDirective.rfind("map_", 0U) == 0U) {
                        MtlTextureMapUVE map;
                        if (ParseMtlTextureMapUVE(blockLine, map)) {
                            textureFiles.emplace_back(std::string(blockDirective), map.textureReference);
                        }
                        // A map line that does not parse is left for the single-material converter
                        // to judge along with every other directive in the block.
                    }
                }
            }
            if (lineEnd == std::string_view::npos) {
                break;
            }
            lineStart = lineEnd + 1U;
        }
        flushBlock();
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

} // namespace UVE::Asset
