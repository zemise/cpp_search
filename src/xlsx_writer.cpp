#include "xlsx_writer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace search {
namespace {

constexpr size_t XLSX_MAX_ROWS = 1048576;
constexpr size_t XLSX_DATA_ROWS_PER_SHEET = XLSX_MAX_ROWS - 1;
constexpr uint64_t ZIP32_MAX = (std::numeric_limits<uint32_t>::max)();

uint32_t updateCrc32(uint32_t crc, const char* data, size_t size) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> values{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1U) ? (value >> 1U) ^ 0xEDB88320U : value >> 1U;
            }
            values[i] = value;
        }
        return values;
    }();
    for (size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ static_cast<unsigned char>(data[i])) & 0xFFU] ^ (crc >> 8U);
    }
    return crc;
}

struct ZipEntry {
    std::string name;
    uint32_t crc = 0;
    uint32_t size = 0;
    uint32_t local_offset = 0;
};

class StoredZipWriter {
public:
    explicit StoredZipWriter(FILE* file) : file_(file) {}

    bool begin(const std::string& name) {
        if (!ok_ || active_ || name.size() > 0xFFFFU || position_ > ZIP32_MAX) return fail();
        active_ = true;
        current_ = {};
        current_.name = name;
        current_.local_offset = static_cast<uint32_t>(position_);
        current_crc_ = 0xFFFFFFFFU;
        current_size_ = 0;
        return raw32(0x04034B50U) && raw16(20) && raw16(0x0808) && raw16(0) &&
               raw16(0) && raw16(0) && raw32(0) && raw32(0) && raw32(0) &&
               raw16(static_cast<uint16_t>(name.size())) && raw16(0) && raw(name);
    }

    bool append(std::string_view data) {
        if (!ok_ || !active_) return fail();
        if (current_size_ + data.size() > ZIP32_MAX) return fail();
        if (!raw(data)) return false;
        current_crc_ = updateCrc32(current_crc_, data.data(), data.size());
        current_size_ += data.size();
        return true;
    }

    bool end() {
        if (!ok_ || !active_) return fail();
        current_.crc = current_crc_ ^ 0xFFFFFFFFU;
        current_.size = static_cast<uint32_t>(current_size_);
        active_ = false;
        if (!raw32(0x08074B50U) || !raw32(current_.crc) ||
            !raw32(current_.size) || !raw32(current_.size)) {
            return false;
        }
        entries_.push_back(current_);
        return true;
    }

    bool add(const std::string& name, std::string_view data) {
        return begin(name) && append(data) && end();
    }

    bool finish() {
        if (!ok_ || active_ || position_ > ZIP32_MAX || entries_.size() > 0xFFFFU) {
            return fail();
        }
        const uint32_t central_offset = static_cast<uint32_t>(position_);
        for (const auto& entry : entries_) {
            if (!raw32(0x02014B50U) || !raw16(20) || !raw16(20) || !raw16(0x0808) ||
                !raw16(0) || !raw16(0) || !raw16(0) || !raw32(entry.crc) ||
                !raw32(entry.size) || !raw32(entry.size) ||
                !raw16(static_cast<uint16_t>(entry.name.size())) || !raw16(0) ||
                !raw16(0) || !raw16(0) || !raw16(0) || !raw32(0) ||
                !raw32(entry.local_offset) || !raw(entry.name)) {
                return false;
            }
        }
        if (position_ > ZIP32_MAX) return fail();
        const uint32_t central_size = static_cast<uint32_t>(position_ - central_offset);
        const uint16_t count = static_cast<uint16_t>(entries_.size());
        return raw32(0x06054B50U) && raw16(0) && raw16(0) && raw16(count) &&
               raw16(count) && raw32(central_size) && raw32(central_offset) && raw16(0);
    }

    bool ok() const { return ok_; }

private:
    bool fail() {
        ok_ = false;
        return false;
    }

    bool raw(std::string_view data) {
        if (!ok_) return false;
        if (data.empty()) return true;
        if (fwrite(data.data(), 1, data.size(), file_) != data.size()) return fail();
        position_ += data.size();
        return true;
    }

    bool raw16(uint16_t value) {
        const char bytes[] = {
            static_cast<char>(value & 0xFFU), static_cast<char>((value >> 8U) & 0xFFU)};
        return raw(std::string_view(bytes, sizeof(bytes)));
    }

    bool raw32(uint32_t value) {
        const char bytes[] = {
            static_cast<char>(value & 0xFFU), static_cast<char>((value >> 8U) & 0xFFU),
            static_cast<char>((value >> 16U) & 0xFFU), static_cast<char>((value >> 24U) & 0xFFU)};
        return raw(std::string_view(bytes, sizeof(bytes)));
    }

    FILE* file_ = nullptr;
    bool ok_ = true;
    bool active_ = false;
    uint64_t position_ = 0;
    uint32_t current_crc_ = 0;
    uint64_t current_size_ = 0;
    ZipEntry current_;
    std::vector<ZipEntry> entries_;
};

std::string xmlEscape(std::string_view text, bool attribute = false) {
    std::string escaped;
    escaped.reserve(text.size() + 16);
    for (const unsigned char ch : text) {
        if (ch < 0x20U && ch != '\t' && ch != '\n' && ch != '\r') continue;
        switch (ch) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += attribute ? "&quot;" : "\""; break;
            case '\'': escaped += attribute ? "&apos;" : "'"; break;
            default: escaped.push_back(static_cast<char>(ch)); break;
        }
    }
    return escaped;
}

std::string columnName(size_t column) {
    std::string name;
    for (++column; column > 0; column = (column - 1) / 26) {
        name.push_back(static_cast<char>('A' + (column - 1) % 26));
    }
    std::reverse(name.begin(), name.end());
    return name;
}

std::string sheetTitle(const std::string& base, size_t index) {
    if (index == 0) return base;
    return base + " (" + std::to_string(index + 1) + ")";
}

std::string workbookXml(const std::string& sheet_name, size_t sheet_count) {
    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                      "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                      "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
                      "<sheets>";
    for (size_t i = 0; i < sheet_count; ++i) {
        xml += "<sheet name=\"" + xmlEscape(sheetTitle(sheet_name, i), true) +
               "\" sheetId=\"" + std::to_string(i + 1) + "\" r:id=\"rId" +
               std::to_string(i + 1) + "\"/>";
    }
    xml += "</sheets></workbook>";
    return xml;
}

std::string workbookRelationships(size_t sheet_count) {
    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
    for (size_t i = 0; i < sheet_count; ++i) {
        xml += "<Relationship Id=\"rId" + std::to_string(i + 1) +
               "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
               "Target=\"worksheets/sheet" + std::to_string(i + 1) + ".xml\"/>";
    }
    xml += "<Relationship Id=\"rId" + std::to_string(sheet_count + 1) +
           "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
           "Target=\"styles.xml\"/></Relationships>";
    return xml;
}

std::string contentTypes(size_t sheet_count) {
    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                      "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                      "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                      "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
                      "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
                      "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>";
    for (size_t i = 0; i < sheet_count; ++i) {
        xml += "<Override PartName=\"/xl/worksheets/sheet" + std::to_string(i + 1) +
               ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>";
    }
    xml += "</Types>";
    return xml;
}

const char* ROOT_RELATIONSHIPS =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
    "</Relationships>";

const char* STYLES =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
    "<fonts count=\"2\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font>"
    "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>"
    "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill>"
    "<fill><patternFill patternType=\"gray125\"/></fill></fills>"
    "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
    "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
    "<cellXfs count=\"2\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
    "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/></cellXfs>"
    "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
    "</styleSheet>";

void appendInlineCell(std::string& row_xml, const std::string& reference,
                      std::string_view value, bool header) {
    if (value.empty() && !header) return;
    row_xml += "<c r=\"" + reference + "\"";
    if (header) row_xml += " s=\"1\"";
    row_xml += " t=\"inlineStr\"><is><t xml:space=\"preserve\">";
    row_xml += xmlEscape(value);
    row_xml += "</t></is></c>";
}

}  // namespace

bool write_xlsx(FILE* file,
                const std::string& sheet_name,
                const std::vector<std::string>& headers,
                size_t row_count,
                const XlsxCellValue& cell_value,
                const XlsxCancelCheck& should_cancel,
                const XlsxProgress& progress,
                size_t& rows_written,
                bool& canceled,
                std::string& error) {
    rows_written = 0;
    canceled = false;
    error.clear();
    if (!file || headers.empty() || !cell_value) {
        error = "invalid XLSX export arguments";
        return false;
    }

    const size_t sheet_count = (std::max)(size_t{1},
        (row_count + XLSX_DATA_ROWS_PER_SHEET - 1) / XLSX_DATA_ROWS_PER_SHEET);
    StoredZipWriter zip(file);
    if (!zip.add("[Content_Types].xml", contentTypes(sheet_count)) ||
        !zip.add("_rels/.rels", ROOT_RELATIONSHIPS) ||
        !zip.add("xl/workbook.xml", workbookXml(sheet_name, sheet_count)) ||
        !zip.add("xl/_rels/workbook.xml.rels", workbookRelationships(sheet_count)) ||
        !zip.add("xl/styles.xml", STYLES)) {
        error = "failed to write XLSX package metadata";
        return false;
    }

    const std::string last_column = columnName(headers.size() - 1);
    for (size_t sheet = 0; sheet < sheet_count; ++sheet) {
        const size_t first_row = sheet * XLSX_DATA_ROWS_PER_SHEET;
        const size_t rows_on_sheet = (std::min)(XLSX_DATA_ROWS_PER_SHEET, row_count - first_row);
        const std::string entry_name = "xl/worksheets/sheet" + std::to_string(sheet + 1) + ".xml";
        if (!zip.begin(entry_name) ||
            !zip.append("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
                        "<sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"1\" topLeftCell=\"A2\" "
                        "activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews><sheetData>")) {
            error = "failed to start XLSX worksheet";
            return false;
        }

        std::string row_xml = "<row r=\"1\">";
        for (size_t column = 0; column < headers.size(); ++column) {
            appendInlineCell(row_xml, columnName(column) + "1", headers[column], true);
        }
        row_xml += "</row>";
        if (!zip.append(row_xml)) {
            error = "failed to write XLSX header row";
            return false;
        }

        for (size_t local_row = 0; local_row < rows_on_sheet; ++local_row) {
            if (should_cancel && should_cancel()) {
                canceled = true;
                return false;
            }
            const size_t source_row = first_row + local_row;
            const size_t excel_row = local_row + 2;
            row_xml = "<row r=\"" + std::to_string(excel_row) + "\">";
            for (size_t column = 0; column < headers.size(); ++column) {
                appendInlineCell(row_xml, columnName(column) + std::to_string(excel_row),
                                 cell_value(source_row, column), false);
            }
            row_xml += "</row>";
            if (!zip.append(row_xml)) {
                error = "failed to write XLSX worksheet data";
                return false;
            }
            rows_written = source_row + 1;
            if (progress && (rows_written % 5000 == 0 || rows_written == row_count)) {
                progress(rows_written, row_count);
            }
        }

        const std::string footer = "</sheetData><autoFilter ref=\"A1:" + last_column +
                                   std::to_string(rows_on_sheet + 1) + "\"/></worksheet>";
        if (!zip.append(footer) || !zip.end()) {
            error = "failed to finish XLSX worksheet";
            return false;
        }
    }

    if (!zip.finish()) {
        error = "XLSX file is too large or could not be finalized";
        return false;
    }
    return true;
}

bool write_xlsx_file(const std::wstring& path,
                     const std::string& sheet_name,
                     const std::vector<std::string>& headers,
                     size_t row_count,
                     const XlsxCellValue& cell_value,
                     std::string& error) {
    const std::wstring temporary_path = path + L".lis-export.tmp";
    FILE* file = nullptr;
#ifdef _WIN32
    DeleteFileW(temporary_path.c_str());
#ifdef _MSC_VER
    _wfopen_s(&file, temporary_path.c_str(), L"wb");
#else
    file = _wfopen(temporary_path.c_str(), L"wb");
#endif
#else
    const std::string narrow_temporary_path(temporary_path.begin(), temporary_path.end());
    std::remove(narrow_temporary_path.c_str());
    file = fopen(narrow_temporary_path.c_str(), "wb");
#endif
    if (!file) {
        error = "could not create destination file";
        return false;
    }

    size_t rows_written = 0;
    bool canceled = false;
    bool ok = write_xlsx(file, sheet_name, headers, row_count, cell_value,
                         {}, {}, rows_written, canceled, error);
    if (fclose(file) != 0 && ok) {
        ok = false;
        error = "could not flush destination file";
    }
    if (!ok) {
#ifdef _WIN32
        DeleteFileW(temporary_path.c_str());
#else
        std::remove(narrow_temporary_path.c_str());
#endif
        return false;
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary_path.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary_path.c_str());
        error = "could not replace destination file";
        return false;
    }
#else
    const std::string narrow_path(path.begin(), path.end());
    if (std::rename(narrow_temporary_path.c_str(), narrow_path.c_str()) != 0) {
        std::remove(narrow_temporary_path.c_str());
        error = "could not replace destination file";
        return false;
    }
#endif
    return true;
}

}  // namespace search
