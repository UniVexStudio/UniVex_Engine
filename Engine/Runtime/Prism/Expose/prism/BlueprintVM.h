// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Blueprint compiler + virtual machine. Turns the node graph an artist edits into a program the engine runs.
//
//   BpProgram prog = compileBlueprint(nodes, wires, variables);     // or graph.program()
//   BlueprintVM vm(prog);
//   vm.registerNative("Apply Damage", [&](BpCall& c) { engine.damage(c.in("Damaged Actor").str, c.in("Base Damage").num); });
//   vm.fire("Event BeginPlay");                                     // runs the exec chain, pure nodes are pulled on demand
//
// Built in: events, Branch, Sequence, For Loop, Do Once, Delay (pass-through), Is Valid, casts, variables (Get / Set),
// Print String, float / bool math, comparisons, Lerp, Clamp, Make / Break Vector, reroutes. Every other impure node is a
// *native*: the host registers a callback by node title; unregistered natives pass execution through their first exec pin
// and are reported in `unhandled()` so nothing fails silently.
#pragma once
#include "BlueprintGraph.h"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace prism {

struct BpValue {
  enum Kind { None, Bool, Number, Vector, String } kind = None;
  double num = 0;                 // Bool (0/1), Number
  float v[3] = {0, 0, 0};         // Vector / Color
  std::string str;                // String / Name / Object reference
  static BpValue boolean(bool b) { BpValue x; x.kind = Bool; x.num = b ? 1 : 0; return x; }
  static BpValue number(double n) { BpValue x; x.kind = Number; x.num = n; return x; }
  static BpValue vector(float a, float b, float c) { BpValue x; x.kind = Vector; x.v[0] = a; x.v[1] = b; x.v[2] = c; return x; }
  static BpValue string(std::string s) { BpValue x; x.kind = String; x.str = std::move(s); return x; }
  static BpValue parse(BlueprintPinType t, const std::string& text);   // a pin's inline default
  bool truthy() const;
  double number() const;
  std::string text() const;
};

struct BpProgram {
  struct Src { int node = -1, pin = -1; };                 // index into `nodes`
  std::vector<BlueprintNode> nodes;
  std::vector<std::vector<Src>> inputSrc;                  // [node][input pin] -> producing output (or -1)
  std::vector<std::vector<std::vector<int>>> execOut;      // [node][output pin] -> target node indices (exec pins only)
  std::map<int, int> indexOfId;
  std::map<std::string, BpValue> variables;                // defaults
  std::vector<int> events;                                 // node indices, top-to-bottom
  std::vector<std::string> errors;                         // structural problems found while compiling
  bool ok() const { return errors.empty(); }
};

BpProgram compileBlueprint(const std::vector<BlueprintNode>& nodes, const std::vector<BlueprintWire>& wires,
                           const std::vector<BlueprintVariable>& variables = {});

class BlueprintVM;
struct BpCall {                                            // what a native sees
  BlueprintVM& vm;
  int node;                                                // program node index
  const BlueprintNode& def;
  BpValue in(int pin) const;
  BpValue in(const std::string& pinName) const;
  void out(int pin, BpValue v);
  void out(const std::string& pinName, BpValue v);
  void then(const std::string& execPinName) { next = execPinName; }   // which exec output continues (default: first)
  std::string next;
  bool halt = false;                                       // stop this exec chain (latent actions the host resumes itself)
};

class BlueprintVM {
public:
  using Native = std::function<void(BpCall&)>;
  explicit BlueprintVM(BpProgram p);

  void registerNative(const std::string& nodeTitle, Native fn) { m_natives[nodeTitle] = std::move(fn); }
  // Runs every event node whose title matches. `args` fill the event's data outputs in order. False if none matched.
  bool fire(const std::string& eventTitle, const std::vector<BpValue>& args = {});
  bool fireNode(int nodeId, const std::vector<BpValue>& args = {});
  void fireAll();                                          // every event, top to bottom (editor "Simulate")

  BpValue variable(const std::string& name) const;
  void setVariable(const std::string& name, BpValue v) { m_vars[name] = std::move(v); }
  const std::vector<int>& trace() const { return m_trace; }            // node ids in the order they executed
  const std::vector<std::string>& log() const { return m_log; }        // Print String output
  const std::set<std::string>& unhandled() const { return m_unhandled; }
  bool aborted() const { return m_aborted; }                           // step budget exhausted (runaway loop)
  int maxSteps = 200000;
  void reset();                                            // clears trace / log / Do Once state; keeps variables
  const BpProgram& program() const { return m_p; }
private:
  friend struct BpCall;
  void exec(int node);
  void follow(int node, int outPin);
  void followNamed(int node, const std::string& pin);
  int firstExecOut(int node) const;
  BpValue input(int node, int pin, int depth = 0);
  BpValue output(int node, int pin, int depth);
  BpProgram m_p;
  std::map<std::string, Native> m_natives;
  std::map<std::string, BpValue> m_vars;
  std::map<std::pair<int, int>, BpValue> m_outVals;       // values latched by executed (impure / event) nodes
  std::set<int> m_doneOnce;
  std::vector<int> m_trace;
  std::vector<std::string> m_log;
  std::set<std::string> m_unhandled;
  int m_steps = 0, m_depth = 0;
  bool m_aborted = false;
};

} // namespace prism
