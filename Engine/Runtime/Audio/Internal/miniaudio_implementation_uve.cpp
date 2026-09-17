// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The single translation unit that compiles miniaudio itself (MINIAUDIO_IMPLEMENTATION pulls the
// function bodies into this one .cpp). Kept separate from every engine TU so the library's own
// pragmas/settings apply exactly once, and so the engine's -Wall/-Wextra/-Wpedantic -Werror
// treat-our-code-strictly policy does not get imposed on vendored code (guarded pragmas below).
//
// Feature trims keep the build lean: this engine decodes WAV through its own pipeline
// (wav_pcm16_decoder_uve), so miniaudio's decoding/encoding/resource-manager/node-graph/engine
// helper layers are all compiled out — only the device/{context,thread} plumbing is used.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include <miniaudio.h>
