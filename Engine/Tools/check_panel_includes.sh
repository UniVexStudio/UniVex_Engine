#!/bin/bash
# Checks that every editor panel translation unit includes a standard header for each std::
# facility it names, instead of relying on one arriving transitively through imgui.h or a
# component header.
#
# WHY THIS EXISTS. Splitting editor_uve.cpp into per-panel files moved code away from the
# includes it had been quietly borrowing. A missing <cstring> compiled cleanly here - even under
# -Wall -Wextra - because another header happened to pull it in, and broke CI's build instead.
# A compiler cannot warn about an include you got away with; this can.
#
# Deliberately a text check rather than a compile: the point is to catch the transitive rescue,
# and any compile on this machine has the same transitive includes available that hid the bug.
# Run it after adding code to a panel TU.
cd /home/user/UNIVEX
INC=$(find Engine -type d -name Expose | sed 's/^/-I/' | tr '\n' ' ')
fail=0
for f in Engine/Editor/EditorCore/Internal/editor_panel_*.cpp; do
  # list every std:: symbol used, and confirm a matching standard header is included
  while read -r sym hdr; do
    if grep -q "std::$sym\b" "$f" && ! grep -q "#include <$hdr>" "$f"; then
      echo "MISSING <$hdr> for std::$sym in ${f##*/}"; fail=1
    fi
  done <<'MAP'
strncpy cstring
strlen cstring
memcpy cstring
memset cstring
snprintf cstdio
printf cstdio
tolower cctype
isdigit cctype
isfinite cmath
fabs cmath
sort algorithm
stable_sort algorithm
clamp algorithm
max algorithm
min algorithm
find_if algorithm
search algorithm
remove_if algorithm
function functional
optional optional
to_string string
string string
string_view string_view
array array
vector vector
uint64_t cstdint
uint32_t cstdint
uintptr_t cstdint
size_t cstddef
move utility
pair utility
filesystem filesystem
error_code system_error
MAP
done
[ $fail -eq 0 ] && echo "include audit: clean"
exit $fail
