// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - BlueprintGraph model: nodes, wires, selection, history, persistence, analysis, library.
#include "prism/BlueprintVM.h"
#include "BlueprintInternal.h"
#include "prism/Application.h"
#include <algorithm>
#include <cstdio>
#include <set>
#include <sstream>

namespace prism {

using PT = BlueprintPinType;
using PK = BlueprintPinKind;

// ============================================================================ shared helpers
namespace bp {

const Font& font(float basePx, Weight w, float zoom) {
  static std::map<std::pair<int, int>, std::shared_ptr<Font>> cache;
  int px = std::max(6, int(std::lround(basePx * zoom)));
  int key = px * 4 + int(w);
  auto it = cache.find({px, int(w)});
  (void)key;
  if (it != cache.end()) return *it->second;
  const char* file = w == Weight::Regular ? "Inter-Regular.otf" : w == Weight::Medium ? "Inter-Medium.otf" : "Inter-SemiBold.otf";
  auto f = std::make_shared<Font>(theme().fontDir + "/fonts/" + file, px);
  if (!f->ok()) {
    const Font& fb = w == Weight::Regular ? *theme().ui : *theme().medium;
    return fb;
  }
  cache[{px, int(w)}] = f;
  return *f;
}

float textWidth(std::string_view s, float basePx, Weight w) { return font(basePx, w, 1.0f).width(s); }

Chrome chromeFor(BlueprintNodeCategory c) {
  auto mk = [](Color base) {
    Chrome ch;
    // Neutral graphite node: the only colour on a node is its title tile (header), matte and slightly muted.
    Color tile = base.mix(Color(96, 98, 104), 0.22f);
    ch.left = tile.lighten(0.04f);
    ch.right = tile.darken(0.16f);
    ch.body = Color(40, 42, 46);
    ch.bodyDark = Color(30, 32, 36);
    ch.sub = Color(212, 216, 224).mix(tile, 0.18f);
    return ch;
  };
  switch (c) {
    case BlueprintNodeCategory::Event:    return mk(Color(176, 36, 36));
    case BlueprintNodeCategory::Function: return mk(Color(54, 104, 158));
    case BlueprintNodeCategory::PureMath: return mk(Color(70, 124, 66));
    case BlueprintNodeCategory::Material: return mk(Color(176, 96, 36));
    case BlueprintNodeCategory::Variable: return mk(Color(124, 62, 168));
    case BlueprintNodeCategory::Texture:  return mk(Color(190, 84, 48));
    case BlueprintNodeCategory::Flow:     return mk(Color(88, 96, 110));
    case BlueprintNodeCategory::Animation: return mk(Color(30, 142, 140));
    case BlueprintNodeCategory::State:    return mk(Color(84, 88, 186));
    case BlueprintNodeCategory::Behavior: return mk(Color(66, 150, 92));
    case BlueprintNodeCategory::Effect:   return mk(Color(190, 66, 150));
    case BlueprintNodeCategory::Audio:    return mk(Color(196, 148, 40));
  }
  return mk(Color(80, 80, 90));
}

} // namespace bp

bool blueprintTypesCompatible(PT from, PT to) {
  if (from == PT::Exec || to == PT::Exec) return from == to;
  if (from == PT::Wildcard || to == PT::Wildcard) return true;
  auto special = [](PT t) { return t == PT::Pose || t == PT::Link || t == PT::Audio; };
  if (special(from) || special(to)) return from == to;
  if (from == to) return true;
  auto num = [](PT t) { return t == PT::Int || t == PT::Float || t == PT::Bool; };
  if (num(from) && num(to)) return true;                       // implicit numeric conversion
  if (to == PT::String || to == PT::Name) return from != PT::Object || to == PT::String;   // anything prints as text
  if (from == PT::Name && to == PT::String) return true;
  if ((from == PT::Vector && to == PT::Color) || (from == PT::Color && to == PT::Vector)) return true;
  return false;
}

const char* blueprintPinTypeName(PT t) {
  switch (t) {
    case PT::Exec: return "Exec";
    case PT::Float: return "Float";
    case PT::Vector: return "Vector";
    case PT::Color: return "Linear Color";
    case PT::Bool: return "Boolean";
    case PT::Object: return "Object Reference";
    case PT::String: return "String";
    case PT::Int: return "Integer";
    case PT::Name: return "Name";
    case PT::Wildcard: return "Wildcard";
    case PT::Pose: return "Pose";
    case PT::Link: return "Link";
    case PT::Audio: return "Audio Signal";
  }
  return "";
}

Color blueprintPinColor(PT t) {
  switch (t) {
    case PT::Exec:     return Color(244, 244, 248);
    case PT::Bool:     return Color(222, 42, 42);
    case PT::Int:      return Color(32, 224, 172);
    case PT::Float:    return Color(156, 244, 72);
    case PT::Vector:   return Color(252, 200, 36);
    case PT::Color:    return Color(250, 140, 34);
    case PT::String:   return Color(250, 56, 214);
    case PT::Name:     return Color(190, 112, 252);
    case PT::Object:   return Color(14, 164, 240);
    case PT::Wildcard: return Color(140, 140, 150);
    case PT::Pose:     return Color(110, 226, 232);
    case PT::Link:     return Color(176, 184, 232);
    case PT::Audio:    return Color(255, 188, 64);
  }
  return Color(200, 200, 200);
}

const char* blueprintPresetName(BlueprintGraphPreset p) {
  switch (p) {
    case BlueprintGraphPreset::MaterialShader: return "Material Graph";
    case BlueprintGraphPreset::GameplayScript: return "Gameplay Script";
    case BlueprintGraphPreset::WidgetBlueprint: return "Widget Blueprint";
    case BlueprintGraphPreset::EventBlueprint: return "Event Blueprint";
    case BlueprintGraphPreset::AnimationGraph: return "Animation Graph";
    case BlueprintGraphPreset::StateMachine: return "State Machine";
    case BlueprintGraphPreset::BehaviorTree: return "Behavior Tree";
    case BlueprintGraphPreset::ParticleGraph: return "VFX Graph";
    case BlueprintGraphPreset::AudioGraph: return "Audio Graph";
  }
  return "";
}
const char* blueprintPresetGroup(BlueprintGraphPreset p) {
  switch (p) {
    case BlueprintGraphPreset::WidgetBlueprint: case BlueprintGraphPreset::EventBlueprint: case BlueprintGraphPreset::GameplayScript: return "Blueprints";
    case BlueprintGraphPreset::AnimationGraph: case BlueprintGraphPreset::StateMachine: return "Animation";
    case BlueprintGraphPreset::BehaviorTree: return "AI";
    default: return "Assets";
  }
}

Icon blueprintPresetIcon(BlueprintGraphPreset p) {
  switch (p) {
    case BlueprintGraphPreset::MaterialShader: return Icon::Material;
    case BlueprintGraphPreset::GameplayScript: return Icon::Blueprint;
    case BlueprintGraphPreset::WidgetBlueprint: return Icon::WidgetGraph;
    case BlueprintGraphPreset::EventBlueprint: return Icon::EventGraph;
    case BlueprintGraphPreset::AnimationGraph: return Icon::AnimGraph;
    case BlueprintGraphPreset::StateMachine: return Icon::StateGraph;
    case BlueprintGraphPreset::BehaviorTree: return Icon::BehaviorGraph;
    case BlueprintGraphPreset::ParticleGraph: return Icon::VfxGraph;
    case BlueprintGraphPreset::AudioGraph: return Icon::AudioGraph;
  }
  return Icon::Blueprint;
}
Color blueprintPresetTint(BlueprintGraphPreset p) {
  switch (p) {
    case BlueprintGraphPreset::MaterialShader: return Color(222, 132, 58);
    case BlueprintGraphPreset::GameplayScript: return Color(98, 160, 232);
    case BlueprintGraphPreset::WidgetBlueprint: return Color(88, 170, 255);
    case BlueprintGraphPreset::EventBlueprint: return Color(232, 84, 84);
    case BlueprintGraphPreset::AnimationGraph: return Color(60, 205, 198);
    case BlueprintGraphPreset::StateMachine: return Color(132, 136, 240);
    case BlueprintGraphPreset::BehaviorTree: return Color(104, 206, 134);
    case BlueprintGraphPreset::ParticleGraph: return Color(238, 110, 196);
    case BlueprintGraphPreset::AudioGraph: return Color(248, 196, 74);
  }
  return Color(180, 190, 210);
}

// ============================================================================ node library
namespace {

BlueprintPin I(std::string n, PT t, std::string def = "", bool hide = false) {
  BlueprintPin p; p.name = std::move(n); p.type = t; p.kind = PK::Input; p.defaultValue = std::move(def); p.hideLabel = hide; return p;
}
BlueprintPin O(std::string n, PT t, bool hide = false) {
  BlueprintPin p; p.name = std::move(n); p.type = t; p.kind = PK::Output; p.hideLabel = hide; return p;
}
BlueprintPin ExIn() { return I("Exec", PT::Exec, "", true); }
BlueprintPin ExOut(std::string n = "Then", bool hide = true) { return O(std::move(n), PT::Exec, hide); }

BlueprintNode mk(std::string title, std::string sub, BlueprintNodeCategory cat, BlueprintNodeStyle st,
                 std::vector<BlueprintPin> ins, std::vector<BlueprintPin> outs, std::string tip = "") {
  BlueprintNode n;
  n.title = std::move(title); n.subtitle = std::move(sub); n.category = cat; n.style = st;
  n.inputs = std::move(ins); n.outputs = std::move(outs); n.tooltip = std::move(tip);
  return n;
}

BlueprintNode getValidated(const std::string& var, PT type) {
  return mk("Get", "", BlueprintNodeCategory::Variable, BlueprintNodeStyle::Compact,
            {ExIn(), I("Context", PT::Object, "Self")},
            {ExOut("Valid", false), O(var, type), ExOut("Invalid", false)},
            "Validated getter: runs Valid when " + var + " exists, otherwise Invalid.");
}
BlueprintNode calcFn(const std::string& name, const std::string& cls = "ASCGame") {
  return mk(name, "From class " + cls, BlueprintNodeCategory::PureMath, BlueprintNodeStyle::Header,
            {I("Context", PT::Object, "Self")}, {O("Result", PT::Bool)}, "Pure function: evaluated when its result is needed.");
}
BlueprintNode mathOp(const char* sym, const char* name, PT t, const char* a = "0.0", const char* b = "0.0") {
  return mk(sym, "", BlueprintNodeCategory::PureMath, BlueprintNodeStyle::Compact,
            {I("A", t, a), I("B", t, b)}, {O("Result", t, true)}, name);
}

std::vector<BlueprintNodeSpec> buildLibrary() {
  std::vector<BlueprintNodeSpec> L;
  auto add = [&](std::string cat, std::string name, std::string kw, std::function<BlueprintNode()> f) {
    L.push_back({std::move(cat), std::move(name), std::move(kw), std::move(f)});
  };
  const auto Ev = BlueprintNodeCategory::Event, Fn = BlueprintNodeCategory::Function, Pu = BlueprintNodeCategory::PureMath,
             Fl = BlueprintNodeCategory::Flow, Ma = BlueprintNodeCategory::Material, Tx = BlueprintNodeCategory::Texture;
  const auto H = BlueprintNodeStyle::Header, C = BlueprintNodeStyle::Compact;

  add("Events", "Event BeginPlay", "start init", [=] { return mk("Event BeginPlay", "", Ev, H, {}, {ExOut("", true)}, "Fires once when play begins."); });
  add("Events", "Event Tick", "update frame", [=] { return mk("Event Tick", "", Ev, H, {}, {ExOut("", true), O("Delta Seconds", PT::Float)}, "Fires every frame."); });
  add("Events", "Event Construct", "widget init", [=] { return mk("Event Construct", "", Ev, H, {}, {ExOut("", true)}, "Fires when the widget is created."); });
  add("Events", "Custom Event", "event", [=] { return mk("Custom Event", "", Ev, H, {}, {ExOut("", true)}); });

  add("Flow Control", "Branch", "if else condition", [=] { return mk("Branch", "", Fl, C, {ExIn(), I("Condition", PT::Bool, "true")}, {ExOut("True", false), ExOut("False", false)}); });
  add("Flow Control", "Sequence", "then multiple", [=] { return mk("Sequence", "", Fl, C, {ExIn()}, {ExOut("Then 0", false), ExOut("Then 1", false), ExOut("Then 2", false), ExOut("Then 3", false)}); });
  add("Flow Control", "For Loop", "loop iterate", [=] { return mk("For Loop", "", Fl, H, {ExIn(), I("First Index", PT::Int, "0"), I("Last Index", PT::Int, "10")}, {ExOut("Loop Body", false), O("Index", PT::Int), ExOut("Completed", false)}); });
  add("Flow Control", "Delay", "wait timer", [=] { return mk("Delay", "Latent action", Fl, H, {ExIn(), I("Duration", PT::Float, "0.2")}, {ExOut("Completed", false)}); });
  add("Flow Control", "Do Once", "once gate", [=] { return mk("Do Once", "", Fl, C, {ExIn(), I("Reset", PT::Exec)}, {ExOut("Completed", false)}); });

  add("Utilities|String", "Print String", "debug log", [=] { return mk("Print String", "Development", Fn, H, {ExIn(), I("In String", PT::String, "Hello"), I("Duration", PT::Float, "2.0")}, {ExOut()}, "Prints text to the screen / log."); });
  add("UI", "SetVisible", "show hide widget", [=] { return mk("SetVisible", "From class VisibleObject", Fn, H, {ExIn(), I("Context", PT::Object, "Self"), I("Visibility (bool)", PT::Bool, "false")}, {ExOut()}, "Shows or hides the target widget."); });
  add("UI", "Set Text", "label", [=] { return mk("Set Text", "From class TextBlock", Fn, H, {ExIn(), I("Context", PT::Object, "Self"), I("In Text", PT::String, "")}, {ExOut()}); });
  add("UI", "Set Is Enabled", "enable disable", [=] { return mk("Set Is Enabled", "From class Widget", Fn, H, {ExIn(), I("Context", PT::Object, "Self"), I("Enabled", PT::Bool, "true")}, {ExOut()}); });
  add("Transformation", "Set Actor Location", "move position", [=] { return mk("Set Actor Location", "From class Actor", Fn, H, {ExIn(), I("Target", PT::Object, "Self"), I("New Location", PT::Vector, "0,0,0")}, {ExOut(), O("Success", PT::Bool)}); });
  add("Transformation", "Add Actor Local Rotation", "rotate", [=] { return mk("Add Actor Local Rotation", "From class Actor", Fn, H, {ExIn(), I("Target", PT::Object, "Self"), I("Delta Rotation", PT::Vector, "0,0,0")}, {ExOut()}); });
  add("Game", "Spawn Actor", "create instantiate", [=] { return mk("Spawn Actor", "From class World", Fn, H, {ExIn(), I("Class", PT::Object, ""), I("Transform", PT::Vector, "0,0,0")}, {ExOut(), O("Return Value", PT::Object)}); });
  add("Audio", "Play Sound 2D", "sound audio", [=] { return mk("Play Sound 2D", "From class GameplayStatics", Fn, H, {ExIn(), I("Sound", PT::Object, ""), I("Volume", PT::Float, "1.0")}, {ExOut()}); });
  add("Utilities", "Is Valid", "null check", [=] { return mk("Is Valid", "", Fl, C, {ExIn(), I("Input Object", PT::Object, "")}, {ExOut("Is Valid", false), ExOut("Is Not Valid", false)}); });

  add("Math|Float", "Add (+)", "plus sum", [=] { return mathOp("+", "Adds two values.", PT::Float); });
  add("Math|Float", "Subtract (-)", "minus", [=] { return mathOp("-", "Subtracts B from A.", PT::Float); });
  add("Math|Float", "Multiply (*)", "times product", [=] { return mathOp("\xC3\x97", "Multiplies two values.", PT::Float, "1.0", "1.0"); });
  add("Math|Float", "Divide (/)", "quotient", [=] { return mathOp("\xC3\xB7", "Divides A by B.", PT::Float, "1.0", "1.0"); });
  add("Math|Float", "Lerp", "mix blend interpolate", [=] { return mk("Lerp", "", Pu, H, {I("A", PT::Float, "0.0"), I("B", PT::Float, "1.0"), I("Alpha", PT::Float, "0.5")}, {O("Return Value", PT::Float)}); });
  add("Math|Float", "Clamp", "limit range", [=] { return mk("Clamp", "", Pu, H, {I("Value", PT::Float, "0.0"), I("Min", PT::Float, "0.0"), I("Max", PT::Float, "1.0")}, {O("Return Value", PT::Float)}); });
  add("Math|Float", "Random Float", "rand", [=] { return mk("Random Float", "", Pu, H, {}, {O("Return Value", PT::Float)}); });
  add("Math|Compare", "Greater (>)", "compare", [=] { auto n = mathOp(">", "True when A > B.", PT::Float); n.outputs[0].type = PT::Bool; return n; });
  add("Math|Compare", "Less (<)", "compare", [=] { auto n = mathOp("<", "True when A < B.", PT::Float); n.outputs[0].type = PT::Bool; return n; });
  add("Math|Compare", "Equal (==)", "compare same", [=] { auto n = mathOp("==", "True when A equals B.", PT::Float); n.outputs[0].type = PT::Bool; return n; });
  add("Math|Boolean", "AND", "boolean", [=] { auto n = mathOp("AND", "Logical and.", PT::Bool, "false", "false"); return n; });
  add("Math|Boolean", "OR", "boolean", [=] { auto n = mathOp("OR", "Logical or.", PT::Bool, "false", "false"); return n; });
  add("Math|Boolean", "NOT", "boolean invert", [=] { return mk("NOT", "", Pu, C, {I("A", PT::Bool, "false")}, {O("Result", PT::Bool, true)}); });
  add("Math|Vector", "Make Vector", "xyz", [=] { return mk("Make Vector", "", Pu, H, {I("X", PT::Float, "0.0"), I("Y", PT::Float, "0.0"), I("Z", PT::Float, "0.0")}, {O("Return Value", PT::Vector)}); });
  add("Math|Vector", "Break Vector", "split xyz", [=] { return mk("Break Vector", "", Pu, H, {I("In Vec", PT::Vector, "0,0,0")}, {O("X", PT::Float), O("Y", PT::Float), O("Z", PT::Float)}); });

  add("Variables", "Get Button Unlock", "validated get", [=] { return getValidated("Button Unlock", PT::Object); });
  add("Variables", "Get Button Play", "validated get", [=] { return getValidated("Button Play", PT::Object); });
  add("Variables", "Get Button Reset", "validated get", [=] { return getValidated("Button Reset", PT::Object); });
  add("Variables", "Get Image Padlock Closed", "validated get", [=] { return getValidated("Image Padlock Closed", PT::Object); });
  add("Variables", "Get Score", "variable", [=] { return mk("Score", "", BlueprintNodeCategory::Variable, C, {}, {O("Score", PT::Int, true)}, "Variable getter."); });
  add("Variables", "Get Health", "variable", [=] { return mk("Health", "", BlueprintNodeCategory::Variable, C, {}, {O("Health", PT::Float, true)}, "Variable getter."); });
  add("Variables", "Set Score", "variable", [=] { return mk("SET", "", BlueprintNodeCategory::Variable, C, {ExIn(), I("Score", PT::Int, "0")}, {ExOut(), O("Output Get", PT::Int)}); });
  add("Variables", "CalcVisibleBtnPlay", "ui calc", [=] { return calcFn("CalcVisibleBtnPlay"); });
  add("Variables", "CalcVisibleBtnReset", "ui calc", [=] { return calcFn("CalcVisibleBtnReset"); });
  add("Variables", "CalcVisibleBtnUnlock", "ui calc", [=] { return calcFn("CalcVisibleBtnUnlock"); });
  add("Variables", "CalcVisibleImgPadlockLocked", "ui calc", [=] { return calcFn("CalcVisibleImgPadlockLocked"); });

  add("Material", "Scalar Parameter", "float param", [=] { auto n = mk("Scalar Param", "Roughness", Ma, H, {I("Default", PT::Float, "0.5")}, {O("Value", PT::Float)}); n.preview = BlueprintNode::Preview::Scalar; n.previewValue = "0.5"; return n; });
  add("Material", "Color Parameter", "vec color", [=] { auto n = mk("Color Param", "Base Color", Ma, H, {I("Default", PT::Color, "1,1,1")}, {O("RGB", PT::Color), O("Alpha", PT::Float)}); n.preview = BlueprintNode::Preview::Color; n.previewValue = "1,1,1"; return n; });
  add("Material", "Texture Sample 2D", "texture map", [=] { auto n = mk("Texture Sample", "Texture2D", Tx, H, {I("UVs", PT::Vector, "0,0")}, {O("RGB", PT::Color), O("R", PT::Float), O("G", PT::Float), O("B", PT::Float)}); n.preview = BlueprintNode::Preview::Texture; return n; });
  add("Material", "Fresnel", "rim", [=] { return mk("Fresnel", "", Ma, H, {I("Exp", PT::Float, "5.0")}, {O("Out", PT::Float)}); });


  // ---------------------------------------------------------------- Event Blueprint
  add("Event Graph|Events", "Event ActorBeginOverlap", "overlap trigger enter", [=] { return mk("Event ActorBeginOverlap", "", Ev, H, {}, {ExOut("", true), O("Other Actor", PT::Object)}, "Fires when another actor starts overlapping this one."); });
  add("Event Graph|Events", "Event ActorEndOverlap", "overlap trigger exit", [=] { return mk("Event ActorEndOverlap", "", Ev, H, {}, {ExOut("", true), O("Other Actor", PT::Object)}, "Fires when an overlap ends."); });
  add("Event Graph|Events", "Event Hit", "collision impact", [=] { return mk("Event Hit", "", Ev, H, {}, {ExOut("", true), O("Other Actor", PT::Object), O("Impact Point", PT::Vector), O("Normal Impulse", PT::Vector)}, "Fires on a blocking collision."); });
  add("Event Graph|Events", "Event EndPlay", "destroy shutdown", [=] { return mk("Event EndPlay", "", Ev, H, {}, {ExOut("", true), O("End Reason", PT::Name)}, "Fires when the actor is removed."); });
  add("Event Graph|Events", "Event AnyDamage", "damage health hurt", [=] { return mk("Event AnyDamage", "", Ev, H, {}, {ExOut("", true), O("Damage", PT::Float), O("Instigator", PT::Object)}, "Fires when the actor takes damage."); });
  add("Event Graph|Input", "Input Action Jump", "key button press", [=] { return mk("Input Action Jump", "Input Action", Ev, H, {}, {ExOut("Pressed", false), ExOut("Released", false), O("Held Seconds", PT::Float)}); });
  add("Event Graph|Input", "Input Axis MoveForward", "move axis stick", [=] { return mk("Input Axis MoveForward", "Input Axis", Ev, H, {}, {ExOut("", true), O("Axis Value", PT::Float)}); });
  add("Event Graph|Actions", "Cast To Player", "cast type check", [=] { return mk("Cast To Player", "", Fn, H, {ExIn(), I("Object", PT::Object, "")}, {ExOut("Cast Succeeded", false), ExOut("Cast Failed", false), O("As Player", PT::Object)}); });
  add("Event Graph|Actions", "Apply Damage", "hurt health", [=] { return mk("Apply Damage", "From class GameplayStatics", Fn, H, {ExIn(), I("Damaged Actor", PT::Object, ""), I("Base Damage", PT::Float, "10.0"), I("Instigator", PT::Object, "Self")}, {ExOut(), O("Damage Applied", PT::Float)}); });
  add("Event Graph|Actions", "Destroy Actor", "remove kill", [=] { return mk("Destroy Actor", "From class Actor", Fn, H, {ExIn(), I("Target", PT::Object, "Self")}, {ExOut()}); });
  add("Event Graph|Actions", "Launch Character", "jump impulse", [=] { return mk("Launch Character", "From class Character", Fn, H, {ExIn(), I("Target", PT::Object, "Self"), I("Launch Velocity", PT::Vector, "0,0,600"), I("XY Override", PT::Bool, "false"), I("Z Override", PT::Bool, "true")}, {ExOut()}); });
  add("Event Graph|Actions", "Add Movement Input", "walk move", [=] { return mk("Add Movement Input", "From class Pawn", Fn, H, {ExIn(), I("Target", PT::Object, "Self"), I("World Direction", PT::Vector, "1,0,0"), I("Scale Value", PT::Float, "1.0")}, {ExOut()}); });
  add("Event Graph|Actions", "Set Timer by Event", "timer loop schedule", [=] { return mk("Set Timer by Event", "", Fn, H, {ExIn(), I("Time", PT::Float, "2.0"), I("Looping", PT::Bool, "true")}, {ExOut(), O("Handle", PT::Name)}); });

  // ---------------------------------------------------------------- Animation Graph
  const auto An = BlueprintNodeCategory::Animation, St = BlueprintNodeCategory::State, Bh = BlueprintNodeCategory::Behavior,
             Fx = BlueprintNodeCategory::Effect, Au = BlueprintNodeCategory::Audio;
  add("Animation|Poses", "Sequence Player", "animation clip play", [=] { return mk("Sequence Player", "Animation Sequence", An, H, {I("Sequence", PT::Object, "Idle"), I("Play Rate", PT::Float, "1.0"), I("Loop", PT::Bool, "true")}, {O("Pose", PT::Pose)}); });
  add("Animation|Poses", "Blend Space Player", "locomotion speed direction", [=] { return mk("Blend Space Player", "Blend Space 2D", An, H, {I("Blend Space", PT::Object, "Locomotion"), I("X (Direction)", PT::Float, "0.0"), I("Y (Speed)", PT::Float, "0.0")}, {O("Pose", PT::Pose)}); });
  add("Animation|Poses", "State Machine Pose", "locomotion states", [=] { return mk("State Machine", "Locomotion", An, H, {}, {O("Pose", PT::Pose)}, "Evaluates the state machine and outputs its pose."); });
  add("Animation|Blend", "Blend Poses by Bool", "switch select", [=] { return mk("Blend Poses by Bool", "", An, H, {I("Active Value", PT::Bool, "false"), I("True Pose", PT::Pose), I("False Pose", PT::Pose), I("Blend Time", PT::Float, "0.2")}, {O("Pose", PT::Pose)}); });
  add("Animation|Blend", "Layered Blend per Bone", "upper body mask additive", [=] { return mk("Layered Blend per Bone", "", An, H, {I("Base Pose", PT::Pose), I("Blend Pose 0", PT::Pose), I("Blend Weight 0", PT::Float, "1.0"), I("Bone Name", PT::Name, "spine_01")}, {O("Pose", PT::Pose)}); });
  add("Animation|Blend", "Default Slot", "montage slot", [=] { return mk("Slot 'DefaultSlot'", "Montage Slot", An, H, {I("Source", PT::Pose)}, {O("Pose", PT::Pose)}); });
  add("Animation|Skeletal Control", "Two Bone IK", "foot hand ik", [=] { return mk("Two Bone IK", "Skeletal Control", An, H, {I("Component Pose", PT::Pose), I("Effector Location", PT::Vector, "0,0,0"), I("Joint Target", PT::Vector, "0,50,0"), I("Alpha", PT::Float, "1.0")}, {O("Pose", PT::Pose)}); });
  add("Animation|Skeletal Control", "Modify Bone", "transform rotate bone", [=] { return mk("Transform (Modify) Bone", "Skeletal Control", An, H, {I("Component Pose", PT::Pose), I("Bone Name", PT::Name, "head"), I("Rotation", PT::Vector, "0,0,0"), I("Alpha", PT::Float, "1.0")}, {O("Pose", PT::Pose)}); });
  add("Animation|Output", "Output Pose", "result final", [=] { BlueprintNode n = mk("Output Pose", "Final Animation Pose", An, H, {I("Result", PT::Pose)}, {}); n.inputs[0].required = true; return n; });
  add("Animation|Variables", "Get Speed", "variable velocity", [=] { return mk("Speed", "", BlueprintNodeCategory::Variable, C, {}, {O("Speed", PT::Float, true)}, "Pawn ground speed."); });
  add("Animation|Variables", "Get Direction", "variable heading", [=] { return mk("Direction", "", BlueprintNodeCategory::Variable, C, {}, {O("Direction", PT::Float, true)}, "Movement direction relative to facing."); });
  add("Animation|Variables", "Get Is In Air", "variable falling", [=] { return mk("Is In Air", "", BlueprintNodeCategory::Variable, C, {}, {O("Is In Air", PT::Bool, true)}); });

  // ---------------------------------------------------------------- State Machine
  add("State Machine", "Entry", "start", [=] { return mk("Entry", "State Machine", St, H, {}, {O("Start", PT::Link)}); });
  add("State Machine", "State", "animation state", [=] { return mk("State", "", St, H, {I("Enter", PT::Link)}, {O("Exit", PT::Link)}, "A pose-producing state."); });
  add("State Machine", "Transition Rule", "condition when", [=] { return mk("Transition Rule", "", St, H, {I("From", PT::Link), I("Can Enter", PT::Bool, "true"), I("Blend Time", PT::Float, "0.2")}, {O("To", PT::Link)}); });

  // ---------------------------------------------------------------- Behavior Tree
  add("Behavior Tree|Composites", "BT Root", "tree start", [=] { return mk("Root", "Behavior Tree", Bh, H, {}, {O("Child", PT::Link)}); });
  add("Behavior Tree|Composites", "BT Selector", "fallback or", [=] { return mk("Selector", "Runs children until one succeeds", Bh, H, {I("Parent", PT::Link)}, {O("Child 0", PT::Link), O("Child 1", PT::Link), O("Child 2", PT::Link)}); });
  add("Behavior Tree|Composites", "BT Sequence", "and in order", [=] { return mk("Sequence", "Runs children until one fails", Bh, H, {I("Parent", PT::Link)}, {O("Child 0", PT::Link), O("Child 1", PT::Link), O("Child 2", PT::Link)}); });
  add("Behavior Tree|Decorators", "BT Blackboard Check", "decorator condition key", [=] { return mk("Blackboard", "Decorator", Bh, H, {I("Parent", PT::Link), I("Key", PT::Name, "HasTarget"), I("Is Set", PT::Bool, "true")}, {O("Child", PT::Link)}); });
  add("Behavior Tree|Tasks", "BT Move To", "navigate chase", [=] { return mk("Move To", "Task", Bh, H, {I("Parent", PT::Link), I("Target Key", PT::Name, "TargetActor"), I("Acceptable Radius", PT::Float, "120.0")}, {}); });
  add("Behavior Tree|Tasks", "BT Attack", "melee shoot", [=] { return mk("Attack", "Task", Bh, H, {I("Parent", PT::Link), I("Montage", PT::Object, "Melee_01")}, {}); });
  add("Behavior Tree|Tasks", "BT Wait", "idle pause", [=] { return mk("Wait", "Task", Bh, H, {I("Parent", PT::Link), I("Duration", PT::Float, "2.0")}, {}); });
  add("Behavior Tree|Tasks", "BT Find Patrol Point", "random location", [=] { return mk("Find Patrol Point", "Task", Bh, H, {I("Parent", PT::Link), I("Radius", PT::Float, "800.0"), I("Result Key", PT::Name, "PatrolPoint")}, {}); });

  // ---------------------------------------------------------------- VFX Graph
  add("VFX|Emitter", "Emitter Spawn", "rate burst", [=] { return mk("Emitter Spawn", "Spawn Rate", Fx, H, {I("Rate (per second)", PT::Float, "120.0"), I("Burst Count", PT::Int, "0")}, {O("Particles", PT::Link)}); });
  add("VFX|Modules", "Initialize Particle", "lifetime size color", [=] { return mk("Initialize Particle", "Module", Fx, H, {I("Particles", PT::Link), I("Lifetime", PT::Float, "2.0"), I("Size", PT::Float, "0.12"), I("Color", PT::Color, "1,0.6,0.2")}, {O("Particles", PT::Link)}); });
  add("VFX|Modules", "Add Velocity", "initial speed cone", [=] { return mk("Add Velocity", "Module", Fx, H, {I("Particles", PT::Link), I("Velocity", PT::Vector, "0,1.5,0"), I("Randomness", PT::Float, "0.4")}, {O("Particles", PT::Link)}); });
  add("VFX|Forces", "Gravity Force", "fall", [=] { return mk("Gravity Force", "Module", Fx, H, {I("Particles", PT::Link), I("Gravity", PT::Vector, "0,-9.8,0")}, {O("Particles", PT::Link)}); });
  add("VFX|Forces", "Curl Noise Force", "turbulence wind", [=] { return mk("Curl Noise Force", "Module", Fx, H, {I("Particles", PT::Link), I("Strength", PT::Float, "1.2"), I("Frequency", PT::Float, "0.6")}, {O("Particles", PT::Link)}); });
  add("VFX|Modules", "Color Over Life", "gradient fade", [=] { return mk("Color Over Life", "Module", Fx, H, {I("Particles", PT::Link), I("Start", PT::Color, "1,0.7,0.2"), I("End", PT::Color, "0.2,0.05,0.0")}, {O("Particles", PT::Link)}); });
  add("VFX|Modules", "Size Over Life", "scale curve", [=] { return mk("Size Over Life", "Module", Fx, H, {I("Particles", PT::Link), I("Start Scale", PT::Float, "1.0"), I("End Scale", PT::Float, "0.0")}, {O("Particles", PT::Link)}); });
  add("VFX|Render", "Sprite Renderer", "billboard output", [=] { BlueprintNode n = mk("Sprite Renderer", "Render", Fx, H, {I("Particles", PT::Link), I("Material", PT::Object, "M_Spark"), I("Blend Mode", PT::Name, "Additive")}, {}); return n; });

  // ---------------------------------------------------------------- Audio Graph
  add("Audio|Sources", "Wave Player", "sample play sound", [=] { return mk("Wave Player", "Source", Au, H, {I("Wave", PT::Object, "Footstep_Dirt"), I("Loop", PT::Bool, "false"), I("Pitch Shift", PT::Float, "0.0")}, {O("Out", PT::Audio)}); });
  add("Audio|Sources", "ADSR Envelope", "attack decay sustain release", [=] { return mk("ADSR Envelope", "Modulator", Au, H, {I("Attack", PT::Float, "0.01"), I("Decay", PT::Float, "0.20"), I("Sustain", PT::Float, "0.60"), I("Release", PT::Float, "0.40")}, {O("Envelope", PT::Float)}); });
  add("Audio|Effects", "Low Pass Filter", "lowpass cutoff muffle", [=] { return mk("Low Pass Filter", "Effect", Au, H, {I("In", PT::Audio), I("Cutoff (Hz)", PT::Float, "8000.0")}, {O("Out", PT::Audio)}); });
  add("Audio|Effects", "Reverb", "room space", [=] { return mk("Reverb", "Effect", Au, H, {I("In", PT::Audio), I("Room Size", PT::Float, "0.5"), I("Wet", PT::Float, "0.3")}, {O("Out", PT::Audio)}); });
  add("Audio|Mix", "Audio Mixer", "sum combine", [=] { return mk("Mixer", "Mix", Au, H, {I("In A", PT::Audio), I("In B", PT::Audio), I("Gain A", PT::Float, "1.0"), I("Gain B", PT::Float, "0.6")}, {O("Out", PT::Audio)}); });
  add("Audio|Mix", "Audio Gain", "volume amplitude", [=] { return mk("Gain", "Mix", Au, H, {I("In", PT::Audio), I("Gain", PT::Float, "1.0")}, {O("Out", PT::Audio)}); });
  add("Audio|Output", "Audio Output", "master out", [=] { BlueprintNode n = mk("Audio Output", "Master", Au, H, {I("In", PT::Audio)}, {}); n.inputs[0].required = true; return n; });

  add("Utilities", "Reroute Node", "knot", [=] { return mk("", "", Fl, BlueprintNodeStyle::Reroute, {I("", PT::Wildcard, "", true)}, {O("", PT::Wildcard, true)}); });
  return L;
}

} // namespace

std::vector<BlueprintNodeSpec>& blueprintNodeLibrary() {
  static std::vector<BlueprintNodeSpec> lib = buildLibrary();
  return lib;
}

// ============================================================================ construction / model
BlueprintGraph::BlueprintGraph() {
  loadWidgetBlueprintPreset();
  m_preset = BlueprintGraphPreset::WidgetBlueprint;
  m_lastSnapshot = serialize();
}

BlueprintGraph::~BlueprintGraph() {
  if (m_runTimer) Application::get().cancelTimer(m_runTimer);
}

void BlueprintGraph::touch() {
  m_indexDirty = true;
  invalidate();
}

BlueprintNode* BlueprintGraph::nodeById(int id) {
  if (m_indexDirty) {
    m_index.clear();
    for (size_t i = 0; i < m_nodes.size(); i++) m_index[m_nodes[i].id] = int(i);
    m_indexDirty = false;
  }
  auto it = m_index.find(id);
  return it == m_index.end() ? nullptr : &m_nodes[size_t(it->second)];
}

const BlueprintNode* BlueprintGraph::findNode(int id) const { return const_cast<BlueprintGraph*>(this)->nodeById(id); }

int BlueprintGraph::nextId() const {
  int id = 1;
  for (auto& n : m_nodes) id = std::max(id, n.id + 1);
  for (auto& c : m_comments) id = std::max(id, c.id + 1);
  return std::max(id, m_nextId);
}

void BlueprintGraph::navBack() {
  if (m_navBack.empty()) return;
  m_navFwd.push_back(m_preset);
  auto p = m_navBack.back(); m_navBack.pop_back();
  m_navigating = true; setPreset(p); m_navigating = false;
}
void BlueprintGraph::navForward() {
  if (m_navFwd.empty()) return;
  m_navBack.push_back(m_preset);
  auto p = m_navFwd.back(); m_navFwd.pop_back();
  m_navigating = true; setPreset(p); m_navigating = false;
}

void BlueprintGraph::updateRelated() {
  m_related.clear();
  for (auto& n : m_nodes) if (n.selected) m_related.insert(n.id);
  if (m_related.empty()) return;
  std::set<int> sel = m_related;
  for (auto& w : m_wires) {
    if (sel.count(w.fromNodeId)) m_related.insert(w.toNodeId);
    if (sel.count(w.toNodeId)) m_related.insert(w.fromNodeId);
  }
}

void BlueprintGraph::jumpToError() {
  if (!m_compiled || m_report.nodeIssues.empty()) { Application::get().notify("No compile errors to jump to", ToastLevel::Info, 1400); return; }
  clearSelection();
  if (auto* n = nodeById(m_report.nodeIssues.begin()->first)) n->selected = true;
  frameSelection();
}

void BlueprintGraph::setPreset(BlueprintGraphPreset preset) {
  if (!m_navigating && preset != m_preset) { m_navBack.push_back(m_preset); m_navFwd.clear(); }
  m_preset = preset;
  switch (preset) {
    case BlueprintGraphPreset::MaterialShader: loadMaterialShaderPreset(); break;
    case BlueprintGraphPreset::GameplayScript: loadGameplayScriptPreset(); break;
    case BlueprintGraphPreset::WidgetBlueprint: loadWidgetBlueprintPreset(); break;
    case BlueprintGraphPreset::EventBlueprint: loadEventBlueprintPreset(); break;
    case BlueprintGraphPreset::AnimationGraph: loadAnimationGraphPreset(); break;
    case BlueprintGraphPreset::StateMachine: loadStateMachinePreset(); break;
    case BlueprintGraphPreset::BehaviorTree: loadBehaviorTreePreset(); break;
    case BlueprintGraphPreset::ParticleGraph: loadParticleGraphPreset(); break;
    case BlueprintGraphPreset::AudioGraph: loadAudioGraphPreset(); break;
  }
  // Presets are hand-placed; node widths depend on content, so push apart anything that ended up touching.
  for (int pass = 0; pass < 6; pass++) {
    bool moved = false;
    for (auto& a : m_nodes) for (auto& b : m_nodes) {
      if (&a == &b || b.pos.x < a.pos.x || (b.pos.x == a.pos.x && b.id < a.id)) continue;
      const float gap = 28;
      bool vOverlap = a.pos.y < b.pos.y + b.size.y && b.pos.y < a.pos.y + a.size.y;
      if (vOverlap && b.pos.x < a.pos.x + a.size.x + gap && b.pos.x + b.size.x > a.pos.x) { b.pos.x = a.pos.x + a.size.x + gap; moved = true; }
    }
    if (!moved) break;
  }
  m_compiled = false;
  m_report = {};
  m_history.clear();
  m_lastSnapshot = serialize();
  touch();
}

void BlueprintGraph::clear() {
  clearNodeBodies();
  m_nodes.clear(); m_wires.clear(); m_comments.clear(); m_vars.clear(); m_subs.clear(); m_breaks.clear();
  stopRun();
  touch();
}

void BlueprintGraph::addNode(BlueprintNode node) {
  if (node.id == 0) node.id = nextId();
  for (auto& p : node.inputs) if (p.type == PT::Exec && p.name == "Exec") p.hideLabel = true;
  for (auto& p : node.outputs) if (p.type == PT::Exec && p.name == "Exec") p.hideLabel = true;
  m_nextId = std::max(m_nextId, node.id + 1);
  layoutNode(node);
  m_nodes.push_back(std::move(node));
  touch();
}

int BlueprintGraph::spawnNode(const BlueprintNodeSpec& spec, Vec2 world) {
  BlueprintNode n = spec.make();
  n.id = nextId();
  n.pos = world;
  int id = n.id;
  addNode(std::move(n));
  return id;
}

void BlueprintGraph::addComment(BlueprintComment c) {
  if (c.id == 0) c.id = nextId();
  m_comments.push_back(std::move(c));
  touch();
}

void BlueprintGraph::connectPins(int fromNode, int fromPin, int toNode, int toPin, PT type) {
  m_wires.push_back(BlueprintWire{fromNode, fromPin, toNode, toPin, type});
  touch();
}

bool BlueprintGraph::isPinConnected(int nodeId, int pinIndex, bool isInput) const {
  for (const auto& w : m_wires) {
    if (isInput ? (w.toNodeId == nodeId && w.toPinIndex == pinIndex) : (w.fromNodeId == nodeId && w.fromPinIndex == pinIndex)) return true;
  }
  return false;
}

PT BlueprintGraph::resolvedType(int nodeId, int pinIndex, bool isInput) const {
  const BlueprintNode* n = findNode(nodeId);
  if (!n) return PT::Wildcard;
  const auto& pins = isInput ? n->inputs : n->outputs;
  if (pinIndex < 0 || pinIndex >= int(pins.size())) return PT::Wildcard;
  PT t = pins[size_t(pinIndex)].type;
  if (t != PT::Wildcard) return t;
  // Follow wildcard chains (reroutes) up to a sane depth: upstream first, then downstream.
  std::set<std::pair<int, int>> seen;
  std::vector<std::tuple<int, int, bool>> stack{{nodeId, pinIndex, isInput}};
  while (!stack.empty() && seen.size() < 64) {
    auto [id, pi, in] = stack.back();
    stack.pop_back();
    if (!seen.insert({id * 2 + (in ? 1 : 0), pi}).second) continue;
    const BlueprintNode* cur = findNode(id);
    if (!cur) continue;
    const auto& cp = in ? cur->inputs : cur->outputs;
    if (pi < 0 || pi >= int(cp.size())) continue;
    if (cp[size_t(pi)].type != PT::Wildcard) return cp[size_t(pi)].type;
    if (cur->style == BlueprintNodeStyle::Reroute) stack.push_back({id, 0, !in});   // the other end of the knot
    for (const auto& w : m_wires) {
      if (in && w.toNodeId == id && w.toPinIndex == pi) stack.push_back({w.fromNodeId, w.fromPinIndex, false});
      if (!in && w.fromNodeId == id && w.fromPinIndex == pi) stack.push_back({w.toNodeId, w.toPinIndex, true});
    }
  }
  return PT::Wildcard;
}

bool BlueprintGraph::canConnect(int fromNode, int fromPin, int toNode, int toPin, std::string* why) const {
  if (!canConnectBase(fromNode, fromPin, toNode, toPin, why)) return false;
  return !canConnectHook || canConnectHook(fromNode, fromPin, toNode, toPin, why);
}

bool BlueprintGraph::canConnectBase(int fromNode, int fromPin, int toNode, int toPin, std::string* why) const {
  auto fail = [&](const char* m) { if (why) *why = m; return false; };
  const BlueprintNode* a = findNode(fromNode);
  const BlueprintNode* b = findNode(toNode);
  if (!a || !b) return fail("Unknown node");
  if (fromPin < 0 || fromPin >= int(a->outputs.size()) || toPin < 0 || toPin >= int(b->inputs.size())) return fail("Unknown pin");
  if (fromNode == toNode) return fail("A node cannot connect to itself");
  PT ta = resolvedType(fromNode, fromPin, false), tb = resolvedType(toNode, toPin, true);
  if ((ta == PT::Exec) != (tb == PT::Exec)) return fail("Exec pins only connect to exec pins");
  if (!blueprintTypesCompatible(ta, tb)) {
    if (why) *why = std::string(blueprintPinTypeName(ta)) + " is not compatible with " + blueprintPinTypeName(tb);
    return false;
  }
  if (ta != PT::Exec) {                       // data loops are illegal (exec loops are what loops are made of)
    std::set<int> seen;
    std::vector<int> stack{toNode};
    while (!stack.empty()) {
      int id = stack.back(); stack.pop_back();
      if (id == fromNode) return fail("Connection would create a loop");
      if (!seen.insert(id).second) continue;
      for (const auto& w : m_wires)
        if (w.fromNodeId == id && w.type != PT::Exec) stack.push_back(w.toNodeId);
    }
  }
  return true;
}

bool BlueprintGraph::connect(int fromNode, int fromPin, int toNode, int toPin) {
  if (!canConnect(fromNode, fromPin, toNode, toPin)) return false;
  PT t = resolvedType(fromNode, fromPin, false);
  if (t == PT::Wildcard) t = resolvedType(toNode, toPin, true);
  if (t == PT::Exec) std::erase_if(m_wires, [&](const BlueprintWire& w) { return w.fromNodeId == fromNode && w.fromPinIndex == fromPin; });   // one exec out
  else {
    std::erase_if(m_wires, [&](const BlueprintWire& w) { return w.toNodeId == toNode && w.toPinIndex == toPin; });                            // one data in
    if (m_features.singleLinkOutputs)   // and one link out: the wire moves
      std::erase_if(m_wires, [&](const BlueprintWire& w) { return w.fromNodeId == fromNode && w.fromPinIndex == fromPin; });
  }
  m_wires.push_back({fromNode, fromPin, toNode, toPin, t});
  touch();
  return true;
}

bool BlueprintGraph::disconnectPin(int nodeId, int pinIndex, bool isInput) {
  size_t before = m_wires.size();
  std::erase_if(m_wires, [&](const BlueprintWire& w) {
    return isInput ? (w.toNodeId == nodeId && w.toPinIndex == pinIndex) : (w.fromNodeId == nodeId && w.fromPinIndex == pinIndex);
  });
  if (m_wires.size() != before) touch();
  return m_wires.size() != before;
}

void BlueprintGraph::removeNode(int id) {
  if (auto it = m_bodies.find(id); it != m_bodies.end()) { removeChild(it->second.get()); m_bodies.erase(it); }
  std::erase_if(m_wires, [&](const BlueprintWire& w) { return w.fromNodeId == id || w.toNodeId == id; });
  std::erase_if(m_nodes, [&](const BlueprintNode& n) { return n.id == id; });
  touch();
}

// ---- selection
void BlueprintGraph::select(int id, bool additive) {
  if (!additive) for (auto& n : m_nodes) n.selected = false;
  if (BlueprintNode* n = nodeById(id)) n->selected = true;
  invalidate();
  notifySelection();
}
void BlueprintGraph::selectAll() { for (auto& n : m_nodes) n.selected = true; invalidate(); notifySelection(); }
void BlueprintGraph::clearSelection() {
  for (auto& n : m_nodes) n.selected = false;
  for (auto& c : m_comments) c.selected = false;
  invalidate();
  notifySelection();
}
void BlueprintGraph::notifySelection() {
  std::vector<int> now = selectedIds();
  if (now == m_lastSelection) return;
  m_lastSelection = now;
  if (onSelectionChanged) onSelectionChanged(now);
}
void BlueprintGraph::setWatermark(std::string title, std::string caption, Icon icon) {
  m_watermarkTitle = std::move(title);
  m_watermarkCaption = std::move(caption);
  m_watermarkIcon = icon;
  invalidate();
}

void BlueprintGraph::setFeatures(const Features& f) { m_features = f; relayout(); invalidate(); }
const std::vector<BlueprintNodeSpec>& BlueprintGraph::nodeLibrary() const { return m_library.empty() ? blueprintNodeLibrary() : m_library; }
void BlueprintGraph::setNodeBody(int nodeId, Ref<Widget> body) {
  if (auto it = m_bodies.find(nodeId); it != m_bodies.end()) { removeChild(it->second.get()); m_bodies.erase(it); }
  if (!body) { invalidate(); return; }
  addChild(body);
  m_bodies[nodeId] = std::move(body);
  invalidate();
}
void BlueprintGraph::clearNodeBodies() {
  for (auto& [id, body] : m_bodies) removeChild(body.get());
  m_bodies.clear();
  invalidate();
}
Rect BlueprintGraph::bodyScreenRect(const BlueprintNode& n) const {
  Rect r = nodeScreenRect(n);
  float h = n.bodyH * m_zoom;
  return {r.x + 8 * m_zoom, r.bottom() - h, r.w - 16 * m_zoom, h - 6 * m_zoom};
}
int BlueprintGraph::selectedCount() const { return int(std::count_if(m_nodes.begin(), m_nodes.end(), [](auto& n) { return n.selected; })); }
std::vector<int> BlueprintGraph::selectedIds() const {
  std::vector<int> v;
  for (auto& n : m_nodes) if (n.selected) v.push_back(n.id);
  return v;
}
void BlueprintGraph::removeSelected() {
  bool any = false;
  for (int id : selectedIds()) { removeNode(id); any = true; }
  size_t cn = m_comments.size();
  std::erase_if(m_comments, [](const BlueprintComment& c) { return c.selected; });
  any |= cn != m_comments.size();
  if (any) { touch(); commit("Delete"); }
}

// ============================================================================ layout
void BlueprintGraph::layoutNode(BlueprintNode& n) const {
  using namespace bp;
  if (n.style == BlueprintNodeStyle::Reroute) { n.size = {34, 22}; return; }
  const bool header = n.style == BlueprintNodeStyle::Header;
  float headerH = !header ? 0.f : (n.subtitle.empty() ? kHeaderShort : kHeaderTall);
  if (n.preview != BlueprintNode::Preview::None) headerH += kPreviewH;
  float leftW = 0, rightW = 0;
  for (auto& p : n.inputs) {
    float w = kPinX + kPinR + 7 + (p.hideLabel ? 0.f : textWidth(p.name, kLabelPx, Weight::Regular));
    if (!p.defaultValue.empty() && p.type != PT::Exec) {
      float bw = p.type == PT::Bool ? 16.f : p.type == PT::Color ? 30.f : std::max(34.f, textWidth(p.defaultValue, kLabelPx - 1, Weight::Regular) + 14.f);
      w += 8 + bw;
    }
    leftW = std::max(leftW, w);
  }
  for (auto& p : n.outputs) rightW = std::max(rightW, kPinX + kPinR + 7 + (p.hideLabel ? 0.f : textWidth(p.name, kLabelPx, Weight::Regular)));
  float w;
  if (header) {
    float titleW = kPinX + 18 + textWidth(n.title, kTitlePx, Weight::Medium) + 22;
    float subW = kPinX + 18 + textWidth(n.subtitle, kSubPx, Weight::Regular) + 16;
    w = std::max({leftW + rightW + 26.f, titleW, subW, 120.f});
  } else {
    float big = n.title.empty() ? 0.f : textWidth(n.title, kBigPx, Weight::Semibold);
    w = std::max(leftW + big + rightW + 28.f, 72.f);
  }
  int rows = int(std::max(n.inputs.size(), n.outputs.size()));
  w = std::max(w, n.bodyW);
  n.size = {std::ceil(w), std::ceil(headerH + kBodyPad * 2 + std::max(1, rows) * kRowH + n.bodyH)};
  n.colL = leftW;
  n.colR = rightW;
}

void BlueprintGraph::layoutAll() { for (auto& n : m_nodes) layoutNode(n); }

Vec2 BlueprintGraph::pinLocal(const BlueprintNode& n, int pin, bool isInput) const {
  using namespace bp;
  if (n.style == BlueprintNodeStyle::Reroute) return isInput ? Vec2(8, n.size.y * 0.5f) : Vec2(n.size.x - 8, n.size.y * 0.5f);
  float headerH = n.style == BlueprintNodeStyle::Header ? (n.subtitle.empty() ? kHeaderShort : kHeaderTall) : 0.f;
  if (n.preview != BlueprintNode::Preview::None) headerH += kPreviewH;
  float y = headerH + kBodyPad + (float(pin) + 0.5f) * kRowH;
  return {isInput ? kPinX : n.size.x - kPinX, y};
}

Rect BlueprintGraph::defaultBoxLocal(const BlueprintNode& n, int pin) const {
  using namespace bp;
  const BlueprintPin& p = n.inputs[size_t(pin)];
  Vec2 c = pinLocal(n, pin, true);
  float lw = p.hideLabel ? 0.f : textWidth(p.name, kLabelPx, Weight::Regular);
  float bw = p.type == PT::Bool ? 16.f : p.type == PT::Color ? 30.f : std::max(34.f, textWidth(p.defaultValue, kLabelPx - 1, Weight::Regular) + 14.f);
  float x = c.x + kPinR + 7 + lw + 8;
  float bh = p.type == PT::Bool ? 16.f : 18.f;
  return {x, c.y - bh * 0.5f, bw, bh};
}

Vec2 BlueprintGraph::worldToScreen(Vec2 w) const { return {m_graphRect.x + m_pan.x + w.x * m_zoom, m_graphRect.y + m_pan.y + w.y * m_zoom}; }
Vec2 BlueprintGraph::screenToWorld(Vec2 s) const { return {(s.x - m_graphRect.x - m_pan.x) / m_zoom, (s.y - m_graphRect.y - m_pan.y) / m_zoom}; }
Rect BlueprintGraph::nodeScreenRect(const BlueprintNode& n) const {
  Vec2 p = worldToScreen(n.pos);
  return {p.x, p.y, n.size.x * m_zoom, n.size.y * m_zoom};
}

Vec2 BlueprintGraph::measure(Vec2 avail) { return m_desired = constrain(avail); }

void BlueprintGraph::arrange(Rect r) {
  m_rect = r;
  m_toolbarH = m_features.toolbar ? 36.f : 0.f;
  m_crumbH = m_features.crumbs ? 30.f : 0.f;
  m_graphRect = {r.x, r.y + m_toolbarH + m_crumbH, r.w, std::max(0.f, r.h - m_toolbarH - m_crumbH)};
  layoutAll();
  if (m_needFrame && m_graphRect.w > 50) { m_needFrame = false; frameAll(); }
}

void BlueprintGraph::frameAll() {
  if (m_nodes.empty()) return;
  Rect b{1e9f, 1e9f, 0, 0};
  float x1 = -1e9f, y1 = -1e9f;
  for (auto& n : m_nodes) {
    b.x = std::min(b.x, n.pos.x); b.y = std::min(b.y, n.pos.y);
    x1 = std::max(x1, n.pos.x + n.size.x); y1 = std::max(y1, n.pos.y + n.size.y);
  }
  for (auto& c : m_comments) { b.x = std::min(b.x, c.bounds.x); b.y = std::min(b.y, c.bounds.y); x1 = std::max(x1, c.bounds.right()); y1 = std::max(y1, c.bounds.bottom()); }
  b.w = x1 - b.x; b.h = y1 - b.y;
  float pad = 40;
  float fit = std::min((m_graphRect.w - 2 * pad) / std::max(1.f, b.w), (m_graphRect.h - 2 * pad) / std::max(1.f, b.h));
  m_zoom = std::clamp(fit, 0.3f, 1.25f);
  m_pan = {(m_graphRect.w - b.w * m_zoom) * 0.5f - b.x * m_zoom, (m_graphRect.h - b.h * m_zoom) * 0.5f - b.y * m_zoom};
  invalidate();
}

void BlueprintGraph::frameSelection() {
  auto ids = selectedIds();
  if (ids.empty()) { frameAll(); return; }
  float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
  for (int id : ids) {
    const BlueprintNode* n = findNode(id);
    x0 = std::min(x0, n->pos.x); y0 = std::min(y0, n->pos.y);
    x1 = std::max(x1, n->pos.x + n->size.x); y1 = std::max(y1, n->pos.y + n->size.y);
  }
  float pad = 80;
  m_zoom = std::clamp(std::min((m_graphRect.w - 2 * pad) / std::max(1.f, x1 - x0), (m_graphRect.h - 2 * pad) / std::max(1.f, y1 - y0)), 0.35f, 1.5f);
  m_pan = {(m_graphRect.w - (x1 - x0) * m_zoom) * 0.5f - x0 * m_zoom, (m_graphRect.h - (y1 - y0) * m_zoom) * 0.5f - y0 * m_zoom};
  invalidate();
}

// ============================================================================ persistence
namespace {

std::string esc(const std::string& s) {
  std::string o;
  for (char ch : s) {
    if (ch == '\\') o += "\\\\";
    else if (ch == '\t') o += "\\t";
    else if (ch == '\n') o += "\\n";
    else o += ch;
  }
  return o;
}
std::string unesc(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) { char n = s[++i]; o += n == 't' ? '\t' : n == 'n' ? '\n' : n; }
    else o += s[i];
  }
  return o;
}
std::vector<std::string> splitTabs(const std::string& line) {
  std::vector<std::string> v;
  size_t s = 0;
  while (true) {
    size_t t = line.find('\t', s);
    v.push_back(line.substr(s, t == std::string::npos ? std::string::npos : t - s));
    if (t == std::string::npos) break;
    s = t + 1;
  }
  return v;
}
bool toInt(const std::string& s, int& o) { try { size_t k; o = std::stoi(s, &k); return k == s.size(); } catch (...) { return false; } }
bool toFloat(const std::string& s, float& o) { try { size_t k; o = std::stof(s, &k); return k == s.size(); } catch (...) { return false; } }

struct Parsed {
  std::vector<BlueprintNode> nodes;
  std::vector<BlueprintWire> wires;
  std::vector<BlueprintComment> comments;
  std::vector<BlueprintVariable> vars;
  struct RawSub { int id; std::string doc, in, out; };
  std::vector<RawSub> subs;
  std::vector<int> breaks;
  bool hasView = false;
  Vec2 pan; float zoom = 1;
};

bool parse(const std::string& text, Parsed& out) {
  std::stringstream ss(text);
  std::string line;
  if (!std::getline(ss, line) || line != "PRISMBP\t1") return false;
  while (std::getline(ss, line)) {
    if (line.empty()) continue;
    auto f = splitTabs(line);
    const std::string& k = f[0];
    if (k == "V") {
      if (f.size() != 4 || !toFloat(f[1], out.pan.x) || !toFloat(f[2], out.pan.y) || !toFloat(f[3], out.zoom)) return false;
      out.hasView = true;
    } else if (k == "N") {
      BlueprintNode n;
      int st, cat;
      if ((f.size() != 9 && f.size() != 11) || !toInt(f[1], n.id) || !toInt(f[2], st) || !toInt(f[3], cat) || !toFloat(f[4], n.pos.x) || !toFloat(f[5], n.pos.y)) return false;
      if (st < 0 || st > 2 || cat < 0 || cat > int(BlueprintNodeCategory::Audio)) return false;
      n.style = BlueprintNodeStyle(st); n.category = BlueprintNodeCategory(cat);
      n.title = unesc(f[6]); n.subtitle = unesc(f[7]); n.tooltip = unesc(f[8]);
      if (f.size() == 11) {
        int pv;
        if (!toInt(f[9], pv) || pv < 0 || pv > 4) return false;
        n.preview = BlueprintNode::Preview(pv); n.previewValue = unesc(f[10]);
      }
      out.nodes.push_back(std::move(n));
    } else if (k == "I" || k == "O") {
      int t, hide, req;
      if (out.nodes.empty() || f.size() != 6 || !toInt(f[1], t) || !toInt(f[4], hide) || !toInt(f[5], req) || t < 0 || t > int(PT::Audio)) return false;
      BlueprintPin p;
      p.type = PT(t); p.name = unesc(f[2]); p.defaultValue = unesc(f[3]); p.hideLabel = hide != 0; p.required = req != 0;
      p.kind = k == "I" ? PK::Input : PK::Output;
      (k == "I" ? out.nodes.back().inputs : out.nodes.back().outputs).push_back(std::move(p));
    } else if (k == "W") {
      BlueprintWire w;
      if (f.size() != 5 || !toInt(f[1], w.fromNodeId) || !toInt(f[2], w.fromPinIndex) || !toInt(f[3], w.toNodeId) || !toInt(f[4], w.toPinIndex)) return false;
      out.wires.push_back(w);
    } else if (k == "S") {
      int id;
      if (f.size() != 5 || !toInt(f[1], id)) return false;
      out.subs.push_back({id, unesc(f[2]), f[3], f[4]});
    } else if (k == "B") {
      int id;
      if (f.size() != 2 || !toInt(f[1], id)) return false;
      out.breaks.push_back(id);
    } else if (k == "X") {
      int t;
      if (f.size() != 4 || !toInt(f[2], t) || t < 0 || t > int(PT::Audio)) return false;
      out.vars.push_back({unesc(f[1]), PT(t), unesc(f[3])});
    } else if (k == "C") {
      BlueprintComment c;
      int r, g, b, a;
      if (f.size() != 11 || !toInt(f[1], c.id) || !toFloat(f[2], c.bounds.x) || !toFloat(f[3], c.bounds.y) || !toFloat(f[4], c.bounds.w) ||
          !toFloat(f[5], c.bounds.h) || !toInt(f[6], r) || !toInt(f[7], g) || !toInt(f[8], b) || !toInt(f[9], a)) return false;
      c.color = Color(uint8_t(r), uint8_t(g), uint8_t(b), uint8_t(a));
      c.title = unesc(f[10]);
      out.comments.push_back(std::move(c));
    } else return false;
  }
  // validate references and ids
  std::map<int, const BlueprintNode*> byId;
  for (auto& n : out.nodes) if (!byId.emplace(n.id, &n).second) return false;
  for (auto& w : out.wires) {
    auto a = byId.find(w.fromNodeId), b = byId.find(w.toNodeId);
    if (a == byId.end() || b == byId.end()) return false;
    if (w.fromPinIndex < 0 || w.fromPinIndex >= int(a->second->outputs.size())) return false;
    if (w.toPinIndex < 0 || w.toPinIndex >= int(b->second->inputs.size())) return false;
    w.type = a->second->outputs[size_t(w.fromPinIndex)].type;
    if (w.type == PT::Wildcard) w.type = b->second->inputs[size_t(w.toPinIndex)].type;
  }
  return true;
}

} // namespace

std::vector<std::string> BlueprintGraph::diffAgainst(const std::string& savedText, std::vector<int>* ids) const {
  std::vector<std::string> out;
  Parsed old;
  if (!parse(savedText, old)) return out;
  std::map<int, const BlueprintNode*> was;
  for (auto& n : old.nodes) was[n.id] = &n;
  auto label = [](const BlueprintNode& n) { return n.title.empty() ? std::string("(node)") : n.title; };
  for (auto& n : m_nodes) {
    auto it = was.find(n.id);
    if (it == was.end()) { out.push_back("+  " + label(n) + "   added"); if (ids) ids->push_back(n.id); continue; }
    const BlueprintNode& o = *it->second;
    std::string what;
    if (std::fabs(o.pos.x - n.pos.x) > 0.5f || std::fabs(o.pos.y - n.pos.y) > 0.5f) what = "moved";
    if (o.title != n.title) what = "renamed";
    for (size_t i = 0; i < n.inputs.size() && i < o.inputs.size(); i++) if (n.inputs[i].defaultValue != o.inputs[i].defaultValue) what = "value changed";
    if (!what.empty()) { out.push_back("~  " + label(n) + "   " + what); if (ids) ids->push_back(n.id); }
    was.erase(it);
  }
  for (auto& kv : was) { out.push_back("-  " + label(*kv.second) + "   removed"); if (ids) ids->push_back(-1); }
  size_t ow = old.wires.size(), nw = m_wires.size();
  if (ow != nw) { out.push_back("~  wires   " + std::to_string(ow) + " -> " + std::to_string(nw)); if (ids) ids->push_back(-1); }
  return out;
}

namespace {
void writeNode(std::ostream& o, const BlueprintNode& n) {
  o << "N\t" << n.id << '\t' << int(n.style) << '\t' << int(n.category) << '\t' << n.pos.x << '\t' << n.pos.y << '\t'
    << esc(n.title) << '\t' << esc(n.subtitle) << '\t' << esc(n.tooltip) << '\t' << int(n.preview) << '\t' << esc(n.previewValue) << '\n';
  for (auto& p : n.inputs) o << "I\t" << int(p.type) << '\t' << esc(p.name) << '\t' << esc(p.defaultValue) << '\t' << p.hideLabel << '\t' << p.required << '\n';
  for (auto& p : n.outputs) o << "O\t" << int(p.type) << '\t' << esc(p.name) << '\t' << esc(p.defaultValue) << '\t' << p.hideLabel << '\t' << p.required << '\n';
}
std::string pairList(const std::vector<std::pair<int, int>>& v) {
  std::string s;
  for (auto& [a, b] : v) s += (s.empty() ? "" : ",") + std::to_string(a) + ":" + std::to_string(b);
  return s;
}
bool parsePairs(const std::string& s, std::vector<std::pair<int, int>>& out) {
  size_t i = 0;
  while (i < s.size()) {
    size_t c = s.find(':', i), e = s.find(',', i);
    if (e == std::string::npos) e = s.size();
    int a, b;
    if (c == std::string::npos || c > e || !toInt(s.substr(i, c - i), a) || !toInt(s.substr(c + 1, e - c - 1), b)) return false;
    out.push_back({a, b});
    i = e + 1;
  }
  return true;
}
} // namespace

std::string BlueprintGraph::serialize(bool selOnly) const {
  std::ostringstream o;
  o << "PRISMBP\t1\n";
  if (!selOnly) o << "V\t" << m_pan.x << '\t' << m_pan.y << '\t' << m_zoom << '\n';
  std::set<int> ids;
  for (auto& n : m_nodes) {
    if (selOnly && !n.selected) continue;
    ids.insert(n.id);
    writeNode(o, n);
  }
  for (auto& w : m_wires)
    if (ids.count(w.fromNodeId) && ids.count(w.toNodeId)) o << "W\t" << w.fromNodeId << '\t' << w.fromPinIndex << '\t' << w.toNodeId << '\t' << w.toPinIndex << '\n';
  if (!selOnly) {
    // collapsed subgraphs (S: node id, nested document, input map, output map) and debugger breakpoints (B)
    std::set<int> live;
    for (auto& n : m_nodes) live.insert(n.id);
    for (auto& [sid, sub] : m_subs) for (auto& n : sub.nodes) live.insert(n.id);
    for (auto& [sid, sub] : m_subs) {
      if (!live.count(sid)) continue;
      std::ostringstream b;
      b << "PRISMBP\t1\n";
      for (auto& n : sub.nodes) writeNode(b, n);
      for (auto& w : sub.wires) b << "W\t" << w.fromNodeId << '\t' << w.fromPinIndex << '\t' << w.toNodeId << '\t' << w.toPinIndex << '\n';
      o << "S\t" << sid << '\t' << esc(b.str()) << '\t' << pairList(sub.inMap) << '\t' << pairList(sub.outMap) << '\n';
    }
    for (int b : m_breaks) if (ids.count(b)) o << "B\t" << b << '\n';
  }
  if (!selOnly) for (auto& v : m_vars) o << "X\t" << esc(v.name) << '\t' << int(v.type) << '\t' << esc(v.value) << '\n';
  for (auto& c : m_comments) {
    if (selOnly && !c.selected) continue;
    o << "C\t" << c.id << '\t' << c.bounds.x << '\t' << c.bounds.y << '\t' << c.bounds.w << '\t' << c.bounds.h << '\t'
      << int(c.color.r) << '\t' << int(c.color.g) << '\t' << int(c.color.b) << '\t' << int(c.color.a) << '\t' << esc(c.title) << '\n';
  }
  return o.str();
}

bool BlueprintGraph::deserialize(const std::string& text) {
  Parsed p;
  if (!parse(text, p)) return false;
  // subgraphs are nested documents; a malformed one rejects the whole file (nothing is half-loaded)
  std::map<int, Sub> subs;
  for (auto& rs : p.subs) {
    Parsed inner;
    Sub sub;
    if (!parse(rs.doc, inner) || !inner.subs.empty() || !parsePairs(rs.in, sub.inMap) || !parsePairs(rs.out, sub.outMap)) return false;
    auto pinOk = [&](const std::pair<int, int>& m, bool input) {
      for (auto& n : inner.nodes) if (n.id == m.first) return m.second >= 0 && m.second < int((input ? n.inputs : n.outputs).size());
      return false;
    };
    for (auto& m : sub.inMap) if (!pinOk(m, true)) return false;
    for (auto& m : sub.outMap) if (!pinOk(m, false)) return false;
    sub.nodes = std::move(inner.nodes);
    sub.wires = std::move(inner.wires);
    subs[rs.id] = std::move(sub);
  }
  m_nodes = std::move(p.nodes);
  m_wires = std::move(p.wires);
  m_subs = std::move(subs);
  if (!m_restoring) { m_breaks.clear(); for (int b : p.breaks) m_breaks.insert(b); }   // undo / redo never moves breakpoints
  m_comments = std::move(p.comments);
  m_vars = std::move(p.vars);
  for (auto& n : m_nodes) { n.selected = false; layoutNode(n); }
  if (p.hasView) { m_pan = p.pan; m_zoom = std::clamp(p.zoom, 0.2f, 3.f); }
  touch();
  if (!m_restoring) { m_history.clear(); m_lastSnapshot = serialize(); }   // a loaded document is the new undo baseline
  return true;
}

void BlueprintGraph::restore(const std::string& snap) {
  Vec2 pan = m_pan; float z = m_zoom;
  std::set<int> sel;
  for (int id : selectedIds()) sel.insert(id);
  m_restoring = true;
  deserialize(snap);
  m_restoring = false;
  m_pan = pan; m_zoom = z;
  for (auto& n : m_nodes) n.selected = sel.count(n.id) > 0;
  m_lastSnapshot = snap;
  m_compiled = false;
  if (onChanged) onChanged();
}

void BlueprintGraph::commit(const std::string& name) {
  if (m_restoring) return;
  std::string after = serialize();
  // The view is not part of an undo step: compare without the "V" line.
  auto strip = [](const std::string& s) { size_t a = s.find("V\t"); size_t b = a == std::string::npos ? a : s.find('\n', a); return a == std::string::npos ? s : s.substr(0, a) + s.substr(b + 1); };
  if (strip(after) == strip(m_lastSnapshot)) { m_lastSnapshot = after; return; }
  std::string before = m_lastSnapshot;
  if (m_features.history) m_history.push(name, [this, after] { restore(after); }, [this, before] { restore(before); });
  m_lastSnapshot = after;
  m_compiled = false;
  if (onChanged) onChanged();
  if (onCommit) onCommit(name);
  invalidate();
}

bool BlueprintGraph::undo() { bool ok = m_history.undo(); if (ok) invalidate(); return ok; }
bool BlueprintGraph::redo() { bool ok = m_history.redo(); if (ok) invalidate(); return ok; }

void BlueprintGraph::copySelected() {
  m_clipboard = serialize(true);
  int n = selectedCount();
  if (n) Application::get().notify("Copied " + std::to_string(n) + (n == 1 ? " node" : " nodes"), ToastLevel::Info, 1200);
}

void BlueprintGraph::paste(Vec2 world) {
  Parsed p;
  if (m_clipboard.empty() || !parse(m_clipboard, p) || p.nodes.empty()) return;
  Vec2 tl{1e9f, 1e9f};
  for (auto& n : p.nodes) { tl.x = std::min(tl.x, n.pos.x); tl.y = std::min(tl.y, n.pos.y); }
  clearSelection();
  std::map<int, int> remap;
  for (auto& n : p.nodes) {
    int old = n.id;
    n.id = nextId();
    m_nextId = n.id + 1;
    remap[old] = n.id;
    n.pos = world + (n.pos - tl);
    n.selected = true;
    layoutNode(n);
    m_nodes.push_back(n);
  }
  for (auto& w : p.wires) m_wires.push_back({remap[w.fromNodeId], w.fromPinIndex, remap[w.toNodeId], w.toPinIndex, w.type});
  for (auto& c : p.comments) { c.id = nextId(); m_nextId = c.id + 1; c.bounds.x += world.x - tl.x; c.bounds.y += world.y - tl.y; m_comments.push_back(c); }
  touch();
  commit("Paste");
}

void BlueprintGraph::duplicateSelected() {
  std::string keep = m_clipboard;
  copySelected();
  Vec2 tl{1e9f, 1e9f};
  for (auto& n : m_nodes) if (n.selected) { tl.x = std::min(tl.x, n.pos.x); tl.y = std::min(tl.y, n.pos.y); }
  if (tl.x < 1e8f) paste(tl + Vec2(32, 32));
  m_clipboard = keep.empty() ? m_clipboard : keep;
}

// ============================================================================ analysis
BlueprintCompileReport BlueprintGraph::compile() {
  BlueprintCompileReport r;
  for (auto& n : m_nodes) {
    bool hasExecIn = std::any_of(n.inputs.begin(), n.inputs.end(), bp::isExec);
    int idx = 0;
    for (auto& p : n.inputs) {
      if (p.type != PT::Exec && p.required && p.defaultValue.empty() && !isPinConnected(n.id, idx, true)) {
        r.errors++;
        std::string m = "'" + n.title + "': required input '" + p.name + "' is not connected";
        r.messages.push_back("Error: " + m);
        r.nodeIssues[n.id] = m;
      }
      idx++;
    }
    if (hasExecIn && n.category != BlueprintNodeCategory::Event) {
      int ei = 0;
      bool wired = false;
      for (auto& p : n.inputs) { if (p.type == PT::Exec && p.name == "Exec" && isPinConnected(n.id, ei, true)) wired = true; ei++; }
      if (!wired) {
        // Exec inputs that are not the first (Reset, ...) do not count: only the primary Exec decides reachability.
        bool primary = !n.inputs.empty() && n.inputs[0].type == PT::Exec;
        if (primary && !isPinConnected(n.id, 0, true)) {
          r.warnings++;
          std::string m = "'" + n.title + (n.subtitle.empty() ? "" : " (" + n.subtitle + ")") + "' never runs: its exec input is not connected";
          r.messages.push_back("Warning: " + m);
          if (!r.nodeIssues.count(n.id)) r.nodeIssues[n.id] = m;
        }
      }
    }
  }
  // graph-specific structure checks (pose / audio outputs, unreachable link nodes)
  auto issue = [&](const BlueprintNode& n, bool error, const std::string& m) {
    (error ? r.errors : r.warnings)++;
    r.messages.push_back(std::string(error ? "Error: " : "Warning: ") + m);
    if (!r.nodeIssues.count(n.id)) r.nodeIssues[n.id] = m;
  };
  bool hasOut = false;
  for (auto& n : m_nodes) {
    if (n.inputs.empty()) continue;
    PT t0 = n.inputs[0].type;
    bool outputNode = n.outputs.empty() && (t0 == PT::Pose || t0 == PT::Audio);
    if (outputNode) {
      hasOut = true;
      if (!isPinConnected(n.id, 0, true)) issue(n, true, "'" + n.title + "': nothing is connected to the output");
    }
    if (t0 == PT::Link && !isPinConnected(n.id, 0, true))
      issue(n, false, "'" + n.title + "' is unreachable: its " + n.inputs[0].name + " link is not connected");
  }
  if ((m_preset == BlueprintGraphPreset::AnimationGraph || m_preset == BlueprintGraphPreset::AudioGraph) && !hasOut) {
    r.errors++;
    r.messages.push_back(std::string("Error: the graph has no ") + (m_preset == BlueprintGraphPreset::AnimationGraph ? "Output Pose" : "Audio Output") + " node");
  }
  m_report = r;
  m_compiled = true;
  invalidate();
  if (m_saveOnCompile && r.errors == 0) saveToFile();
  return r;
}

std::vector<int> BlueprintGraph::executionOrder() const {
  std::vector<int> order;
  std::set<int> seen;
  std::function<void(int, int)> visit = [&](int id, int depth) {
    if (depth > 256 || !seen.insert(id).second) return;
    const BlueprintNode* n = findNode(id);
    if (!n) return;
    // pure inputs are evaluated before the node that consumes them
    for (int i = 0; i < int(n->inputs.size()); i++) {
      if (n->inputs[size_t(i)].type == PT::Exec) continue;
      for (auto& w : m_wires) {
        if (w.toNodeId != id || w.toPinIndex != i) continue;
        const BlueprintNode* src = findNode(w.fromNodeId);
        if (src && std::none_of(src->inputs.begin(), src->inputs.end(), bp::isExec) && src->category != BlueprintNodeCategory::Event) visit(src->id, depth + 1);
      }
    }
    order.push_back(id);
    for (int o = 0; o < int(n->outputs.size()); o++) {
      if (n->outputs[size_t(o)].type != PT::Exec) continue;
      for (auto& w : m_wires)
        if (w.fromNodeId == id && w.fromPinIndex == o) visit(w.toNodeId, depth + 1);
    }
  };
  std::vector<const BlueprintNode*> events;
  for (auto& n : m_nodes) if (n.category == BlueprintNodeCategory::Event) events.push_back(&n);
  std::sort(events.begin(), events.end(), [](auto* a, auto* b) { return a->pos.y < b->pos.y; });
  for (auto* e : events) visit(e->id, 0);
  return order;
}

bool BlueprintGraph::usesExecFlow() const {
  switch (m_preset) {
    case BlueprintGraphPreset::AnimationGraph: case BlueprintGraphPreset::StateMachine: case BlueprintGraphPreset::BehaviorTree:
    case BlueprintGraphPreset::ParticleGraph: case BlueprintGraphPreset::AudioGraph: return false;
    default: return true;
  }
}

// Data-flow order for non-exec graphs: Kahn topological sort (sources first, ties by x position).
std::vector<int> BlueprintGraph::flowOrder() const {
  std::map<int, int> indeg;
  for (auto& n : m_nodes) indeg[n.id] = 0;
  for (auto& w : m_wires) indeg[w.toNodeId]++;
  std::vector<int> order, ready;
  auto byX = [&](int a, int b) { return findNode(a)->pos.x < findNode(b)->pos.x; };
  for (auto& kv : indeg) if (kv.second == 0) ready.push_back(kv.first);
  while (!ready.empty()) {
    std::sort(ready.begin(), ready.end(), byX);
    int id = ready.front();
    ready.erase(ready.begin());
    order.push_back(id);
    for (auto& w : m_wires) if (w.fromNodeId == id && --indeg[w.toNodeId] == 0) ready.push_back(w.toNodeId);
  }
  return order;
}

void BlueprintGraph::run() {
  m_paused = false; m_pauseIdx = m_resumeIdx = m_stepIdx = -1;
  m_runLog.clear(); m_runIssues.clear();
  if (usesExecFlow()) {
    // Really execute the graph: branches take the branch their condition selects, loops loop, variables change.
    BlueprintVM vm(program());
    vm.maxSteps = 20000;
    vm.fireAll();
    m_runLog = vm.log();
    for (auto& e : vm.program().errors) m_runIssues.push_back(e);
    for (auto& u : vm.unhandled()) m_runIssues.push_back("no native registered for '" + u + "' (passed through)");
    if (vm.aborted()) m_runIssues.push_back("stopped: step budget exhausted (runaway loop?)");
    m_runOrder.clear();
    std::set<int> seen;
    for (int id : vm.trace()) if (findNode(id) && seen.insert(id).second) m_runOrder.push_back(id);   // collapsed-in nodes have no widget
    if (m_runOrder.empty()) m_runOrder = executionOrder();
    for (auto& l : m_runLog) Application::get().notify("Print: " + l, ToastLevel::Info, 1600);
  } else m_runOrder = flowOrder();
  if (m_runOrder.empty()) { Application::get().notify(usesExecFlow() ? "Nothing to run: add an Event node and wire its exec pin" : "Nothing to run: add nodes and wire them together", ToastLevel::Warning); return; }
  m_runActive = true;
  m_runClock = 0;
  startFlowTimer();
}

void BlueprintGraph::debugRunAt(float seconds) {
  m_runOrder = usesExecFlow() ? executionOrder() : flowOrder();
  m_runActive = !m_runOrder.empty();
  m_runClock = seconds;
  invalidate();
}

void BlueprintGraph::startFlowTimer() {
  if (m_runTimer) return;
  m_runTimer = Application::get().addTimer(16, [this] {
    m_flowTime += 0.016f;
    if (m_paused) { invalidate(); return true; }
    m_runClock += 0.016f;
    int idx = int(m_runClock / 0.42f);
    if (idx >= 0 && idx < int(m_runOrder.size()) && idx != m_resumeIdx && (m_breaks.count(m_runOrder[size_t(idx)]) || idx == m_stepIdx)) {
      m_paused = true; m_pauseIdx = idx; m_stepIdx = -1;
      m_runClock = float(idx) * 0.42f + 0.001f;
      invalidate(); return true;
    }
    if (m_runClock > float(m_runOrder.size()) * 0.42f + 0.8f) { m_runActive = false; m_runTimer = 0; invalidate(); return false; }
    invalidate();
    return true;
  });
}

} // namespace prism
