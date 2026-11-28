// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_content_catalogue.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace UVE::Editor {
namespace {

using Kind = Scene::Objects::SceneObjectKind;
using Object = ContentCatalogueObject;
using Action = ContentCatalogueAction;

template <std::size_t N>
using Tree = std::array<Object, N>;

constexpr Tree<1> kOne(Kind kind) { return {Object{kind, -1, {}}}; }

// Every Entity item is ONE object. A new entity opens in the Entity Editor with nothing inside it
// that the author did not put there: a Character that arrives with a Mesh, or a Trigger that
// arrives with a Shape, is a child the author has to look at, decide about, and delete before the
// body is theirs. What an entity carries is the entity's own decision, made where it is edited -
// the same reason a Character3D is no longer born with a collider.
constexpr Tree<1> kCharacter = kOne(Kind::Character3D);
constexpr Tree<1> kPlayer = kOne(Kind::Player3D);
constexpr Tree<1> kProp = kOne(Kind::Static3D);
constexpr Tree<1> kPhysicsProp = kOne(Kind::Rigid3D);
constexpr Tree<1> kTrigger = kOne(Kind::Area3D);
constexpr Tree<1> kFollowCamera = kOne(Kind::SpringArm3D);

constexpr Tree<1> kEmpty = kOne(Kind::Object3D);
constexpr Tree<1> kViewport3D = kOne(Kind::Viewport3D);
constexpr Tree<1> kSpawner = kOne(Kind::SpawnPoint3D);
constexpr Tree<1> kBox = kOne(Kind::BoxMesh3D);
constexpr Tree<1> kSphere = kOne(Kind::SphereMesh3D);
constexpr Tree<1> kPlane = kOne(Kind::PlaneMesh3D);
constexpr Tree<1> kMesh = kOne(Kind::MeshInstance3D);
constexpr Tree<1> kLight = kOne(Kind::Light3D);
constexpr Tree<1> kEnvironment = kOne(Kind::WorldEnvironment3D);
constexpr Tree<1> kProbe = kOne(Kind::ReflectionProbe3D);
constexpr Tree<1> kFog = kOne(Kind::FogVolume3D);
constexpr Tree<1> kDecal = kOne(Kind::Decal3D);
constexpr Tree<1> kCamera = kOne(Kind::Camera3D);
constexpr Tree<1> kStaticBody = kOne(Kind::Static3D);
constexpr Tree<1> kRigid3D = kOne(Kind::Rigid3D);
constexpr Tree<1> kCollider = kOne(Kind::Collider3D);
constexpr Tree<1> kArea = kOne(Kind::Area3D);
constexpr Tree<1> kRayCast = kOne(Kind::RayCast3D);
constexpr Tree<1> kAnimationSequencer = kOne(Kind::AnimationSequencer);
constexpr Tree<1> kAnimationGraph = kOne(Kind::AnimationGraph);
constexpr Tree<1> kAudio = kOne(Kind::AudioSource3D);
constexpr Tree<1> kParticles = kOne(Kind::ParticleEmitter3D);
constexpr Tree<1> kCanvas = kOne(Kind::Canvas);
constexpr Tree<1> kLevelStreamer = kOne(Kind::LevelStreamer3D);
constexpr Tree<1> kWorldPartition = kOne(Kind::WorldPartition3D);
constexpr Tree<1> kNavigation = kOne(Kind::NavMeshVolume3D);
constexpr Tree<1> kOccluder = kOne(Kind::Occluder3D);

constexpr std::array<std::string_view, 9> kGroups{"CREATE BASIC ASSET", "Entity", "Shapes", "Lighting", "Camera",
                                                  "Physics", "Animation", "Audio, VFX & UI", "World"};

// The entity classes, in the order the Entity Class window lists them: the two things a player is made of
// first, then what a level is built from, then the two that mark a place or an area. Each is a
// catalogue item, so the window reads its name and icon from the menu rather than restating them.
constexpr std::array<ContentCatalogueEntityClass, 6> kEntityClasses{{
    {"player", "The body the game gives its input to.",
     "A Player3D is the object play begins from: input drives it, and the view the player sees looks "
     "out from whatever you attach to it. Add the mesh it wears, the spring arm that holds the "
     "camera, and the skeleton its animation bends."},
    {"character", "A body the world can push and animate.",
     "A Character3D walks under its own controller and carries a skeleton for animation. Start here "
     "for anything that moves on its own - hitboxes, IK and clips all need a body to hang from."},
    {"prop", "Something solid that never moves.",
     "A Static3D is the level's furniture: walls, crates, railings. It blocks what runs into it and "
     "never gives way. Add the mesh it shows and the collider it blocks with."},
    {"physics-prop", "Something solid the world can throw.",
     "A Rigid3D falls, rolls and takes a shove. Gravity and forces move it, its collider decides "
     "what it hits. Good for debris, doors and anything a character can push."},
    {"trigger", "A volume that reports what passes through it.",
     "An Area3D notices bodies entering and leaving without ever blocking them: doors, pickups, "
     "scripted moments. Give it the shape it watches."},
    {"spawner", "The place a player appears.",
     "A SpawnPoint3D is a mark, not a model: the game reads it when play starts, or when a body has "
     "to come back. Its transform is the whole object, so there is nothing to add to it."},
}};

constexpr std::array<ContentCatalogueItem, 40> kItems{{
    {"folder", "Folder", "CREATE BASIC ASSET", "A new folder here in Content", Action::Folder, {}, "Folder"},
    {"viewport_3d", "Viewport 3D", "CREATE BASIC ASSET",
     "A new 3D level: a Viewport ready for objects, empty until you add one", Action::SceneAsset, kViewport3D, "Viewport3D"},
    {"material", "Material", "CREATE BASIC ASSET", "A new standard PBR material asset", Action::MaterialAsset, {}, "M_Standard"},
    {"shader_script", "New Shader", "CREATE BASIC ASSET", "A new custom shader code asset", Action::ShaderAsset, {}, "NewShader"},
    {"empty", "Empty Entity 3D", "CREATE BASIC ASSET", "A bare Object3D to build your own tree on", Action::EntityAsset, kEmpty, "Empty"},

    {"character", "Character 3D", "Entity",
     "A playable body: Character3D, empty until you add what it wears - a mesh, an animation, a "
     "collider",
     Action::EntityAsset,
     kCharacter, "Character"},
    {"player", "Player 3D", "Entity",
     "The possessed body: a Player3D to build the view and the body on", Action::EntityAsset, kPlayer, "Player"},
    {"prop", "Static Prop 3D", "Entity", "Something solid to place: a Static3D, empty until you add its shape",
     Action::EntityAsset, kProp, "Prop"},
    {"physics-prop", "Physics Prop 3D", "Entity",
     "A prop that falls and gets pushed: a Rigid3D, empty until you add its shape", Action::EntityAsset,
     kPhysicsProp, "Physics Prop"},
    {"trigger", "Trigger Area 3D", "Entity", "An Area3D that reports what enters and leaves it - add the shape it watches",
     Action::EntityAsset, kTrigger, "Trigger"},
    {"spawner", "Spawn Point 3D", "Entity", "A SpawnPoint3D: where a player appears when the game starts",
     Action::EntityAsset, kSpawner, "Spawner"},

    {"box", "Box Mesh 3D", "Shapes", "A box mesh with its own collider", Action::EntityAsset, kBox, "Box"},
    {"sphere", "Sphere Mesh 3D", "Shapes", "A sphere mesh with its own collider", Action::EntityAsset, kSphere, "Sphere"},
    {"plane", "Plane Mesh 3D", "Shapes", "A flat plane mesh - a floor to start from", Action::EntityAsset, kPlane, "Plane"},
    {"mesh", "Mesh Instance 3D", "Shapes", "A MeshInstance3D: pick any imported model as its source", Action::EntityAsset, kMesh, "Mesh"},

    {"sun", "Directional Light 3D", "Lighting", "A directional Light3D that lights the whole level", Action::EntityAsset, kLight, "Sun"},
    {"light", "Light 3D", "Lighting", "A Light3D: directional, point or spot", Action::EntityAsset, kLight, "Light"},
    {"environment", "World Environment 3D", "Lighting", "Sky, ambient light, fog and tone mapping for the level",
     Action::EntityAsset, kEnvironment, "Environment"},
    {"reflection-probe", "Reflection Probe 3D", "Lighting", "Captures reflections for the area around it",
     Action::EntityAsset, kProbe, "Reflection Probe"},
    {"fog", "Fog Volume 3D", "Lighting", "Local fog inside a box or sphere", Action::EntityAsset, kFog, "Fog"},
    {"decal", "Decal 3D", "Lighting", "Projects a texture onto the surfaces it touches", Action::EntityAsset, kDecal, "Decal"},

    {"camera", "Camera 3D", "Camera", "A Camera3D", Action::EntityAsset, kCamera, "Camera"},
    {"follow-camera", "Spring Arm 3D", "Camera", "A SpringArm3D for a third-person view - add the camera it carries",
     Action::EntityAsset, kFollowCamera, "Follow Camera"},

    {"static-body", "Static 3D", "Physics", "A Static3D: collides but never moves", Action::EntityAsset,
     kStaticBody, "Static 3D"},
    {"rigid-body", "Rigid 3D", "Physics", "A Rigid3D: moved by gravity and forces", Action::EntityAsset,
     kRigid3D, "Rigid 3D"},
    {"collider", "Collider 3D", "Physics", "A Collider3D shape for the body it is placed under", Action::EntityAsset,
     kCollider, "Collider"},
    {"area", "Area 3D", "Physics", "An Area3D: detects overlaps without blocking", Action::EntityAsset, kArea, "Area"},
    {"raycast", "RayCast 3D", "Physics", "A RayCast3D: reports the first thing along a line", Action::EntityAsset,
     kRayCast, "RayCast"},

    {"animation-player", "Timeline", "Animation", "Plays animation clips on its parent", Action::EntityAsset,
     kAnimationSequencer, "AnimationSequencer"},
    {"animation-tree", "Animation Graph", "Animation", "Blends clips with a graph and a state machine",
     Action::EntityAsset, kAnimationGraph, "AnimationGraph"},
    {"animation-library", "Animation Library", "Animation", "A named collection of animation clips that sequencers load from",
     Action::LibraryAsset, {}, "Animation Library"},
    {"animation-montage", "Motion Montage", "Animation",
     "An attack, a reload, an emote: clips in sections with notifies, played from a character's script",
     Action::MontageAsset, {}, "MM_Attack"},
    {"montage-library", "Motion Montage Library", "Animation", "A named collection of motion montages that MontageSequencers link to",
     Action::MontageLibraryAsset, {}, "Motion Montage Library"},

    {"audio", "Audio Source 3D", "Audio, VFX & UI", "An AudioSource3D that plays a clip in 3D", Action::EntityAsset,
     kAudio, "AudioSource"},
    {"particles", "Particle Emitter 3D", "Audio, VFX & UI", "A ParticleEmitter3D", Action::EntityAsset, kParticles, "Particles"},
    {"canvas", "UI Canvas", "Audio, VFX & UI", "A Canvas to hold text, images and buttons", Action::EntityAsset,
     kCanvas, "Canvas"},

    {"level-streamer", "Level Streamer 3D", "World", "Loads and unloads a scene as the player gets near",
     Action::EntityAsset, kLevelStreamer, "LevelStreamer"},
    {"world-partition", "World Partition 3D", "World", "Splits a large world into cells that stream in",
     Action::EntityAsset, kWorldPartition, "WorldPartition"},
    {"navigation", "Nav Mesh Volume 3D", "World", "The walkable area agents find paths on", Action::EntityAsset,
     kNavigation, "Navigation"},
    {"occluder", "Occluder 3D", "World", "Hides what is behind it from rendering", Action::EntityAsset, kOccluder, "Occluder"},
}};

[[nodiscard]] char Lower(const char character) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

[[nodiscard]] bool Contains(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) {
        return true;
    }
    for (std::size_t start = 0U; start + needle.size() <= haystack.size(); ++start) {
        std::size_t matched = 0U;
        while (matched < needle.size() && Lower(haystack[start + matched]) == Lower(needle[matched])) {
            ++matched;
        }
        if (matched == needle.size()) {
            return true;
        }
    }
    return false;
}

} // namespace

std::span<const ContentCatalogueItem> GetContentCatalogueItems() noexcept {
    return kItems;
}

std::span<const std::string_view> GetContentCatalogueGroups() noexcept {
    return kGroups;
}

std::span<const ContentCatalogueEntityClass> GetContentCatalogueEntityClasses() noexcept {
    return kEntityClasses;
}

bool IsContentCatalogueEntityClass(const std::string_view itemId) noexcept {
    return std::ranges::any_of(kEntityClasses, [itemId](const ContentCatalogueEntityClass& entry) {
        return entry.itemId == itemId;
    });
}

const ContentCatalogueItem* FindContentCatalogueItem(const std::string_view id) noexcept {
    for (const ContentCatalogueItem& item : kItems) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

bool DoesContentCatalogueItemMatch(const ContentCatalogueItem& item, std::string_view query) noexcept {
    while (!query.empty()) {
        const std::size_t space = query.find(' ');
        const std::string_view word = query.substr(0U, space);
        if (!word.empty() && !Contains(item.label, word) && !Contains(item.group, word) &&
            !Contains(item.tooltip, word)) {
            return false;
        }
        if (space == std::string_view::npos) {
            break;
        }
        query.remove_prefix(space + 1U);
    }
    return true;
}

int RankContentCatalogueItem(const ContentCatalogueItem& item, const std::string_view query) noexcept {
    if (!DoesContentCatalogueItemMatch(item, query)) {
        return 0;
    }
    const std::size_t start = query.find_first_not_of(' ');
    if (start == std::string_view::npos) {
        return 1;
    }
    const std::string_view word = query.substr(start, query.find(' ', start) - start);
    if (word.size() <= item.label.size() && Contains(item.label.substr(0U, word.size()), word)) {
        return 3;
    }
    return Contains(item.label, word) ? 2 : 1;
}

Scene::Objects::SceneObjectKind GetContentCatalogueIconKind(const ContentCatalogueItem& item) noexcept {
    if (item.action == ContentCatalogueAction::Folder) {
        return Kind::Folder;
    }
    if (item.action == ContentCatalogueAction::MontageAsset || item.action == ContentCatalogueAction::LibraryAsset ||
        item.action == ContentCatalogueAction::MontageLibraryAsset) {
        return Kind::AnimationSequencer;
    }
    if (item.objects.empty()) {
        return Kind::Folder;
    }
    return item.objects.front().kind;
}

} // namespace UVE::Editor
