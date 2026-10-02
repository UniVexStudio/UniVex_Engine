# Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#
# Basis Universal v2.50, pinned at the v2_50 release tag. Do not add_subdirectory() upstream's
# root project: it changes directory-wide compiler/linker flags and always creates the `basisu`
# command-line executable. This small integration builds only the transcoder and (on desktop)
# encoder libraries. Zstd, OpenCL, ASTCENC and the optional Android ASTC decoder are not needed for
# ETC1S/UASTC LDR KTX2 import or runtime transcode.

include(FetchContent)
find_package(Threads REQUIRED)

FetchContent_Declare(
    uve_basis_universal
    GIT_REPOSITORY https://github.com/BinomialLLC/basis_universal.git
    GIT_TAG        v2_50
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(uve_basis_universal)
if(NOT uve_basis_universal_POPULATED)
    FetchContent_Populate(uve_basis_universal)
endif()

set(_uve_basis_root "${uve_basis_universal_SOURCE_DIR}")
set(_uve_basis_common_definitions
    BASISD_SUPPORT_KTX2=1
    BASISD_SUPPORT_KTX2_ZSTD=0
    BASISU_SUPPORT_OPENCL=0
    BASISU_SUPPORT_SSE=0
    BASISU_SUPPORT_ASTCENC=0
    BASISU_DISABLE_ANDROID_ASTC_DECOMP=1
)

add_library(uve_basisu_transcoder STATIC
    "${_uve_basis_root}/transcoder/basisu_transcoder.cpp"
)
target_include_directories(uve_basisu_transcoder SYSTEM PUBLIC
    "${_uve_basis_root}/encoder"
    "${_uve_basis_root}/transcoder"
)
target_compile_definitions(uve_basisu_transcoder PRIVATE ${_uve_basis_common_definitions})
set_target_properties(uve_basisu_transcoder PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE ON
)
if(MSVC)
    target_compile_options(uve_basisu_transcoder PRIVATE /w)
else()
    target_compile_options(uve_basisu_transcoder PRIVATE -w -fno-strict-aliasing)
endif()

# Texture imports normally run in editor/desktop builds. Mobile builds retain the small runtime
# transcoder for loading packaged KTX2 assets without shipping the authoring encoder.
if(NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_library(uve_basisu_encoder STATIC
        "${_uve_basis_root}/encoder/basisu_backend.cpp"
        "${_uve_basis_root}/encoder/basisu_basis_file.cpp"
        "${_uve_basis_root}/encoder/basisu_comp.cpp"
        "${_uve_basis_root}/encoder/basisu_enc.cpp"
        "${_uve_basis_root}/encoder/basisu_etc.cpp"
        "${_uve_basis_root}/encoder/basisu_frontend.cpp"
        "${_uve_basis_root}/encoder/basisu_gpu_texture.cpp"
        "${_uve_basis_root}/encoder/basisu_pvrtc1_4.cpp"
        "${_uve_basis_root}/encoder/basisu_resampler.cpp"
        "${_uve_basis_root}/encoder/basisu_resample_filters.cpp"
        "${_uve_basis_root}/encoder/basisu_ssim.cpp"
        "${_uve_basis_root}/encoder/basisu_uastc_enc.cpp"
        "${_uve_basis_root}/encoder/basisu_bc7e_scalar.cpp"
        "${_uve_basis_root}/encoder/basisu_dds_export.cpp"
        "${_uve_basis_root}/encoder/basisu_bc7enc.cpp"
        "${_uve_basis_root}/encoder/jpgd.cpp"
        "${_uve_basis_root}/encoder/basisu_kernels_sse.cpp"
        "${_uve_basis_root}/encoder/basisu_bc15_spmd.cpp"
        "${_uve_basis_root}/encoder/basisu_bc15_spmd_sse.cpp"
        "${_uve_basis_root}/encoder/basisu_opencl.cpp"
        "${_uve_basis_root}/encoder/pvpngreader.cpp"
        "${_uve_basis_root}/encoder/basisu_uastc_hdr_4x4_enc.cpp"
        "${_uve_basis_root}/encoder/basisu_astc_hdr_6x6_enc.cpp"
        "${_uve_basis_root}/encoder/basisu_astc_hdr_common.cpp"
        "${_uve_basis_root}/encoder/basisu_astc_ldr_common.cpp"
        "${_uve_basis_root}/encoder/basisu_astc_ldr_encode.cpp"
        "${_uve_basis_root}/encoder/basisu_astc_ldr_fencode.cpp"
        "${_uve_basis_root}/encoder/basisu_xbc7_encode.cpp"
        "${_uve_basis_root}/encoder/basisu_tinyexr.cpp"
    )
    target_include_directories(uve_basisu_encoder SYSTEM PUBLIC
        "${_uve_basis_root}/encoder"
        "${_uve_basis_root}/transcoder"
    )
    target_compile_definitions(uve_basisu_encoder PRIVATE ${_uve_basis_common_definitions})
    target_link_libraries(uve_basisu_encoder PRIVATE uve_basisu_transcoder Threads::Threads)
    set_target_properties(uve_basisu_encoder PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        POSITION_INDEPENDENT_CODE ON
    )
    if(MSVC)
        target_compile_options(uve_basisu_encoder PRIVATE /w)
    else()
        target_compile_options(uve_basisu_encoder PRIVATE -w -fno-strict-aliasing)
    endif()
    set(UVE_BASIS_ENCODER_AVAILABLE ON)
else()
    set(UVE_BASIS_ENCODER_AVAILABLE OFF)
endif()

unset(_uve_basis_common_definitions)
unset(_uve_basis_root)
