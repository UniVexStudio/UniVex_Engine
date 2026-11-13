// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - Blueprint graph editor: typed exec/data pins, PRISM-style nodes (header / compact "Get" / reroute),
// coloured spline wires, comment boxes, search palette with pin-aware filtering, undo/redo, copy/paste,
// serialisation, validation ("compile") and an animated execution trace.
//
// Everything is drawn with PRISM's Canvas, so it renders identically on the CPU rasterizer and on any GPU backend.
#pragma once
#include "Controls.h"
#include "Transaction.h"
#include "Widget.h"
#include "Icons.h"
#include <functional>
#include <optional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace prism {

enum class BlueprintPinType {
  Exec,     // white flow arrow
  Float,
  Vector,
  Color,
  Bool,
  Object,
  String,
  Int,
  Name,
  Wildcard, // takes the type of whatever it is wired to (reroute, select, ...)
  Pose,     // animation pose (Animation Graph)
  Link,     // structural link: state-machine transitions, behavior-tree children, VFX module stack
  Audio,    // audio signal (Audio Graph)
};

enum class BlueprintPinKind { Input, Output };

struct BlueprintPin {
  std::string name;
  BlueprintPinType type = BlueprintPinType::Exec;
  BlueprintPinKind kind = BlueprintPinKind::Input;
  std::string defaultValue;   // inline value shown in a box while an input is unconnected ("Self", "0.5", "true")
  bool hideLabel = false;
  bool required = false;      // unconnected required inputs are reported by compile()
  bool editable = false;      // keeps its value box even while empty (text a user may clear)
};

enum class BlueprintNodeCategory {
  Event,     // red header
  Function,  // blue header (impure function)
  PureMath,  // green header (pure function, no exec pins)
  Material,  // bronze
  Variable,  // violet
  Texture,   // terracotta
  Flow,      // slate (branch / sequence / loops)
  Animation, // teal   (pose nodes)
  State,     // indigo (state machine states / transitions)
  Behavior,  // green  (behavior tree composites, decorators, tasks)
  Effect,    // magenta (VFX modules)
  Audio,     // amber  (audio nodes)
};

enum class BlueprintNodeStyle {
  Header,    // coloured gradient header with icon, title and "From class X" subtitle
  Compact,   // no header: big centred title ("Get", "+", "Branch") between the pin columns
  Reroute,   // tiny pass-through knot
};

struct BlueprintNode {
  int id = 0;
  std::string title;
  std::string subtitle;
  BlueprintNodeCategory category = BlueprintNodeCategory::Function;
  BlueprintNodeStyle style = BlueprintNodeStyle::Header;
  Vec2 pos{0, 0};        // world coordinates (top-left)
  Vec2 size{190, 110};   // computed from content by the graph
  float colL = 0, colR = 0;   // width of the input / output pin columns (computed; the compact title sits between them)
  std::vector<BlueprintPin> inputs;
  std::vector<BlueprintPin> outputs;
  bool selected = false;
  std::string tooltip;
  enum class Preview { None, Color, Scalar, Texture, Normal } preview = Preview::None;   // small thumbnail under the header (material nodes)
  std::string previewValue;                                                               // "r,g,b" or a 0..1 number
  // The header icon; None picks one from the title.
  Icon icon = Icon::None;
  // A host-owned settings area under the pins (setNodeBody): its height and minimum width in world
  // units, and the line shown in its place when the view is zoomed too far out for widgets.
  float bodyH = 0, bodyW = 0;
  std::string bodySummary;
};

struct BlueprintVariable {
  std::string name;
  BlueprintPinType type = BlueprintPinType::Float;
  std::string value;          // default value ("0", "true", "1,0.5,0" ...)
};

struct BlueprintWire {
  int fromNodeId = 0;
  int fromPinIndex = 0;
  int toNodeId = 0;
  int toPinIndex = 0;
  BlueprintPinType type = BlueprintPinType::Exec;
};

struct BlueprintComment {
  int id = 0;
  std::string title;
  Rect bounds{0, 0, 200, 150};
  Color color{30, 80, 130, 45};
  bool selected = false;
};

enum class BlueprintGraphPreset {
  MaterialShader,   // PBR surface shader graph
  GameplayScript,   // gameplay logic
  WidgetBlueprint,  // UI widget visibility logic (Get / Calc / SetVisible)
  EventBlueprint,   // actor event graph: overlap / hit / damage / input events -> actions
  AnimationGraph,   // pose graph: sequence players, blends, IK -> Output Pose
  StateMachine,     // animation state machine: states + transition rules
  BehaviorTree,     // AI behavior tree: root -> selector / sequence -> tasks
  ParticleGraph,    // VFX emitter: spawn -> modules -> renderer
  AudioGraph,       // procedural audio: players -> filters / mix -> output
};
const char* blueprintPresetName(BlueprintGraphPreset p);
const char* blueprintPresetGroup(BlueprintGraphPreset p);
constexpr int kBlueprintPresetCount = 9;
Icon blueprintPresetIcon(BlueprintGraphPreset p);       // logo of each graph type
Color blueprintPresetTint(BlueprintGraphPreset p);      // its accent colour

struct BlueprintCompileReport {
  int errors = 0, warnings = 0;
  std::vector<std::string> messages;
  std::map<int, std::string> nodeIssues;   // nodeId -> message (drawn as a badge on the node)
};

// What a palette / context menu is allowed to offer when it was opened by dragging a wire off a pin.
struct BlueprintPinFilter {
  bool active = false;
  BlueprintPinType type = BlueprintPinType::Exec;
  BlueprintPinKind fromKind = BlueprintPinKind::Output;   // kind of the pin the wire started at
};

struct BlueprintNodeSpec {
  std::string category;          // "Flow Control", "Math|Float" ...
  std::string name;
  std::string keywords;
  std::function<BlueprintNode()> make;
};

// All spawnable nodes. Hosts can append their own entries (engine functions, material expressions, ...).
std::vector<BlueprintNodeSpec>& blueprintNodeLibrary();

bool blueprintTypesCompatible(BlueprintPinType from, BlueprintPinType to);
const char* blueprintPinTypeName(BlueprintPinType t);
Color blueprintPinColor(BlueprintPinType t);

struct BpProgram;

class BlueprintGraph : public Widget {
public:
  BlueprintGraph();
  ~BlueprintGraph() override;

  // ---- model
  // What the graph shows and does. Everything is on by default; a host embedding the graph for its
  // own data (an engine animation graph) turns off what does not apply.
  struct Features {
    bool toolbar = true;     // the 36 px tool row (compile, save, find, simulate ...)
    bool crumbs = true;      // the graph-type chip and breadcrumb strip
    bool minimap = true;
    bool watermark = true;
    bool shortcuts = true;   // C, Ctrl+C/V/X/D/Z/Y/S, F9/F10/F5, Ctrl+Enter (off: Delete, Ctrl+A, F, Home, Esc only)
    bool history = true;     // the graph's own undo (off: the host's undo, through onCommit)
    // An output feeds one input (a pose graph): wiring it elsewhere moves its wire, and a node
    // added off a wired pin is inserted into that wire.
    bool singleLinkOutputs = false;
    // Comment boxes (the C key) even when `shortcuts` is off.
    bool comments = false;
  };
  void setFeatures(const Features& f);
  /// The watermark's title, caption and glyph in place of the preset's (an empty title restores it).
  void setWatermark(std::string title, std::string caption, Icon icon);
  const Features& features() const { return m_features; }
  // The nodes the palette offers; empty uses the global blueprintNodeLibrary().
  void setNodeLibrary(std::vector<BlueprintNodeSpec> library) { m_library = std::move(library); }
  const std::vector<BlueprintNodeSpec>& nodeLibrary() const;
  // Picking a node in the palette: the host makes it (addNode with the id it returns, -1 for none).
  std::function<int(const BlueprintNodeSpec&, Vec2 world)> onSpawn;
  // A finished user edit ("Move nodes", "Connect pins", "Delete", ...), after the graph changed.
  Callback<const std::string&> onCommit;
  Callback<const std::vector<int>&> onSelectionChanged;
  Callback<int> onNodeDoubleClicked;
  // The host's own rule for a connection, asked after the graph's type checks pass.
  std::function<bool(int fromNode, int fromPin, int toNode, int toPin, std::string* why)> canConnectHook;
  // The values an input may take (the host's widgets, assets ...): editing it opens a list of
  // them, with "Type a value..." for anything else. Empty: the plain text box.
  std::function<std::vector<std::string>(const BlueprintNode& node, int inputPin)> pinChoices;
  // A widget drawn and used inside node `nodeId` under its pins (the node's bodyH tall); null removes it.
  void setNodeBody(int nodeId, Ref<Widget> body);
  void clearNodeBodies();

  void setPreset(BlueprintGraphPreset preset);
  BlueprintGraphPreset preset() const { return m_preset; }
  void addNode(BlueprintNode node);               // id 0 = assign a fresh id
  int spawnNode(const BlueprintNodeSpec& spec, Vec2 world);
  void addComment(BlueprintComment comment);
  void connectPins(int fromNode, int fromPin, int toNode, int toPin, BlueprintPinType type);   // unchecked (presets)
  bool canConnect(int fromNode, int fromPin, int toNode, int toPin, std::string* why = nullptr) const;
  bool canConnectBase(int fromNode, int fromPin, int toNode, int toPin, std::string* why = nullptr) const;   // the graph's own type / loop checks
  bool connect(int fromNode, int fromPin, int toNode, int toPin);                               // validated, replaces
  bool disconnectPin(int nodeId, int pinIndex, bool isInput);
  void removeNode(int nodeId);
  void clear();

  bool isPinConnected(int nodeId, int pinIndex, bool isInput) const;
  const BlueprintNode* findNode(int id) const;
  // Sets input `pinIndex` of node `nodeId` to `value` (one undo step), as editing its box does.
  // False when there is no such pin or the value is not one of its type.
  bool setPinDefault(int nodeId, int pinIndex, const std::string& value);
  BlueprintPinType resolvedType(int nodeId, int pinIndex, bool isInput) const;   // resolves Wildcard pins

  int nodeCount() const { return int(m_nodes.size()); }
  int wireCount() const { return int(m_wires.size()); }
  int commentCount() const { return int(m_comments.size()); }
  const std::vector<BlueprintNode>& nodes() const { return m_nodes; }
  const std::vector<BlueprintWire>& wires() const { return m_wires; }
  const std::vector<BlueprintComment>& comments() const { return m_comments; }

  // ---- selection
  void select(int nodeId, bool additive = false);
  void selectAll();
  void clearSelection();
  int selectedCount() const;
  std::vector<int> selectedIds() const;
  void removeSelected();
  void duplicateSelected();
  void copySelected();
  void paste(Vec2 world);

  // ---- history / persistence
  void commit(const std::string& name);            // call after a user-visible edit; records an undo step
  bool undo();
  bool redo();
  bool canUndo() const { return m_history.canUndo(); }
  bool canRedo() const { return m_history.canRedo(); }
  TransactionService& history() { return m_history; }
  std::string serialize(bool selectionOnly = false) const;
  bool deserialize(const std::string& text);        // replaces the graph; false (and unchanged) if malformed
  Callback<> onBrowse;                               // "Browse": reveal this graph in the content browser
  // A host that owns the file and the compiler: Save / Ctrl+S and Compile call these instead of the
  // graph's own save and validation; the host reports back with setCompileResult.
  std::function<void()> onSaveRequested;
  std::function<void()> onCompileRequested;
  void setCompileResult(BlueprintCompileReport report) { m_report = std::move(report); m_compiled = true; invalidate(); }
  void clearCompileResult() { m_compiled = false; m_report = {}; invalidate(); }
  std::string saveDir = "Graphs";                    // Save / Load folder (one .prismbp file per graph type)
  bool saveToFile();                                 // writes <saveDir>/<graph type>.prismbp
  bool loadFromFile(const std::string& path);
  std::vector<std::string> savedGraphs() const;      // files found in saveDir
  void openFind(Rect anchor, bool allGraphs = false); // search nodes / pins (this graph, or every saved graph)
  void navBack();                                    // back / forward through the graph types visited
  void navForward();
  void jumpToError();                                // select + frame the first node with a compile issue
  void showDiff(Rect anchor);                        // compare with the saved file
  std::vector<std::string> diffAgainst(const std::string& savedText, std::vector<int>* nodeIds = nullptr) const;
  void showClassSettings();
  void showClassDefaults();
  void setHideUnrelated(bool v) { m_hideUnrelated = v; invalidate(); }
  bool hideUnrelated() const { return m_hideUnrelated; }
  bool saveOnCompile() const { return m_saveOnCompile; }
  bool canNavBack() const { return !m_navBack.empty(); }
  bool canNavForward() const { return !m_navFwd.empty(); }
  void openLoadMenu(Rect anchor);
  Callback<> onChanged;                              // any structural / value change

  // ---- authoring tools
  void autoLayout(bool selectionOnly = false);       // layered left-to-right layout (undoable); frames the result
  bool collapseSelected();                           // selected nodes -> one "Collapsed Graph" node (boundary wires become pins)
  bool expandNode(int nodeId);                       // reverse of collapseSelected
  bool isCollapsed(int nodeId) const { return m_subs.count(nodeId) > 0; }
  // variables
  const std::vector<BlueprintVariable>& variables() const { return m_vars; }
  BlueprintVariable* addVariable(const std::string& name, BlueprintPinType type, const std::string& value = "");   // unique-ifies the name
  bool removeVariable(const std::string& name);                                                                    // also removes its Get/Set nodes
  bool renameVariable(const std::string& from, const std::string& to);
  bool setVariableValue(const std::string& name, const std::string& value);                                       // its default
  int spawnVariableNode(const std::string& name, bool setter, Vec2 world);                                          // -1 if unknown
  void showVariables();                                                                                             // floating Variables panel
  // debugger: breakpoints pause the animated run on that node
  void toggleBreakpoint(int nodeId);
  bool hasBreakpoint(int nodeId) const { return m_breaks.count(nodeId) > 0; }
  void clearBreakpoints() { m_breaks.clear(); invalidate(); }
  const std::set<int>& breakpoints() const { return m_breaks; }
  bool paused() const { return m_paused; }
  int pausedNode() const { return m_paused && m_pauseIdx >= 0 && m_pauseIdx < int(m_runOrder.size()) ? m_runOrder[size_t(m_pauseIdx)] : -1; }
  void resume();
  void step();                                       // run to the next node, then pause again
  void stopRun();

  // ---- execution (see BlueprintVM.h)
  BpProgram program() const;                  // compiled program; collapsed subgraphs are flattened in
  const std::vector<std::string>& runLog() const { return m_runLog; }      // Print String output of the last Simulate
  const std::vector<std::string>& runIssues() const { return m_runIssues; } // nodes without a native, runaway loops

  // ---- analysis
  BlueprintCompileReport compile();
  std::vector<int> executionOrder() const;           // node ids reachable from Event nodes, in firing order
  void run();                                        // animates the execution trace
  bool running() const { return m_runActive; }

  // ---- view
  Vec2 worldToScreen(Vec2 world) const;
  Vec2 screenToWorld(Vec2 screen) const;
  float zoom() const { return m_zoom; }
  Vec2 panOffset() const { return m_pan; }
  Vec2 pinScreenPos(int nodeId, int pin, bool isInput) const { const BlueprintNode* n = findNode(nodeId); return n ? pinScreen(*n, pin, isInput) : Vec2{}; }
  void setView(Vec2 pan, float zoom) { m_pan = pan; m_zoom = std::clamp(zoom, 0.3f, 2.0f); m_needFrame = false; invalidate(); }
  void debugRunAt(float seconds);   // freeze the execution trace at a given time (docs, tests)
  void frameAll();
  void frameSelection();
  void setSnap(bool on) { m_snap = on; invalidate(); }
  void openPalette(Vec2 screenPos, const BlueprintPinFilter& filter = {}, int fromNode = -1, int fromPin = -1);

  Vec2 measure(Vec2 avail) override;
  void arrange(Rect r) override;
  void paint(Canvas& c) override;
  bool onMouseDown(const MouseEvent& e) override;
  bool onMouseMove(const MouseEvent& e) override;
  bool onMouseUp(const MouseEvent& e) override;
  void onMouseLeave() override;
  bool onMouseWheel(const MouseEvent& e) override;
  bool onKeyDown(const KeyEvent& e) override;
  const std::string& tooltipAt(Vec2 p) const override;
  Cursor cursor() const override;
  bool focusable() const override { return true; }

private:
  friend struct BlueprintPaint;

  // layout
  void layoutNode(BlueprintNode& n) const;
  void layoutAll();
  Vec2 pinLocal(const BlueprintNode& n, int pin, bool isInput) const;       // world offset from node.pos
  Vec2 pinWorld(const BlueprintNode& n, int pin, bool isInput) const { return n.pos + pinLocal(n, pin, isInput); }
  Vec2 pinScreen(const BlueprintNode& n, int pin, bool isInput) const { return worldToScreen(pinWorld(n, pin, isInput)); }
  Rect nodeScreenRect(const BlueprintNode& n) const;
  Rect defaultBoxLocal(const BlueprintNode& n, int pin) const;               // inline value box (world, relative to pos)
  void touch();                                                              // structure changed
  BlueprintNode* nodeById(int id);
  int nextId() const;

  // hit testing
  struct PinHit { int node = -1, pin = -1; bool input = false; explicit operator bool() const { return node >= 0; } };
  PinHit pinAt(Vec2 screen) const;
  const BlueprintNode* nodeAt(Vec2 screen) const;
  int wireAt(Vec2 screen) const;
  BlueprintComment* commentTitleAt(Vec2 screen);
  BlueprintComment* commentResizeAt(Vec2 screen);
  std::vector<Vec2> wirePoints(Vec2 a, Vec2 b, int segments) const;
  PinHit defaultBoxAt(Vec2 screen) const;

  // interaction helpers
  void beginWire(const PinHit& h);
  void finishWire(Vec2 screen);
  void editDefault(const PinHit& h);
  void editComment(BlueprintComment& c);
  void showToolbarTooltip(Vec2 p);
  bool toolbarClick(Vec2 p);
  void paintToolbar(Canvas& c);
  void paintMinimap(Canvas& c);
  void paintGrid(Canvas& c, Rect view);
  void addCommentAround(const std::vector<int>& ids, Vec2 fallbackWorld);
  void startFlowTimer();
  void insertReroute(int wireIndex, Vec2 world);
  void finishSpawn(const BlueprintNodeSpec& spec, Vec2 world, const BlueprintPinFilter& f, int fromNode, int fromPin);
  void zoomAt(Vec2 screen, float factor);
  void restore(const std::string& snapshot);
  void loadMaterialShaderPreset();
  void loadGameplayScriptPreset();
  void loadWidgetBlueprintPreset();
  void loadEventBlueprintPreset();
  void loadAnimationGraphPreset();
  void loadStateMachinePreset();
  void loadBehaviorTreePreset();
  void loadParticleGraphPreset();
  void loadAudioGraphPreset();
  void placeNode(int id, const char* spec, float x, float y, const char* title = "", const char* sub = "");
  std::vector<int> flowOrder() const;
  bool usesExecFlow() const;

  Features m_features;
  std::vector<BlueprintNodeSpec> m_library;
  std::map<int, Ref<Widget>> m_bodies;
  std::vector<int> m_lastSelection;
  void notifySelection();
  bool onMouseUpInner(const MouseEvent& e);
  Rect bodyScreenRect(const BlueprintNode& n) const;
  BlueprintGraphPreset m_preset = BlueprintGraphPreset::WidgetBlueprint;
  std::vector<BlueprintNode> m_nodes;
  std::vector<BlueprintWire> m_wires;
  std::vector<BlueprintComment> m_comments;
  std::unordered_map<int, int> m_index;       // node id -> vector index
  bool m_indexDirty = true;

  Vec2 m_pan{60, 50};
  float m_zoom = 1.0f;
  bool m_typingChoice = false;   // "Type a value..." asked for the text box over the list
  bool m_snap = true;
  bool m_needFrame = false;   // frame the whole graph on the next arrange (after a preset load)

  enum class Drag { None, Pan, Nodes, Marquee, Wire, Comment, CommentResize, Minimap } m_drag = Drag::None;
  Vec2 m_dragStartScreen{0, 0}, m_panStart{0, 0}, m_dragCurrent{0, 0};
  bool m_dragMoved = false, m_rightDown = false;
  std::vector<std::pair<int, Vec2>> m_dragOrigin;        // node id -> original world pos
  std::vector<std::pair<int, Vec2>> m_commentOrigin;     // comment id -> original world pos (moved with a dragged comment)
  int m_dragCommentId = -1;
  int m_pendingSelect = -1;      // node to select alone if the press turns out to be a click, not a drag
  Vec2 m_commentSizeStart{0, 0};
  // wiring
  PinHit m_wireFrom;
  // The wire picked up off a connected input (pose graphs): a node added from the palette at the end
  // of that drag goes back into it.
  std::optional<BlueprintWire> m_pickedWire;
  PinHit m_wireTarget;
  bool m_wireValid = false;
  std::string m_wireWhy;
  // hover
  PinHit m_hoverPin;
  int m_hoverNode = -1, m_hoverWire = -1;
  mutable std::string m_tipCache;
  int m_toolbarHover = -1;
  // view helpers
  bool m_showMinimap = true;
  Rect m_minimapRect;
  struct Btn { Rect r; std::string id; std::string tip; };
  std::vector<Btn> m_btns;
  Rect m_graphRect;                  // canvas area under the toolbar
  float m_toolbarH = 36;
  float m_crumbH = 30;               // tab chip + breadcrumb strip under the toolbar
  std::string m_watermarkTitle, m_watermarkCaption;
  Icon m_watermarkIcon{};
  bool m_hideUnrelated = false, m_saveOnCompile = false, m_navigating = false;
  std::vector<BlueprintGraphPreset> m_navBack, m_navFwd;
  std::set<int> m_related;           // selection + directly linked nodes (for Hide Unrelated)
  void paintCrumbs(Canvas& c);
  void paintWatermark(Canvas& c);
  void updateRelated();
  // run / compile
  bool m_runActive = false;
  // debugger / tools state
  std::set<int> m_breaks;
  bool m_paused = false;
  int m_pauseIdx = -1, m_resumeIdx = -1, m_stepIdx = -1;
  std::vector<BlueprintVariable> m_vars;
  struct Sub {
    std::vector<BlueprintNode> nodes;
    std::vector<BlueprintWire> wires;
    std::vector<std::pair<int, int>> inMap, outMap;   // collapsed-node pin -> (inner node, inner pin)
  };
  std::map<int, Sub> m_subs;
  static void expandSub(std::vector<BlueprintNode>& nodes, std::vector<BlueprintWire>& wires, std::map<int, Sub>& subs, int nodeId, bool select);
  std::vector<std::string> m_runLog, m_runIssues;
  void paintDebugMarks(Canvas& c);
  std::vector<int> m_runOrder;
  float m_runClock = 0;
  int m_runTimer = 0;
  BlueprintCompileReport m_report;
  bool m_compiled = false;
  float m_flowTime = 0;
  // history / clipboard
  TransactionService m_history{200};
  std::string m_lastSnapshot;
  bool m_restoring = false;
  std::string m_clipboard;
  int m_nextId = 1000;
};

} // namespace prism
