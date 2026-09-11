#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace search {

using XlsxCellValue = std::function<std::string(size_t row, size_t column)>;
using XlsxCancelCheck = std::function<bool()>;
using XlsxProgress = std::function<void(size_t completed, size_t total)>;

// Writes a complete OOXML workbook to an already-open binary file. All cell
// values are emitted as inline Unicode strings so identifiers keep leading
// zeroes and Excel never has to guess a CSV character encoding.
bool write_xlsx(FILE* file,
                const std::string& sheet_name,
                const std::vector<std::string>& headers,
                size_t row_count,
                const XlsxCellValue& cell_value,
                const XlsxCancelCheck& should_cancel,
                const XlsxProgress& progress,
                size_t& rows_written,
                bool& canceled,
                std::string& error);

// Convenience wrapper for the synchronous exports used by smaller result
// pages. It owns the destination file and removes an incomplete workbook when
// generation fails.
bool write_xlsx_file(const std::wstring& path,
                     const std::string& sheet_name,
                     const std::vector<std::string>& headers,
                     size_t row_count,
                     const XlsxCellValue& cell_value,
                     std::string& error);

}  // namespace search
