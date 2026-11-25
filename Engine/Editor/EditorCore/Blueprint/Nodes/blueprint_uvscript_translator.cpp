// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_nodes.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace UVE::Asset {
namespace {

using T = BlueprintPinType;
using Role = BlueprintNodeRole;
using Pin = BlueprintPinSpec;

[[nodiscard]] std::string Identifier(std::string_view text) {
    std::string out;
    for (const char c : text) {
        out.push_back(std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' ? c : '_');
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out.front())) != 0) {
        out.insert(out.begin(), '_');
    }
    return out;
}

/// An unwired input's typed value as UVScript.
[[nodiscard]] std::string Literal(const T type, const std::string& text) {
    switch (type) {
        case T::Bool: return text == "true" || text == "1" ? "true" : "false";
        case T::Int: {
            try {
                return std::to_string(std::stoll(text.empty() ? "0" : text));
            } catch (...) {
                return "0";
            }
        }
        case T::Float: {
            double value = 0.0;
            try {
                value = text.empty() ? 0.0 : std::stod(text);
            } catch (...) {
            }
            std::ostringstream s;
            s.precision(9);
            s << value;
            std::string out = s.str();
            if (out.find_first_of(".eE") == std::string::npos) {
                out += ".0";
            }
            return out;
        }
        case T::Vector: {
            std::string clean;
            for (const char c : text) {
                if (c != '(' && c != ')' && c != ' ') {
                    clean.push_back(c);
                }
            }
            std::vector<std::string> parts;
            std::stringstream in(clean);
            for (std::string part; std::getline(in, part, ',');) {
                parts.push_back(part);
            }
            parts.resize(3U);
            return "vec3(" + Literal(T::Float, parts[0]) + ", " + Literal(T::Float, parts[1]) + ", " + Literal(T::Float, parts[2]) + ")";
        }
        case T::String: {
            std::string out = "\"";
            for (const char c : text) {
                if (c == '"' || c == '\\') {
                    out.push_back('\\');
                }
                if (c == '{' || c == '}') {
                    out.push_back(c);   // doubled: a typed value is literal text, never interpolation
                }
                out.push_back(c == '\n' ? ' ' : c);
            }
            return out + "\"";
        }
        // An unwired object pin means this object ("self"), as the palette's tooltips say.
        case T::Object: return "world.self()";
        case T::ObjectArray:
        case T::IntArray:
        case T::FloatArray:
        case T::StringArray: return "[]";
        case T::Exec: break;
    }
    return "none";
}

/// Fills `{n}` in `pattern` with `args[n]`.
[[nodiscard]] std::string Format(const std::string& pattern, const std::vector<std::string>& args) {
    std::string out;
    for (std::size_t i = 0U; i < pattern.size(); ++i) {
        if (pattern[i] == '{') {
            const std::size_t close = pattern.find('}', i);
            if (close != std::string::npos) {
                const std::string index = pattern.substr(i + 1U, close - i - 1U);
                if (!index.empty() && std::all_of(index.begin(), index.end(), [](const char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
                    const std::size_t n = static_cast<std::size_t>(std::stoul(index));
                    out += n < args.size() ? args[n] : std::string{"none"};
                    i = close;
                    continue;
                }
            }
        }
        out.push_back(pattern[i]);
    }
    return out;
}

/// The timer a Retriggerable Delay node waits on.
[[nodiscard]] std::string RetriggerTimer(const std::uint32_t id) { return "__delay" + std::to_string(id); }

class Translator final {
public:
    explicit Translator(const BlueprintAsset& blueprint) : m_bp(blueprint) {
        for (const BlueprintNode& node : m_bp.nodes) {
            m_nodes[node.id] = &node;
        }
    }

    BlueprintTranslation Run(const std::string_view objectName, const std::string_view objectKind) {
        Line("entity " + Identifier(objectName.empty() ? std::string_view{"Object"} : objectName) + " : " + std::string(objectKind), 0U);
        Line("", 0U);
        for (const BlueprintVariable& variable : m_bp.variables) {
            const std::string keyword = variable.exposed ? "export" : "var";
            const std::string init = variable.type == T::Object ? std::string{} : " = " + Literal(variable.type, variable.defaultText);
            Line(keyword + " " + Identifier(variable.name) + ": " + std::string(BlueprintPinTypeScriptName(variable.type)) + init, 0U);
        }
        // Event chains, grouped by event so two Event Tick nodes run one after the other.
        std::map<std::string, std::vector<const BlueprintNode*>> byEvent;
        std::vector<std::string> order;
        for (const BlueprintNode& node : m_bp.nodes) {
            const BlueprintNodeType* const type = FindBlueprintNodeType(node.type);
            if (type == nullptr) {
                m_out.problems.emplace_back(node.id, "unknown node \"" + node.type + "\"");
                continue;
            }
            if (type->role == Role::Event) {
                if (!byEvent.contains(type->event)) {
                    order.push_back(type->event);
                }
                byEvent[type->event].push_back(&node);
            }
            if (type->role == Role::RetriggerableDelay) {
                m_delays.push_back(node.id);
            }
        }
        // A Retriggerable Delay's Completed runs when its timer does.
        if (!m_delays.empty() && !byEvent.contains("timer")) {
            order.push_back("timer");
            byEvent["timer"];
        }
        // Body first (it declares the Do Once / Flip Flop fields), then fields, then the body.
        std::vector<std::pair<std::string, std::uint32_t>> body;
        std::swap(body, m_lines);
        for (const std::string& event : order) {
            const auto& nodes = byEvent[event];
            const BlueprintNodeType* const type =
                nodes.empty() ? FindBlueprintNodeType("event_timer") : FindBlueprintNodeType(nodes.front()->type);
            const std::uint32_t at = nodes.empty() ? m_delays.front() : nodes.front()->id;
            std::string params;
            for (std::size_t i = 1U; i < type->outputs.size(); ++i) {
                params += (params.empty() ? "" : ", ") + std::string("arg") + std::to_string(i);
            }
            Line("", 0U);
            Line("on " + event + (params.empty() ? std::string{} : "(" + params + ")") + ":", at);
            const std::size_t before = m_lines.size();
            for (const BlueprintNode* node : nodes) {
                // A widget event names its widget: only that one's (empty: every widget's).
                const std::string widget = !type->inputs.empty() && !node->inputValues.empty() ? node->inputValues[0] : std::string{};
                if (!widget.empty() && type->outputs.size() > 1U) {
                    Line("    if arg1 == " + Literal(T::String, widget) + ":", node->id);
                    EmitBlock(node->id, 0U, 2);
                } else {
                    EmitFrom(node->id, 0U, 1);
                }
            }
            if (event == "timer") {
                for (const std::uint32_t delay : m_delays) {
                    Line("    if arg1 == \"" + RetriggerTimer(delay) + "\":", delay);
                    EmitBlock(delay, 0U, 2);
                }
            }
            if (m_lines.size() == before) {
                Line("    pass", at);
            }
        }
        std::vector<std::pair<std::string, std::uint32_t>> handlers;
        std::swap(handlers, m_lines);
        m_lines = std::move(body);
        for (const std::string& field : m_fields) {
            Line(field, 0U);
        }
        for (auto& line : handlers) {
            m_lines.push_back(std::move(line));
        }
        for (const auto& [text, node] : m_lines) {
            m_out.source += text + "\n";
            m_out.lineNodes.push_back(node);
        }
        return std::move(m_out);
    }

private:
    void Line(std::string text, const std::uint32_t node) { m_lines.emplace_back(std::move(text), node); }
    /// The field an action's value output `pin` is kept in (`ret7`, then `ret7_2` ...).
    static std::string ReturnField(const std::string& tag, const std::uint32_t pin) {
        return pin <= 1U ? "ret" + tag : "ret" + tag + "_" + std::to_string(pin);
    }
    /// A field once per node, however many exec wires lead into it.
    void FieldOnce(const std::string& name, const std::string& declaration) {
        if (m_declared.insert(name).second) {
            m_fields.push_back(declaration);
        }
    }
    /// An unwired input's typed value (a field's starting value cannot be an expression).
    [[nodiscard]] std::string TypedValue(const BlueprintNode& node, const std::uint32_t pin) const {
        const BlueprintNodeType* const type = FindBlueprintNodeType(node.type);
        if (type == nullptr || pin >= type->inputs.size()) {
            return "none";
        }
        const std::string& typed = pin < node.inputValues.size() && !node.inputValues[pin].empty() ? node.inputValues[pin] : type->inputs[pin].defaultText;
        return Literal(type->inputs[pin].type, typed);
    }
    static std::string Indent(const int depth) { return std::string(static_cast<std::size_t>(depth) * 4U, ' '); }

    [[nodiscard]] const BlueprintWire* WireFromOutput(const std::uint32_t node, const std::uint32_t pin) const {
        for (const BlueprintWire& wire : m_bp.wires) {
            if (wire.fromNode == node && wire.fromPin == pin) {
                return &wire;
            }
        }
        return nullptr;
    }
    [[nodiscard]] const BlueprintWire* WireToInput(const std::uint32_t node, const std::uint32_t pin) const {
        for (const BlueprintWire& wire : m_bp.wires) {
            if (wire.toNode == node && wire.toPin == pin) {
                return &wire;
            }
        }
        return nullptr;
    }

    /// Follows the exec wire out of `node`'s output `pin` and writes what it leads to.
    void EmitFrom(const std::uint32_t node, const std::uint32_t pin, const int depth) {
        if (const BlueprintWire* wire = WireFromOutput(node, pin)) {
            EmitNode(wire->toNode, depth, wire->toPin);
        }
    }

    /// Writes a block; an empty one gets `pass`.
    void EmitBlock(const std::uint32_t node, const std::uint32_t pin, const int depth) {
        const std::size_t before = m_lines.size();
        EmitFrom(node, pin, depth);
        if (m_lines.size() == before) {
            Line(Indent(depth) + "pass", node);
        }
    }

    void EmitNode(const std::uint32_t id, const int depth, const std::uint32_t entry = 0U) {
        const auto found = m_nodes.find(id);
        if (found == m_nodes.end()) {
            return;
        }
        if (m_active.contains(id) || depth > 64) {
            m_out.problems.emplace_back(id, "its exec wires lead back into themselves");
            return;
        }
        const BlueprintNode& node = *found->second;
        const BlueprintNodeType* const type = FindBlueprintNodeType(node.type);
        if (type == nullptr) {
            return;
        }
        m_active.insert(id);
        const std::string pad = Indent(depth);
        const std::string tag = std::to_string(id);
        switch (type->role) {
            case Role::Action: {
                std::vector<std::string> args;
                for (std::uint32_t i = 0U; i < type->inputs.size(); ++i) {
                    args.push_back(type->inputs[i].type == T::Exec ? std::string{} : InputExpr(node, i));
                }
                if (!type->outputCode.empty() && type->outputs.size() > 1U) {
                    // An action with values: each kept in a field for the nodes after it, the
                    // first from the call itself and the rest read right after it.
                    for (std::uint32_t out = 1U; out < type->outputs.size() && out - 1U < type->outputCode.size(); ++out) {
                        const T valueType = type->outputs[out].type;
                        const std::string field = ReturnField(tag, out);
                        if (m_declared.insert(field).second) {
                            m_fields.push_back("var " + field + ": " + std::string(BlueprintPinTypeScriptName(valueType)) +
                                               (valueType == T::Object ? std::string{} : " = " + Literal(valueType, "")));
                        }
                        Line(pad + field + " = " + Format(type->outputCode[out - 1U], args), id);
                    }
                } else {
                    Line(pad + Format(type->code, args), id);
                }
                EmitFrom(id, 0U, depth);
                break;
            }
            case Role::SetVariable: {
                Line(pad + Identifier(node.variable) + " = " + InputExpr(node, 1U), id);
                EmitFrom(id, 0U, depth);
                break;
            }
            case Role::Branch: {
                Line(pad + "if " + InputExpr(node, 1U) + ":", id);
                EmitBlock(id, 0U, depth + 1);
                if (WireFromOutput(id, 1U) != nullptr) {
                    Line(pad + "else:", id);
                    EmitBlock(id, 1U, depth + 1);
                }
                break;
            }
            case Role::Sequence:
                for (std::uint32_t pin = 0U; pin < type->outputs.size(); ++pin) {
                    EmitFrom(id, pin, depth);
                }
                break;
            case Role::ForLoop:
                Line(pad + "for i" + tag + " in " + InputExpr(node, 1U) + "..(" + InputExpr(node, 2U) + " + 1):", id);
                EmitBlock(id, 0U, depth + 1);
                EmitFrom(id, 2U, depth);
                break;
            case Role::While:
                Line(pad + "while " + InputExpr(node, 1U) + ":", id);
                EmitBlock(id, 0U, depth + 1);
                EmitFrom(id, 1U, depth);
                break;
            case Role::DoOnce:
                FieldOnce("once" + tag, "var once" + tag + " = false");
                Line(pad + "if not once" + tag + ":", id);
                Line(pad + "    once" + tag + " = true", id);
                EmitFrom(id, 0U, depth + 1);
                break;
            case Role::FlipFlop:
                FieldOnce("flip" + tag, "var flip" + tag + " = false");
                Line(pad + "flip" + tag + " = not flip" + tag, id);
                Line(pad + "if flip" + tag + ":", id);
                EmitBlock(id, 0U, depth + 1);
                if (WireFromOutput(id, 1U) != nullptr) {
                    Line(pad + "else:", id);
                    EmitBlock(id, 1U, depth + 1);
                }
                break;
            case Role::Delay:
                Line(pad + "wait " + InputExpr(node, 1U), id);
                EmitFrom(id, 0U, depth);
                break;
            case Role::IsValid:
                Line(pad + "if " + InputExpr(node, 1U) + " != none:", id);
                EmitBlock(id, 0U, depth + 1);
                if (WireFromOutput(id, 1U) != nullptr) {
                    Line(pad + "else:", id);
                    EmitBlock(id, 1U, depth + 1);
                }
                break;
            case Role::Gate: {
                // Open unless "Start Closed": Enter passes only while open.
                FieldOnce("gate" + tag, std::string("var gate") + tag + " = " + (TypedValue(node, 4U) == "true" ? "false" : "true"));
                if (entry == 1U) {
                    Line(pad + "gate" + tag + " = true", id);
                } else if (entry == 2U) {
                    Line(pad + "gate" + tag + " = false", id);
                } else if (entry == 3U) {
                    Line(pad + "gate" + tag + " = not gate" + tag, id);
                } else {
                    Line(pad + "if gate" + tag + ":", id);
                    EmitBlock(id, 0U, depth + 1);
                }
                break;
            }
            case Role::DoN: {
                FieldOnce("count" + tag, "var count" + tag + ": int = 0");
                if (entry == 2U) {
                    Line(pad + "count" + tag + " = 0", id);
                } else {
                    Line(pad + "if count" + tag + " < " + InputExpr(node, 1U) + ":", id);
                    Line(pad + "    count" + tag + " += 1", id);
                    EmitFrom(id, 0U, depth + 1);
                }
                break;
            }
            case Role::MultiGate: {
                FieldOnce("step" + tag, "var step" + tag + ": int = 0");
                const std::uint32_t outs = static_cast<std::uint32_t>(type->outputs.size());
                if (entry == 1U) {
                    Line(pad + "step" + tag + " = 0", id);
                    break;
                }
                // Taken before it runs on, so a Delay further down cannot run an output twice.
                Line(pad + "if " + InputExpr(node, 2U) + " and step" + tag + " >= " + std::to_string(outs) + ":", id);
                Line(pad + "    step" + tag + " = 0", id);
                Line(pad + "let at" + tag + " = step" + tag, id);
                Line(pad + "step" + tag + " += 1", id);
                bool first = true;
                for (std::uint32_t out = 0U; out < outs; ++out) {
                    if (WireFromOutput(id, out) == nullptr) {
                        continue;
                    }
                    Line(pad + (first ? "if " : "elif ") + "at" + tag + " == " + std::to_string(out) + ":", id);
                    EmitBlock(id, out, depth + 1);
                    first = false;
                }
                break;
            }
            case Role::RetriggerableDelay:
                // A timer of its own: entering again starts it over; what follows runs in `on timer`.
                Line(pad + "timers.set(\"" + RetriggerTimer(id) + "\", " + InputExpr(node, 1U) + ", false)", id);
                break;
            case Role::SwitchInt:
            case Role::SwitchString: {
                const bool text = type->role == Role::SwitchString;
                const std::uint32_t cases = static_cast<std::uint32_t>(type->outputs.size()) - 1U;
                Line(pad + "let pick" + tag + " = " + InputExpr(node, 1U), id);
                bool first = true;
                for (std::uint32_t out = 0U; out < cases; ++out) {
                    if (WireFromOutput(id, out) == nullptr) {
                        continue;
                    }
                    const std::string value = text ? InputExpr(node, 2U + out) : std::to_string(out);
                    Line(pad + (first ? "if " : "elif ") + "pick" + tag + " == " + value + ":", id);
                    EmitBlock(id, out, depth + 1);
                    first = false;
                }
                if (WireFromOutput(id, cases) != nullptr) {
                    if (first) {
                        EmitFrom(id, cases, depth);
                    } else {
                        Line(pad + "else:", id);
                        EmitBlock(id, cases, depth + 1);
                    }
                }
                break;
            }
            case Role::ForEach: {
                // Over a copy taken once, so the body may change the array it walks.
                const std::string items = "items" + tag;
                FieldOnce(items, "var " + items + ": " + std::string(BlueprintPinTypeScriptName(type->inputs[1].type)) + " = []");
                Line(pad + items + " = " + InputExpr(node, 1U), id);
                Line(pad + "for i" + tag + " in 0.." + items + ".length():", id);
                Line(pad + "    let e" + tag + " = " + items + "[i" + tag + "]", id);
                EmitFrom(id, 0U, depth + 1);
                EmitFrom(id, 3U, depth);
                break;
            }
            case Role::Event:
                m_out.problems.emplace_back(id, "an event cannot be wired into");
                break;
            case Role::Pure:
            case Role::GetVariable:
                m_out.problems.emplace_back(id, "has no exec input");
                break;
        }
        m_active.erase(id);
    }

    /// The expression for input `pin` of `node`: the wired output's, else the typed value.
    [[nodiscard]] std::string InputExpr(const BlueprintNode& node, const std::uint32_t pin) {
        const BlueprintNodePins pins = ResolveBlueprintNodePins(m_bp, node);
        if (const BlueprintWire* wire = WireToInput(node.id, pin)) {
            return OutputExpr(wire->fromNode, wire->fromPin);
        }
        if (pin >= pins.inputs.size()) {
            return "none";
        }
        const std::string& typed = pin < node.inputValues.size() && !node.inputValues[pin].empty() ? node.inputValues[pin] : pins.inputs[pin].defaultText;
        return Literal(pins.inputs[pin].type, typed);
    }

    [[nodiscard]] std::string OutputExpr(const std::uint32_t id, const std::uint32_t pin) {
        const auto found = m_nodes.find(id);
        if (found == m_nodes.end()) {
            return "none";
        }
        const BlueprintNode& node = *found->second;
        const BlueprintNodeType* const type = FindBlueprintNodeType(node.type);
        if (type == nullptr) {
            return "none";
        }
        const std::string tag = std::to_string(id);
        switch (type->role) {
            case Role::Event: return "arg" + std::to_string(pin);
            case Role::Action: return type->outputCode.empty() ? std::string{"none"} : ReturnField(tag, pin);
            case Role::DoN: return "count" + tag;
            case Role::ForEach: return pin == 1U ? "e" + tag : "i" + tag;
            case Role::GetVariable:
            case Role::SetVariable: return Identifier(node.variable);
            case Role::ForLoop: return "i" + tag;
            case Role::FlipFlop: return "flip" + tag;
            case Role::Pure: {
                if (m_pureDepth > 64 || pin >= type->outputCode.size()) {
                    m_out.problems.emplace_back(id, "its data wires lead back into themselves");
                    return "none";
                }
                ++m_pureDepth;
                std::vector<std::string> args;
                for (std::uint32_t i = 0U; i < type->inputs.size(); ++i) {
                    args.push_back(InputExpr(node, i));
                }
                --m_pureDepth;
                return "(" + Format(type->outputCode[pin], args) + ")";
            }
            default: return "none";
        }
    }

    const BlueprintAsset& m_bp;
    std::map<std::uint32_t, const BlueprintNode*> m_nodes;
    std::vector<std::pair<std::string, std::uint32_t>> m_lines;
    std::vector<std::string> m_fields;
    std::set<std::uint32_t> m_active;
    std::set<std::string> m_declared;
    std::vector<std::uint32_t> m_delays;
    int m_pureDepth = 0;
    BlueprintTranslation m_out;
};

} // namespace

std::string_view BlueprintPinTypeScriptName(const BlueprintPinType type) noexcept {
    switch (type) {
        case T::Bool: return "bool";
        case T::Int: return "int";
        case T::Float: return "float";
        case T::Vector: return "vec3";
        case T::String: return "str";
        case T::Object: return "Object3D";
        case T::ObjectArray: return "list[Object3D]";
        case T::IntArray: return "list[int]";
        case T::FloatArray: return "list[float]";
        case T::StringArray: return "list[str]";
        case T::Exec: break;
    }
    return "none";
}

BlueprintNodePins ResolveBlueprintNodePins(const BlueprintAsset& blueprint, const BlueprintNode& node) {
    BlueprintNodePins pins;
    const BlueprintNodeType* const type = FindBlueprintNodeType(node.type);
    if (type == nullptr) {
        return pins;
    }
    pins.inputs = type->inputs;
    pins.outputs = type->outputs;
    if (type->role == Role::GetVariable || type->role == Role::SetVariable) {
        const auto variable = std::find_if(blueprint.variables.begin(), blueprint.variables.end(),
                                           [&node](const BlueprintVariable& v) { return v.name == node.variable; });
        const T value = variable != blueprint.variables.end() ? variable->type : T::Float;
        const std::string name = variable != blueprint.variables.end() ? variable->name : node.variable;
        for (Pin& pin : pins.inputs) {
            if (pin.type != T::Exec) {
                pin.type = value;
                pin.name = name;
                pin.defaultText = variable != blueprint.variables.end() ? variable->defaultText : std::string{};
            }
        }
        for (Pin& pin : pins.outputs) {
            if (pin.type != T::Exec) {
                pin.type = value;
                pin.name = name;
            }
        }
    }
    return pins;
}

BlueprintTranslation TranslateBlueprintToUVScript(const BlueprintAsset& blueprint, const std::string_view objectName,
                                                       const std::string_view objectKind) {
    Translator translator(blueprint);
    return translator.Run(objectName, objectKind);
}

} // namespace UVE::Asset
