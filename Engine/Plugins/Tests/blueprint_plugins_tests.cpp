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

class SampleBlueprintPlugin final : public Plugins::IBlueprintPluginExtension {
public:
    SampleBlueprintPlugin(std::string id, std::vector<std::string> deps, std::string nodeKey,
                             Plugins::BlueprintPluginApiVersion reqApi = Plugins::kCurrentBlueprintPluginApiVersion)
        : m_id(std::move(id)), m_deps(std::move(deps)), m_nodeKey(std::move(nodeKey)), m_reqApi(reqApi) {}

    Plugins::BlueprintPluginManifest GetManifest() const override {
        Plugins::BlueprintPluginManifest m;
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

    bool OnLoad() override {
        ++loadCount;
        return true;
    }

    void OnUnload() override { ++unloadCount; }

    void RegisterNodes(std::vector<Asset::BlueprintNodeType>& outNodes) const override {
        if (m_nodeKey.empty()) {
            return;
        }
        Asset::BlueprintNodeType node;
        node.key = m_nodeKey;
        node.title = "Custom Plugin Node";
        node.category = "Plugins|Sample";
        node.role = Asset::BlueprintNodeRole::Pure;
        node.outputs.push_back({"Value", Asset::BlueprintPinType::Int, "42"});
        node.outputCode.push_back("42");
        outNodes.push_back(std::move(node));
    }

    int loadCount = 0;
    int unloadCount = 0;

private:
    std::string m_id;
    std::vector<std::string> m_deps;
    std::string m_nodeKey;
    Plugins::BlueprintPluginApiVersion m_reqApi;
};

TEST(BlueprintPluginsTest, ApiCompatibilityAndManifestValidation) {
    EXPECT_TRUE(Plugins::IsBlueprintPluginVersionCompatible({1U, 2U, 0U}, {1U, 0U, 0U}));
    EXPECT_FALSE(Plugins::IsBlueprintPluginVersionCompatible({1U, 0U, 0U}, {1U, 1U, 0U}));
    EXPECT_FALSE(Plugins::IsBlueprintPluginVersionCompatible({2U, 0U, 0U}, {1U, 0U, 0U}));

    Plugins::BlueprintPluginManifest bad;
    bad.id = "bad id with spaces";
    bad.displayName = "";
    bad.requiredApiVersion = {9U, 0U, 0U};
    const auto validation = Plugins::ValidateBlueprintPluginManifest(bad);
    EXPECT_FALSE(validation.valid);
    EXPECT_GE(validation.errors.size(), 3U);
}

TEST(BlueprintPluginsTest, RegistrationDependencyLoadAndUnload) {
    Plugins::BlueprintPluginManager manager;

    auto basePlugin = std::make_shared<SampleBlueprintPlugin>("com.univex.base", std::vector<std::string>{},
                                                                 "plugin_base_answer");
    auto extPlugin = std::make_shared<SampleBlueprintPlugin>(
        "com.univex.ext", std::vector<std::string>{"com.univex.base"}, "plugin_ext_value");
    auto incompatPlugin = std::make_shared<SampleBlueprintPlugin>(
        "com.univex.future", std::vector<std::string>{}, "plugin_future_node",
        Plugins::BlueprintPluginApiVersion{2U, 0U, 0U});

    std::string err;
    ASSERT_TRUE(manager.RegisterExtension(basePlugin, &err)) << err;
    ASSERT_TRUE(manager.RegisterExtension(extPlugin, &err)) << err;
    EXPECT_FALSE(manager.RegisterExtension(incompatPlugin, &err));

    // Loading extPlugin automatically loads its dependency basePlugin first.
    ASSERT_TRUE(manager.LoadPlugin("com.univex.ext", &err)) << err;
    EXPECT_EQ(basePlugin->loadCount, 1);
    EXPECT_EQ(extPlugin->loadCount, 1);
    EXPECT_EQ(manager.GetRegisteredNodes().size(), 2U);
    EXPECT_NE(manager.FindRegisteredNode("plugin_base_answer"), nullptr);
    EXPECT_NE(manager.FindRegisteredNode("plugin_ext_value"), nullptr);

    ASSERT_TRUE(manager.UnloadPlugin("com.univex.ext"));
    EXPECT_EQ(extPlugin->unloadCount, 1);
    EXPECT_EQ(manager.FindRegisteredNode("plugin_ext_value"), nullptr);
    EXPECT_NE(manager.FindRegisteredNode("plugin_base_answer"), nullptr);
}

TEST(BlueprintPluginsTest, DiscoverManifestFilesFromDirectory) {
    const std::filesystem::path scratchDir = UVE::Tests::MakeTestCaseDirectory("bp_plugins_discovery");
    Plugins::BlueprintPluginManifest manifest;
    manifest.id = "com.univex.discovered";
    manifest.displayName = "Discovered Plugin";
    manifest.author = "UniVex";
    manifest.providedNodeKeys = {"plugin_discovered_node"};

    const std::filesystem::path manifestFile = scratchDir / "sample.uvbpplugin";
    {
        std::ofstream out(manifestFile);
        out << Plugins::SerializeBlueprintPluginManifestJson(manifest);
    }

    Plugins::BlueprintPluginManager manager;
    const auto discovered = manager.DiscoverManifests(scratchDir);
    ASSERT_EQ(discovered.size(), 1U);
    EXPECT_EQ(discovered[0].id, "com.univex.discovered");
    EXPECT_EQ(discovered[0].displayName, "Discovered Plugin");
}

} // namespace
} // namespace UVE::Blueprint::Tests
