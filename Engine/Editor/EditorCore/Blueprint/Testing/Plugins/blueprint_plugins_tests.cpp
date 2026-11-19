// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "blueprint_plugin_api.h"
#include "test_scratch.h"

namespace UVE::Blueprint::Tests {
namespace {

class SampleBlueprintPluginUVE final : public Plugins::IBlueprintPluginExtensionUVE {
public:
    SampleBlueprintPluginUVE(std::string id, std::vector<std::string> deps, std::string nodeKey,
                             Plugins::BlueprintPluginApiVersionUVE reqApi = Plugins::kCurrentBlueprintPluginApiVersionUVE)
        : m_id(std::move(id)), m_deps(std::move(deps)), m_nodeKey(std::move(nodeKey)), m_reqApi(reqApi) {}

    Plugins::BlueprintPluginManifestUVE GetManifestUVE() const override {
        Plugins::BlueprintPluginManifestUVE m;
        m.id = m_id;
        m.displayName = "Plugin " + m_id;
        m.author = "UniVex Tests";
        m.requiredApiVersion = m_reqApi;
        m.dependencies = m_deps;
        if (!m_nodeKey.empty()) {
            m.providedNodeKeys.push_back(m_nodeKey);
        }
        return m;
    }

    bool OnLoadUVE() override {
        ++loadCount;
        return true;
    }

    void OnUnloadUVE() override { ++unloadCount; }

    void RegisterNodesUVE(std::vector<Asset::BlueprintNodeTypeUVE>& outNodes) const override {
        if (m_nodeKey.empty()) {
            return;
        }
        Asset::BlueprintNodeTypeUVE node;
        node.key = m_nodeKey;
        node.title = "Custom Plugin Node";
        node.category = "Plugins|Sample";
        node.role = Asset::BlueprintNodeRoleUVE::Pure;
        node.outputs.push_back({"Value", Asset::BlueprintPinTypeUVE::Int, "42"});
        node.outputCode.push_back("42");
        outNodes.push_back(std::move(node));
    }

    int loadCount = 0;
    int unloadCount = 0;

private:
    std::string m_id;
    std::vector<std::string> m_deps;
    std::string m_nodeKey;
    Plugins::BlueprintPluginApiVersionUVE m_reqApi;
};

TEST(BlueprintPluginsUVETest, ApiCompatibilityAndManifestValidation) {
    EXPECT_TRUE(Plugins::IsBlueprintPluginVersionCompatibleUVE({1U, 2U, 0U}, {1U, 0U, 0U}));
    EXPECT_FALSE(Plugins::IsBlueprintPluginVersionCompatibleUVE({1U, 0U, 0U}, {1U, 1U, 0U}));
    EXPECT_FALSE(Plugins::IsBlueprintPluginVersionCompatibleUVE({2U, 0U, 0U}, {1U, 0U, 0U}));

    Plugins::BlueprintPluginManifestUVE bad;
    bad.id = "bad id with spaces";
    bad.displayName = "";
    bad.requiredApiVersion = {9U, 0U, 0U};
    const auto validation = Plugins::ValidateBlueprintPluginManifestUVE(bad);
    EXPECT_FALSE(validation.valid);
    EXPECT_GE(validation.errors.size(), 3U);
}

TEST(BlueprintPluginsUVETest, RegistrationDependencyLoadAndUnload) {
    Plugins::BlueprintPluginManagerUVE manager;

    auto basePlugin = std::make_shared<SampleBlueprintPluginUVE>("com.univex.base", std::vector<std::string>{},
                                                                 "plugin_base_answer");
    auto extPlugin = std::make_shared<SampleBlueprintPluginUVE>(
        "com.univex.ext", std::vector<std::string>{"com.univex.base"}, "plugin_ext_value");
    auto incompatPlugin = std::make_shared<SampleBlueprintPluginUVE>(
        "com.univex.future", std::vector<std::string>{}, "plugin_future_node",
        Plugins::BlueprintPluginApiVersionUVE{2U, 0U, 0U});

    std::string err;
    ASSERT_TRUE(manager.RegisterExtensionUVE(basePlugin, &err)) << err;
    ASSERT_TRUE(manager.RegisterExtensionUVE(extPlugin, &err)) << err;
    EXPECT_FALSE(manager.RegisterExtensionUVE(incompatPlugin, &err));

    // Loading extPlugin automatically loads its dependency basePlugin first.
    ASSERT_TRUE(manager.LoadPluginUVE("com.univex.ext", &err)) << err;
    EXPECT_EQ(basePlugin->loadCount, 1);
    EXPECT_EQ(extPlugin->loadCount, 1);
    EXPECT_EQ(manager.GetRegisteredNodesUVE().size(), 2U);
    EXPECT_NE(manager.FindRegisteredNodeUVE("plugin_base_answer"), nullptr);
    EXPECT_NE(manager.FindRegisteredNodeUVE("plugin_ext_value"), nullptr);

    ASSERT_TRUE(manager.UnloadPluginUVE("com.univex.ext"));
    EXPECT_EQ(extPlugin->unloadCount, 1);
    EXPECT_EQ(manager.FindRegisteredNodeUVE("plugin_ext_value"), nullptr);
    EXPECT_NE(manager.FindRegisteredNodeUVE("plugin_base_answer"), nullptr);
}

TEST(BlueprintPluginsUVETest, DiscoverManifestFilesFromDirectory) {
    const std::filesystem::path scratchDir = UVE::Tests::MakeTestCaseDirectoryUVE("bp_plugins_discovery");
    Plugins::BlueprintPluginManifestUVE manifest;
    manifest.id = "com.univex.discovered";
    manifest.displayName = "Discovered Plugin";
    manifest.author = "UniVex";
    manifest.providedNodeKeys = {"plugin_discovered_node"};

    const std::filesystem::path manifestFile = scratchDir / "sample.uvbpplugin";
    {
        std::ofstream out(manifestFile);
        out << Plugins::SerializeBlueprintPluginManifestJsonUVE(manifest);
    }

    Plugins::BlueprintPluginManagerUVE manager;
    const auto discovered = manager.DiscoverManifestsUVE(scratchDir);
    ASSERT_EQ(discovered.size(), 1U);
    EXPECT_EQ(discovered[0].id, "com.univex.discovered");
    EXPECT_EQ(discovered[0].displayName, "Discovered Plugin");
}

} // namespace
} // namespace UVE::Blueprint::Tests
