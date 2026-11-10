// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_content_catalogue.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace UVE::Editor {
namespace {

using Kind = Scene::Objects::SceneObjectKindUVE;
using Object = ContentCatalogueObjectUVE;
using Action = ContentCatalogueActionUVE;

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
constexpr Tree<1> kViewport2D = kOne(Kind::Viewport3D);
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

// The Montage's bodies, in the order the window lists them: the two things a player is made of
// first, then what a level is built from, then the two that mark a place or an area. Each is a
// catalogue item, so the window reads its name and icon from the menu rather than restating them.
constexpr std::array<ContentCatalogueMontageEntryUVE, 6> kMontageEntries{{
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

constexpr std::array<ContentCatalogueItemUVE, 41> kItems{{
    {"folder", "Folder", "CREATE BASIC ASSET", "A new folder here in Content", Action::Folder, {}, Scene::ViewportDimensionUVE::ThreeD, "Folder"},
    {"basic_script", "New Script", "CREATE BASIC ASSET", "A new UVScript file (.uvs) ready to edit", Action::ScriptAsset, {}, Scene::ViewportDimensionUVE::ThreeD, "Script"},
    {"viewport_3d", "Viewport 3D", "CREATE BASIC ASSET",
     "A new 3D level: a Viewport ready for objects, empty until you add one", Action::SceneAsset, kViewport3D, Scene::ViewportDimensionUVE::ThreeD, "Viewport3D"},
    {"viewport_2d", "Viewport 2D", "CREATE BASIC ASSET",
     "A new 2D scene: a Viewport authored flat on the XY plane, saved as 2D in the file",
     Action::SceneAsset, kViewport2D, Scene::ViewportDimensionUVE::TwoD, "Viewport2D"},
    {"material", "Material", "CREATE BASIC ASSET", "A new standard PBR material asset", Action::MaterialAsset, {}, Scene::ViewportDimensionUVE::ThreeD, "M_Standard"},
    {"shader_script", "New Shader", "CREATE BASIC ASSET", "A new custom shader code asset", Action::ShaderAsset, {}, Scene::ViewportDimensionUVE::ThreeD, "NewShader"},
    {"empty", "Empty Entity 3D", "CREATE BASIC ASSET", "A bare Object3D to build your own tree on", Action::EntityAsset, kEmpty, Scene::ViewportDimensionUVE::ThreeD, "Empty"},

    // First in the group: the Montage is the way in, and the six items under it are the shortcut
    // for when the author already knows which body they want.
    {"body-montage", "3D Montage", "Entity",
     "Pick the body an entity is built on - Player, Character, Prop, Physics Prop, Trigger or Spawner - "
     "and open it to build",
     Action::Montage, {}},
    {"character", "Character 3D", "Entity",
     "A playable body: Character3D, empty until you add what it wears - a mesh, an animation, a "
     "collider",
     Action::EntityAsset,
     kCharacter, Scene::ViewportDimensionUVE::ThreeD, "Character"},
    {"player", "Player 3D", "Entity",
     "The possessed body: a Player3D to build the view and the body on", Action::EntityAsset, kPlayer, Scene::ViewportDimensionUVE::ThreeD, "Player"},
    {"prop", "Static Prop 3D", "Entity", "Something solid to place: a Static3D, empty until you add its shape",
     Action::EntityAsset, kProp, Scene::ViewportDimensionUVE::ThreeD, "Prop"},
    {"physics-prop", "Physics Prop 3D", "Entity",
     "A prop that falls and gets pushed: a Rigid3D, empty until you add its shape", Action::EntityAsset,
     kPhysicsProp, Scene::ViewportDimensionUVE::ThreeD, "Physics Prop"},
    {"trigger", "Trigger Area 3D", "Entity", "An Area3D that reports what enters and leaves it - add the shape it watches",
     Action::EntityAsset, kTrigger, Scene::ViewportDimensionUVE::ThreeD, "Trigger"},
    {"spawner", "Spawn Point 3D", "Entity", "A SpawnPoint3D: where a player appears when the game starts",
     Action::EntityAsset, kSpawner, Scene::ViewportDimensionUVE::ThreeD, "Spawner"},

    {"box", "Box Mesh 3D", "Shapes", "A box mesh with its own collider", Action::EntityAsset, kBox, Scene::ViewportDimensionUVE::ThreeD, "Box"},
    {"sphere", "Sphere Mesh 3D", "Shapes", "A sphere mesh with its own collider", Action::EntityAsset, kSphere, Scene::ViewportDimensionUVE::ThreeD, "Sphere"},
    {"plane", "Plane Mesh 3D", "Shapes", "A flat plane mesh - a floor to start from", Action::EntityAsset, kPlane, Scene::ViewportDimensionUVE::ThreeD, "Plane"},
    {"mesh", "Mesh Instance 3D", "Shapes", "A MeshInstance3D: pick any imported model as its source", Action::EntityAsset, kMesh, Scene::ViewportDimensionUVE::ThreeD, "Mesh"},

    {"sun", "Directional Light 3D", "Lighting", "A directional Light3D that lights the whole level", Action::EntityAsset, kLight, Scene::ViewportDimensionUVE::ThreeD, "Sun"},
    {"light", "Light 3D", "Lighting", "A Light3D: directional, point or spot", Action::EntityAsset, kLight, Scene::ViewportDimensionUVE::ThreeD, "Light"},
    {"environment", "World Environment 3D", "Lighting", "Sky, ambient light, fog and tone mapping for the level",
     Action::EntityAsset, kEnvironment, Scene::ViewportDimensionUVE::ThreeD, "Environment"},
    {"reflection-probe", "Reflection Probe 3D", "Lighting", "Captures reflections for the area around it",
     Action::EntityAsset, kProbe, Scene::ViewportDimensionUVE::ThreeD, "Reflection Probe"},
    {"fog", "Fog Volume 3D", "Lighting", "Local fog inside a box or sphere", Action::EntityAsset, kFog, Scene::ViewportDimensionUVE::ThreeD, "Fog"},
    {"decal", "Decal 3D", "Lighting", "Projects a texture onto the surfaces it touches", Action::EntityAsset, kDecal, Scene::ViewportDimensionUVE::ThreeD, "Decal"},

    {"camera", "Camera 3D", "Camera", "A Camera3D", Action::EntityAsset, kCamera, Scene::ViewportDimensionUVE::ThreeD, "Camera"},
    {"follow-camera", "Spring Arm 3D", "Camera", "A SpringArm3D for a third-person view - add the camera it carries",
     Action::EntityAsset, kFollowCamera, Scene::ViewportDimensionUVE::ThreeD, "Follow Camera"},

    {"static-body", "Static 3D", "Physics", "A Static3D: collides but never moves", Action::EntityAsset,
     kStaticBody, Scene::ViewportDimensionUVE::ThreeD, "Static 3D"},
    {"rigid-body", "Rigid 3D", "Physics", "A Rigid3D: moved by gravity and forces", Action::EntityAsset,
     kRigid3D, Scene::ViewportDimensionUVE::ThreeD, "Rigid 3D"},
    {"collider", "Collider 3D", "Physics", "A Collider3D shape for the body it is placed under", Action::EntityAsset,
     kCollider, Scene::ViewportDimensionUVE::ThreeD, "Collider"},
    {"area", "Area 3D", "Physics", "An Area3D: detects overlaps without blocking", Action::EntityAsset, kArea, Scene::ViewportDimensionUVE::ThreeD, "Area"},
    {"raycast", "RayCast 3D", "Physics", "A RayCast3D: reports the first thing along a line", Action::EntityAsset,
     kRayCast, Scene::ViewportDimensionUVE::ThreeD, "RayCast"},

    {"animation-player", "Animation Sequencer", "Animation", "Plays animation clips on its parent", Action::EntityAsset,
     kAnimationSequencer, Scene::ViewportDimensionUVE::ThreeD, "AnimationSequencer"},
    {"animation-tree", "Animation Graph", "Animation", "Blends clips with a graph and a state machine",
     Action::EntityAsset, kAnimationGraph, Scene::ViewportDimensionUVE::ThreeD, "AnimationGraph"},
    {"animation-library", "Animation Library", "Animation", "A named collection of animation clips that sequencers load from",
     Action::LibraryAsset, {}, Scene::ViewportDimensionUVE::ThreeD, "Animation Library"},

    {"audio", "Audio Source 3D", "Audio, VFX & UI", "An AudioSource3D that plays a clip in 3D", Action::EntityAsset,
     kAudio, Scene::ViewportDimensionUVE::ThreeD, "AudioSource"},
    {"particles", "Particle Emitter 3D", "Audio, VFX & UI", "A ParticleEmitter3D", Action::EntityAsset, kParticles, Scene::ViewportDimensionUVE::ThreeD, "Particles"},
    {"canvas", "UI Canvas", "Audio, VFX & UI", "A Canvas to hold text, images and buttons", Action::EntityAsset,
     kCanvas, Scene::ViewportDimensionUVE::ThreeD, "Canvas"},

    {"level-streamer", "Level Streamer 3D", "World", "Loads and unloads a scene as the player gets near",
     Action::EntityAsset, kLevelStreamer, Scene::ViewportDimensionUVE::ThreeD, "LevelStreamer"},
    {"world-partition", "World Partition 3D", "World", "Splits a large world into cells that stream in",
     Action::EntityAsset, kWorldPartition, Scene::ViewportDimensionUVE::ThreeD, "WorldPartition"},
    {"navigation", "Nav Mesh Volume 3D", "World", "The walkable area agents find paths on", Action::EntityAsset,
     kNavigation, Scene::ViewportDimensionUVE::ThreeD, "Navigation"},
    {"occluder", "Occluder 3D", "World", "Hides what is behind it from rendering", Action::EntityAsset, kOccluder, Scene::ViewportDimensionUVE::ThreeD, "Occluder"},
}};

[[nodiscard]] char LowerUVE(const char character) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

[[nodiscard]] bool ContainsUVE(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) {
        return true;
    }
    for (std::size_t start = 0U; start + needle.size() <= haystack.size(); ++start) {
        std::size_t matched = 0U;
        while (matched < needle.size() && LowerUVE(haystack[start + matched]) == LowerUVE(needle[matched])) {
            ++matched;
        }
        if (matched == needle.size()) {
            return true;
        }
    }
    return false;
}

} // namespace

std::span<const ContentCatalogueItemUVE> GetContentCatalogueItemsUVE() noexcept {
    return kItems;
}

std::span<const std::string_view> GetContentCatalogueGroupsUVE() noexcept {
    return kGroups;
}

std::span<const ContentCatalogueMontageEntryUVE> GetContentCatalogueMontageEntriesUVE() noexcept {
    return kMontageEntries;
}

bool IsContentCatalogueMontageBodyUVE(const std::string_view itemId) noexcept {
    return std::ranges::any_of(kMontageEntries, [itemId](const ContentCatalogueMontageEntryUVE& entry) {
        return entry.itemId == itemId;
    });
}

const ContentCatalogueItemUVE* FindContentCatalogueItemUVE(const std::string_view id) noexcept {
    for (const ContentCatalogueItemUVE& item : kItems) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

bool DoesContentCatalogueItemMatchUVE(const ContentCatalogueItemUVE& item, std::string_view query) noexcept {
    while (!query.empty()) {
        const std::size_t space = query.find(' ');
        const std::string_view word = query.substr(0U, space);
        if (!word.empty() && !ContainsUVE(item.label, word) && !ContainsUVE(item.group, word) &&
            !ContainsUVE(item.tooltip, word)) {
            return false;
        }
        if (space == std::string_view::npos) {
            break;
        }
        query.remove_prefix(space + 1U);
    }
    return true;
}

int RankContentCatalogueItemUVE(const ContentCatalogueItemUVE& item, const std::string_view query) noexcept {
    if (!DoesContentCatalogueItemMatchUVE(item, query)) {
        return 0;
    }
    const std::size_t start = query.find_first_not_of(' ');
    if (start == std::string_view::npos) {
        return 1;
    }
    const std::string_view word = query.substr(start, query.find(' ', start) - start);
    if (word.size() <= item.label.size() && ContainsUVE(item.label.substr(0U, word.size()), word)) {
        return 3;
    }
    return ContainsUVE(item.label, word) ? 2 : 1;
}

Scene::Objects::SceneObjectKindUVE GetContentCatalogueIconKindUVE(const ContentCatalogueItemUVE& item) noexcept {
    if (item.action == ContentCatalogueActionUVE::Montage) {
        // A skeleton: every body the window offers is either already wearing bones or is about to.
        return Kind::Skeleton3D;
    }
    if (item.action == ContentCatalogueActionUVE::Folder) {
        return Kind::Folder;
    }
    if (item.action == ContentCatalogueActionUVE::ScriptAsset) {
        return Kind::Script;
    }
    if (item.objects.empty()) {
        return Kind::Folder;
    }
    return item.objects.front().kind;
}

} // namespace UVE::Editor
