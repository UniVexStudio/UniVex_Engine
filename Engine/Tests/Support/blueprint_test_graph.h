// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// A small builder for Blueprint graphs in tests: variables, nodes and wires, then the graph itself, its file bytes (what
// `.uvbp` holds) or a file on disk. A test that needs "a script that counts its ticks" builds that graph here instead
// of writing text.
//
//     Tests::TestGraph g;
//     g.Var("ticks", BlueprintPinType::Int, "0");
//     const auto tick = g.Node("event_tick");
//     const auto add = g.AddToVariable("ticks", 1);          // ticks = ticks + 1
//     g.Exec(tick, add.set);
//     const std::vector<std::byte> bytes = g.Bytes();

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "blueprint_asset.h"
#include "uve_file_envelope.h"

namespace UVE::Tests {

class TestGraph final {
public:
    using PinType = Asset::BlueprintPinType;

    /// The nodes made for "set `variable` to `variable` + `amount`": run `set`, it reads `get` and `add`.
    struct Increment final {
        std::uint32_t set = 0U;
        std::uint32_t get = 0U;
        std::uint32_t add = 0U;
    };

    explicit TestGraph(std::string parentKind = "Object3D") { m_asset.parentKind = std::move(parentKind); }

    /// A variable; `exposed` makes it an export the object's Script component can set.
    TestGraph& Var(std::string name, const PinType type, std::string defaultText = {}, const bool exposed = false) {
        m_asset.variables.push_back(Asset::BlueprintVariable{std::move(name), type, std::move(defaultText), exposed});
        return *this;
    }

    /// A node of palette type `key`; `inputValues` are its unwired inputs' typed values, by pin. Returns its id.
    std::uint32_t Node(const std::string& key, std::vector<std::string> inputValues = {}, const std::string& variable = {}) {
        Asset::BlueprintNode node;
        node.id = m_nextId++;
        node.type = key;
        node.variable = variable;
        node.inputValues = std::move(inputValues);
        node.x = static_cast<float>(m_asset.nodes.size()) * 220.0F;
        m_asset.nodes.push_back(std::move(node));
        return m_asset.nodes.back().id;
    }

    TestGraph& Wire(const std::uint32_t fromNode, const std::uint32_t fromPin, const std::uint32_t toNode,
                    const std::uint32_t toPin) {
        m_asset.wires.push_back(Asset::BlueprintWire{fromNode, fromPin, toNode, toPin});
        return *this;
    }

    /// The exec wire from `from`'s first exec output to `to`'s exec input.
    TestGraph& Exec(const std::uint32_t from, const std::uint32_t to) { return Wire(from, 0U, to, 0U); }

    /// Runs `nodes` one after the other, each after the previous one.
    TestGraph& Chain(std::initializer_list<std::uint32_t> nodes) {
        std::uint32_t previous = 0U;
        for (const std::uint32_t node : nodes) {
            if (previous != 0U) Exec(previous, node);
            previous = node;
        }
        return *this;
    }

    /// `variable = variable + amount` for an Int variable (the get, the add and the set, wired).
    Increment AddToVariable(const std::string& variable, const std::int64_t amount) {
        Increment made;
        made.get = Node("get_variable", {}, variable);
        made.add = Node("int_add", {"", std::to_string(amount)});
        made.set = Node("set_variable", {}, variable);
        Wire(made.get, 0U, made.add, 0U);
        Wire(made.add, 0U, made.set, 1U);
        return made;
    }

    /// `variable = value` with `value` typed in (an unwired input of the Set Variable node).
    std::uint32_t SetVariable(const std::string& variable, std::string value) {
        return Node("set_variable", {"", std::move(value)}, variable);
    }

    [[nodiscard]] const Asset::BlueprintAsset& Graph() const noexcept { return m_asset; }
    [[nodiscard]] Asset::BlueprintAsset& Graph() noexcept { return m_asset; }

    /// The file's bytes, as a `.uvbp` holds them.
    [[nodiscard]] std::vector<std::byte> Bytes() const {
        const std::string text = Asset::SerializeBlueprintAsset(m_asset);
        const auto* const data = reinterpret_cast<const std::byte*>(text.data());
        return Asset::EncodeUveFileEnvelope(Asset::AssetKind::Blueprint, std::vector<std::byte>(data, data + text.size()));
    }

    /// Writes the graph to `path` as a `.uvbp`.
    [[nodiscard]] bool Save(const std::filesystem::path& path) const { return Asset::SaveBlueprintAsset(m_asset, path); }

private:
    Asset::BlueprintAsset m_asset;
    std::uint32_t m_nextId = 1U;
};

} // namespace UVE::Tests
