# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Backend-agnostic audio device/source system (AudioSystemUVE, AudioSourceSystemUVE,
# NullAudioDeviceUVE, WAV/PCM16 import) ported from the reference engine's Audio module.
#
# WavImporterUVE::RegisterWavImporterUVE() uses the AudioAssetUVE value type and its
# LoadAudioAssetUVE/SaveAudioAssetUVE free functions, both now provided by uve_asset (moved there
# from this module's own include/uve/asset/ tree while porting EngineCoreUVE - the concrete
# AssetImporterUVE and the full Asset pipeline are fully ported there now).
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
target_link_libraries(uve_audio PUBLIC uve_platform uve_debug uve_math uve_scene uve_asset)
target_compile_features(uve_audio PUBLIC cxx_std_20)
uve_set_warnings(uve_audio)
