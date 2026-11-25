#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace UVE::Asset {

enum class DataTableColumnType : std::uint8_t {
    Boolean = 0,
    Integer,
    Number,
    String,
};

using DataTableValue = std::variant<bool, std::int64_t, double, std::string>;

struct DataTableColumn final {
    std::string name;
    DataTableColumnType type = DataTableColumnType::String;
    [[nodiscard]] bool operator==(const DataTableColumn&) const = default;
};

struct DataTableRow final {
    std::string identifier;
    std::vector<DataTableValue> values;
    [[nodiscard]] bool operator==(const DataTableRow&) const = default;
};

enum class DataTableDiagnosticCode : std::uint8_t {
    InvalidDocument = 0,
    InvalidHeader,
    HeaderMismatch,
    InvalidRow,
    DuplicateRow,
    InvalidValue,
    ValueOutOfBounds,
    LimitExceeded,
};

struct DataTableDiagnostic final {
    DataTableDiagnosticCode code = DataTableDiagnosticCode::InvalidDocument;
    std::size_t line = 0U;
    std::size_t column = 0U;
    std::string message;
    [[nodiscard]] bool operator==(const DataTableDiagnostic&) const = default;
};

struct DataTableSnapshot final {
    std::uint64_t generation = 0U;
    std::string name;
    std::vector<DataTableColumn> columns;
    std::vector<DataTableRow> rows;
    std::vector<DataTableDiagnostic> diagnostics;
    bool diagnosticsTruncated = false;
    [[nodiscard]] bool operator==(const DataTableSnapshot&) const = default;
};

class DataTableCsvImporter;
class DataTableTsvImporter;
class DataTableJsonImporter;

class DataTable final {
public:
    static constexpr std::size_t kMaximumColumns = 256U;
    static constexpr std::size_t kMaximumRows = 65536U;
    static constexpr std::size_t kMaximumIdentifierBytes = 64U;
    static constexpr std::size_t kMaximumStringBytes = 1024U;
    static constexpr std::size_t kMaximumDocumentBytes = 4U * 1024U * 1024U;
    static constexpr std::size_t kMaximumDiagnostics = 128U;

    explicit DataTable(std::string name = {});
    DataTable(const DataTable&) = default;
    DataTable& operator=(const DataTable&) = default;
    DataTable(DataTable&&) noexcept = default;
    DataTable& operator=(DataTable&&) noexcept = default;
    ~DataTable() = default;

    [[nodiscard]] bool DefineColumn(std::string name, DataTableColumnType type);
    [[nodiscard]] bool AddRow(std::string identifier, std::vector<DataTableValue> values);
    [[nodiscard]] bool ClearRows() noexcept;
    [[nodiscard]] bool ImportCsv(std::string_view document);
    [[nodiscard]] bool ImportTsv(std::string_view document);
    [[nodiscard]] bool ImportJson(std::string_view document);
    [[nodiscard]] std::size_t GetResidentBytes() const noexcept;
    [[nodiscard]] DataTableSnapshot GetSnapshot() const;
    [[nodiscard]] const DataTableRow* FindRow(std::string_view identifier) const noexcept;

private:
    friend class DataTableCsvImporter;
    friend class DataTableTsvImporter;
    friend class DataTableJsonImporter;

    [[nodiscard]] static bool IsBoundedIdentifier(std::string_view value) noexcept;
    [[nodiscard]] static bool IsValueCompatible(const DataTableValue& value,
                                                    DataTableColumnType type) noexcept;
    [[nodiscard]] bool ValidateRow(std::string_view identifier,
                                      const std::vector<DataTableValue>& values) const noexcept;
    void SetDiagnostics(std::vector<DataTableDiagnostic> diagnostics, bool truncated) noexcept;
    void IncrementGeneration() noexcept;

    std::string m_name;
    std::vector<DataTableColumn> m_columns;
    std::vector<DataTableRow> m_rows;
    std::vector<DataTableDiagnostic> m_diagnostics;
    bool m_diagnosticsTruncated = false;
    std::uint64_t m_generation = 1U;
};

class DataTableCsvImporter final {
public:
    [[nodiscard]] static bool Import(std::string_view document, DataTable& table);
    [[nodiscard]] static bool ImportDelimited(std::string_view document, char delimiter, DataTable& table);
};

class DataTableTsvImporter final {
public:
    [[nodiscard]] static bool Import(std::string_view document, DataTable& table);
};

class DataTableJsonImporter final {
public:
    [[nodiscard]] static bool Import(std::string_view document, DataTable& table);
};

class DataTableAssetSerializer final {
public:
    [[nodiscard]] static bool Serialize(const DataTable& table, std::string& document);
    [[nodiscard]] static bool Deserialize(std::string_view document, DataTable& table);
};

struct DataTableCatalogEntry final {
    std::string name;
    std::uint64_t generation = 0U;
    std::size_t columnCount = 0U;
    std::size_t rowCount = 0U;
    bool valid = false;
    [[nodiscard]] bool operator==(const DataTableCatalogEntry&) const = default;
};

struct DataTableCatalogSnapshot final {
    std::uint64_t generation = 0U;
    std::vector<DataTableCatalogEntry> entries;
    bool entriesTruncated = false;
    [[nodiscard]] bool operator==(const DataTableCatalogSnapshot&) const = default;
};

class DataTableCatalog final {
public:
    static constexpr std::size_t kMaximumEntries = 1024U;

    [[nodiscard]] bool Upsert(const DataTableSnapshot& table);
    [[nodiscard]] bool Remove(std::string_view name);
    [[nodiscard]] DataTableCatalogSnapshot GetSnapshot() const;
    [[nodiscard]] bool Contains(std::string_view name) const noexcept;

private:
    void IncrementGeneration() noexcept;

    std::vector<DataTableCatalogEntry> m_entries;
    std::uint64_t m_generation = 1U;
};

} // namespace UVE::Asset
