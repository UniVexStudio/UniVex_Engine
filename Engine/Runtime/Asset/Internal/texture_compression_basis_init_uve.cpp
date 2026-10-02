// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "texture_compression_basis_init_uve.h"

#include <mutex>

#ifndef UVE_HAS_BASIS_ENCODER
#define UVE_HAS_BASIS_ENCODER 0
#endif

#if UVE_HAS_BASIS_ENCODER
#include "basisu_comp.h"
#else
#include "basisu_transcoder.h"
#endif

namespace UVE::Asset::Detail {

bool EnsureBasisUniversalInitializedUVE() noexcept {
    static std::once_flag initializationFlag;
    static bool initialized = false;

    try {
        std::call_once(initializationFlag, [] {
#if UVE_HAS_BASIS_ENCODER
            // The Basis encoder initializer also initializes the transcoder. Using one shared
            // entry point avoids Basis's non-idempotent transcoder initializer being called
            // independently by the encode and decode paths.
            initialized = basisu::basisu_encoder_init(false);
#else
            basist::basisu_transcoder_init();
            initialized = true;
#endif
        });
    } catch (...) {
        return false;
    }

    return initialized;
}

} // namespace UVE::Asset::Detail
