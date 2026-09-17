// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "import_helpers_uve.h"

#include <fstream>
#include <limits>

namespace UVE::Asset::Detail {

[[nodiscard]] bool ReadBoundedSourceBytesUVE(const std::filesystem::path& sourcePath,
                                             const char* importerName,
                                             const std::uint64_t maximumBytes,
                                             std::vector<std::byte>& outBytes) {
    std::ifstream input(sourcePath, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        UVE_ERROR("{}: failed to open source \"{}\"", importerName, sourcePath.string());
        return false;
    }
    const std::streamoff fileSize = input.tellg();
    if (fileSize < 0 || static_cast<std::uint64_t>(fileSize) > maximumBytes ||
        static_cast<std::uint64_t>(fileSize) > std::numeric_limits<std::size_t>::max()) {
        UVE_ERROR("{}: source \"{}\" exceeds the bounded source-size limit", importerName, sourcePath.string());
        return false;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(fileSize));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            UVE_ERROR("{}: source \"{}\" could not be read completely", importerName, sourcePath.string());
            return false;
        }
    }
    outBytes = std::move(bytes);
    return true;
}

} // namespace UVE::Asset::Detail
