// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "asset_guid.h"

#include <limits>
#include <random>

namespace UVE::Asset {

namespace {
[[nodiscard]] std::mt19937_64& GetRandomEngine() {
    thread_local std::mt19937_64 engine{std::random_device{}()};
    return engine;
}
} // namespace

AssetGuid GenerateAssetGuid() {
    std::uniform_int_distribution<std::uint64_t> distribution(1, std::numeric_limits<std::uint64_t>::max());
    return AssetGuid{distribution(GetRandomEngine())};
}

} // namespace UVE::Asset
