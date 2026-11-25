// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_events_nodes.h"

#include "blueprint_node_builder.h"

namespace UVE::Blueprint::Nodes {

void RegisterEventsBlueprintNodes(std::vector<Asset::BlueprintNodeType>& out) {
    using T = BlueprintNodeBuilder::T;
    using Pin = BlueprintNodeBuilder::Pin;
    BlueprintNodeBuilder b{out};

    // Lifecycle, physics, animation, and montage events
    b.Event("event_ready", "Event Ready", "ready", {}, "beginplay begin play start", "Once, when the object starts");
    b.Event("event_tick", "Event Tick", "tick", {Pin{"Delta Seconds", T::Float, ""}}, "update frame", "Every frame");
    b.Event("event_collision_enter", "On Collision Enter", "collision_enter", {Pin{"Other", T::String, ""}}, "hit contact", "Started touching another body");
    b.Event("event_collision_exit", "On Collision Exit", "collision_exit", {Pin{"Other", T::String, ""}}, "hit contact", "Stopped touching another body");
    b.Event("event_overlap_enter", "On Overlap Enter", "overlap_enter", {Pin{"Other", T::String, ""}}, "trigger begin overlap", "Something entered");
    b.Event("event_overlap_exit", "On Overlap Exit", "overlap_exit", {Pin{"Other", T::String, ""}}, "trigger end overlap", "Something left");
    b.Event("event_animation", "On Animation Event", "animation_event", {Pin{"Event", T::String, ""}}, "notify footstep clip", "A clip passed one of its events");
    b.Event("event_montage_notify", "On Montage Notify", "montage_notify", {Pin{"Notify", T::String, ""}}, "anim notify", "A montage passed a notify");
    b.Event("event_montage_notify_begin", "On Montage Notify Begin", "montage_notify_begin", {Pin{"Notify", T::String, ""}}, "notify state", "A montage entered a notify state");
    b.Event("event_montage_notify_end", "On Montage Notify End", "montage_notify_end", {Pin{"Notify", T::String, ""}}, "notify state", "A montage left a notify state");
    b.Event("event_montage_ended", "On Montage Ended", "montage_ended", {Pin{"Montage", T::String, ""}}, "finished", "A montage ended");

    // Named events: the node names which one (empty: any)
    b.NamedEvent("event_custom", "Custom Event", "Events", "custom_event", "Event", "MyEvent",
                 {Pin{"Event", T::String, ""}, Pin{"Payload", T::String, ""}}, "custom signal message dispatcher broadcast",
                 "Runs when Call Custom Event names it (here, or from another object)");
    b.NamedEvent("event_timer", "On Timer", "Events", "timer", "Timer", "Spawn", {Pin{"Timer", T::String, ""}}, "timer elapsed interval repeat",
                 "Runs when the timer Set Timer started (by its name) runs out");
    b.Event("event_destroyed", "On Destroyed", "destroyed", {}, "end play removed killed", "Just before the object is removed");

    // Event emission & timer control actions/queries
    b.Action("call_custom_event", "Call Custom Event", "Utilities|Events", {Pin{"Event", T::String, "MyEvent"}, Pin{"Payload", T::String, ""}},
             "events.emit({1}, {2})", "send signal message fire broadcast", "Runs this object's Custom Event of that name, after the current one");
    b.Action("call_custom_event_on", "Call Custom Event On", "Utilities|Events",
             {Pin{"Target", T::Object, ""}, Pin{"Event", T::String, "MyEvent"}, Pin{"Payload", T::String, ""}}, "events.emit_to({1}, {2}, {3})",
             "send signal message other object", "Runs another object's Custom Event of that name");
    b.Action("set_timer", "Set Timer", "Utilities|Timers", {Pin{"Timer", T::String, "Spawn"}, Pin{"Seconds", T::Float, "1.0"}, Pin{"Looping", T::Bool, "false"}},
             "timers.set({1}, {2}, {3})", "timer by event interval repeat", "On Timer of that name runs when it runs out (again and again when looping)");
    b.Action("clear_timer", "Clear Timer", "Utilities|Timers", {Pin{"Timer", T::String, "Spawn"}}, "timers.clear({1})", "stop cancel timer");
    b.Pure("is_timer_active", "Is Timer Active", "Utilities|Timers", {Pin{"Timer", T::String, "Spawn"}}, {Pin{"Active", T::Bool, ""}},
           {"timers.active({0})"}, "running timer");
    b.Pure("get_timer_remaining", "Get Timer Remaining", "Utilities|Timers", {Pin{"Timer", T::String, "Spawn"}}, {Pin{"Seconds", T::Float, ""}},
           {"timers.remaining({0})"}, "time left timer");
}

} // namespace UVE::Blueprint::Nodes
