// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// Shared C ABI between UniVex Hub (libunivex_hub.so) and the dynamically loaded Android Engine
// Editor library (uve_editor inside univex-engine-*-android-<abi>.uvpackage).
//
// Both repositories carry an identical copy of this header under contracts/v1/.

#include <stdint.h>

struct android_app;

#ifdef __cplusplus
extern "C" {
#endif

#define UVE_ANDROID_EDITOR_ABI_VERSION 1U
#define UVE_ANDROID_EDITOR_ENTRY_SYMBOL "UveEditorRunUVE"
#define UVE_ANDROID_EDITOR_ABI_VERSION_SYMBOL "UveEditorAbiVersionUVE"

/// Mandatory C entry point exported by the Android editor library.
/// Runs the editor on `app`'s NativeActivity using argv[0] as the path to the loaded editor
/// library and argv[1..argc-1] as desktop-compatible CLI options, returning the editor exit code.
typedef int (*UveEditorRunFnUVE)(struct android_app* app, int argc, char** argv);

/// Optional ABI version probe. When exported, must return UVE_ANDROID_EDITOR_ABI_VERSION.
typedef uint32_t (*UveEditorAbiVersionFnUVE)(void);

#ifdef __cplusplus
}
#endif
