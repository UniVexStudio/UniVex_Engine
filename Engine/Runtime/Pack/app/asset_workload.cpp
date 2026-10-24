// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <thread>
#include <nlohmann/json.hpp>
#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_manager_uve.h"
#include "uve/asset/asset_handle_uve.h"
#include "uve/asset/asset_resident_size_uve.h"
#include "uve/core/engine_core_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/primitive_mesh_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/scene/scene_serializer_uve.h"
#include "uve/scene/i_scene_graph_uve.h"
#include "uve/platform/editor_project_package_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/threading/thread_pool_uve.h"
namespace {
void Require(bool success) {if(!success) throw std::runtime_error("Asset workload operation failed");}
void Fixture(const std::filesystem::path& directory, bool small) {
    using namespace UVE;
    if(std::filesystem::exists(directory)) throw std::runtime_error("Fixture destination exists");
    std::filesystem::create_directories(directory/"content");
    Core::EngineConfigUVE config; config.headlessUVE=true; config.enableConsoleLogging=false;
    config.projectRootDirectoryUVE=directory; config.projectContentRootUVE=directory/"content";
    config.settingsFilePath=directory/"settings"; config.logFilePath=directory/"fixture.log";
    config.assetDatabaseFilePath=directory/".uvassetdb"; config.threadPoolWorkerCount=2;
    Core::EngineCoreUVE engine(config); engine.Init();
    auto& services=engine.GetServicesUVE(); auto& entities=services.GetEntityManagerUVE();
    const auto camera=entities.CreateEntityUVE();
    Scene::TransformComponentUVE transform; transform.localPosition={0,0,4};
    services.GetSceneGraphUVE().AttachTransformUVE(entities,camera,transform);
    entities.AddComponentUVE<Scene::CameraComponentUVE>(camera);
    const auto cube=entities.CreateEntityUVE();
    services.GetSceneGraphUVE().AttachTransformUVE(entities,cube,{});
    Scene::PrimitiveMeshComponentUVE primitive; primitive.baseColor={0.2F,0.5F,0.9F};
    entities.AddComponentUVE<Scene::PrimitiveMeshComponentUVE>(cube,primitive);
    Scene::SceneSerializerUVE serializer;
    Require(serializer.SaveUVE(entities,{camera,cube},directory/"content/main.uvscene",Scene::SceneAssetTypeUVE::Scene));
    Platform::EditorProjectPackageUVE package; package.revision=1; package.projectId="asset-workload";
    package.displayName="Asset Workload"; package.productMetadata.name="Asset Workload";
    package.productMetadata.shortName="asset-workload"; package.contentRoot="content";
    package.applicationSettings.portableUserData=true;
    package.startupScenePath="main.uvscene"; package.assetDatabasePath=".uvassetdb"; package.settingsPath="project.uvsettings";
    Require(Platform::EditorProjectPackageCodecUVE::SaveUVE(directory/"sample.uvproject",package).IsAcceptedUVE());
    Asset::AssetDatabaseUVE db;
    for(unsigned i=0;i<(small ? 0U : 8U);++i) {
        Asset::TextureAssetUVE texture; texture.width=2048; texture.height=1024;
        texture.pixels.resize(static_cast<std::size_t>(texture.width)*texture.height*4);
        for(std::size_t j=0;j<texture.pixels.size();j+=4) {
            texture.pixels[j]=static_cast<std::byte>((j/4+i)%256);
            texture.pixels[j+1]=static_cast<std::byte>((j/8192)%256);
            texture.pixels[j+2]=std::byte{128};texture.pixels[j+3]=std::byte{255};
        }
        const auto relative=std::filesystem::path("content")/("texture-"+std::to_string(i)+".uvtex");
        Require(Asset::SaveTextureAssetUVE(texture,directory/relative));
        static_cast<void>(db.RegisterUVE(relative));
    }
    Asset::TextureAssetUVE surface; surface.width=4; surface.height=4;
    surface.pixels.resize(64);
    for(std::size_t j=0;j<surface.pixels.size();j+=4) {
        surface.pixels[j]=std::byte{64};surface.pixels[j+1]=std::byte{128};
        surface.pixels[j+2]=std::byte{255};surface.pixels[j+3]=std::byte{255};
    }
    Require(Asset::SaveTextureAssetUVE(surface,directory/"content/surface.uvtex"));
    Asset::MaterialAssetUVE material;material.albedoTexture=db.RegisterUVE("content/surface.uvtex");
    Require(Asset::SaveMaterialAssetUVE(material,directory/"content/surface.uvmat"));
    entities.GetComponentUVE<Scene::PrimitiveMeshComponentUVE>(cube).materialGuid=db.RegisterUVE("content/surface.uvmat");
    Require(serializer.SaveUVE(entities,{camera,cube},directory/"content/main.uvscene",Scene::SceneAssetTypeUVE::Scene));
    Require(db.SaveUVE(directory/".uvassetdb"));
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
        Asset::AssetDatabaseUVE db; Require(db.LoadUVE(argv[1])); Require(db.RootPathsUVE(argv[2]));
        Threading::ThreadPoolUVE pool(2); Events::EventSystemUVE events; Asset::AssetManagerUVE manager(pool,events);
        manager.RegisterLoaderUVE<Asset::TextureAssetUVE>(&Asset::LoadTextureAssetUVE,
            [](const auto& v){return Asset::AssetResidentBytesUVE(v);});
        constexpr std::size_t budget=16U*1024*1024; manager.SetResidentBudgetUVE(budget);
        nlohmann::json samples=nlohmann::json::array();
        const auto begin=std::chrono::steady_clock::now();
        Require(manager.GetLoadedAssetCountUVE()==0);
        for(const auto& record:db.GetRegisteredAssetsUVE()) {
            if(record.path.extension()!=".uvtex") continue;
            const auto start=std::chrono::steady_clock::now();
            {
                auto h=manager.LoadUVE<Asset::TextureAssetUVE>(record.guid,db);
                const auto deadline=start+std::chrono::seconds(30);
                while(!h.IsReadyUVE()&&!h.HasFailedUVE()&&std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
                Require(h.IsReadyUVE()); Require(manager.GetResidentBytesUVE()<=budget);
                samples.push_back({{"guid",record.guid.value},{"loadMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()},
                    {"residentBytes",manager.GetResidentBytesUVE()}});
            }
            manager.CollectGarbageUVE(); Require(manager.GetResidentBytesUVE()==0);
        }
        Require(!samples.empty());
        nlohmann::json report{{"schema",1},{"workload","sequential native texture demand load and release"},
            {"budgetBytes",budget},{"peakCountedResidentBytes",manager.GetPeakResidentBytesUVE()},
            {"finalCountedResidentBytes",manager.GetResidentBytesUVE()},
            {"elapsedMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()},
            {"samples",samples},{"physical3GbDeviceVerified",false},{"gpuMemoryMeasured",false}};
        std::ofstream output(argv[3]); output<<report.dump(2); Require(static_cast<bool>(output));return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
