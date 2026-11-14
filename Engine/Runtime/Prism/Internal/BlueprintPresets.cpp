// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - built-in Blueprint presets (shader graph, gameplay script, widget visibility logic).
#include "BlueprintInternal.h"

namespace prism {

void BlueprintGraph::loadMaterialShaderPreset() {
  clear();
  m_pan = Vec2(30.0f, 35.0f);
  m_zoom = 1.0f;

  // Comment 1: Base Color & AO Blending
  BlueprintComment c1;
  c1.id = 101;
  c1.title = "// PBR Base Color & Ambient Occlusion";
  c1.bounds = Rect{20.0f, 10.0f, 560.0f, 390.0f};
  c1.color = Color(22, 66, 91, 45); // Teal Tint
  addComment(c1);

  // Comment 2: Surface Microfacet Normals & Roughness
  BlueprintComment c2;
  c2.id = 102;
  c2.title = "// Surface Microfacet & Normal Mapping";
  c2.bounds = Rect{320.0f, 410.0f, 300.0f, 440.0f};
  c2.color = Color(91, 50, 22, 45); // Amber Rust Tint
  addComment(c2);

  // Comment 3: Optical Fresnel Rim
  BlueprintComment c3;
  c3.id = 103;
  c3.title = "// Optical Fresnel Rim Lighting";
  c3.bounds = Rect{20.0f, 455.0f, 260.0f, 190.0f};
  c3.color = Color(66, 22, 91, 45); // Violet Tint
  addComment(c3);

  // Comment 4: Master Surface Material Output
  BlueprintComment c4;
  c4.id = 104;
  c4.title = "// Master PBR Surface Output";
  c4.bounds = Rect{690.0f, 10.0f, 290.0f, 300.0f};
  c4.color = Color(35, 45, 60, 45); // Charcoal Steel
  addComment(c4);

  // Node 1: PBR Master Material Output
  BlueprintNode matOutput;
  matOutput.id = 1;
  matOutput.title = "PBR Master Material";
  matOutput.subtitle = "Surface Material Output";
  matOutput.category = BlueprintNodeCategory::Material;
  matOutput.pos = Vec2(715, 50);
  matOutput.size = Vec2(220, 200);
  matOutput.inputs = {
    {"Base Color", BlueprintPinType::Vector, BlueprintPinKind::Input, "1,1,1"},
    {"Metallic", BlueprintPinType::Float, BlueprintPinKind::Input, "0.0"},
    {"Roughness", BlueprintPinType::Float, BlueprintPinKind::Input, "0.5"},
    {"Emissive", BlueprintPinType::Color, BlueprintPinKind::Input, "0,0,0"},
    {"Normal", BlueprintPinType::Vector, BlueprintPinKind::Input, "0,0,1"},
    {"Ambient Occlusion", BlueprintPinType::Float, BlueprintPinKind::Input, "1.0"},
    {"Opacity", BlueprintPinType::Float, BlueprintPinKind::Input, "1.0"}
  };
  addNode(matOutput);

  // Node 2: Vector Parameter (Gold Albedo)
  BlueprintNode albedoNode;
  albedoNode.id = 2;
  albedoNode.title = "Gold Albedo";
  albedoNode.subtitle = "Vector Parameter";
  albedoNode.category = BlueprintNodeCategory::Variable;
  albedoNode.pos = Vec2(40, 50);
  albedoNode.size = Vec2(180, 85);
  albedoNode.outputs = {
    {"RGB", BlueprintPinType::Vector, BlueprintPinKind::Output, ""},
    {"Alpha", BlueprintPinType::Float, BlueprintPinKind::Output, ""}
  };
  albedoNode.preview = BlueprintNode::Preview::Color; albedoNode.previewValue = "0.96,0.74,0.22";
  addNode(albedoNode);

  // Node 3: Multiply (Albedo x AO)
  BlueprintNode multNode;
  multNode.id = 3;
  multNode.title = "Multiply";
  multNode.subtitle = "Math Function";
  multNode.category = BlueprintNodeCategory::PureMath;
  multNode.pos = Vec2(340, 50);
  multNode.size = Vec2(185, 95);
  multNode.inputs = {
    {"A", BlueprintPinType::Vector, BlueprintPinKind::Input, ""},
    {"B", BlueprintPinType::Float, BlueprintPinKind::Input, "1.0"}
  };
  multNode.outputs = {
    {"Result", BlueprintPinType::Vector, BlueprintPinKind::Output, ""}
  };
  addNode(multNode);

  // Node 4: Scalar Parameter (Ambient Occlusion)
  BlueprintNode aoNode;
  aoNode.id = 4;
  aoNode.title = "AO Intensity";
  aoNode.subtitle = "Scalar Parameter";
  aoNode.category = BlueprintNodeCategory::Variable;
  aoNode.pos = Vec2(40, 232);
  aoNode.size = Vec2(180, 75);
  aoNode.outputs = {
    {"Value", BlueprintPinType::Float, BlueprintPinKind::Output, "0.85"}
  };
  aoNode.preview = BlueprintNode::Preview::Scalar; aoNode.previewValue = "0.85";
  addNode(aoNode);

  // Node 5: Texture Sample (Normal Map)
  BlueprintNode normalNode;
  normalNode.id = 5;
  normalNode.title = "Normal Map Sample";
  normalNode.subtitle = "Texture Sample 2D";
  normalNode.category = BlueprintNodeCategory::Texture;
  normalNode.pos = Vec2(340, 450);
  normalNode.size = Vec2(210, 110);
  normalNode.inputs = {
    {"UVs", BlueprintPinType::Vector, BlueprintPinKind::Input, ""}
  };
  normalNode.outputs = {
    {"RGB", BlueprintPinType::Vector, BlueprintPinKind::Output, ""},
    {"R", BlueprintPinType::Float, BlueprintPinKind::Output, ""},
    {"G", BlueprintPinType::Float, BlueprintPinKind::Output, ""}
  };
  normalNode.preview = BlueprintNode::Preview::Normal;
  addNode(normalNode);

  // Node 6: Scalar Parameter (Roughness)
  BlueprintNode roughNode;
  roughNode.id = 6;
  roughNode.title = "Roughness Val";
  roughNode.subtitle = "Scalar Parameter";
  roughNode.category = BlueprintNodeCategory::Variable;
  roughNode.pos = Vec2(340, 680);
  roughNode.size = Vec2(180, 75);
  roughNode.outputs = {
    {"Value", BlueprintPinType::Float, BlueprintPinKind::Output, "0.22"}
  };
  roughNode.preview = BlueprintNode::Preview::Scalar; roughNode.previewValue = "0.35";
  addNode(roughNode);

  // Node 7: Fresnel Exponent
  BlueprintNode fresnelNode;
  fresnelNode.id = 7;
  fresnelNode.title = "Fresnel";
  fresnelNode.subtitle = "Optical Utility";
  fresnelNode.category = BlueprintNodeCategory::PureMath;
  fresnelNode.pos = Vec2(40, 490);
  fresnelNode.size = Vec2(180, 95);
  fresnelNode.inputs = {
    {"Exp", BlueprintPinType::Float, BlueprintPinKind::Input, "5.0"}
  };
  fresnelNode.outputs = {
    {"Out", BlueprintPinType::Float, BlueprintPinKind::Output, ""}
  };
  fresnelNode.preview = BlueprintNode::Preview::Scalar; fresnelNode.previewValue = "0.6";
  addNode(fresnelNode);

  // Connect Wires
  connectPins(2, 0, 3, 0, BlueprintPinType::Vector); // Albedo RGB -> Multiply A
  connectPins(4, 0, 3, 1, BlueprintPinType::Float);  // AO Value -> Multiply B
  connectPins(3, 0, 1, 0, BlueprintPinType::Vector); // Multiply Result -> Master Base Color
  connectPins(5, 0, 1, 4, BlueprintPinType::Vector); // Normal RGB -> Master Normal
  connectPins(6, 0, 1, 2, BlueprintPinType::Float);  // Roughness -> Master Roughness
  connectPins(7, 0, 1, 3, BlueprintPinType::Color);  // Fresnel Out -> Master Emissive
}

void BlueprintGraph::loadGameplayScriptPreset() {
  clear();
  m_pan = Vec2(30.0f, 35.0f);
  m_zoom = 1.0f;

  // Comment 1: Player Locomotion & Rotation Control
  BlueprintComment c1;
  c1.id = 201;
  c1.title = "// Player Locomotion & Rotation Control";
  c1.bounds = Rect{30.0f, 20.0f, 1290.0f, 215.0f};
  c1.color = Color(20, 50, 85, 45); // navy blue
  addComment(c1);

  // Comment 2: Initialization & Diagnostics
  BlueprintComment c2;
  c2.id = 202;
  c2.title = "// Engine Initialization & Diagnostics";
  c2.bounds = Rect{30.0f, 255.0f, 480.0f, 150.0f};
  c2.color = Color(20, 75, 40, 45); // Forest Green
  addComment(c2);

  // Node 1: Event Tick
  BlueprintNode tickNode;
  tickNode.id = 1;
  tickNode.title = "Event Tick";
  tickNode.subtitle = "Engine Loop Event";
  tickNode.category = BlueprintNodeCategory::Event;
  tickNode.pos = Vec2(50, 55);
  tickNode.size = Vec2(185, 95);
  tickNode.outputs = {
    {"Exec", BlueprintPinType::Exec, BlueprintPinKind::Output, ""},
    {"Delta Seconds", BlueprintPinType::Float, BlueprintPinKind::Output, ""}
  };
  addNode(tickNode);

  // Node 2: Branch (Iconic Flow Control Node)
  BlueprintNode branchNode;
  branchNode.id = 2;
  branchNode.title = "Branch";
  branchNode.subtitle = "Flow Control";
  branchNode.category = BlueprintNodeCategory::Function;
  branchNode.pos = Vec2(275, 55);
  branchNode.size = Vec2(175, 95);
  branchNode.inputs = {
    {"Exec", BlueprintPinType::Exec, BlueprintPinKind::Input, ""},
    {"Condition", BlueprintPinType::Bool, BlueprintPinKind::Input, "true"}
  };
  branchNode.outputs = {
    {"True", BlueprintPinType::Exec, BlueprintPinKind::Output, ""},
    {"False", BlueprintPinType::Exec, BlueprintPinKind::Output, ""}
  };
  addNode(branchNode);

  // Node 3: Multiply
  BlueprintNode multNode;
  multNode.id = 3;
  multNode.title = "Multiply";
  multNode.subtitle = "Math Function";
  multNode.category = BlueprintNodeCategory::PureMath;
  multNode.pos = Vec2(545, 100);
  multNode.size = Vec2(175, 95);
  multNode.inputs = {
    {"A", BlueprintPinType::Float, BlueprintPinKind::Input, ""},
    {"Speed", BlueprintPinType::Float, BlueprintPinKind::Input, "90.0"}
  };
  multNode.outputs = {
    {"Yaw Delta", BlueprintPinType::Float, BlueprintPinKind::Output, ""}
  };
  addNode(multNode);

  // Node 4: Make Rotator
  BlueprintNode rotNode;
  rotNode.id = 4;
  rotNode.title = "Make Rotator";
  rotNode.subtitle = "Transform Utility";
  rotNode.category = BlueprintNodeCategory::PureMath;
  rotNode.pos = Vec2(800, 85);
  rotNode.size = Vec2(215, 120);
  rotNode.inputs = {
    {"Pitch", BlueprintPinType::Float, BlueprintPinKind::Input, "0.0"},
    {"Yaw", BlueprintPinType::Float, BlueprintPinKind::Input, "0.0"},
    {"Roll", BlueprintPinType::Float, BlueprintPinKind::Input, "0.0"}
  };
  rotNode.outputs = {
    {"Rotation", BlueprintPinType::Vector, BlueprintPinKind::Output, ""}
  };
  addNode(rotNode);

  // Node 5: Add Actor Local Rotation
  BlueprintNode addRotNode;
  addRotNode.id = 5;
  addRotNode.title = "Add Actor Local Rotation";
  addRotNode.subtitle = "Actor Movement";
  addRotNode.category = BlueprintNodeCategory::Function;
  addRotNode.pos = Vec2(1050, 55);
  addRotNode.size = Vec2(225, 135);
  addRotNode.inputs = {
    {"Exec", BlueprintPinType::Exec, BlueprintPinKind::Input, ""},
    {"Target", BlueprintPinType::Object, BlueprintPinKind::Input, "Self"},
    {"Delta Rotation", BlueprintPinType::Vector, BlueprintPinKind::Input, ""},
    {"Sweep", BlueprintPinType::Bool, BlueprintPinKind::Input, "false"}
  };
  addRotNode.outputs = {
    {"Exec", BlueprintPinType::Exec, BlueprintPinKind::Output, ""}
  };
  addNode(addRotNode);

  // Node 6: Event BeginPlay
  BlueprintNode beginNode;
  beginNode.id = 6;
  beginNode.title = "Event BeginPlay";
  beginNode.subtitle = "Initialization Event";
  beginNode.category = BlueprintNodeCategory::Event;
  beginNode.pos = Vec2(50, 290);
  beginNode.size = Vec2(185, 80);
  beginNode.outputs = {
    {"Exec", BlueprintPinType::Exec, BlueprintPinKind::Output, ""}
  };
  addNode(beginNode);

  // Node 7: Print String
  BlueprintNode printNode;
  printNode.id = 7;
  printNode.title = "Print String";
  printNode.subtitle = "Debug Output";
  printNode.category = BlueprintNodeCategory::Function;
  printNode.pos = Vec2(275, 285);
  printNode.size = Vec2(210, 115);
  printNode.inputs = {
    {"Exec", BlueprintPinType::Exec, BlueprintPinKind::Input, ""},
    {"Message", BlueprintPinType::String, BlueprintPinKind::Input, "PRISM Engine"},
    {"Duration", BlueprintPinType::Float, BlueprintPinKind::Input, "2.0"}
  };
  printNode.outputs = {
    {"Exec", BlueprintPinType::Exec, BlueprintPinKind::Output, ""}
  };
  addNode(printNode);

  // Connect Wires
  connectPins(1, 0, 2, 0, BlueprintPinType::Exec);   // Tick Exec -> Branch Exec
  connectPins(2, 0, 5, 0, BlueprintPinType::Exec);   // Branch True Exec -> AddRot Exec
  connectPins(1, 1, 3, 0, BlueprintPinType::Float);  // Delta Seconds -> Multiply A
  connectPins(3, 0, 4, 1, BlueprintPinType::Float);  // Yaw Delta -> Make Rotator Yaw
  connectPins(4, 0, 5, 2, BlueprintPinType::Vector); // Rotation -> AddRot Delta Rotation
  connectPins(6, 0, 7, 0, BlueprintPinType::Exec);   // BeginPlay Exec -> Print Exec
}


// UI widget logic: Event -> Sequence -> four (validated Get -> SetVisible) pairs, each fed by a pure CalcVisible* function.
void BlueprintGraph::loadWidgetBlueprintPreset() {
  using PT = BlueprintPinType;
  using PK = BlueprintPinKind;
  clear();
  m_pan = Vec2(40.0f, 40.0f);
  m_zoom = 0.85f;
  m_needFrame = true;

  auto spec = [](const char* name) -> const BlueprintNodeSpec* {
    for (auto& s : blueprintNodeLibrary()) if (s.name == name) return &s;
    return nullptr;
  };
  struct Row { const char* get; const char* calc; };
  const Row rows[4] = {{"Get Button Unlock", "CalcVisibleBtnUnlock"}, {"Get Image Padlock Closed", "CalcVisibleImgPadlockLocked"},
                       {"Get Button Reset", "CalcVisibleBtnReset"}, {"Get Button Play", "CalcVisibleBtnPlay"}};

  BlueprintComment c;
  c.id = 301; c.title = "Widget visibility state"; c.bounds = Rect{470, -34, 790, 880}; c.color = Color(40, 90, 140, 40);
  addComment(c);

  auto place = [&](const char* name, int id, Vec2 p) { if (auto* s = spec(name)) { BlueprintNode n = s->make(); n.id = id; n.pos = p; addNode(std::move(n)); } };
  place("Event Construct", 1, Vec2(0, 260));
  place("Sequence", 2, Vec2(250, 230));
  for (int k = 0; k < 4; k++) {
    float gy = float(k) * 210.0f;
    place(rows[k].get, 10 + k, Vec2(500, gy));
    place(rows[k].calc, 20 + k, Vec2(500, gy + 104));
    place("SetVisible", 30 + k, Vec2(930, gy + 8));
  }
  connectPins(1, 0, 2, 0, PT::Exec);
  for (int k = 0; k < 4; k++) {
    connectPins(2, k, 10 + k, 0, PT::Exec);
    connectPins(10 + k, 0, 30 + k, 0, PT::Exec);     // Valid -> SetVisible exec
    connectPins(10 + k, 1, 30 + k, 1, PT::Object);   // variable -> Context
    connectPins(20 + k, 0, 30 + k, 2, PT::Bool);     // Result -> Visibility
  }
  (void)sizeof(PK);
}

} // namespace prism
