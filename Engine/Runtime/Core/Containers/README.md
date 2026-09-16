# Engine/Runtime/Core/Containers — Scaffolding

Scaffolding for future custom container library.

**Current status:** Empty. Currently using std containers (`std::vector`, `std::unordered_map`) directly.

**Why keep?**
- Custom containers could be allocator-aware (using `IAllocatorUVE` / `MemoryManager`), track memory, avoid STL overhead in hot paths, provide stable handles.
- Examples: `ArrayUVE`, `HashMapUVE`, `HandleTableUVE`, `RingBufferUVE`.

**Plan:** Keep as scaffolding. When first container is needed for perf or memory tracking, implement here with `Expose/uve/core/containers/<name>_uve.h`.

Currently low priority — std is fine for now.

