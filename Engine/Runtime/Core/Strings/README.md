# Engine/Runtime/Core/Strings — Scaffolding

Scaffolding for future string library.

**Current status:** Empty. Currently using `std::string`, `std::string_view`, `std::filesystem::path`.

**Why keep?**
- Custom strings could be: allocator-aware, small-string-optimized with known size, UTF-8 handling, path abstraction, interned strings for asset GUIDs/tags, localization-ready.
- Examples: `StringUVE`, `StringViewUVE`, `NameUVE` (interned), `PathUVE`.

**Plan:** Keep as scaffolding. If memory tracking or perf needs arise (e.g., many NameComponent lookups), implement here.

Currently low priority.

