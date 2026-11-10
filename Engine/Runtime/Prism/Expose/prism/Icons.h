// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// PRISM - procedural vector icon set (drawn with the Canvas, tinted by any colour).
#pragma once
#include "Canvas.h"

namespace prism {

enum class Icon : uint8_t {
  None, TriRight, TriDown, ChevronRight, ChevronDown, ChevronUp, ChevronLeft,
  Close, Plus, Minus, Check, Search, Eye, EyeOff, Lock, Unlock,
  Cube, Sphere, Cylinder, Cone, Plane, Light, Camera, Folder, FolderOpen, File,
  Play, Pause, Stop, Undo, Redo, Move, Rotate, Scale, Select, Grid, Save, Open,
  Settings, Trash, Copy, Paste, Info, Warning, Error, Filter, Layers, Dots, Refresh,
  Magnet, Terminal, Frame, Globe, Wireframe, Shaded, Dot, Script, Material, Duplicate,
  // graph / blueprint kinds
  Blueprint, EventGraph, WidgetGraph, AnimGraph, StateGraph, BehaviorGraph, VfxGraph, AudioGraph,
  // blueprint toolbar
  Compile, Browse, Properties,
  // per-node glyphs (blueprint node title tiles)
  NodeFunction, NodeMath, NodeCompare, NodeLogic, NodeVector, NodeRandom, NodeLerp, NodeClamp,
  NodeBranch, NodeSequence, NodeLoop, NodeClock, NodeTimer, NodeHourglass, NodeText, NodeGate, NodeCast,
  NodeDamage, NodeLaunch, NodeSound, NodeInput, NodeSpawn, NodeOverlap, NodeImpact, NodeFlag, NodeTick, NodeStop,
  NodeToggle, NodeClip, NodeBlendSpace, NodeBlend, NodeBone, NodeSlot, NodeOutput, NodeEntry, NodeTransition,
  NodeSelector, NodeChain, NodeDecorator, NodeMoveTo, NodeAttack, NodeRoute, NodeEmitter, NodeGravity, NodeNoise,
  NodeGradient, NodeScale, NodeSprite, NodeWave, NodeEnvelope, NodeFilter, NodeEcho, NodeMixer, NodeSlider,
  NodePalette, NodeTexture, NodeFresnel,
};

// Draws `icon` centred in `box`, scaled so a 16x16 design grid fits the shorter side.
void drawIcon(Canvas& c, Icon icon, Rect box, Color color, float stroke = 1.3f);
// The PRISM mark: a faceted prism.
void drawLogo(Canvas& c, Rect box);

} // namespace prism
