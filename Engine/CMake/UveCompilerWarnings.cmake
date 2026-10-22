# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# uve_set_warnings(target) applies UVE's standard strict-warnings-as-errors
# policy to a single CMake target. Verified compatible with both GCC and
# Clang. Applied uniformly to every uve_* target so the whole codebase shares
# one quality bar.

function(uve_set_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wnon-virtual-dtor
        -Woverloaded-virtual
        -Wold-style-cast
        -Wcast-align
        -Werror
    )

    # GCC 12 reports a bogus overlap for ordinary string building at -O2/-O3: it inlines
    # `const char* + std::string&&` down to `char_traits<char>::copy`, derives the copy size by
    # subtraction, loses the bound and then claims the source and destination may overlap. The
    # code it flags (`"(" + std::to_string(x) + ")"` in a ToString helper) is correct, and nothing
    # fires in Debug because none of it is inlined - which is why this only surfaced once real
    # Release and Shipping profiles existed. Scoped to the compiler that has the bug rather than
    # disabled project-wide, so the check still guards every other toolchain.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
        target_compile_options(${target} PRIVATE -Wno-restrict)
    endif()
endfunction()
