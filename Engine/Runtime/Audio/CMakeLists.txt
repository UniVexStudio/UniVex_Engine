# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Backend-agnostic audio device/source system (AudioSystemUVE, AudioSourceSystemUVE,
# NullAudioDeviceUVE, WAV/PCM16 import) ported from the reference engine's Audio module.
#
# WavImporterUVE::RegisterWavImporterUVE() only needs the small IAssetImporterUVE interface
# (117 lines) and the AudioAssetUVE value type (31 lines) - both ported here at
# include/uve/asset/, their original reference include path, since neither has any dependency
# beyond the already-ported AssetGuidUVE/IAssetDatabaseUVE. The concrete AssetImporterUVE that
# registers WAV alongside every other format (bmp/gltf/jpeg/mtl/obj/png/tga/shader-source, ~413
# lines) is NOT ported - it needs the full reference Asset pipeline (~12,000 lines), well beyond
# this slice's scope. Its own dedicated test (wav_importer_uve_tests.cpp) is excluded for the
# same reason - see Test/Audio's own note.
#
# uve_scene: AudioSourceSystemUVE walks WorldTransformComponentUVE + AudioSourceComponentUVE via
# Scene::IEntityManagerUVE (mirrors uve_render's own uve_scene dependency for MeshRendererUVE).

add_library(uve_audio STATIC
    src/wav_pcm16_decoder_uve.cpp
    src/pcm_gain_effect_uve.cpp
    src/audio_mixer_group_uve.cpp
    src/null_audio_device_uve.cpp
    src/audio_system_uve.cpp
    src/audio_listener_orientation_validation_uve.cpp
    src/audio_source_system_uve.cpp
    src/wav_metadata_uve.cpp
    src/wav_importer_uve.cpp
)
target_include_directories(uve_audio PUBLIC include)
target_link_libraries(uve_audio PUBLIC uve_platform uve_debug uve_math uve_scene)
target_compile_features(uve_audio PUBLIC cxx_std_20)
uve_set_warnings(uve_audio)
