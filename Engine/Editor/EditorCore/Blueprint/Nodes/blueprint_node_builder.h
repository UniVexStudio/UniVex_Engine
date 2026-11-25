// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "blueprint_nodes.h"

namespace UVE::Blueprint::Nodes {

/// Shared node construction helpers used by per-category Blueprint node registration units.
struct BlueprintNodeBuilder final {
    using T = Asset::BlueprintPinType;
    using Role = Asset::BlueprintNodeRole;
    using Pin = Asset::BlueprintPinSpec;
    using NodeType = Asset::BlueprintNodeType;

    std::vector<NodeType>& out;

    [[nodiscard]] static Pin Exec(std::string name = "") {
        return Pin{std::move(name), T::Exec, ""};
    }

    void Add(NodeType node) {
        out.push_back(std::move(node));
    }

    void Event(std::string key, std::string title, std::string ev, std::vector<Pin> data,
               std::string keywords, std::string tip) {
        NodeType n;
        n.key = std::move(key);
        n.title = std::move(title);
        n.category = "Events";
        n.keywords = std::move(keywords);
        n.role = Role::Event;
        n.event = std::move(ev);
        n.outputs.push_back(Exec());
        for (Pin& p : data) {
            n.outputs.push_back(std::move(p));
        }
        n.tooltip = std::move(tip);
        out.push_back(std::move(n));
    }

    void NamedEvent(std::string key, std::string title, std::string category, std::string ev,
                    std::string field, std::string fieldDefault, std::vector<Pin> data,
                    std::string keywords, std::string tip) {
        NodeType n;
        n.key = std::move(key);
        n.title = std::move(title);
        n.category = std::move(category);
        n.keywords = std::move(keywords);
        n.role = Role::Event;
        n.event = std::move(ev);
        n.inputs.push_back(Pin{std::move(field), T::String, std::move(fieldDefault)});
        n.outputs.push_back(Exec());
        for (Pin& p : data) {
            n.outputs.push_back(std::move(p));
        }
        n.tooltip = std::move(tip);
        out.push_back(std::move(n));
    }

    void WidgetEvent(std::string key, std::string title, std::string ev, std::string widget,
                     std::vector<Pin> data, std::string keywords, std::string tip) {
        NodeType n;
        n.key = std::move(key);
        n.title = std::move(title);
        n.category = "User Interface|Events";
        n.keywords = std::move(keywords);
        n.role = Role::Event;
        n.event = std::move(ev);
        n.inputs.push_back(Pin{std::move(widget), T::String, ""});
        n.outputs.push_back(Exec());
        for (Pin& p : data) {
            n.outputs.push_back(std::move(p));
        }
        n.tooltip = std::move(tip);
        out.push_back(std::move(n));
    }

    void Action(std::string key, std::string title, std::string category, std::vector<Pin> data,
                std::string code, std::string keywords = "", std::string tip = "") {
        NodeType n;
        n.key = std::move(key);
        n.title = std::move(title);
        n.category = std::move(category);
        n.keywords = std::move(keywords);
        n.role = Role::Action;
        n.inputs.push_back(Exec());
        for (Pin& p : data) {
            n.inputs.push_back(std::move(p));
        }
        n.outputs.push_back(Exec());
        n.code = std::move(code);
        n.tooltip = std::move(tip);
        out.push_back(std::move(n));
    }

    void ValuedAction(std::string key, std::string title, std::string category,
                      std::vector<Pin> data, std::vector<Pin> values,
                      std::vector<std::string> codes, std::string keywords = "",
                      std::string tip = "") {
        NodeType n;
        n.key = std::move(key);
        n.title = std::move(title);
        n.category = std::move(category);
        n.keywords = std::move(keywords);
        n.role = Role::Action;
        n.inputs.push_back(Exec());
        for (Pin& p : data) {
            n.inputs.push_back(std::move(p));
        }
        n.outputs.push_back(Exec());
        for (Pin& p : values) {
            n.outputs.push_back(std::move(p));
        }
        n.outputCode = std::move(codes);
        n.tooltip = std::move(tip);
        out.push_back(std::move(n));
    }

    void Pure(std::string key, std::string title, std::string category, std::vector<Pin> inputs,
              std::vector<Pin> outputs, std::vector<std::string> codes,
              std::string keywords = "", std::string tip = "") {
        NodeType n;
        n.key = std::move(key);
        n.title = std::move(title);
        n.category = std::move(category);
        n.keywords = std::move(keywords);
        n.role = Role::Pure;
        n.inputs = std::move(inputs);
        n.outputs = std::move(outputs);
        n.outputCode = std::move(codes);
        n.tooltip = std::move(tip);
        out.push_back(std::move(n));
    }

    void Flow(std::string key, std::string title, Role role, std::vector<Pin> inputs,
              std::vector<Pin> outputs, std::string keywords, std::string tip) {
        NodeType n;
        n.key = std::move(key);
        n.title = std::move(title);
        n.category = "Flow Control";
        n.keywords = std::move(keywords);
        n.role = role;
        n.inputs = std::move(inputs);
        n.outputs = std::move(outputs);
        n.tooltip = std::move(tip);
        out.push_back(std::move(n));
    }
};

} // namespace UVE::Blueprint::Nodes
