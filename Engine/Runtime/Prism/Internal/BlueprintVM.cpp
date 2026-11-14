// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "prism/BlueprintVM.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace prism {
using PT = BlueprintPinType;

// ------------------------------------------------------------------ values
BpValue BpValue::parse(PT t, const std::string& s) {
  switch (t) {
    case PT::Bool: return boolean(s == "true" || s == "1");
    case PT::Float: case PT::Int: return number(std::strtod(s.c_str(), nullptr));
    case PT::Vector: case PT::Color: {
      float a = 0, b = 0, c = 0;
      std::sscanf(s.c_str(), "%f,%f,%f", &a, &b, &c);
      return vector(a, b, c);
    }
    case PT::Exec: case PT::Link: case PT::Pose: case PT::Audio: return {};
    default: return string(s);
  }
}
bool BpValue::truthy() const {
  switch (kind) {
    case Bool: case Number: return num != 0;
    case Vector: return v[0] != 0 || v[1] != 0 || v[2] != 0;
    case String: return !str.empty() && str != "false" && str != "None";
    default: return false;
  }
}
double BpValue::number() const {
  switch (kind) {
    case Bool: case Number: return num;
    case Vector: return v[0];
    case String: return std::strtod(str.c_str(), nullptr);
    default: return 0;
  }
}
std::string BpValue::text() const {
  char b[96];
  switch (kind) {
    case Bool: return num != 0 ? "true" : "false";
    case Number:
      if (num == std::floor(num) && std::fabs(num) < 1e15) std::snprintf(b, sizeof b, "%.0f", num);
      else std::snprintf(b, sizeof b, "%g", num);
      return b;
    case Vector: std::snprintf(b, sizeof b, "%g,%g,%g", double(v[0]), double(v[1]), double(v[2])); return b;
    case String: return str;
    default: return "";
  }
}

// ------------------------------------------------------------------ compiler
BpProgram compileBlueprint(const std::vector<BlueprintNode>& nodes, const std::vector<BlueprintWire>& wires,
                           const std::vector<BlueprintVariable>& variables) {
  BpProgram p;
  p.nodes = nodes;
  for (size_t i = 0; i < nodes.size(); i++) {
    if (!p.indexOfId.emplace(nodes[i].id, int(i)).second) p.errors.push_back("duplicate node id " + std::to_string(nodes[i].id));
  }
  p.inputSrc.resize(nodes.size());
  p.execOut.resize(nodes.size());
  for (size_t i = 0; i < nodes.size(); i++) {
    p.inputSrc[i].assign(nodes[i].inputs.size(), {});
    p.execOut[i].assign(nodes[i].outputs.size(), {});
  }
  for (auto& w : wires) {
    auto a = p.indexOfId.find(w.fromNodeId), b = p.indexOfId.find(w.toNodeId);
    if (a == p.indexOfId.end() || b == p.indexOfId.end()) { p.errors.push_back("wire references a missing node"); continue; }
    const BlueprintNode &from = nodes[size_t(a->second)], &to = nodes[size_t(b->second)];
    if (w.fromPinIndex < 0 || w.fromPinIndex >= int(from.outputs.size()) || w.toPinIndex < 0 || w.toPinIndex >= int(to.inputs.size())) {
      p.errors.push_back("wire references a missing pin on '" + from.title + "' -> '" + to.title + "'");
      continue;
    }
    if (from.outputs[size_t(w.fromPinIndex)].type == PT::Exec) p.execOut[size_t(a->second)][size_t(w.fromPinIndex)].push_back(b->second);
    else p.inputSrc[size_t(b->second)][size_t(w.toPinIndex)] = {a->second, w.fromPinIndex};
  }
  for (auto& v : variables) p.variables[v.name] = BpValue::parse(v.type, v.value);
  for (size_t i = 0; i < nodes.size(); i++) if (nodes[i].category == BlueprintNodeCategory::Event) p.events.push_back(int(i));
  std::stable_sort(p.events.begin(), p.events.end(), [&](int x, int y) { return nodes[size_t(x)].pos.y < nodes[size_t(y)].pos.y; });
  // a data cycle through pure nodes can never be evaluated
  std::vector<int> state(nodes.size(), 0);
  std::function<bool(int)> cyc = [&](int n) {
    if (state[size_t(n)] == 1) return true;
    if (state[size_t(n)] == 2) return false;
    state[size_t(n)] = 1;
    bool pure = std::none_of(nodes[size_t(n)].inputs.begin(), nodes[size_t(n)].inputs.end(), [](auto& q) { return q.type == PT::Exec; }) &&
                nodes[size_t(n)].category != BlueprintNodeCategory::Event;
    if (pure) for (auto& s : p.inputSrc[size_t(n)]) if (s.node >= 0 && cyc(s.node)) return true;
    state[size_t(n)] = 2;
    return false;
  };
  for (size_t i = 0; i < nodes.size(); i++) if (cyc(int(i))) { p.errors.push_back("data cycle through '" + nodes[i].title + "'"); break; }
  return p;
}

// ------------------------------------------------------------------ native call surface
static int pinByName(const std::vector<BlueprintPin>& pins, const std::string& name) {
  for (size_t i = 0; i < pins.size(); i++) if (pins[i].name == name) return int(i);
  return -1;
}
BpValue BpCall::in(int pin) const { return pin >= 0 && pin < int(def.inputs.size()) ? vm.input(node, pin) : BpValue{}; }
BpValue BpCall::in(const std::string& n) const { return in(pinByName(def.inputs, n)); }
void BpCall::out(int pin, BpValue v) { if (pin >= 0 && pin < int(def.outputs.size())) vm.m_outVals[{node, pin}] = std::move(v); }
void BpCall::out(const std::string& n, BpValue v) { out(pinByName(def.outputs, n), std::move(v)); }

// ------------------------------------------------------------------ VM
BlueprintVM::BlueprintVM(BpProgram p) : m_p(std::move(p)) { m_vars = m_p.variables; }

void BlueprintVM::reset() { m_trace.clear(); m_log.clear(); m_unhandled.clear(); m_doneOnce.clear(); m_outVals.clear(); m_steps = 0; m_depth = 0; m_aborted = false; }

BpValue BlueprintVM::variable(const std::string& name) const {
  auto it = m_vars.find(name);
  return it == m_vars.end() ? BpValue{} : it->second;
}

int BlueprintVM::firstExecOut(int n) const {
  auto& o = m_p.nodes[size_t(n)].outputs;
  for (size_t i = 0; i < o.size(); i++) if (o[i].type == PT::Exec) return int(i);
  return -1;
}

void BlueprintVM::follow(int n, int outPin) {
  if (outPin < 0 || outPin >= int(m_p.execOut[size_t(n)].size())) return;
  for (int t : std::vector<int>(m_p.execOut[size_t(n)][size_t(outPin)])) exec(t);
}
void BlueprintVM::followNamed(int n, const std::string& pin) {
  auto& o = m_p.nodes[size_t(n)].outputs;
  for (size_t i = 0; i < o.size(); i++) if (o[i].type == PT::Exec && o[i].name == pin) { follow(n, int(i)); return; }
}

BpValue BlueprintVM::input(int n, int pin, int depth) {
  const BlueprintNode& nd = m_p.nodes[size_t(n)];
  if (pin < 0 || pin >= int(nd.inputs.size())) return {};
  auto src = m_p.inputSrc[size_t(n)][size_t(pin)];
  if (src.node >= 0) return output(src.node, src.pin, depth + 1);
  return BpValue::parse(nd.inputs[size_t(pin)].type, nd.inputs[size_t(pin)].defaultValue);
}

// Value of an output pin. Executed nodes latch their outputs; pure nodes are evaluated on demand.
BpValue BlueprintVM::output(int n, int pin, int depth) {
  if (depth > 512 || m_aborted) { m_aborted = true; return {}; }
  const BlueprintNode& nd = m_p.nodes[size_t(n)];
  auto latched = m_outVals.find({n, pin});
  bool impure = nd.category == BlueprintNodeCategory::Event || std::any_of(nd.inputs.begin(), nd.inputs.end(), [](auto& q) { return q.type == PT::Exec; });
  if (impure) return latched != m_outVals.end() ? latched->second : BpValue{};
  const std::string& t = nd.title;
  auto A = [&](int i) { return input(n, i, depth); };
  if (nd.style == BlueprintNodeStyle::Reroute) return A(0);
  if (nd.category == BlueprintNodeCategory::Variable) {            // getter: title (or pin) names the variable
    const std::string& name = nd.outputs.empty() ? t : (nd.outputs[size_t(pin)].name.empty() ? t : nd.outputs[size_t(pin)].name);
    auto it = m_vars.find(name);
    if (it != m_vars.end()) return it->second;
    it = m_vars.find(t);
    return it != m_vars.end() ? it->second : BpValue::parse(nd.outputs[size_t(pin)].type, "");
  }
  if (t == "+") return BpValue::number(A(0).number() + A(1).number());
  if (t == "-") return BpValue::number(A(0).number() - A(1).number());
  if (t == "\xC3\x97" || t == "*") return BpValue::number(A(0).number() * A(1).number());
  if (t == "\xC3\xB7" || t == "/") { double d = A(1).number(); return BpValue::number(d == 0 ? 0 : A(0).number() / d); }   // divide by zero yields 0, never a crash
  if (t == ">") return BpValue::boolean(A(0).number() > A(1).number());
  if (t == "<") return BpValue::boolean(A(0).number() < A(1).number());
  if (t == "==") { BpValue a = A(0), b = A(1); return BpValue::boolean(a.kind == BpValue::String || b.kind == BpValue::String ? a.text() == b.text() : a.number() == b.number()); }
  if (t == "AND") return BpValue::boolean(A(0).truthy() && A(1).truthy());
  if (t == "OR") return BpValue::boolean(A(0).truthy() || A(1).truthy());
  if (t == "NOT") return BpValue::boolean(!A(0).truthy());
  if (t == "Lerp") { double a = A(0).number(), b = A(1).number(), k = A(2).number(); return BpValue::number(a + (b - a) * k); }
  if (t == "Clamp") { double lo = A(1).number(), hi = A(2).number(); if (lo > hi) std::swap(lo, hi); return BpValue::number(std::clamp(A(0).number(), lo, hi)); }
  if (t == "Make Vector") return BpValue::vector(float(A(0).number()), float(A(1).number()), float(A(2).number()));
  if (t == "Break Vector") { BpValue v = A(0); return BpValue::number(pin >= 0 && pin < 3 ? v.v[pin] : 0); }
  auto nat = m_natives.find(t);                                    // host-provided pure function
  if (nat != m_natives.end()) {
    BpCall call{*this, n, nd, {}, false};
    nat->second(call);
    auto it = m_outVals.find({n, pin});
    BpValue r = it != m_outVals.end() ? it->second : BpValue{};
    for (int i = 0; i < int(nd.outputs.size()); i++) m_outVals.erase({n, i});   // pure results are never cached
    return r;
  }
  m_unhandled.insert(t);
  return BpValue::parse(nd.outputs[size_t(pin)].type, "");
}

void BlueprintVM::exec(int n) {
  if (m_aborted) return;
  if (++m_steps > maxSteps || m_depth > 2000) { m_aborted = true; return; }
  m_depth++;
  const BlueprintNode& nd = m_p.nodes[size_t(n)];
  m_trace.push_back(nd.id);
  const std::string& t = nd.title;
  auto IN = [&](const char* name) { return input(n, pinByName(nd.inputs, name)); };
  auto nat = m_natives.find(t);
  if (nat != m_natives.end()) {                                    // host natives win, so built-ins can be overridden
    BpCall call{*this, n, nd, {}, false};
    nat->second(call);
    if (!call.halt) { if (call.next.empty()) follow(n, firstExecOut(n)); else followNamed(n, call.next); }
  } else if (nd.category == BlueprintNodeCategory::Event) {
    follow(n, firstExecOut(n));
  } else if (t == "Branch") {
    followNamed(n, IN("Condition").truthy() ? "True" : "False");
  } else if (t == "Sequence") {
    for (int i = 0; i < int(nd.outputs.size()) && !m_aborted; i++) if (nd.outputs[size_t(i)].type == PT::Exec) follow(n, i);
  } else if (t == "For Loop") {
    long first = long(IN("First Index").number()), last = long(IN("Last Index").number());
    int idx = pinByName(nd.outputs, "Index");
    for (long i = first; i <= last && !m_aborted; i++) {
      m_outVals[{n, idx}] = BpValue::number(double(i));
      followNamed(n, "Loop Body");
    }
    followNamed(n, "Completed");
  } else if (t == "Do Once") {
    if (m_doneOnce.insert(n).second) follow(n, firstExecOut(n));
  } else if (t == "Is Valid") {
    BpValue o = input(n, 1);
    followNamed(n, o.truthy() ? "Is Valid" : "Is Not Valid");
  } else if (t == "Print String") {
    m_log.push_back(IN("In String").text());
    follow(n, firstExecOut(n));
  } else if (nd.category == BlueprintNodeCategory::Variable && nd.inputs.size() >= 2 && nd.inputs[0].type == PT::Exec && nd.inputs[1].type != PT::Exec && t != "Get") {
    const std::string& name = nd.inputs[1].name;                   // setter: the value pin is named after the variable
    BpValue v = input(n, 1);
    m_vars[name] = v;
    for (int i = 0; i < int(nd.outputs.size()); i++) if (nd.outputs[size_t(i)].type != PT::Exec) m_outVals[{n, i}] = v;
    follow(n, firstExecOut(n));
  } else if (t.rfind("Cast To", 0) == 0) {
    BpValue o = input(n, 1);
    for (int i = 0; i < int(nd.outputs.size()); i++) if (nd.outputs[size_t(i)].type != PT::Exec) m_outVals[{n, i}] = o;
    followNamed(n, o.truthy() ? "Cast Succeeded" : "Cast Failed");
  } else {
    // Delay and every engine function without a registered native: record it and keep the chain running.
    if (t != "Delay") m_unhandled.insert(t);
    follow(n, firstExecOut(n));
  }
  m_depth--;
}

bool BlueprintVM::fireNode(int nodeId, const std::vector<BpValue>& args) {
  auto it = m_p.indexOfId.find(nodeId);
  if (it == m_p.indexOfId.end()) return false;
  int n = it->second;
  const BlueprintNode& nd = m_p.nodes[size_t(n)];
  size_t a = 0;
  for (int i = 0; i < int(nd.outputs.size()); i++)
    if (nd.outputs[size_t(i)].type != PT::Exec) m_outVals[{n, i}] = a < args.size() ? args[a++] : BpValue::parse(nd.outputs[size_t(i)].type, "");
  m_depth = 0;
  exec(n);
  return true;
}

bool BlueprintVM::fire(const std::string& title, const std::vector<BpValue>& args) {
  bool any = false;
  for (int e : m_p.events) {
    const BlueprintNode& nd = m_p.nodes[size_t(e)];
    if (nd.title == title || (!nd.subtitle.empty() && nd.subtitle == title)) { fireNode(nd.id, args); any = true; }
  }
  return any;
}

void BlueprintVM::fireAll() { for (int e : std::vector<int>(m_p.events)) fireNode(m_p.nodes[size_t(e)].id); }

} // namespace prism
