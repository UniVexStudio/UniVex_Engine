// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - sample graphs for the non-material blueprint types: Event Blueprint, Animation Graph,
// State Machine, Behavior Tree, VFX Graph and Audio Graph. Every node comes from blueprintNodeLibrary().
#include "BlueprintInternal.h"

namespace prism {

using PT = BlueprintPinType;

void BlueprintGraph::placeNode(int id, const char* specName, float x, float y, const char* title, const char* sub) {
  for (auto& s : blueprintNodeLibrary()) {
    if (s.name != specName) continue;
    BlueprintNode n = s.make();
    n.id = id;
    n.pos = Vec2(x, y);
    if (title && *title) n.title = title;
    if (sub && *sub) n.subtitle = std::string(sub) == "-" ? std::string() : std::string(sub);
    addNode(std::move(n));
    return;
  }
}

namespace {
BlueprintComment box(int id, const char* title, Rect r, Color c) {
  BlueprintComment k; k.id = id; k.title = title; k.bounds = r; k.color = c; return k;
}
}

void BlueprintGraph::loadEventBlueprintPreset() {
  clear();
  m_pan = Vec2(30, 30); m_zoom = 0.8f; m_needFrame = true;
  addComment(box(901, "// Pickup: damage the player, play a sound, remove the actor", {30, 20, 1330, 310}, Color(20, 50, 85, 45)));
  addComment(box(902, "// Movement input", {30, 350, 640, 420}, Color(20, 75, 40, 45)));
  addComment(box(903, "// Lifecycle & timers", {690, 350, 670, 420}, Color(85, 60, 20, 45)));

  placeNode(1, "Event ActorBeginOverlap", 50, 70);
  placeNode(2, "Cast To Player", 290, 70);
  placeNode(3, "Apply Damage", 530, 60, "", "-");
  placeNode(4, "Play Sound 2D", 870, 60, "", "-");
  placeNode(5, "Destroy Actor", 1120, 60, "", "-");
  placeNode(6, "Print String", 530, 225, "", "Not a player");
  placeNode(7, "Input Action Jump", 50, 395);
  placeNode(8, "Launch Character", 330, 390, "", "-");
  placeNode(9, "Input Axis MoveForward", 50, 600, "Input Axis Move", "");
  placeNode(10, "Add Movement Input", 330, 610, "", "-");
  placeNode(11, "Event BeginPlay", 710, 400);
  placeNode(12, "Set Timer by Event", 960, 400);
  placeNode(13, "Custom Event", 710, 600, "Custom Event: Respawn", "");
  placeNode(14, "Set Actor Location", 1000, 590, "", "-");

  connectPins(1, 0, 2, 0, PT::Exec);
  connectPins(1, 1, 2, 1, PT::Object);
  connectPins(2, 0, 3, 0, PT::Exec);
  connectPins(2, 2, 3, 1, PT::Object);
  connectPins(3, 0, 4, 0, PT::Exec);
  connectPins(4, 0, 5, 0, PT::Exec);
  connectPins(2, 1, 6, 0, PT::Exec);
  connectPins(7, 0, 8, 0, PT::Exec);
  connectPins(9, 0, 10, 0, PT::Exec);
  connectPins(9, 1, 10, 3, PT::Float);
  connectPins(11, 0, 12, 0, PT::Exec);
  connectPins(13, 0, 14, 0, PT::Exec);
}

void BlueprintGraph::loadAnimationGraphPreset() {
  clear();
  m_pan = Vec2(30, 40); m_zoom = 0.8f; m_needFrame = true;
  addComment(box(911, "// Locomotion", {30, 20, 560, 420}, Color(20, 85, 85, 45)));
  addComment(box(912, "// Layers & IK", {620, 20, 800, 420}, Color(35, 60, 90, 45)));

  placeNode(1, "Get Is In Air", 50, 60);
  placeNode(2, "Sequence Player", 50, 150, "", "Jump_Loop");
  placeNode(3, "State Machine Pose", 50, 290, "State Machine", "Locomotion");
  placeNode(4, "Blend Poses by Bool", 360, 120);
  placeNode(5, "Sequence Player", 640, 270, "", "Fire_Rifle");
  placeNode(6, "Layered Blend per Bone", 660, 60);
  placeNode(7, "Two Bone IK", 960, 60, "", "Left Foot");
  placeNode(8, "Default Slot", 1220, 60);
  placeNode(9, "Output Pose", 1220, 250);

  connectPins(1, 0, 4, 0, PT::Bool);
  connectPins(2, 0, 4, 1, PT::Pose);
  connectPins(3, 0, 4, 2, PT::Pose);
  connectPins(4, 0, 6, 0, PT::Pose);
  connectPins(5, 0, 6, 1, PT::Pose);
  connectPins(6, 0, 7, 0, PT::Pose);
  connectPins(7, 0, 8, 0, PT::Pose);
  connectPins(8, 0, 9, 0, PT::Pose);
}

void BlueprintGraph::loadStateMachinePreset() {
  clear();
  m_pan = Vec2(30, 40); m_zoom = 0.8f; m_needFrame = true;
  addComment(box(921, "// Locomotion state machine", {30, 20, 1500, 400}, Color(50, 50, 120, 45)));

  placeNode(1, "Entry", 50, 100);
  placeNode(2, "State", 270, 100, "Idle", "Blend Space: Idle");
  placeNode(3, "Transition Rule", 500, 100, "Speed > 3", "Idle -> Walk");
  placeNode(4, "State", 760, 100, "Walk", "Blend Space: Walk");
  placeNode(5, "Transition Rule", 990, 100, "Speed > 300", "Walk -> Run");
  placeNode(6, "State", 1250, 100, "Run", "Blend Space: Run");
  placeNode(7, "Transition Rule", 1250, 260, "Is In Air", "Run -> Jump");
  placeNode(8, "State", 990, 280, "Jump", "Sequence: Jump_Loop");
  placeNode(9, "Transition Rule", 500, 280, "Landed", "Jump -> Idle");

  connectPins(1, 0, 2, 0, PT::Link);
  connectPins(2, 0, 3, 0, PT::Link);
  connectPins(3, 0, 4, 0, PT::Link);
  connectPins(4, 0, 5, 0, PT::Link);
  connectPins(5, 0, 6, 0, PT::Link);
  connectPins(6, 0, 7, 0, PT::Link);
  connectPins(7, 0, 8, 0, PT::Link);
  connectPins(8, 0, 9, 0, PT::Link);
  connectPins(9, 0, 2, 0, PT::Link);   // loops back to Idle
}

void BlueprintGraph::loadBehaviorTreePreset() {
  clear();
  m_pan = Vec2(30, 30); m_zoom = 0.8f; m_needFrame = true;
  addComment(box(931, "// Combat", {400, 10, 640, 270}, Color(120, 40, 40, 40)));
  addComment(box(932, "// Patrol", {400, 290, 640, 270}, Color(30, 90, 60, 45)));

  placeNode(1, "BT Root", 20, 230);
  placeNode(2, "BT Selector", 190, 215);
  placeNode(3, "BT Blackboard Check", 420, 40, "Has Target?", "Decorator");
  placeNode(4, "BT Sequence", 660, 40, "Sequence", "Combat");
  placeNode(5, "BT Move To", 870, 30);
  placeNode(6, "BT Attack", 870, 150);
  placeNode(7, "BT Sequence", 420, 330, "Sequence", "Patrol");
  placeNode(8, "BT Find Patrol Point", 660, 310);
  placeNode(9, "BT Move To", 660, 440, "Move To", "Task: PatrolPoint");
  placeNode(10, "BT Wait", 870, 400);
  placeNode(11, "BT Wait", 420, 480, "Wait", "Fallback idle");

  connectPins(1, 0, 2, 0, PT::Link);
  connectPins(2, 0, 3, 0, PT::Link);
  connectPins(3, 0, 4, 0, PT::Link);
  connectPins(4, 0, 5, 0, PT::Link);
  connectPins(4, 1, 6, 0, PT::Link);
  connectPins(2, 1, 7, 0, PT::Link);
  connectPins(7, 0, 8, 0, PT::Link);
  connectPins(7, 1, 9, 0, PT::Link);
  connectPins(7, 2, 10, 0, PT::Link);
  connectPins(2, 2, 11, 0, PT::Link);
}

void BlueprintGraph::loadParticleGraphPreset() {
  clear();
  m_pan = Vec2(30, 40); m_zoom = 0.8f; m_needFrame = true;
  addComment(box(941, "// Spark emitter: spawn + initial state", {30, 20, 1110, 250}, Color(120, 40, 100, 40)));
  addComment(box(942, "// Forces, over-life and render", {30, 290, 1110, 250}, Color(90, 40, 120, 40)));

  placeNode(1, "Emitter Spawn", 50, 60);
  placeNode(2, "Initialize Particle", 290, 50);
  placeNode(3, "Add Velocity", 560, 50);
  placeNode(4, "Gravity Force", 820, 50);
  placeNode(5, "Curl Noise Force", 50, 330);
  placeNode(6, "Color Over Life", 310, 330);
  placeNode(7, "Size Over Life", 590, 330);
  placeNode(8, "Sprite Renderer", 850, 330);
  placeNode(9, "Random Float", 60, 200, "Random Float", "Lifetime jitter");

  connectPins(1, 0, 2, 0, PT::Link);
  connectPins(2, 0, 3, 0, PT::Link);
  connectPins(3, 0, 4, 0, PT::Link);
  connectPins(4, 0, 5, 0, PT::Link);
  connectPins(5, 0, 6, 0, PT::Link);
  connectPins(6, 0, 7, 0, PT::Link);
  connectPins(7, 0, 8, 0, PT::Link);
  connectPins(9, 0, 2, 1, PT::Float);
}

void BlueprintGraph::loadAudioGraphPreset() {
  clear();
  m_pan = Vec2(30, 40); m_zoom = 0.8f; m_needFrame = true;
  addComment(box(951, "// Footstep sound", {30, 20, 1620, 420}, Color(110, 80, 20, 40)));

  placeNode(1, "Wave Player", 50, 60, "Wave Player", "Layer A");
  placeNode(2, "Wave Player", 50, 250, "Wave Player", "Layer B");
  placeNode(3, "Random Float", 50, 400, "Random Float", "Pitch jitter");
  placeNode(4, "Audio Mixer", 340, 120);
  placeNode(5, "Low Pass Filter", 620, 120);
  placeNode(6, "Reverb", 890, 120);
  placeNode(7, "Audio Gain", 1150, 120);
  placeNode(8, "ADSR Envelope", 890, 300);
  placeNode(9, "Audio Output", 1400, 120);

  connectPins(1, 0, 4, 0, PT::Audio);
  connectPins(2, 0, 4, 1, PT::Audio);
  connectPins(3, 0, 1, 2, PT::Float);
  connectPins(4, 0, 5, 0, PT::Audio);
  connectPins(5, 0, 6, 0, PT::Audio);
  connectPins(6, 0, 7, 0, PT::Audio);
  connectPins(8, 0, 7, 1, PT::Float);
  connectPins(7, 0, 9, 0, PT::Audio);
}

} // namespace prism
