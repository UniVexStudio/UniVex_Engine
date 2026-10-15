// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// One implementation TU. Vendor includes use the existing SYSTEM include directory; engine
// code retains -Werror. Decoding is enabled for FLAC/MP3 and miniaudio's bundled stb_vorbis.
// WAV still uses the engine's existing PCM16 importer. No new dependency is downloaded.
// The encoding/resource-manager/object-graph/engine helpers remain disabled.
#define STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include <miniaudio.h>
#undef STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>
