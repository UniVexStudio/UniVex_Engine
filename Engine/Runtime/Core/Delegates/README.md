# Engine/Runtime/Core/Delegates — Scaffolding

Scaffolding for future delegate / event callback library.

**Current status:** Empty. Currently using `std::function`, `std::function<void()>`, and `IEventSystemUVE` for events.

**Why keep?**
- A custom delegate could be: non-allocating for small captures, multicast, explicit lifetime, faster than std::function.
- Could provide `DelegateUVE`, `MulticastDelegateUVE`, `EventUVE` with handle-based subscription.

**Plan:** Keep as scaffolding. If EventSystem needs perf or allocation guarantees, implement here.

Currently low priority — `std::function` + EventSystem works.

