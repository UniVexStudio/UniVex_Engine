// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_game_nodes.h"

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterGameBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Pin = BlueprintNodeBuilder::Pin;
    BlueprintNodeBuilder b{out};

    // Time, pause, levels
    b.Pure("get_game_time", "Get Game Time In Seconds", "Game|Time", {}, {Pin{"Seconds", T::Float, ""}}, {"game.time()"}, "time seconds elapsed clock");
    b.Pure("get_delta_seconds", "Get World Delta Seconds", "Game|Time", {}, {Pin{"Seconds", T::Float, ""}}, {"game.delta()"}, "frame time dt");
    b.Pure("get_time_scale", "Get Global Time Dilation", "Game|Time", {}, {Pin{"Scale", T::Float, ""}}, {"game.time_scale()"}, "slow motion speed");
    b.Action("set_time_scale", "Set Global Time Dilation", "Game|Time", {Pin{"Scale", T::Float, "1.0"}}, "game.set_time_scale({1})",
             "slow motion bullet time speed", "Slower (0.1) or faster (up to 32) game time");
    b.Action("pause_game", "Pause Game", "Game", {}, "game.pause()", "pause stop freeze", "Stops the world; a widget or object set to run while paused goes on");
    b.Action("resume_game", "Resume Game", "Game", {}, "game.resume()", "unpause continue");
    b.Pure("is_game_paused", "Is Game Paused", "Game", {}, {Pin{"Paused", T::Bool, ""}}, {"game.is_paused()"}, "paused");
    b.Action("quit_game", "Quit Game", "Game", {}, "game.quit()", "exit close end", "Ends the game (stops Play in the editor)");
    b.Action("open_level", "Open Level", "Game", {Pin{"Level", T::String, "Levels/Main.uvscene"}}, "game.open_level({1})",
             "load map scene travel", "Loads another level once this frame is done");
    b.Pure("get_level_name", "Get Current Level Name", "Game", {}, {Pin{"Level", T::String, ""}}, {"game.level_name()"}, "map scene");

    // Saved game data
    b.Action("save_set_float", "Set Game Data (Float)", "Game|Save", {Pin{"Key", T::String, "Score"}, Pin{"Value", T::Float, "0.0"}},
             "save.set_float({1}, {2})", "save game variable store", "A value kept for the whole game, written by Save Game");
    b.Action("save_set_int", "Set Game Data (Int)", "Game|Save", {Pin{"Key", T::String, "Coins"}, Pin{"Value", T::Int, "0"}}, "save.set_int({1}, {2})",
             "save game variable store");
    b.Action("save_set_bool", "Set Game Data (Bool)", "Game|Save", {Pin{"Key", T::String, "DoorOpen"}, Pin{"Value", T::Bool, "true"}},
             "save.set_bool({1}, {2})", "save game variable store");
    b.Action("save_set_string", "Set Game Data (String)", "Game|Save", {Pin{"Key", T::String, "PlayerName"}, Pin{"Value", T::String, ""}},
             "save.set_str({1}, {2})", "save game variable store");
    b.Pure("save_get_float", "Get Game Data (Float)", "Game|Save", {Pin{"Key", T::String, "Score"}, Pin{"Default", T::Float, "0.0"}},
           {Pin{"Value", T::Float, ""}}, {"save.get_float({0}, {1})"}, "load game variable");
    b.Pure("save_get_int", "Get Game Data (Int)", "Game|Save", {Pin{"Key", T::String, "Coins"}, Pin{"Default", T::Int, "0"}}, {Pin{"Value", T::Int, ""}},
           {"save.get_int({0}, {1})"}, "load game variable");
    b.Pure("save_get_bool", "Get Game Data (Bool)", "Game|Save", {Pin{"Key", T::String, "DoorOpen"}, Pin{"Default", T::Bool, "false"}},
           {Pin{"Value", T::Bool, ""}}, {"save.get_bool({0}, {1})"}, "load game variable");
    b.Pure("save_get_string", "Get Game Data (String)", "Game|Save", {Pin{"Key", T::String, "PlayerName"}, Pin{"Default", T::String, ""}},
           {Pin{"Value", T::String, ""}}, {"save.get_str({0}, {1})"}, "load game variable");
    b.Pure("save_has", "Has Game Data", "Game|Save", {Pin{"Key", T::String, "Score"}}, {Pin{"Has", T::Bool, ""}}, {"save.has({0})"}, "exists key");
    b.Action("save_game", "Save Game To Slot", "Game|Save", {Pin{"Slot", T::String, "Slot1"}}, "save.write({1})", "save game slot write disk",
             "Writes all the game data to a slot");
    b.Action("load_game", "Load Game From Slot", "Game|Save", {Pin{"Slot", T::String, "Slot1"}}, "save.read({1})", "load game slot read disk",
             "Reads the slot's game data back (Get Game Data reads it)");
    b.Pure("does_save_exist", "Does Save Game Exist", "Game|Save", {Pin{"Slot", T::String, "Slot1"}}, {Pin{"Exists", T::Bool, ""}},
           {"save.exists({0})"}, "slot");
    b.Action("delete_save", "Delete Save Game In Slot", "Game|Save", {Pin{"Slot", T::String, "Slot1"}}, "save.delete({1})", "remove slot");
}

} // namespace UVE::Blueprint::Nodes
