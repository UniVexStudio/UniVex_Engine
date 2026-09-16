# Engine/Runtime/Input

Real, implemented input module — NOT a placeholder (old README was stale).

**What exists now (fully ported, ~1915 LOC):**
- `IInputSystemUVE` / `InputSystemUVE` — desktop keyboard/mouse, action bindings, edge state, frame counter, `InputActionTriggeredEventUVE`
- `IGamepadInputSystemUVE` / `GamepadInputSystemUVE` — gamepad abstraction, committed before InputSystem so action bindings read current snapshot
- `IMobileInputSystemUVE` / `MobileInputSystemUVE` — touch input, unconditional construction (not Android-only), available uniformly
- `IMobileGestureSystemUVE` / `MobileGestureSystemUVE` + `MobileGestureRecognizerUVE` + `TouchCoordinateTransformUVE` — gesture recognition consuming mobile snapshot

**Integration:**
- `EngineCoreUVE` owns and ticks: Gamepad -> Mobile -> MobileGesture -> Input (order matters per EngineCore doc)
- Scripting bindings: keyboard/mouse input is bound to visual scripting nodes (gamepad/action-mapped still TODO per ROADMAP 7.1)

**Scaffolding / Future (ROADMAP 7.2):**
- [ ] Formal input-action-mapping layer (bind logical action "Jump" to any physical input, rebinding support) — currently scripts poll raw key codes
- [ ] Full gamepad mapping, mobile safe-area handling, touch-first UI scaling

This module is real and tested — see `Test/Input/`.

