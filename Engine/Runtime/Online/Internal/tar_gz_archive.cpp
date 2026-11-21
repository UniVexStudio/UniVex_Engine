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

inline constexpr std::size_t kTarBlockBytesUVE = 512U;
inline constexpr std::size_t kNameOffsetUVE = 0U;
inline constexpr std::size_t kNameBytesUVE = 100U;
inline constexpr std::size_t kModeOffsetUVE = 100U;
inline constexpr std::size_t kModeBytesUVE = 8U;
inline constexpr std::size_t kSizeOffsetUVE = 124U;
inline constexpr std::size_t kSizeBytesUVE = 12U;
inline constexpr std::size_t kTypeOffsetUVE = 156U;
inline constexpr std::size_t kMagicOffsetUVE = 257U;
inline constexpr std::string_view kUstarMagicUVE = "ustar";
inline constexpr std::size_t kPrefixOffsetUVE = 345U;
inline constexpr std::size_t kPrefixBytesUVE = 155U;
/// Window bits for zlib with gzip framing: 15 for the window plus 16 to select gzip.
inline constexpr int kGzipWindowBitsUVE = 15 + 16;
inline constexpr std::size_t kInflateChunkBytesUVE = 1U << 16U;
/// The longest metadata record (long name or pax header) worth reading.
inline constexpr std::uint64_t kMaximumMetadataBytesUVE = 1U << 20U;
inline constexpr std::string_view kPaxPathKeyUVE = "path=";
inline constexpr unsigned kExecutableBitsUVE = 0111U;

enum class EntryTypeUVE : char {
    Regular = '0',
    RegularOld = '\0',
    Directory = '5',
    GnuLongName = 'L',
    PaxExtended = 'x',
    PaxGlobal = 'g',
};

/// Streams decompressed bytes out of a gzip file.
class GzipReaderUVE final {
public:
    explicit GzipReaderUVE(const std::filesystem::path& file) : m_input(file, std::ios::binary) {
        m_ok = m_input.is_open() && inflateInit2(&m_stream, kGzipWindowBitsUVE) == Z_OK;
        m_initialized = m_ok;
    }
    GzipReaderUVE(const GzipReaderUVE&) = delete;
    GzipReaderUVE& operator=(const GzipReaderUVE&) = delete;
    ~GzipReaderUVE() {
        if (m_initialized) {
            inflateEnd(&m_stream);
        }
    }

    [[nodiscard]] bool OkUVE() const noexcept { return m_ok; }

    /// Reads exactly `size` bytes; false at end of data or on a corrupt stream.
    [[nodiscard]] bool ReadUVE(std::uint8_t* destination, std::size_t size) {
        while (size > 0U) {
            if (m_outputBegin == m_outputEnd && !FillUVE()) {
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
    [[nodiscard]] bool FillUVE() {
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
    std::array<std::uint8_t, kInflateChunkBytesUVE> m_inputBuffer{};
    std::array<std::uint8_t, kInflateChunkBytesUVE> m_output{};
    std::size_t m_outputBegin = 0U;
    std::size_t m_outputEnd = 0U;
    bool m_ok = false;
    bool m_initialized = false;
    bool m_finished = false;
};

[[nodiscard]] std::string FieldTextUVE(const std::uint8_t* field, const std::size_t bytes) {
    const auto* begin = reinterpret_cast<const char*>(field);
    return {begin, strnlen(begin, bytes)};
}

[[nodiscard]] std::optional<std::uint64_t> OctalFieldUVE(const std::uint8_t* field, const std::size_t bytes) {
    std::string text = FieldTextUVE(field, bytes);
    text.erase(std::remove(text.begin(), text.end(), ' '), text.end());
    if (text.empty()) {
        return std::uint64_t{0};
    }
    std::uint64_t value = 0U;
    constexpr int kOctalBaseUVE = 8;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, kOctalBaseUVE);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::uint64_t PaddedSizeUVE(const std::uint64_t size) noexcept {
    return (size + kTarBlockBytesUVE - 1U) / kTarBlockBytesUVE * kTarBlockBytesUVE;
}

/// The destination path of an archive entry, or nothing when it is absolute or climbs out.
[[nodiscard]] std::optional<std::filesystem::path> ContainedPathUVE(const std::filesystem::path& root,
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

[[nodiscard]] bool ReadRecordTextUVE(GzipReaderUVE& reader, const std::uint64_t size, std::string& text) {
    if (size > kMaximumMetadataBytesUVE) {
        return false;
    }
    std::vector<std::uint8_t> bytes(PaddedSizeUVE(size));
    if (!reader.ReadUVE(bytes.data(), bytes.size())) {
        return false;
    }
    text.assign(reinterpret_cast<const char*>(bytes.data()), static_cast<std::size_t>(size));
    return true;
}

/// The "path" a pax record sets, if any. Records are "<length> <key>=<value>\n".
[[nodiscard]] std::optional<std::string> PaxPathUVE(const std::string& records) {
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
        if (record.starts_with(kPaxPathKeyUVE)) {
            return std::string(record.substr(kPaxPathKeyUVE.size()));
        }
        position += length;
    }
    return std::nullopt;
}

[[nodiscard]] ArchiveExtractResultUVE FailUVE(ArchiveExtractResultUVE result, std::string message) {
    result.extracted = false;
    result.message = std::move(message);
    return result;
}

} // namespace

ArchiveExtractResultUVE ExtractTarGzUVE(const std::filesystem::path& archive, const std::filesystem::path& destination,
                                        const ArchiveLimitsUVE& limits) {
    ArchiveExtractResultUVE result;
    std::error_code error;
    if (!std::filesystem::is_directory(destination, error) || !std::filesystem::is_empty(destination, error)) {
        return FailUVE(result, "The extraction folder must exist and be empty: " + destination.string());
    }
    GzipReaderUVE reader(archive);
    if (!reader.OkUVE()) {
        return FailUVE(result, "Unable to open the archive: " + archive.string());
    }

    std::optional<std::string> pendingName;
    std::array<std::uint8_t, kTarBlockBytesUVE> header{};
    std::vector<std::uint8_t> buffer(kInflateChunkBytesUVE);
    while (true) {
        if (!reader.ReadUVE(header.data(), header.size())) {
            return FailUVE(result, "The archive ends early or is damaged.");
        }
        if (std::all_of(header.begin(), header.end(), [](const std::uint8_t value) { return value == 0U; })) {
            break; // end-of-archive marker
        }
        const std::optional<std::uint64_t> size = OctalFieldUVE(header.data() + kSizeOffsetUVE, kSizeBytesUVE);
        const std::optional<std::uint64_t> mode = OctalFieldUVE(header.data() + kModeOffsetUVE, kModeBytesUVE);
        if (!size.has_value() || !mode.has_value()) {
            return FailUVE(result, "The archive has a damaged entry header.");
        }
        const auto type = static_cast<EntryTypeUVE>(header[kTypeOffsetUVE]);

        if (type == EntryTypeUVE::GnuLongName || type == EntryTypeUVE::PaxExtended || type == EntryTypeUVE::PaxGlobal) {
            std::string text;
            if (!ReadRecordTextUVE(reader, *size, text)) {
                return FailUVE(result, "The archive has a damaged metadata record.");
            }
            if (type == EntryTypeUVE::GnuLongName) {
                pendingName = std::string(text.c_str());
            } else if (type == EntryTypeUVE::PaxExtended) {
                if (std::optional<std::string> path = PaxPathUVE(text)) {
                    pendingName = std::move(path);
                }
            }
            continue;
        }

        std::string name = FieldTextUVE(header.data() + kNameOffsetUVE, kNameBytesUVE);
        if (FieldTextUVE(header.data() + kMagicOffsetUVE, kUstarMagicUVE.size()) == kUstarMagicUVE) {
            const std::string prefix = FieldTextUVE(header.data() + kPrefixOffsetUVE, kPrefixBytesUVE);
            if (!prefix.empty()) {
                name = prefix + "/" + name;
            }
        }
        if (pendingName.has_value()) {
            name = std::move(*pendingName);
            pendingName.reset();
        }
        if (++result.entries > limits.maximumEntries) {
            return FailUVE(result, "The archive has more entries than allowed.");
        }
        const std::optional<std::filesystem::path> target = ContainedPathUVE(destination, name);
        if (!target.has_value()) {
            return FailUVE(result, "The archive names a path outside its folder: " + name);
        }

        const bool included = !limits.include || limits.include(std::filesystem::path(name).lexically_normal());
        if (type == EntryTypeUVE::Directory) {
            if (!included) {
                continue;
            }
            std::filesystem::create_directories(*target, error);
            if (error) {
                return FailUVE(result, "Unable to create " + target->string() + ": " + error.message());
            }
            continue;
        }
        if (type != EntryTypeUVE::Regular && type != EntryTypeUVE::RegularOld) {
            return FailUVE(result, "The archive holds a link or special file, which packages never contain: " + name);
        }
        if (!included) {
            std::uint64_t skip = PaddedSizeUVE(*size);
            while (skip > 0U) {
                const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(skip, buffer.size()));
                if (!reader.ReadUVE(buffer.data(), take)) {
                    return FailUVE(result, "The archive ends inside " + name);
                }
                skip -= take;
            }
            continue;
        }
        if (result.bytesWritten + *size > limits.maximumTotalBytes) {
            return FailUVE(result, "The archive expands beyond the allowed size.");
        }
        std::filesystem::create_directories(target->parent_path(), error);
        std::ofstream output(*target, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            return FailUVE(result, "Unable to write " + target->string());
        }
        std::uint64_t remaining = *size;
        while (remaining > 0U) {
            const std::size_t take = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
            if (!reader.ReadUVE(buffer.data(), take)) {
                return FailUVE(result, "The archive ends inside " + name);
            }
            output.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(take));
            remaining -= take;
        }
        output.close();
        if (!output) {
            return FailUVE(result, "Unable to finish writing " + target->string());
        }
        const std::uint64_t padding = PaddedSizeUVE(*size) - *size;
        if (padding > 0U && !reader.ReadUVE(buffer.data(), static_cast<std::size_t>(padding))) {
            return FailUVE(result, "The archive ends early or is damaged.");
        }
        result.bytesWritten += *size;
        if ((*mode & kExecutableBitsUVE) != 0U) {
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
