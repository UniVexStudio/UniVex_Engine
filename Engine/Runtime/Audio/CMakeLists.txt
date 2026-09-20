# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Backend-agnostic audio device/source system (AudioSystemUVE, AudioSourceSystemUVE,
# NullAudioDeviceUVE, WAV/PCM16 import) ported from the reference engine's Audio module.
#
# WavImporterUVE::RegisterWavImporterUVE() uses the AudioAssetUVE value type and its
# LoadAudioAssetUVE/SaveAudioAssetUVE free functions, both now provided by uve_asset (moved there
# from this module's own Expose/uve/asset/ tree while porting EngineCoreUVE - the concrete
# AssetImporterUVE and the full Asset pipeline are fully ported there now).
#
# uve_scene: AudioSourceSystemUVE walks WorldTransformComponentUVE + AudioSourceComponentUVE via
# Scene::IEntityManagerUVE (mirrors uve_render's own uve_scene dependency for MeshRendererUVE).

# miniaudio (vendored via FetchContent, pinned tag): single-header audio backend used by
# MiniaudioAudioDeviceUVE - the engine's first real IAudioDeviceUVE implementation. Pulled in as
# source only (no library target of its own): one engine TU compiles it with
# MINIAUDIO_IMPLEMENTATION (see Internal/miniaudio_implementation_uve.cpp). The include dir is
# SYSTEM so the vendored header is exempt from this codebase's -Wall/-Wextra/-Wpedantic/-Werror
# policy - engine headers deliberately never see miniaudio symbols (confinement discipline).
include(FetchContent)
FetchContent_Declare(
    uve_miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.25
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(uve_miniaudio)
add_library(uve_miniaudio_interface INTERFACE)
target_include_directories(uve_miniaudio_interface SYSTEM INTERFACE "${uve_miniaudio_SOURCE_DIR}")
find_package(Threads REQUIRED)

add_library(uve_audio STATIC
    Internal/wav_pcm16_decoder_uve.cpp
    Internal/pcm_gain_effect_uve.cpp
    Internal/audio_mixer_group_uve.cpp
    Internal/null_audio_device_uve.cpp
    Internal/miniaudio_audio_device_uve.cpp
    Internal/miniaudio_implementation_uve.cpp
    Internal/audio_system_uve.cpp
    Internal/audio_listener_orientation_validation_uve.cpp
    Internal/audio_source_system_uve.cpp
    Internal/wav_metadata_uve.cpp
    Internal/wav_importer_uve.cpp
)
target_include_directories(uve_audio PUBLIC Expose)
target_link_libraries(uve_audio
    PUBLIC uve_platform uve_logging uve_math uve_scene uve_asset
    # miniaudio dlopen()s its platform backends on POSIX (dl), drives the stream on its own
    # thread (Threads) and uses libm for resampling helpers (m). Private: engine code never sees
    # the vendor header.
    PRIVATE uve_miniaudio_interface Threads::Threads ${CMAKE_DL_LIBS} m
)
target_compile_features(uve_audio PUBLIC cxx_std_20)
uve_set_warnings(uve_audio)
