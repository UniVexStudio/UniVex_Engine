// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <thread>
#include <nlohmann/json.hpp>
#include "asset_database.h"
#include "asset_manager.h"
#include "asset_handle.h"
#include "asset_resident_size.h"
#include "engine_core.h"
#include "camera_component.h"
#include "primitive_mesh_component.h"
#include "transform_component.h"
#include "scene_serializer.h"
#include "i_scene_graph.h"
#include "editor_project_package.h"
#include "event_system.h"
#include "thread_pool.h"
namespace {
void Require(bool success) {if(!success) throw std::runtime_error("Asset workload operation failed");}
void Fixture(const std::filesystem::path& directory, bool small) {
    using namespace UVE;
    if(std::filesystem::exists(directory)) throw std::runtime_error("Fixture destination exists");
    std::filesystem::create_directories(directory/"content");
    Core::EngineConfig config; config.headless=true; config.enableConsoleLogging=false;
    config.projectRootDirectory=directory; config.projectContentRoot=directory/"content";
    config.settingsFilePath=directory/"settings"; config.logFilePath=directory/"fixture.log";
    config.assetDatabaseFilePath=directory/".uvassetdb"; config.threadPoolWorkerCount=2;
    Core::EngineCore engine(config); engine.Init();
    auto& services=engine.GetServices(); auto& entities=services.GetEntityManager();
    const auto camera=entities.CreateEntity();
    Scene::TransformComponent transform; transform.localPosition={0,0,4};
    services.GetSceneGraph().AttachTransform(entities,camera,transform);
    entities.AddComponent<Scene::CameraComponent>(camera);
    const auto cube=entities.CreateEntity();
    services.GetSceneGraph().AttachTransform(entities,cube,{});
    Scene::PrimitiveMeshComponent primitive; primitive.baseColor={0.2F,0.5F,0.9F};
    entities.AddComponent<Scene::PrimitiveMeshComponent>(cube,primitive);
    Scene::SceneSerializer serializer;
    Require(serializer.Save(entities,{camera,cube},directory/"content/main.uvscene",Scene::SceneAssetType::Scene));
    Platform::EditorProjectPackage package; package.revision=1; package.projectId="asset-workload";
    package.displayName="Asset Workload"; package.productMetadata.name="Asset Workload";
    package.productMetadata.shortName="asset-workload"; package.contentRoot="content";
    package.applicationSettings.portableUserData=true;
    package.startupScenePath="main.uvscene"; package.assetDatabasePath=".uvassetdb"; package.settingsPath="project.uvsettings";
    Require(Platform::EditorProjectPackageCodec::Save(directory/"sample.uvproject",package).IsAccepted());
    Asset::AssetDatabase db;
    for(unsigned i=0;i<(small ? 0U : 8U);++i) {
        Asset::TextureAsset texture; texture.width=2048; texture.height=1024;
        texture.pixels.resize(static_cast<std::size_t>(texture.width)*texture.height*4);
        for(std::size_t j=0;j<texture.pixels.size();j+=4) {
            texture.pixels[j]=static_cast<std::byte>((j/4+i)%256);
            texture.pixels[j+1]=static_cast<std::byte>((j/8192)%256);
            texture.pixels[j+2]=std::byte{128};texture.pixels[j+3]=std::byte{255};
        }
        const auto relative=std::filesystem::path("content")/("texture-"+std::to_string(i)+".uvtex");
        Require(Asset::SaveTextureAsset(texture,directory/relative));
        static_cast<void>(db.Register(relative));
    }
    Asset::TextureAsset surface; surface.width=4; surface.height=4;
    surface.pixels.resize(64);
    for(std::size_t j=0;j<surface.pixels.size();j+=4) {
        surface.pixels[j]=std::byte{64};surface.pixels[j+1]=std::byte{128};
        surface.pixels[j+2]=std::byte{255};surface.pixels[j+3]=std::byte{255};
    }
    Require(Asset::SaveTextureAsset(surface,directory/"content/surface.uvtex"));
    Asset::MaterialAsset material;material.albedoTexture=db.Register("content/surface.uvtex");
    Require(Asset::SaveMaterialAsset(material,directory/"content/surface.uvmat"));
    entities.GetComponent<Scene::PrimitiveMeshComponent>(cube).materialGuid=db.Register("content/surface.uvmat");
    Require(serializer.Save(entities,{camera,cube},directory/"content/main.uvscene",Scene::SceneAssetType::Scene));
    Require(db.Save(directory/".uvassetdb"));
    std::ofstream(directory/"content/authoring.png") << "source excluded";
    engine.Shutdown();
}
}
int main(int argc,char** argv) {
    using namespace UVE;
    try {
        if(argc==3 && (std::string_view(argv[1])=="--create-fixture" || std::string_view(argv[1])=="--create-smoke-fixture")) {
            Fixture(std::filesystem::absolute(argv[2]),std::string_view(argv[1])=="--create-smoke-fixture");return 0;
        }
        if(argc!=4) {std::cerr<<"Usage: uve_asset_workload <catalog> <package-root> <report.json> OR --create-fixture <new-directory>\n";return 2;}
        Asset::AssetDatabase db; Require(db.Load(argv[1])); Require(db.RootPaths(argv[2]));
        Threading::ThreadPool pool(2); Events::EventSystem events; Asset::AssetManager manager(pool,events);
        manager.RegisterLoader<Asset::TextureAsset>(&Asset::LoadTextureAsset,
            [](const auto& v){return Asset::AssetResidentBytes(v);});
        constexpr std::size_t budget=16U*1024*1024; manager.SetResidentBudget(budget);
        nlohmann::json samples=nlohmann::json::array();
        const auto begin=std::chrono::steady_clock::now();
        Require(manager.GetLoadedAssetCount()==0);
        for(const auto& record:db.GetRegisteredAssets()) {
            if(record.path.extension()!=".uvtex") continue;
            const auto start=std::chrono::steady_clock::now();
            {
                auto h=manager.Load<Asset::TextureAsset>(record.guid,db);
                const auto deadline=start+std::chrono::seconds(30);
                while(!h.IsReady()&&!h.HasFailed()&&std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
                Require(h.IsReady()); Require(manager.GetResidentBytes()<=budget);
                samples.push_back({{"guid",record.guid.value},{"loadMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()},
                    {"residentBytes",manager.GetResidentBytes()}});
            }
            manager.CollectGarbage(); Require(manager.GetResidentBytes()==0);
        }
        Require(!samples.empty());
        nlohmann::json report{{"schema",1},{"workload","sequential native texture demand load and release"},
            {"budgetBytes",budget},{"peakCountedResidentBytes",manager.GetPeakResidentBytes()},
            {"finalCountedResidentBytes",manager.GetResidentBytes()},
            {"elapsedMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()},
            {"samples",samples},{"physical3GbDeviceVerified",false},{"gpuMemoryMeasured",false}};
        std::ofstream output(argv[3]); output<<report.dump(2); Require(static_cast<bool>(output));return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
