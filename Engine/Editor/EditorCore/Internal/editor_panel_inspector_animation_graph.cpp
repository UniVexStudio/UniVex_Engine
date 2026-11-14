// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// AnimationGraph's Inspector blocks: the parameter table and the graph itself. The graph is shown
// as the tree it is - Output at the top, each node's inputs indented under it - with nodes nothing
// uses yet listed after, so a graph can be built piece by piece and wired up as it grows.

#include "editor.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "animation_graph_component.h"
#include "animation_graph_editing.h"

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKindUVE;
using Scene::AnimationGraphNodeUVE;
using Scene::AnimationParameterTypeUVE;
using Scene::AnimationParameterUVE;
using Scene::AnimationGraphTransitionUVE;

}

void EditorUVE::RenameAnimationParameterReferencesUVE(const std::string& from, const std::string& to) {
    m_pendingAnimationParameterRename = std::make_pair(from, to);
}

} // namespace UVE::Editor
