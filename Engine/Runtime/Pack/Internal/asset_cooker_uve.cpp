// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/pack/asset_cooker_uve.h"
#include <algorithm>
#include <cstdio>
#include <cctype>
#include <fstream>
#include <set>
#include <map>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "uve/asset/asset_database_uve.h"
#include "uve/asset/font_asset_uve.h"
#include "uve/asset/skeleton_asset_uve.h"
#include "uve/asset/animation_library_asset_uve.h"
#include "uve/asset/data_table_asset_uve.h"
#include "uve/asset/asset_resident_size_uve.h"
#include "uve/asset/texture_mipmap_uve.h"
#include "uve/asset/texture_compression_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/platform/editor_project_package_uve.h"
namespace UVE::Pack {
namespace {
using Json = nlohmann::ordered_json;
bool Relative(const std::filesystem::path& p) {
    const auto n = p.lexically_normal();
    return !n.empty() && !n.has_root_path() && *n.begin() != ".." && n != ".";
}
std::filesystem::path Contained(const std::filesystem::path& root, const std::filesystem::path& p) {
    const auto full = std::filesystem::canonical(p);
    if (!Relative(full.lexically_relative(root))) throw std::runtime_error("Cook path leaves project root: " + p.string());
    return full;
}
std::string Extension(const std::filesystem::path& path) {
    auto value = path.extension().string();
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
bool RuntimePath(const std::filesystem::path& path) {
    static const std::set<std::string> extensions{ ".uvtex", ".uvmodel", ".uvmat", ".uvshader", ".uvscene",
        ".uvprefab", ".uvaudio", ".uvanim", ".uvskel", ".uvanimlib", ".uvfont", ".uvtable",
        ".uvs", ".glsl", ".vert", ".frag", ".comp", ".glslinc" };
    return extensions.contains(Extension(path));
}
void CheckGuid(Asset::AssetGuidUVE guid, const Asset::AssetDatabaseUVE& db) {
    if (guid.value == 0) return;
    if (!db.HasGuidUVE(guid)) throw std::runtime_error("Missing asset GUID dependency: " + std::to_string(guid.value));
    if (!RuntimePath(db.ResolveUVE(guid)))
        throw std::runtime_error("GUID dependency references authoring source; import a native asset first");
}
void CheckJsonGuids(const Json& json, const Asset::AssetDatabaseUVE& db, const std::string& key = {}) {
    if (json.is_object()) {
        for (const auto& item : json.items()) CheckJsonGuids(item.value(),db,item.key());
    } else if (json.is_array()) {
        for (const auto& value : json) CheckJsonGuids(value,db,key);
    } else if ((key.ends_with("Guid") || key.ends_with("Guids") || key == "clip" || key == "libraryRef" ||
                key == "library" || key == "clipA" || key == "clipB") && json.is_number_unsigned()) {
        CheckGuid({json.get<std::uint64_t>()},db);
    }
}
bool Validate(const std::filesystem::path& path, Asset::AssetDatabaseUVE& db) {
    const auto extension = Extension(path);
    const std::set<std::string> text{ ".uvs", ".glsl", ".vert", ".frag", ".comp", ".glslinc" };
    if (text.contains(extension)) {
        if (std::filesystem::file_size(path) > 4U*1024U*1024U) return false;
        std::ifstream input(path,std::ios::binary);
        const std::string content{std::istreambuf_iterator<char>(input),{}};
        return !input.bad() && content.find('\0') == std::string::npos;
    }
    auto envelope = Asset::ReadUveFileUVE(path);
    if (!envelope) return false;
    const std::map<std::string,Asset::AssetKindUVE> kinds{
        {".uvtex",Asset::AssetKindUVE::Texture},{".uvmodel",Asset::AssetKindUVE::Mesh},
        {".uvmat",Asset::AssetKindUVE::Material},{".uvshader",Asset::AssetKindUVE::Shader},
        {".uvscene",Asset::AssetKindUVE::Scene},{".uvprefab",Asset::AssetKindUVE::Prefab},
        {".uvaudio",Asset::AssetKindUVE::Audio},{".uvanim",Asset::AssetKindUVE::Animation},
        {".uvskel",Asset::AssetKindUVE::Skeleton},{".uvanimlib",Asset::AssetKindUVE::AnimationLibrary},
        {".uvfont",Asset::AssetKindUVE::Font},{".uvtable",Asset::AssetKindUVE::DataTable}};
    if (const auto it = kinds.find(extension); it == kinds.end() || it->second != envelope->first.assetType) return false;
    switch (envelope->first.assetType) {
    case Asset::AssetKindUVE::Texture: { Asset::TextureAssetUVE v; return Asset::LoadTextureAssetUVE(path,v); }
    case Asset::AssetKindUVE::Mesh: {
        Asset::MeshAssetUVE v;
        if (!Asset::LoadMeshAssetUVE(path,v)) return false;
        for (const auto guid : v.materialGuids) CheckGuid(guid,db);
        return true;
    }
    case Asset::AssetKindUVE::Material: {
        Asset::MaterialAssetUVE v;
        if (!Asset::LoadMaterialAssetUVE(path,v)) return false;
        for (const auto guid : {v.albedoTexture,v.normalTexture,v.aoTexture,v.vertexShader,v.fragmentShader,
            v.metallicRoughnessTexture,v.emissiveTexture,v.metallicTexture,v.roughnessTexture,v.heightTexture,
            v.parentMaterial,v.nextPassMaterial,v.customShader}) CheckGuid(guid,db);
        return true;
    }
    case Asset::AssetKindUVE::Shader: { Asset::ShaderAssetUVE v; return Asset::LoadShaderAssetUVE(path,v); }
    case Asset::AssetKindUVE::Audio: { Asset::AudioAssetUVE v; return Asset::LoadAudioAssetUVE(path,v); }
    case Asset::AssetKindUVE::Animation: { Asset::AnimationClipAssetUVE v; return Asset::LoadAnimationClipAssetUVE(path,v); }
    case Asset::AssetKindUVE::Font: { Asset::FontAssetUVE v; return Asset::LoadFontAssetUVE(path,v); }
    case Asset::AssetKindUVE::Skeleton: { Asset::SkeletonAssetUVE v; return Asset::LoadSkeletonAssetUVE(path,v); }
    case Asset::AssetKindUVE::DataTable: { Asset::DataTableUVE v; return Asset::LoadDataTableAssetUVE(path,v); }
    case Asset::AssetKindUVE::AnimationLibrary: {
        Asset::AnimationLibraryAssetUVE v;
        if (!Asset::LoadAnimationLibraryAssetUVE(path,v)) return false;
        for (const auto& entry : v.entries) CheckGuid(entry.clip,db);
        return true;
    }
    case Asset::AssetKindUVE::Scene:
    case Asset::AssetKindUVE::Prefab: {
        const auto& bytes = envelope->second;
        const auto json = Json::parse(reinterpret_cast<const char*>(bytes.data()),
            reinterpret_cast<const char*>(bytes.data()) + bytes.size());
        CheckJsonGuids(json,db);
        return json.is_object() && json.contains("entities") && json.at("entities").is_array();
    }
    default: return false; // saves/bundles/blobs are not independently cooked runtime assets
    }
}
void CookTexture(const std::filesystem::path& source, const std::filesystem::path& dest, CookTargetUVE target) {
    Asset::TextureAssetUVE texture;
    if (!Asset::LoadTextureAssetUVE(source,texture)) throw std::runtime_error("Invalid texture: " + source.string());
    const std::uint32_t limit = target == CookTargetUVE::Android ? 1024U : 2048U;
    if (texture.payloadEncoding == Asset::TexturePayloadEncodingUVE::BasisUniversalKtx2 &&
        (texture.width > limit || texture.height > limit)) {
        Asset::TextureTranscodedMipChainUVE decoded;
        if (!Asset::TranscodeTextureAssetUVE(texture,Asset::TextureTranscodeTargetUVE::Rgba8Unorm,decoded))
            throw std::runtime_error("Cannot decode oversized compressed texture");
        texture.payloadEncoding = Asset::TexturePayloadEncodingUVE::RawPixels;
        texture.basisKtx2Data.clear();
        const auto baseBytes = static_cast<std::size_t>(texture.width)*texture.height*4U;
        texture.pixels.assign(decoded.pixels.begin(),decoded.pixels.begin()+static_cast<std::ptrdiff_t>(baseBytes));
        texture.mipLevels.clear();
    }
    if (texture.payloadEncoding == Asset::TexturePayloadEncodingUVE::RawPixels) {
        if (!Asset::GenerateTextureMipmapsUVE(texture,Asset::TextureMipmapFilterUVE::Box))
            throw std::runtime_error("Cannot generate texture mips");
        if (texture.width > limit || texture.height > limit) {
            const auto it = std::find_if(texture.mipLevels.begin(),texture.mipLevels.end(),[limit](const auto& m) {
                return m.width <= limit && m.height <= limit;
            });
            if (it == texture.mipLevels.end()) throw std::runtime_error("No bounded texture level");
            texture.width = it->width; texture.height = it->height; texture.pixels = std::move(it->pixels);
            texture.mipLevels.erase(texture.mipLevels.begin(),it+1);
        }
#if UVE_HAS_BASIS_ENCODER
        if (texture.format == Asset::TextureAssetFormatUVE::RGBA8Unorm &&
            !Asset::CompressTextureAssetWithBasisUVE(texture,Asset::TextureCompressionModeUVE::BasisUASTC,75U,1U))
            throw std::runtime_error("Texture compression failed");
#endif
    }
    if (!Asset::SaveTextureAssetUVE(texture,dest)) throw std::runtime_error("Cannot write cooked texture");
}
}
AssetCookResultUVE CookProjectAssetsUVE(const std::filesystem::path& projectFile,
    const std::filesystem::path& output, CookTargetUVE target) {
    AssetCookResultUVE result;
    std::filesystem::path stage;
    bool ownsStage = false;
    try {
        const auto loaded = Platform::EditorProjectPackageCodecUVE::LoadUVE(projectFile);
        if (!loaded.IsAcceptedUVE()) throw std::runtime_error("Invalid project manifest");
        auto package = *loaded.package;
        const auto root = std::filesystem::canonical(projectFile).parent_path();
        if (!Relative(package.contentRoot) || !Relative(package.assetDatabasePath) || !Relative(package.startupScenePath))
            throw std::runtime_error("Cook requires contained relative project paths");
        if (std::filesystem::exists(output)) throw std::runtime_error("Cook output already exists");
        const auto content = Contained(root,root/package.contentRoot);
        const auto out = std::filesystem::weakly_canonical(std::filesystem::absolute(output));
        if (out == root || out == content || Relative(out.lexically_relative(content)))
            throw std::runtime_error("Cook output cannot be inside content root");
        stage = out; stage += ".cooking";
        if (std::filesystem::exists(stage)) throw std::runtime_error("Cook staging directory already exists");
        ownsStage = true;
        std::filesystem::create_directories(stage/package.contentRoot);
        Asset::AssetDatabaseUVE db;
        const auto registry = root/package.assetDatabasePath;
        if (std::filesystem::exists(registry)) {
            std::ifstream file(Contained(root,registry)); Json document; file >> document;
            if (!document.is_object()) throw std::runtime_error("Invalid asset database object");
            for (const auto& item : document.items()) {
                if (item.key().size() != 16 || item.key() == "0000000000000000" ||
                    !std::all_of(item.key().begin(),item.key().end(),[](char c) {
                        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                    }) || !item.value().is_string()) throw std::runtime_error("Invalid asset database GUID");
            }
            if (!db.LoadUVE(registry)) throw std::runtime_error("Invalid asset database");
        }
        Json catalog = Json::object();
        std::set<std::filesystem::path> required;
        required.insert(package.contentRoot/package.startupScenePath);
        const auto resource = [&](const auto& p) { if (!p.empty()) {
            if (!Relative(p)) throw std::runtime_error("Invalid application resource path");
            required.insert(package.contentRoot/p);
        }};
        resource(package.applicationSettings.splashImagePath);
        resource(package.applicationSettings.cursorImagePath);
        for (const auto& [name,icons] : package.applicationSettings.iconPathsByTarget) {
            static_cast<void>(name);
            for (const auto& [size,p] : icons) { static_cast<void>(size); resource(p); }
        }
        for (const auto& record : db.GetRegisteredAssetsUVE()) {
            // Editor registries can include authoring sources as well as imported outputs.
            // Omit those source records; a runtime reference to one is rejected by CheckGuid.
            if (!RuntimePath(record.path)) continue;
            std::filesystem::path source = record.path;
            if (!source.is_absolute()) source = root/source;
            source = Contained(root,source);
            const auto relative = source.lexically_relative(root);
            if (!Relative(source.lexically_relative(content))) throw std::runtime_error("Registered asset outside content");
            if (!Validate(source,db)) throw std::runtime_error("Invalid registered native asset: " + source.string());
            required.insert(relative);
            char hex[17]; std::snprintf(hex,sizeof(hex),"%016llx",static_cast<unsigned long long>(record.guid.value));
            if (record.guid.value == 0) throw std::runtime_error("Invalid zero GUID");
            catalog[hex] = relative.generic_string();
        }
        const std::set<std::string> native{ ".uvtex",".uvmodel",".uvmat",".uvshader",".uvscene",".uvprefab",
            ".uvaudio",".uvanim",".uvskel",".uvanimlib",".uvfont",".uvtable" };
        const std::set<std::string> text{ ".uvs",".glsl",".vert",".frag",".comp",".glslinc" };
        for (const auto& entry : std::filesystem::recursive_directory_iterator(content)) {
            if (entry.is_symlink()) throw std::runtime_error("Cook does not accept symlinks");
            if (!entry.is_regular_file()) continue;
            const auto extension = Extension(entry.path());
            if (native.contains(extension) || text.contains(extension)) required.insert(entry.path().lexically_relative(root));
        }
        Json runtimeFiles = Json::array();
        for (const auto& relative : required) {
            runtimeFiles.push_back(relative.generic_string());
            const auto source = Contained(root,root/relative);
            const auto dest = stage/relative;
            std::filesystem::create_directories(dest.parent_path());
            const auto extension = Extension(source);
            if ((native.contains(extension) || text.contains(extension)) && !Validate(source,db)) throw std::runtime_error("Corrupt native content: " + source.string());
            if (extension == ".uvtex") CookTexture(source,dest,target);
            else std::filesystem::copy_file(source,dest);
            result.sourceBytes += std::filesystem::file_size(source);
            result.cookedBytes += std::filesystem::file_size(dest);
            ++result.fileCount;
        }
        std::filesystem::create_directories((stage/package.assetDatabasePath).parent_path());
        { std::ofstream file(stage/package.assetDatabasePath); file << catalog.dump(2); if (!file) throw std::runtime_error("Catalog write failed"); }
        std::filesystem::copy_file(projectFile,stage/projectFile.filename());
        runtimeFiles.push_back(projectFile.filename().generic_string());
        runtimeFiles.push_back(package.assetDatabasePath.generic_string());
        runtimeFiles.push_back("cooked.json");
        for (const auto& p : {package.settingsPath,std::filesystem::path("project.uvinput")}) {
            if (!Relative(p)) throw std::runtime_error("Invalid settings path");
            if (std::filesystem::is_regular_file(root/p)) {
                const auto source = Contained(root,root/p);
                std::filesystem::create_directories((stage/p).parent_path());
                std::filesystem::copy_file(source,stage/p);
                runtimeFiles.push_back(p.generic_string());
            }
        }
        Json metadata{{"schema",1},{"target",target == CookTargetUVE::Android ? "android" : "windows"},
            {"assetBudgetBytes",target == CookTargetUVE::Android ? 134217728U : 268435456U},
            {"runtimeFiles",runtimeFiles},{"sourceBytes",result.sourceBytes},{"cookedBytes",result.cookedBytes},{"files",result.fileCount}};
        { std::ofstream file(stage/"cooked.json"); file << metadata.dump(2); if (!file) throw std::runtime_error("Cook report write failed"); }
        std::filesystem::rename(stage,out); stage.clear();
        result.success = true; result.message = "Cooked native content and stable GUID catalog";
    } catch (const std::exception& error) {
        result.message = error.what();
        if (ownsStage && !stage.empty()) { std::error_code ignored; std::filesystem::remove_all(stage,ignored); }
    }
    return result;
}
} // namespace UVE::Pack
