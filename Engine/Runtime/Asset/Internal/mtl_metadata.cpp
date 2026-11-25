// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "mtl_metadata.h"
#include <charconv>
#include <cctype>
#if defined(__ANDROID__)
#include <cerrno>
#include <cstdlib>
#endif
#include <cmath>
#include <new>
#include <system_error>
#include <string>
#include <utility>
namespace UVE::Asset { namespace {
constexpr std::uint32_t kMax = 1'000'000U;
bool Inc(std::uint32_t& value) noexcept { if (value >= kMax) return false; ++value; return true; }
std::string_view Token(std::string_view& rest) noexcept { while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())) != 0) rest.remove_prefix(1U); const auto end = rest.find_first_of(" \t\r\n"); const auto token = rest.substr(0U, end); rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end); return token; }
bool Has(std::string_view& rest, const std::size_t count) noexcept { for (std::size_t i=0U;i<count;++i) if (Token(rest).empty()) return false; return true; }
}

bool ValidateMtlTextureReference(const std::string_view path) noexcept {
    if (path.empty() || path.size() > kMaximumMtlTextureReferenceBytes ||
        path.front() == '/' || path.front() == '\\' || path.contains('\0') ||
        (path.size() >= 2U && path[1U] == ':')) {
        return false;
    }
    std::size_t segmentStart = 0U;
    while (segmentStart <= path.size()) {
        const std::size_t separator = path.find_first_of("/\\", segmentStart);
        const std::string_view segment = path.substr(
            segmentStart, separator == std::string_view::npos ? std::string_view::npos : separator - segmentStart);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        if (separator == std::string_view::npos) {
            break;
        }
        segmentStart = separator + 1U;
    }
    return true;
}

bool ParseMtlMaterialProperty(const std::string_view sourceLine, MtlMaterialProperty& outProperty) {
    if (sourceLine.empty() || sourceLine.size() > kMaximumMtlTextureReferenceBytes) {
        return false;
    }
    const auto parseFloat = [](const std::string_view text, float& outValue) noexcept {
        if (text.empty()) return false;
#if defined(__ANDROID__)
        std::string copy(text);
        char* end = nullptr;
        errno = 0;
        const float value = std::strtof(copy.c_str(), &end);
        if (errno == ERANGE || end != copy.c_str() + copy.size() || !std::isfinite(value)) {
            return false;
        }
        outValue = value;
        return true;
#else
        float value = 0.0F;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value,
                                            std::chars_format::general);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !std::isfinite(value)) {
            return false;
        }
        outValue = value;
        return true;
#endif
    };
    try {
        std::string_view rest = sourceLine;
        const std::string_view directive = Token(rest);
        MtlMaterialProperty candidate;
        if (directive == "Kd" || directive == "Ka" || directive == "Ks" || directive == "Ke" || directive == "Tf") {
            candidate.kind = MtlMaterialPropertyKind::Vector3;
            for (std::size_t index = 0U; index < candidate.vectorValue.size(); ++index) {
                if (!parseFloat(Token(rest), candidate.vectorValue[index])) return false;
            }
            if (!Token(rest).empty()) return false;
        } else if (directive == "Ns" || directive == "Ni" || directive == "d" || directive == "Tr" || directive == "illum") {
            candidate.kind = MtlMaterialPropertyKind::Scalar;
            if (!parseFloat(Token(rest), candidate.scalarValue) || !Token(rest).empty()) return false;
        } else if (directive.rfind("map_", 0U) == 0U) {
            const std::string_view reference = Token(rest);
            if (!ValidateMtlTextureReference(reference) || !Token(rest).empty()) return false;
            candidate.kind = MtlMaterialPropertyKind::TextureReference;
            candidate.textureReference.assign(reference);
        } else {
            return false;
        }
        outProperty = std::move(candidate);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

bool ParseMtlTextureMap(const std::string_view sourceLine, MtlTextureMap& outMap) {
    if (sourceLine.empty() || sourceLine.size() > kMaximumMtlTextureReferenceBytes) return false;
    const auto parseFloat = [](const std::string_view text, float& outValue) noexcept {
        if (text.empty()) return false;
#if defined(__ANDROID__)
        std::string copy(text);
        char* end = nullptr;
        errno = 0;
        const float value = std::strtof(copy.c_str(), &end);
        if (errno == ERANGE || end != copy.c_str() + copy.size() || !std::isfinite(value)) return false;
        outValue = value;
        return true;
#else
        float value = 0.0F;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value,
                                            std::chars_format::general);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !std::isfinite(value)) return false;
        outValue = value;
        return true;
#endif
    };
    try {
        std::string_view rest = sourceLine;
        if (Token(rest).rfind("map_", 0U) != 0U) return false;
        MtlTextureMap candidate;
        bool hasReference = false;
        while (true) {
            const std::string_view option = Token(rest);
            if (option.empty()) break;
            if (option == "-s" || option == "-o") {
                auto& values = option == "-s" ? candidate.scale : candidate.offset;
                for (float& value : values) {
                    if (!parseFloat(Token(rest), value)) return false;
                }
            } else if (option == "-clamp") {
                const std::string_view mode = Token(rest);
                if (mode == "on") candidate.clamp = true;
                else if (mode == "off") candidate.clamp = false;
                else return false;
            } else {
                if (option.front() == '-' || !ValidateMtlTextureReference(option)) return false;
                candidate.textureReference.assign(option);
                hasReference = true;
                if (!Token(rest).empty()) return false;
                break;
            }
        }
        if (!hasReference) return false;
        outMap = std::move(candidate);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

std::optional<MtlMetadata> ParseMtlMetadata(const std::string_view source) {
    MtlMetadata out; std::size_t start=0U;
    while (start <= source.size()) {
        const auto end=source.find('\n', start); auto line=source.substr(start, end==std::string_view::npos?std::string_view::npos:end-start); if(!line.empty()&&line.back()=='\r') line.remove_suffix(1U);
        const auto comment = line.find('#'); if (comment != std::string_view::npos) line = line.substr(0U, comment);
        while(!line.empty()&&(line.front()==' '||line.front()=='\t')) line.remove_prefix(1U);
        if(!line.empty()) { auto rest=line; const auto directive=Token(rest);
            if(directive=="newmtl") { if(!Has(rest,1U)||!Token(rest).empty()||!Inc(out.materialCount)) return std::nullopt; }
            else if(directive.rfind("map_",0U)==0U) { MtlTextureMap map; if(!ParseMtlTextureMap(line, map)||!Inc(out.textureMapCount)) return std::nullopt; }
            else if(directive=="Kd"||directive=="Ka"||directive=="Ks"||directive=="Ke"||directive=="Tf") { if(!Has(rest,3U)||!Token(rest).empty()||!Inc(out.vectorPropertyCount)) return std::nullopt; }
            else if(directive=="Ns"||directive=="Ni"||directive=="d"||directive=="Tr"||directive=="illum") { if(!Has(rest,1U)||!Token(rest).empty()||!Inc(out.scalarPropertyCount)) return std::nullopt; }
            else if(!Inc(out.ignoredStatementCount)) return std::nullopt;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
    }
    return out;
}
} // namespace UVE::Asset
