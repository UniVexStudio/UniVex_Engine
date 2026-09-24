// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_content_catalogue_uve.h"

#include <array>
#include <cctype>

namespace UVE::Editor {
namespace {

using Kind = Scene::Nodes::SceneNodeKindUVE;
using Node = ContentCatalogueNodeUVE;
using Action = ContentCatalogueActionUVE;

template <std::size_t N>
using Tree = std::array<Node, N>;

constexpr Tree<1> kOne(Kind kind) { return {Node{kind, -1, {}}}; }

// Templates with more than one node. Everything else is a single node of the kind it names.
constexpr Tree<4> kCharacter{{{Kind::CharacterBody3D, -1, {}},
                              {Kind::MeshInstance3D, 0, "Mesh"},
                              {Kind::AnimationPlayer, 0, "AnimationPlayer"},
                              {Kind::AnimationTree, 0, "AnimationTree"}}};
constexpr Tree<3> kProp{{{Kind::StaticBody3D, -1, {}}, {Kind::MeshInstance3D, 0, "Mesh"}, {Kind::Collider3D, 0, "Collider"}}};
constexpr Tree<3> kPhysicsProp{
    {{Kind::RigidBody3D, -1, {}}, {Kind::MeshInstance3D, 0, "Mesh"}, {Kind::Collider3D, 0, "Collider"}}};
constexpr Tree<2> kTrigger{{{Kind::Area3D, -1, {}}, {Kind::Collider3D, 0, "Shape"}}};
constexpr Tree<2> kFollowCamera{{{Kind::SpringArm3D, -1, {}}, {Kind::Camera3D, 0, "Camera"}}};

constexpr Tree<1> kEmpty = kOne(Kind::Node3D);
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
constexpr Tree<1> kStaticBody = kOne(Kind::StaticBody3D);
constexpr Tree<1> kRigidBody = kOne(Kind::RigidBody3D);
constexpr Tree<1> kCollider = kOne(Kind::Collider3D);
constexpr Tree<1> kArea = kOne(Kind::Area3D);
constexpr Tree<1> kRayCast = kOne(Kind::RayCast3D);
constexpr Tree<1> kAnimationPlayer = kOne(Kind::AnimationPlayer);
constexpr Tree<1> kAnimationTree = kOne(Kind::AnimationTree);
constexpr Tree<1> kAudio = kOne(Kind::AudioSource3D);
constexpr Tree<1> kParticles = kOne(Kind::ParticleEmitter3D);
constexpr Tree<1> kCanvas = kOne(Kind::Canvas);
constexpr Tree<1> kLevelStreamer = kOne(Kind::LevelStreamer3D);
constexpr Tree<1> kWorldPartition = kOne(Kind::WorldPartition3D);
constexpr Tree<1> kNavigation = kOne(Kind::NavigationRegion3D);
constexpr Tree<1> kOccluder = kOne(Kind::Occluder3D);

constexpr std::array<std::string_view, 9> kGroups{"Basic",     "Entity",    "Shapes",     "Lighting", "Camera",
                                                  "Physics",   "Animation", "Audio, VFX & UI", "World"};

constexpr std::array<ContentCatalogueItemUVE, 33> kItems{{
    {"folder", "Folder", "Basic", "A new folder here in Content", Action::Folder, {}},
    {"empty", "Empty Entity", "Basic", "A bare Node3D to build your own tree on", Action::EntityAsset, kEmpty},

    {"character", "Character", "Entity",
     "A playable body: CharacterBody3D with a Mesh, an AnimationPlayer and an AnimationTree", Action::EntityAsset,
     kCharacter},
    {"prop", "Prop", "Entity", "Something solid to place: StaticBody3D with a Mesh and a Collider",
     Action::EntityAsset, kProp},
    {"physics-prop", "Physics Prop", "Entity", "A prop that falls and gets pushed: RigidBody3D, Mesh and Collider",
     Action::EntityAsset, kPhysicsProp},
    {"trigger", "Trigger", "Entity", "An Area3D with a shape that reports what enters and leaves it",
     Action::EntityAsset, kTrigger},
    {"spawner", "Spawner", "Entity", "A SpawnPoint3D: where a player appears when the game starts",
     Action::EntityAsset, kSpawner},

    {"box", "Box", "Shapes", "A box mesh with its own collider", Action::EntityAsset, kBox},
    {"sphere", "Sphere", "Shapes", "A sphere mesh with its own collider", Action::EntityAsset, kSphere},
    {"plane", "Plane", "Shapes", "A flat plane mesh - a floor to start from", Action::EntityAsset, kPlane},
    {"mesh", "Mesh", "Shapes", "A MeshInstance3D: pick any imported model as its source", Action::EntityAsset, kMesh},

    {"sun", "Sun", "Lighting", "A directional Light3D that lights the whole level", Action::EntityAsset, kLight},
    {"light", "Light", "Lighting", "A Light3D: directional, point or spot", Action::EntityAsset, kLight},
    {"environment", "World Environment", "Lighting", "Sky, ambient light, fog and tone mapping for the level",
     Action::EntityAsset, kEnvironment},
    {"reflection-probe", "Reflection Probe", "Lighting", "Captures reflections for the area around it",
     Action::EntityAsset, kProbe},
    {"fog", "Fog Volume", "Lighting", "Local fog inside a box or sphere", Action::EntityAsset, kFog},
    {"decal", "Decal", "Lighting", "Projects a texture onto the surfaces it touches", Action::EntityAsset, kDecal},

    {"camera", "Camera", "Camera", "A Camera3D", Action::EntityAsset, kCamera},
    {"follow-camera", "Follow Camera", "Camera", "A SpringArm3D holding a Camera3D - pulls in when a wall is behind it",
     Action::EntityAsset, kFollowCamera},

    {"static-body", "Static Body", "Physics", "A StaticBody3D: collides but never moves", Action::EntityAsset,
     kStaticBody},
    {"rigid-body", "Rigid Body", "Physics", "A RigidBody3D: moved by gravity and forces", Action::EntityAsset,
     kRigidBody},
    {"collider", "Collider", "Physics", "A Collider3D shape for the body it is placed under", Action::EntityAsset,
     kCollider},
    {"area", "Area", "Physics", "An Area3D: detects overlaps without blocking", Action::EntityAsset, kArea},
    {"raycast", "RayCast", "Physics", "A RayCast3D: reports the first thing along a line", Action::EntityAsset,
     kRayCast},

    {"animation-player", "AnimationPlayer", "Animation", "Plays animation clips on its parent", Action::EntityAsset,
     kAnimationPlayer},
    {"animation-tree", "AnimationTree", "Animation", "Blends clips with a graph and a state machine",
     Action::EntityAsset, kAnimationTree},

    {"audio", "Audio Source", "Audio, VFX & UI", "An AudioSource3D that plays a clip in 3D", Action::EntityAsset,
     kAudio},
    {"particles", "Particles", "Audio, VFX & UI", "A ParticleEmitter3D", Action::EntityAsset, kParticles},
    {"canvas", "UI Canvas", "Audio, VFX & UI", "A Canvas to hold text, images and buttons", Action::EntityAsset,
     kCanvas},

    {"level-streamer", "Level Streamer", "World", "Loads and unloads a scene as the player gets near",
     Action::EntityAsset, kLevelStreamer},
    {"world-partition", "World Partition", "World", "Splits a large world into cells that stream in",
     Action::EntityAsset, kWorldPartition},
    {"navigation", "Navigation Region", "World", "The walkable area agents find paths on", Action::EntityAsset,
     kNavigation},
    {"occluder", "Occluder", "World", "Hides what is behind it from rendering", Action::EntityAsset, kOccluder},
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

Scene::Nodes::SceneNodeKindUVE GetContentCatalogueIconKindUVE(const ContentCatalogueItemUVE& item) noexcept {
    if (item.action == ContentCatalogueActionUVE::Folder || item.nodes.empty()) {
        return Kind::Folder;
    }
    return item.nodes.front().kind;
}

} // namespace UVE::Editor
