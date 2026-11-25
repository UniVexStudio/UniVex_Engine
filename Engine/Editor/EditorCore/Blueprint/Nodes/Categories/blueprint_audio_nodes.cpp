// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_audio_nodes.h"

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterAudioBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Pin = BlueprintNodeBuilder::Pin;
    BlueprintNodeBuilder b{out};

    // AudioSource component actions/queries
    b.Action("audio_play", "Play Sound", "Audio", {}, "audio.play()", "sound sfx", "Its AudioSource");
    b.Action("audio_stop", "Stop Sound", "Audio", {}, "audio.stop()", "sound sfx", "Its AudioSource");
    b.Pure("audio_is_playing", "Is Sound Playing", "Audio", {}, {Pin{"Playing", T::Bool, ""}}, {"audio.is_playing()"});
    b.Action("set_volume", "Set Volume", "Audio", {Pin{"Volume", T::Float, "1.0"}}, "volume = {1}");
    b.Action("set_pitch", "Set Pitch", "Audio", {Pin{"Pitch", T::Float, "1.0"}}, "pitch = {1}");

    // One-shot spatial & 2D sounds
    b.Action("play_sound_at_location", "Play Sound At Location", "Audio|One Shot",
             {Pin{"Sound", T::String, "Sounds/Hit.wav"}, Pin{"Location", T::Vector, "0,0,0"}, Pin{"Volume", T::Float, "1.0"}},
             "audio.play_at({1}, {2}, {3})", "sfx one shot spawn sound 3d", "A sound file heard from a place, once");
    b.Action("play_sound_2d", "Play Sound 2D", "Audio|One Shot", {Pin{"Sound", T::String, "Sounds/Click.wav"}, Pin{"Volume", T::Float, "1.0"}},
             "audio.play_2d({1}, {2})", "sfx one shot ui music", "A sound file heard everywhere, once (menus, music stings)");
}

} // namespace UVE::Blueprint::Nodes
