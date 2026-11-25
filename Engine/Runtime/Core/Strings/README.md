# uve_strings

Interned names (`UVE::Strings`, Tier 0 foundation). A `StringId` is a cheap-to-copy 32-bit id
standing in for a string: comparison and hashing are integer operations, while the text itself
lives exactly once in a process-wide table and is always recoverable.

## Types

| Type | Storage | Equality | Use when |
|------|---------|----------|----------|
| `StringId` | `uint32_t` index into the intern table | Integer `==`/`!=` + `std::hash` | A bounded vocabulary is compared or keyed often (type ids, property types) |

## Conventions (Tier 0, item 0.4)

- **Constructors are implicit on purpose.** Call sites keep passing literals and `std::string`s
  (`FindType("component.transform")`); the read paths compare integers. There is no
  conversion back — recovering text spells `ToString()`/`ToCString()` explicitly.
- **Intern bounded vocabularies only.** Every unique string is kept for the rest of the process,
  so user text, file contents or generated names must never be interned — that would be a
  permanent leak by construction.
- **Ids are process-local.** Indices are arrival order: never persist them, persist
  `ToString()`. There is no `operator<` for the same reason — sort by `ToString()` when
  alphabetical order matters.
- **Strings are immortal.** The table never shrinks and entries never move, so views and
  C strings stay valid forever. Locking covers interning and recovery (cold paths:
  registration, serialization, display); comparison and copying are lock-free.

## Consumers

- `uve_object`: `TypeMetadataEntry::typeId`, `TypeMetadataProperty::typeId` and
  `nestedUnderTypeIds` are ids; registry lookup compares integers, snapshot order still sorts
  by the recovered text.
- `uve_scene`: the `kPropertyType*UVE` vocabulary is interned once at startup, so the
  inspector's property-type dispatch compares integers.
