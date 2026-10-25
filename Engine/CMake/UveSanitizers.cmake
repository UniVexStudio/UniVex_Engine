# Opt-in memory and undefined-behavior instrumentation for native validation builds.
option(UVE_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(UVE_ENABLE_SANITIZERS)
    if(WIN32 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
        message(FATAL_ERROR "UVE sanitizers require a native GNU or Clang build on a supported Unix host")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined -fno-sanitize-recover=all)
endif()
