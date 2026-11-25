// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "tar_gz_archive.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <fstream>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

#include <zlib.h>

namespace UVE::Online {
namespace {

inline constexpr std::size_t kTarBlockBytes = 512U;
inline constexpr std::size_t kNameOffset = 0U;
inline constexpr std::size_t kNameBytes = 100U;
inline constexpr std::size_t kModeOffset = 100U;
inline constexpr std::size_t kModeBytes = 8U;
inline constexpr std::size_t kSizeOffset = 124U;
inline constexpr std::size_t kSizeBytes = 12U;
inline constexpr std::size_t kTypeOffset = 156U;
inline constexpr std::size_t kMagicOffset = 257U;
inline constexpr std::string_view kUstarMagic = "ustar";
inline constexpr std::size_t kPrefixOffset = 345U;
inline constexpr std::size_t kPrefixBytes = 155U;
/// Window bits for zlib with gzip framing: 15 for the window plus 16 to select gzip.
inline constexpr int kGzipWindowBits = 15 + 16;
inline constexpr std::size_t kInflateChunkBytes = 1U << 16U;
/// The longest metadata record (long name or pax header) worth reading.
inline constexpr std::uint64_t kMaximumMetadataBytes = 1U << 20U;
inline constexpr std::string_view kPaxPathKey = "path=";
inline constexpr unsigned kExecutableBits = 0111U;

enum class EntryType : char {
    Regular = '0',
    RegularOld = '\0',
    Directory = '5',
    GnuLongName = 'L',
    PaxExtended = 'x',
    PaxGlobal = 'g',
};

/// Streams decompressed bytes out of a gzip file.
class GzipReader final {
public:
    explicit GzipReader(const std::filesystem::path& file) : m_input(file, std::ios::binary) {
        m_ok = m_input.is_open() && inflateInit2(&m_stream, kGzipWindowBits) == Z_OK;
        m_initialized = m_ok;
    }
    GzipReader(const GzipReader&) = delete;
    GzipReader& operator=(const GzipReader&) = delete;
    ~GzipReader() {
        if (m_initialized) {
            inflateEnd(&m_stream);
        }
    }

    [[nodiscard]] bool Ok() const noexcept { return m_ok; }

    /// Reads exactly `size` bytes; false at end of data or on a corrupt stream.
    [[nodiscard]] bool Read(std::uint8_t* destination, std::size_t size) {
        while (size > 0U) {
            if (m_outputBegin == m_outputEnd && !Fill()) {
                return false;
            }
            const std::size_t take = std::min(size, m_outputEnd - m_outputBegin);
            std::memcpy(destination, m_output.data() + m_outputBegin, take);
            m_outputBegin += take;
            destination += take;
            size -= take;
        }
        return true;
    }

private:
    [[nodiscard]] bool Fill() {
        if (!m_ok || m_finished) {
            return false;
        }
        m_stream.next_out = m_output.data();
        m_stream.avail_out = static_cast<uInt>(m_output.size());
        while (m_stream.avail_out == m_output.size()) {
            if (m_stream.avail_in == 0U) {
                m_input.read(reinterpret_cast<char*>(m_inputBuffer.data()), static_cast<std::streamsize>(m_inputBuffer.size()));
                const std::streamsize read = m_input.gcount();
                if (read <= 0) {
                    m_ok = false;
                    return false;
                }
                m_stream.next_in = m_inputBuffer.data();
                m_stream.avail_in = static_cast<uInt>(read);
            }
            const int status = inflate(&m_stream, Z_NO_FLUSH);
            if (status == Z_STREAM_END) {
                m_finished = true;
                break;
            }
            if (status != Z_OK) {
                m_ok = false;
                return false;
            }
        }
        m_outputBegin = 0U;
        m_outputEnd = m_output.size() - m_stream.avail_out;
        return m_outputEnd > 0U;
    }

    std::ifstream m_input;
    z_stream m_stream{};
    std::array<std::uint8_t, kInflateChunkBytes> m_inputBuffer{};
    std::array<std::uint8_t, kInflateChunkBytes> m_output{};
    std::size_t m_outputBegin = 0U;
    std::size_t m_outputEnd = 0U;
    bool m_ok = false;
    bool m_initialized = false;
    bool m_finished = false;
};

[[nodiscard]] std::string FieldText(const std::uint8_t* field, const std::size_t bytes) {
    const auto* begin = reinterpret_cast<const char*>(field);
    return {begin, strnlen(begin, bytes)};
}

[[nodiscard]] std::optional<std::uint64_t> OctalField(const std::uint8_t* field, const std::size_t bytes) {
    std::string text = FieldText(field, bytes);
    text.erase(std::remove(text.begin(), text.end(), ' '), text.end());
    if (text.empty()) {
        return std::uint64_t{0};
    }
    std::uint64_t value = 0U;
    constexpr int kOctalBase = 8;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, kOctalBase);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::uint64_t PaddedSize(const std::uint64_t size) noexcept {
    return (size + kTarBlockBytes - 1U) / kTarBlockBytes * kTarBlockBytes;
}

/// The destination path of an archive entry, or nothing when it is absolute or climbs out.
[[nodiscard]] std::optional<std::filesystem::path> ContainedPath(const std::filesystem::path& root,
                                                                    const std::string& name) {
    const std::filesystem::path relative = std::filesystem::path(name).lexically_normal();
    if (name.empty() || relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        return std::nullopt;
    }
    for (const std::filesystem::path& part : relative) {
        if (part == "..") {
            return std::nullopt;
        }
    }
    if (relative == ".") {
        return root;
    }
    return root / relative;
}

[[nodiscard]] bool ReadRecordText(GzipReader& reader, const std::uint64_t size, std::string& text) {
    if (size > kMaximumMetadataBytes) {
        return false;
    }
    std::vector<std::uint8_t> bytes(PaddedSize(size));
    if (!reader.Read(bytes.data(), bytes.size())) {
        return false;
    }
    text.assign(reinterpret_cast<const char*>(bytes.data()), static_cast<std::size_t>(size));
    return true;
}

/// The "path" a pax record sets, if any. Records are "<length> <key>=<value>\n".
[[nodiscard]] std::optional<std::string> PaxPath(const std::string& records) {
    std::size_t position = 0U;
    while (position < records.size()) {
        const std::size_t space = records.find(' ', position);
        if (space == std::string::npos) {
            break;
        }
        std::size_t length = 0U;
        const auto [end, error] = std::from_chars(records.data() + position, records.data() + space, length);
        if (error != std::errc{} || length == 0U || position + length > records.size()) {
            break;
        }
        const std::string_view record(records.data() + space + 1U, position + length - space - 2U);
        if (record.starts_with(kPaxPathKey)) {
            return std::string(record.substr(kPaxPathKey.size()));
        }
        position += length;
    }
    return std::nullopt;
}

[[nodiscard]] ArchiveExtractResult Fail(ArchiveExtractResult result, std::string message) {
    result.extracted = false;
    result.message = std::move(message);
    return result;
}

} // namespace

ArchiveExtractResult ExtractTarGz(const std::filesystem::path& archive, const std::filesystem::path& destination,
                                        const ArchiveLimits& limits) {
    ArchiveExtractResult result;
    std::error_code error;
    if (!std::filesystem::is_directory(destination, error) || !std::filesystem::is_empty(destination, error)) {
        return Fail(result, "The extraction folder must exist and be empty: " + destination.string());
    }
    GzipReader reader(archive);
    if (!reader.Ok()) {
        return Fail(result, "Unable to open the archive: " + archive.string());
    }

    std::optional<std::string> pendingName;
    std::array<std::uint8_t, kTarBlockBytes> header{};
    std::vector<std::uint8_t> buffer(kInflateChunkBytes);
    while (true) {
        if (!reader.Read(header.data(), header.size())) {
            return Fail(result, "The archive ends early or is damaged.");
        }
        if (std::all_of(header.begin(), header.end(), [](const std::uint8_t value) { return value == 0U; })) {
            break; // end-of-archive marker
        }
        const std::optional<std::uint64_t> size = OctalField(header.data() + kSizeOffset, kSizeBytes);
        const std::optional<std::uint64_t> mode = OctalField(header.data() + kModeOffset, kModeBytes);
        if (!size.has_value() || !mode.has_value()) {
            return Fail(result, "The archive has a damaged entry header.");
        }
        const auto type = static_cast<EntryType>(header[kTypeOffset]);

        if (type == EntryType::GnuLongName || type == EntryType::PaxExtended || type == EntryType::PaxGlobal) {
            std::string text;
            if (!ReadRecordText(reader, *size, text)) {
                return Fail(result, "The archive has a damaged metadata record.");
            }
            if (type == EntryType::GnuLongName) {
                pendingName = std::string(text.c_str());
            } else if (type == EntryType::PaxExtended) {
                if (std::optional<std::string> path = PaxPath(text)) {
                    pendingName = std::move(path);
                }
            }
            continue;
        }

        std::string name = FieldText(header.data() + kNameOffset, kNameBytes);
        if (FieldText(header.data() + kMagicOffset, kUstarMagic.size()) == kUstarMagic) {
            const std::string prefix = FieldText(header.data() + kPrefixOffset, kPrefixBytes);
            if (!prefix.empty()) {
                name = prefix + "/" + name;
            }
        }
        if (pendingName.has_value()) {
            name = std::move(*pendingName);
            pendingName.reset();
        }
        if (++result.entries > limits.maximumEntries) {
            return Fail(result, "The archive has more entries than allowed.");
        }
        const std::optional<std::filesystem::path> target = ContainedPath(destination, name);
        if (!target.has_value()) {
            return Fail(result, "The archive names a path outside its folder: " + name);
        }

        const bool included = !limits.include || limits.include(std::filesystem::path(name).lexically_normal());
        if (type == EntryType::Directory) {
            if (!included) {
                continue;
            }
            std::filesystem::create_directories(*target, error);
            if (error) {
                return Fail(result, "Unable to create " + target->string() + ": " + error.message());
            }
            continue;
        }
        if (type != EntryType::Regular && type != EntryType::RegularOld) {
            return Fail(result, "The archive holds a link or special file, which packages never contain: " + name);
        }
        if (!included) {
            std::uint64_t skip = PaddedSize(*size);
            while (skip > 0U) {
                const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(skip, buffer.size()));
                if (!reader.Read(buffer.data(), take)) {
                    return Fail(result, "The archive ends inside " + name);
                }
                skip -= take;
            }
            continue;
        }
        if (result.bytesWritten + *size > limits.maximumTotalBytes) {
            return Fail(result, "The archive expands beyond the allowed size.");
        }
        std::filesystem::create_directories(target->parent_path(), error);
        std::ofstream output(*target, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return Fail(result, "Unable to write " + target->string());
        }
        std::uint64_t remaining = *size;
        while (remaining > 0U) {
            const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
            if (!reader.Read(buffer.data(), take)) {
                return Fail(result, "The archive ends inside " + name);
            }
            output.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(take));
            remaining -= take;
        }
        output.close();
        if (!output) {
            return Fail(result, "Unable to finish writing " + target->string());
        }
        const std::uint64_t padding = PaddedSize(*size) - *size;
        if (padding > 0U && !reader.Read(buffer.data(), static_cast<std::size_t>(padding))) {
            return Fail(result, "The archive ends early or is damaged.");
        }
        result.bytesWritten += *size;
        if ((*mode & kExecutableBits) != 0U) {
            std::filesystem::permissions(*target,
                                         std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                                             std::filesystem::perms::others_exec,
                                         std::filesystem::perm_options::add, error);
        }
    }
    result.extracted = true;
    result.message = "Extracted " + std::to_string(result.entries) + " entries.";
    return result;
}

} // namespace UVE::Online
