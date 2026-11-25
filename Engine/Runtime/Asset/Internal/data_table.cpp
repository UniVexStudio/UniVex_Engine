#include "data_table.h"

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

namespace UVE::Asset {
namespace {

struct CsvField final {
    std::string value;
    std::size_t line = 1U;
    std::size_t column = 1U;
};

using CsvRecord = std::vector<CsvField>;

void AddDiagnostic(std::vector<DataTableDiagnostic>& diagnostics,
                      bool& truncated,
                      const DataTableDiagnosticCode code,
                      const std::size_t line,
                      const std::size_t column,
                      std::string message) {
    if (diagnostics.size() >= DataTable::kMaximumDiagnostics) {
        truncated = true;
        return;
    }
    diagnostics.push_back(DataTableDiagnostic{code, line, column, std::move(message)});
}

[[nodiscard]] bool IsNewline(const std::string_view document, const std::size_t index) noexcept {
    return document[index] == '\n' || document[index] == '\r';
}

[[nodiscard]] std::size_t NewlineWidth(const std::string_view document, const std::size_t index) noexcept {
    return document[index] == '\r' && index + 1U < document.size() && document[index + 1U] == '\n' ? 2U : 1U;
}

[[nodiscard]] bool ParseDelimitedRecords(std::string_view document,
                                      const char delimiter,
                                      std::vector<CsvRecord>& records,
                                      std::vector<DataTableDiagnostic>& diagnostics,
                                      bool& diagnosticsTruncated) {
    if (document.empty()) {
        AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::InvalidDocument, 1U, 1U,
                         "The CSV document is empty.");
        return false;
    }
    if (document.size() > DataTable::kMaximumDocumentBytes) {
        AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::LimitExceeded, 1U, 1U,
                         "The CSV document exceeds the supported byte limit.");
        return false;
    }

    CsvRecord record;
    std::string field;
    std::size_t fieldLine = 1U;
    std::size_t fieldColumn = 1U;
    std::size_t line = 1U;
    std::size_t column = 1U;
    bool quoted = false;
    bool afterQuote = false;
    bool anyField = false;

    const auto pushField = [&]() {
        record.push_back(CsvField{std::move(field), fieldLine, fieldColumn});
        field.clear();
        anyField = true;
    };
    const auto pushRecord = [&]() {
        if (!record.empty()) {
            records.push_back(std::move(record));
            record = CsvRecord{};
        }
        anyField = false;
    };

    std::size_t index = 0U;
    while (index < document.size()) {
        const char character = document[index];
        if (quoted) {
            if (character == '"') {
                if (index + 1U < document.size() && document[index + 1U] == '"') {
                    field.push_back('"');
                    index += 2U;
                    column += 2U;
                    continue;
                }
                quoted = false;
                afterQuote = true;
                ++index;
                ++column;
                continue;
            }
            field.push_back(character);
            if (IsNewline(document, index)) {
                const std::size_t width = NewlineWidth(document, index);
                index += width;
                ++line;
                column = 1U;
            } else {
                ++index;
                ++column;
            }
            continue;
        }

        if (afterQuote) {
            if (character == delimiter) {
                pushField();
                afterQuote = false;
                ++index;
                ++column;
                fieldLine = line;
                fieldColumn = column;
                continue;
            }
            if (IsNewline(document, index)) {
                pushField();
                afterQuote = false;
                pushRecord();
                const std::size_t width = NewlineWidth(document, index);
                index += width;
                ++line;
                column = 1U;
                fieldLine = line;
                fieldColumn = column;
                continue;
            }
            AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::InvalidDocument,
                             line, column, "Unexpected characters follow a closing CSV quote.");
            return false;
        }

        if (character == '"' && field.empty()) {
            quoted = true;
            anyField = true;
            ++index;
            ++column;
            continue;
        }
        if (character == delimiter) {
            pushField();
            ++index;
            ++column;
            fieldLine = line;
            fieldColumn = column;
            continue;
        }
        if (IsNewline(document, index)) {
            pushField();
            pushRecord();
            const std::size_t width = NewlineWidth(document, index);
            index += width;
            ++line;
            column = 1U;
            fieldLine = line;
            fieldColumn = column;
            continue;
        }
        field.push_back(character);
        anyField = true;
        ++index;
        ++column;
    }

    if (quoted) {
        AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::InvalidDocument,
                         line, column, "The CSV document ends inside a quoted field.");
        return false;
    }
    if (afterQuote || anyField || !field.empty() || !record.empty()) {
        pushField();
        pushRecord();
    }
    if (records.empty()) {
        AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::InvalidDocument,
                         1U, 1U, "The CSV document contains no records.");
        return false;
    }
    return true;
}

[[nodiscard]] bool ParseBoolean(const std::string_view value, bool& result) noexcept {
    if (value == "true") {
        result = true;
        return true;
    }
    if (value == "false") {
        result = false;
        return true;
    }
    return false;
}

[[nodiscard]] bool ParseInteger(const std::string_view value, std::int64_t& result) noexcept {
    if (value.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    return error == std::errc{} && end == value.data() + value.size();
}

[[nodiscard]] bool ParseNumber(const std::string_view value, double& result) {
    if (value.empty() || value.size() > DataTable::kMaximumStringBytes) {
        return false;
    }
    std::string copy(value);
    char* end = nullptr;
    errno = 0;
    result = std::strtod(copy.c_str(), &end);
    if (errno == ERANGE || end != copy.c_str() + copy.size() || !std::isfinite(result)) {
        return false;
    }
    return true;
}

[[nodiscard]] bool ConvertValue(const CsvField& field,
                                   const DataTableColumn& column,
                                   DataTableValue& value,
                                   std::vector<DataTableDiagnostic>& diagnostics,
                                   bool& diagnosticsTruncated) {
    if (field.value.size() > DataTable::kMaximumStringBytes) {
        AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::ValueOutOfBounds,
                         field.line, field.column, "The field exceeds the supported string byte limit.");
        return false;
    }
    switch (column.type) {
        case DataTableColumnType::Boolean: {
            bool parsed = false;
            if (!ParseBoolean(field.value, parsed)) {
                AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::InvalidValue,
                                 field.line, field.column, "Expected lowercase true or false for column " + column.name + ".");
                return false;
            }
            value = parsed;
            return true;
        }
        case DataTableColumnType::Integer: {
            std::int64_t parsed = 0;
            if (!ParseInteger(field.value, parsed)) {
                AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::InvalidValue,
                                 field.line, field.column, "Expected a signed 64-bit integer for column " + column.name + ".");
                return false;
            }
            value = parsed;
            return true;
        }
        case DataTableColumnType::Number: {
            double parsed = 0.0;
            if (!ParseNumber(field.value, parsed)) {
                AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::InvalidValue,
                                 field.line, field.column, "Expected a finite number for column " + column.name + ".");
                return false;
            }
            value = parsed;
            return true;
        }
        case DataTableColumnType::String:
            value = field.value;
            return true;
    }
    return false;
}

} // namespace

DataTable::DataTable(std::string name) : m_name(std::move(name)) {
    if (m_name.size() > kMaximumIdentifierBytes || (!m_name.empty() && !IsBoundedIdentifier(m_name))) {
        m_name.clear();
    }
}

bool DataTable::IsBoundedIdentifier(const std::string_view value) noexcept {
    if (value.empty() || value.size() > kMaximumIdentifierBytes) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.';
    });
}

bool DataTable::IsValueCompatible(const DataTableValue& value,
                                        const DataTableColumnType type) noexcept {
    switch (type) {
        case DataTableColumnType::Boolean:
            return std::holds_alternative<bool>(value);
        case DataTableColumnType::Integer:
            return std::holds_alternative<std::int64_t>(value);
        case DataTableColumnType::Number:
            return std::holds_alternative<double>(value) && std::isfinite(std::get<double>(value));
        case DataTableColumnType::String:
            return std::holds_alternative<std::string>(value) &&
                   std::get<std::string>(value).size() <= kMaximumStringBytes;
    }
    return false;
}

bool DataTable::ValidateRow(const std::string_view identifier,
                                  const std::vector<DataTableValue>& values) const noexcept {
    if (!IsBoundedIdentifier(identifier) || values.size() != m_columns.size() ||
        m_rows.size() >= kMaximumRows) {
        return false;
    }
    if (FindRow(identifier) != nullptr) {
        return false;
    }
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (!IsValueCompatible(values[index], m_columns[index].type)) {
            return false;
        }
    }
    return true;
}

bool DataTable::DefineColumn(std::string name, const DataTableColumnType type) {
    const bool validType = type == DataTableColumnType::Boolean ||
                           type == DataTableColumnType::Integer ||
                           type == DataTableColumnType::Number ||
                           type == DataTableColumnType::String;
    if (!validType || !IsBoundedIdentifier(name) || m_columns.size() >= kMaximumColumns ||
        std::any_of(m_columns.begin(), m_columns.end(), [&name](const DataTableColumn& column) {
            return column.name == name;
        })) {
        return false;
    }
    m_columns.push_back(DataTableColumn{std::move(name), type});
    IncrementGeneration();
    return true;
}

bool DataTable::AddRow(std::string identifier, std::vector<DataTableValue> values) {
    if (!ValidateRow(identifier, values)) {
        return false;
    }
    m_rows.push_back(DataTableRow{std::move(identifier), std::move(values)});
    IncrementGeneration();
    return true;
}

bool DataTable::ClearRows() noexcept {
    if (m_rows.empty()) {
        return false;
    }
    m_rows.clear();
    IncrementGeneration();
    return true;
}

const DataTableRow* DataTable::FindRow(const std::string_view identifier) const noexcept {
    const auto iterator = std::find_if(m_rows.begin(), m_rows.end(), [identifier](const DataTableRow& row) {
        return row.identifier == identifier;
    });
    return iterator == m_rows.end() ? nullptr : &*iterator;
}

void DataTable::SetDiagnostics(std::vector<DataTableDiagnostic> diagnostics, const bool truncated) noexcept {
    m_diagnostics = std::move(diagnostics);
    m_diagnosticsTruncated = truncated;
}

void DataTable::IncrementGeneration() noexcept {
    if (m_generation < std::numeric_limits<std::uint64_t>::max()) {
        ++m_generation;
    }
}

std::size_t DataTable::GetResidentBytes() const noexcept {
    auto n = sizeof(*this) + m_name.capacity() + 1 + m_columns.capacity()*sizeof(DataTableColumn)
        + m_rows.capacity()*sizeof(DataTableRow) + m_diagnostics.capacity()*sizeof(DataTableDiagnostic);
    for (const auto& c : m_columns) n += c.name.capacity()+1;
    for (const auto& r : m_rows) {
        n += r.identifier.capacity()+1+r.values.capacity()*sizeof(DataTableValue);
        for (const auto& v : r.values) if (const auto* text = std::get_if<std::string>(&v)) n += text->capacity()+1;
    }
    for (const auto& d : m_diagnostics) n += d.message.capacity()+1;
    return n;
}

DataTableSnapshot DataTable::GetSnapshot() const {
    return DataTableSnapshot{m_generation, m_name, m_columns, m_rows, m_diagnostics, m_diagnosticsTruncated};
}

bool DataTable::ImportCsv(const std::string_view document) {
    return DataTableCsvImporter::Import(document, *this);
}

bool DataTable::ImportTsv(const std::string_view document) {
    return DataTableTsvImporter::Import(document, *this);
}

bool DataTable::ImportJson(const std::string_view document) {
    return DataTableJsonImporter::Import(document, *this);
}

bool DataTableCsvImporter::Import(const std::string_view document, DataTable& table) {
    return ImportDelimited(document, ',', table);
}

bool DataTableCsvImporter::ImportDelimited(const std::string_view document, const char delimiter,
                                                  DataTable& table) {
    if (delimiter == '\n' || delimiter == '\r' || delimiter == '"') {
        return false;
    }
    std::vector<DataTableDiagnostic> diagnostics;
    bool diagnosticsTruncated = false;
    std::vector<CsvRecord> records;
    if (!ParseDelimitedRecords(document, delimiter, records, diagnostics, diagnosticsTruncated)) {
        table.SetDiagnostics(std::move(diagnostics), diagnosticsTruncated);
        table.IncrementGeneration();
        return false;
    }
    const CsvRecord& header = records.front();
    if (header.size() != table.m_columns.size() + 1U || header.front().value != "id") {
        AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::HeaderMismatch,
                         header.front().line, header.front().column,
                         "The CSV header must start with id and contain the schema columns in order.");
        table.SetDiagnostics(std::move(diagnostics), diagnosticsTruncated);
        table.IncrementGeneration();
        return false;
    }
    for (std::size_t index = 0U; index < table.m_columns.size(); ++index) {
        if (header[index + 1U].value != table.m_columns[index].name) {
            AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::HeaderMismatch,
                             header[index + 1U].line, header[index + 1U].column,
                             "The CSV header does not match the declared schema at column " +
                                 table.m_columns[index].name + ".");
            table.SetDiagnostics(std::move(diagnostics), diagnosticsTruncated);
            table.IncrementGeneration();
            return false;
        }
    }

    std::vector<DataTableRow> importedRows;
    importedRows.reserve(records.size() - 1U);
    for (std::size_t recordIndex = 1U; recordIndex < records.size(); ++recordIndex) {
        const CsvRecord& record = records[recordIndex];
        if (record.size() != table.m_columns.size() + 1U) {
            AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::InvalidRow,
                             record.empty() ? 1U : record.front().line,
                             record.empty() ? 1U : record.front().column,
                             "The CSV row does not contain exactly one id and one value per schema column.");
            continue;
        }
        if (!DataTable::IsBoundedIdentifier(record.front().value) ||
            std::any_of(importedRows.begin(), importedRows.end(), [&record](const DataTableRow& row) {
                return row.identifier == record.front().value;
            })) {
            AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::DuplicateRow,
                             record.front().line, record.front().column,
                             "The CSV row identifier is invalid or duplicated.");
            continue;
        }
        std::vector<DataTableValue> values;
        values.reserve(table.m_columns.size());
        bool rowValid = true;
        for (std::size_t columnIndex = 0U; columnIndex < table.m_columns.size(); ++columnIndex) {
            DataTableValue value;
            if (!ConvertValue(record[columnIndex + 1U], table.m_columns[columnIndex], value,
                                 diagnostics, diagnosticsTruncated)) {
                rowValid = false;
                continue;
            }
            values.push_back(std::move(value));
        }
        if (!rowValid) {
            continue;
        }
        if (importedRows.size() >= DataTable::kMaximumRows) {
            AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::LimitExceeded,
                             record.front().line, record.front().column,
                             "The CSV document exceeds the maximum row limit.");
            break;
        }
        importedRows.push_back(DataTableRow{record.front().value, std::move(values)});
    }

    if (!diagnostics.empty()) {
        table.SetDiagnostics(std::move(diagnostics), diagnosticsTruncated);
        table.IncrementGeneration();
        return false;
    }
    table.m_rows = std::move(importedRows);
    table.SetDiagnostics({}, false);
    table.IncrementGeneration();
    return true;
}

bool DataTableTsvImporter::Import(const std::string_view document, DataTable& table) {
    return DataTableCsvImporter::ImportDelimited(document, '\t', table);
}

bool DataTableJsonImporter::Import(const std::string_view document, DataTable& table) {
    std::vector<DataTableDiagnostic> diagnostics;
    bool diagnosticsTruncated = false;
    const auto fail = [&]() {
        table.SetDiagnostics(std::move(diagnostics), diagnosticsTruncated);
        table.IncrementGeneration();
        return false;
    };
    if (document.empty() || document.size() > DataTable::kMaximumDocumentBytes) {
        AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::LimitExceeded, 1U, 1U,
                         document.empty() ? "The JSON document is empty." : "The JSON document exceeds the supported byte limit.");
        return fail();
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(document.begin(), document.end());
    } catch (const nlohmann::json::exception&) {
        AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::InvalidDocument, 1U, 1U,
                         "The JSON document is malformed.");
        return fail();
    }
    if (!root.is_array() || root.size() > DataTable::kMaximumRows) {
        AddDiagnostic(diagnostics, diagnosticsTruncated,
                         root.is_array() ? DataTableDiagnosticCode::LimitExceeded
                                         : DataTableDiagnosticCode::InvalidDocument,
                         1U, 1U, root.is_array() ? "The JSON document exceeds the maximum row limit."
                                                  : "The JSON root must be an array of row objects.");
        return fail();
    }

    std::vector<DataTableRow> importedRows;
    importedRows.reserve(root.size());
    for (std::size_t rowIndex = 0U; rowIndex < root.size(); ++rowIndex) {
        const nlohmann::json& object = root[rowIndex];
        const std::size_t line = rowIndex + 1U;
        if (!object.is_object() || object.size() != table.m_columns.size() + 1U ||
            !object.contains("id") || !object.at("id").is_string()) {
            AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::InvalidRow,
                             line, 1U, "Each JSON row must contain exactly one string id and every schema column.");
            continue;
        }
        const std::string identifier = object.at("id").get<std::string>();
        if (!DataTable::IsBoundedIdentifier(identifier) ||
            std::any_of(importedRows.begin(), importedRows.end(), [&identifier](const DataTableRow& row) {
                return row.identifier == identifier;
            })) {
            AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::DuplicateRow,
                             line, 1U, "The JSON row identifier is invalid or duplicated.");
            continue;
        }

        std::vector<DataTableValue> values;
        values.reserve(table.m_columns.size());
        bool rowValid = true;
        for (const DataTableColumn& column : table.m_columns) {
            if (!object.contains(column.name)) {
                AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::InvalidRow,
                                 line, 1U, "The JSON row is missing schema column " + column.name + ".");
                rowValid = false;
                continue;
            }
            const nlohmann::json& jsonValue = object.at(column.name);
            try {
                switch (column.type) {
                    case DataTableColumnType::Boolean:
                        if (!jsonValue.is_boolean()) {
                            throw nlohmann::json::type_error::create(302, "expected boolean", &jsonValue);
                        }
                        values.emplace_back(jsonValue.get<bool>());
                        break;
                    case DataTableColumnType::Integer:
                        if (!jsonValue.is_number_integer()) {
                            throw nlohmann::json::type_error::create(302, "expected signed integer", &jsonValue);
                        }
                        values.emplace_back(jsonValue.get<std::int64_t>());
                        break;
                    case DataTableColumnType::Number: {
                        if (!jsonValue.is_number()) {
                            throw nlohmann::json::type_error::create(302, "expected number", &jsonValue);
                        }
                        const double value = jsonValue.get<double>();
                        if (!std::isfinite(value)) {
                            throw nlohmann::json::type_error::create(302, "expected finite number", &jsonValue);
                        }
                        values.emplace_back(value);
                        break;
                    }
                    case DataTableColumnType::String: {
                        if (!jsonValue.is_string()) {
                            throw nlohmann::json::type_error::create(302, "expected string", &jsonValue);
                        }
                        const std::string value = jsonValue.get<std::string>();
                        if (value.size() > DataTable::kMaximumStringBytes) {
                            throw nlohmann::json::type_error::create(302, "string exceeds bound", &jsonValue);
                        }
                        values.emplace_back(value);
                        break;
                    }
                }
            } catch (const nlohmann::json::exception&) {
                AddDiagnostic(diagnostics, diagnosticsTruncated, DataTableDiagnosticCode::InvalidValue,
                                 line, 1U, "The JSON value does not match schema column " + column.name + ".");
                rowValid = false;
            }
        }
        if (rowValid) {
            importedRows.push_back(DataTableRow{identifier, std::move(values)});
        }
    }

    if (!diagnostics.empty()) {
        return fail();
    }
    table.m_rows = std::move(importedRows);
    table.SetDiagnostics({}, false);
    table.IncrementGeneration();
    return true;
}

namespace {

[[nodiscard]] const char* ColumnTypeName(const DataTableColumnType type) noexcept {
    switch (type) {
        case DataTableColumnType::Boolean:
            return "boolean";
        case DataTableColumnType::Integer:
            return "integer";
        case DataTableColumnType::Number:
            return "number";
        case DataTableColumnType::String:
            return "string";
    }
    return "";
}

[[nodiscard]] std::optional<DataTableColumnType> ParseColumnType(const std::string_view type) noexcept {
    if (type == "boolean") {
        return DataTableColumnType::Boolean;
    }
    if (type == "integer") {
        return DataTableColumnType::Integer;
    }
    if (type == "number") {
        return DataTableColumnType::Number;
    }
    if (type == "string") {
        return DataTableColumnType::String;
    }
    return std::nullopt;
}

[[nodiscard]] nlohmann::json SerializeValue(const DataTableValue& value) {
    nlohmann::json encoded = nlohmann::json::object();
    std::visit([&encoded](const auto& current) {
        using ValueType = std::decay_t<decltype(current)>;
        if constexpr (std::is_same_v<ValueType, bool>) {
            encoded["type"] = "boolean";
        } else if constexpr (std::is_same_v<ValueType, std::int64_t>) {
            encoded["type"] = "integer";
        } else if constexpr (std::is_same_v<ValueType, double>) {
            encoded["type"] = "number";
        } else {
            encoded["type"] = "string";
        }
        encoded["value"] = current;
    }, value);
    return encoded;
}

[[nodiscard]] bool IsCatalogName(const std::string_view name) noexcept {
    if (name.empty() || name.size() > DataTable::kMaximumIdentifierBytes) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](const char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '_' || character == '-' || character == '.';
    });
}

} // namespace

bool DataTableAssetSerializer::Serialize(const DataTable& table, std::string& document) {
    const DataTableSnapshot snapshot = table.GetSnapshot();
    if (!IsCatalogName(snapshot.name)) {
        return false;
    }
    nlohmann::json envelope = nlohmann::json::object();
    envelope["format"] = "uve.data_table";
    envelope["version"] = 1U;
    envelope["name"] = snapshot.name;
    envelope["columns"] = nlohmann::json::array();
    for (const DataTableColumn& column : snapshot.columns) {
        envelope["columns"].push_back({{"name", column.name}, {"type", ColumnTypeName(column.type)}});
    }
    envelope["rows"] = nlohmann::json::array();
    for (const DataTableRow& row : snapshot.rows) {
        nlohmann::json values = nlohmann::json::array();
        for (const DataTableValue& value : row.values) {
            values.push_back(SerializeValue(value));
        }
        envelope["rows"].push_back({{"id", row.identifier}, {"values", std::move(values)}});
    }
    const std::string serialized = envelope.dump();
    if (serialized.size() > DataTable::kMaximumDocumentBytes) {
        return false;
    }
    document = serialized;
    return true;
}

bool DataTableAssetSerializer::Deserialize(const std::string_view document, DataTable& table) {
    if (document.empty() || document.size() > DataTable::kMaximumDocumentBytes) {
        return false;
    }
    try {
        const nlohmann::json envelope = nlohmann::json::parse(document.begin(), document.end());

        if (!envelope.is_object() || envelope.size() != 5U || envelope.value("format", "") != "uve.data_table" ||
            envelope.value("version", 0U) != 1U || !envelope.contains("name") || !envelope.contains("columns") ||
            !envelope.contains("rows") || !envelope.at("name").is_string() ||
            !envelope.at("columns").is_array() || !envelope.at("rows").is_array()) {
            return false;
        }
        const std::string name = envelope.at("name").get<std::string>();
        DataTable candidate(name);
        if (candidate.GetSnapshot().name != name || envelope.at("columns").size() > DataTable::kMaximumColumns ||
            envelope.at("rows").size() > DataTable::kMaximumRows) {
            return false;
        }
        for (const nlohmann::json& column : envelope.at("columns")) {
            if (!column.is_object() || column.size() != 2U || !column.contains("name") || !column.contains("type") ||
                !column.at("name").is_string() || !column.at("type").is_string()) {
                return false;
            }
            const std::optional<DataTableColumnType> type = ParseColumnType(column.at("type").get<std::string>());
            if (!type.has_value() || !candidate.DefineColumn(column.at("name").get<std::string>(), *type)) {
                return false;
            }
        }
        for (const nlohmann::json& row : envelope.at("rows")) {
            if (!row.is_object() || row.size() != 2U || !row.contains("id") || !row.contains("values") ||
                !row.at("id").is_string() || !row.at("values").is_array() ||
                row.at("values").size() != candidate.GetSnapshot().columns.size()) {
                return false;
            }
            std::vector<DataTableValue> values;
            values.reserve(row.at("values").size());
            for (const nlohmann::json& encoded : row.at("values")) {
                if (!encoded.is_object() || encoded.size() != 2U || !encoded.contains("type") || !encoded.contains("value") ||
                    !encoded.at("type").is_string()) {
                    return false;
                }
                const std::string type = encoded.at("type").get<std::string>();
                const nlohmann::json& value = encoded.at("value");
                if (type == "boolean" && value.is_boolean()) {
                    values.emplace_back(value.get<bool>());
                } else if (type == "integer" && value.is_number_integer()) {
                    values.emplace_back(value.get<std::int64_t>());
                } else if (type == "number" && value.is_number() && std::isfinite(value.get<double>())) {
                    values.emplace_back(value.get<double>());
                } else if (type == "string" && value.is_string() && value.get<std::string>().size() <= DataTable::kMaximumStringBytes) {
                    values.emplace_back(value.get<std::string>());
                } else {
                    return false;
                }
            }
            if (!candidate.AddRow(row.at("id").get<std::string>(), std::move(values))) {
                return false;
            }
        }
        table = std::move(candidate);
        return true;
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool DataTableCatalog::Upsert(const DataTableSnapshot& table) {
    if (!IsCatalogName(table.name)) {
        return false;
    }
    const DataTableCatalogEntry entry{table.name, table.generation, table.columns.size(), table.rows.size(),
                                         table.diagnostics.empty()};
    const auto iterator = std::lower_bound(m_entries.begin(), m_entries.end(), entry.name,
                                           [](const DataTableCatalogEntry& current, const std::string& name) {
                                               return current.name < name;
                                           });
    if (iterator != m_entries.end() && iterator->name == entry.name) {
        if (*iterator == entry) {
            return false;
        }
        *iterator = entry;
    } else {
        if (m_entries.size() >= kMaximumEntries) {
            return false;
        }
        m_entries.insert(iterator, entry);
    }
    IncrementGeneration();
    return true;
}

bool DataTableCatalog::Remove(const std::string_view name) {
    const auto iterator = std::lower_bound(m_entries.begin(), m_entries.end(), name,
                                           [](const DataTableCatalogEntry& current, const std::string_view target) {
                                               return current.name < target;
                                           });
    if (iterator == m_entries.end() || iterator->name != name) {
        return false;
    }
    m_entries.erase(iterator);
    IncrementGeneration();
    return true;
}

DataTableCatalogSnapshot DataTableCatalog::GetSnapshot() const {
    return DataTableCatalogSnapshot{m_generation, m_entries, false};
}

bool DataTableCatalog::Contains(const std::string_view name) const noexcept {
    return std::any_of(m_entries.begin(), m_entries.end(), [name](const DataTableCatalogEntry& entry) {
        return entry.name == name;
    });
}

void DataTableCatalog::IncrementGeneration() noexcept {
    if (m_generation < std::numeric_limits<std::uint64_t>::max()) {
        ++m_generation;
    }
}

} // namespace UVE::Asset