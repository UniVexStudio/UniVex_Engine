// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "uve/input/i_input_system_uve.h"
#include "uve/scripting/script_vm_uve.h"

namespace UVE::Core {

/// Backing store for the real, production ScriptEngineCallBindingsUVE built by
/// MakeScriptGameplayBindingsUVE() below - every binding function receives this struct back as its
/// `void* userData` and reads whichever subsystem pointer it needs. EngineCoreUVE holds one of
/// these for its whole lifetime and keeps `inputSystem` pointed at its own real InputSystemUVE, so
/// the bindings struct's userData pointer stays valid for as long as ScriptRuntimeUVE::TickUVE()
/// might call back into it.
struct ScriptGameplayBindingContextUVE final {
    Input::IInputSystemUVE* inputSystem = nullptr;
};

/// Builds a real ScriptEngineCallBindingsUVE wired to `context`'s subsystem pointers - the first
/// non-test-only implementation of these bindings anywhere in the engine (previously only
/// Test/Integration/Scripting/script_graph_uve_tests.cpp ever populated real function bodies for
/// this struct; every production call site left the whole struct null).
///
/// Only the keyboard/mouse input bindings are wired this increment (inputKeyPressed/Released/Down,
/// inputMousePosition, inputMouseButton). Every other field (gamepad/action-layer input, entity
/// spawn/component mutation, camera, animation, physics queries, audio) is deliberately left
/// nullptr: script_vm_uve.cpp's node executors already treat an individually-unset binding as
/// "that one node type fails cleanly, every other node still runs" (see ExecuteInputNodeUVE's own
/// per-field null checks), so leaving a field unset is an honest "not wired yet," not a silent
/// false capability. Gamepad/action bindings need a numeric-token<->name mapping this increment
/// doesn't define; entity/camera/animation/physics/audio bindings need real accessors (e.g. an
/// entity-to-voice-handle lookup for audio) that don't exist publicly yet - real, scoped follow-up
/// work, not an oversight.
[[nodiscard]] Scripting::ScriptEngineCallBindingsUVE MakeScriptGameplayBindingsUVE(
    ScriptGameplayBindingContextUVE& context) noexcept;

} // namespace UVE::Core
