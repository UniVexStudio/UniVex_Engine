// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <imgui.h>

namespace UVE::Editor {

/// The monospace font, built into the atlas during InitUVE() and used by panels that print
/// aligned numeric or code-like text.
///
/// WHY IT IS DECLARED HERE RATHER THAN MADE A MEMBER. editor_uve.h states as a design rule that no
/// Dear ImGui type appears in its public interface, and ImFont is one. Promoting this pointer to
/// an EditorUVE member would drag imgui.h into every consumer of that header purely to satisfy a
/// file split - so it stays a file-scope pointer, and this INTERNAL header is what lets a second
/// translation unit in the same library see it without widening the public surface.
///
/// Defined in editor_uve.cpp, which owns the atlas. Set once after the atlas is built and never
/// reassigned, with static storage duration for the life of the process, so there is no lifetime
/// or ownership question beyond that. Null until InitUVE() has run: every reader must check, and
/// the readers that existed before this split already did.
extern ImFont* g_monoFontUVE;

} // namespace UVE::Editor
