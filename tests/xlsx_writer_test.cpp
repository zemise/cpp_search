#include "xlsx_writer.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::string readAll(FILE* file) {
    check(fseek(file, 0, SEEK_END) == 0, "seek to end");
    const long size = ftell(file);
    check(size > 0, "workbook should not be empty");
    check(fseek(file, 0, SEEK_SET) == 0, "seek to start");
    std::string bytes(static_cast<size_t>(size), '\0');
    check(fread(bytes.data(), 1, bytes.size(), file) == bytes.size(), "read workbook");
    return bytes;
}

}  // namespace

int main(int argc, char** argv) {
    FILE* file = argc > 1 ? fopen(argv[1], "w+b") : tmpfile();
    check(file != nullptr, "create temporary workbook");

    const std::vector<std::string> headers = {"条形码", "姓名", "备注"};
    const std::vector<std::vector<std::string>> rows = {
        {"001234", "张三", "含有 & < > 符号"},
        {"000002", "李四", "第一行\n第二行"},
    };
    size_t written = 0;
    bool canceled = false;
    std::string error;
    const bool ok = search::write_xlsx(
        file, "已签收条码", headers, rows.size(),
        [&rows](size_t row, size_t column) -> std::string {
            return rows[row][column];
        }, {}, {}, written, canceled, error);

    check(ok, error.c_str());
    check(!canceled, "export should not be canceled");
    check(written == rows.size(), "all rows should be written");
    const std::string bytes = readAll(file);
    fclose(file);

    check(bytes.compare(0, 2, "PK") == 0, "XLSX should be a ZIP package");
    check(bytes.find("[Content_Types].xml") != std::string::npos, "content types entry");
    check(bytes.find("xl/worksheets/sheet1.xml") != std::string::npos, "worksheet entry");
    check(bytes.find("条形码") != std::string::npos, "UTF-8 Chinese header");
    check(bytes.find("001234") != std::string::npos, "leading zero identifier");
    check(bytes.find("含有 &amp; &lt; &gt; 符号") != std::string::npos, "XML escaping");
    check(bytes.find("<c r=\"A2\" t=\"inlineStr\">") != std::string::npos,
          "data should be stored as inline strings");
    check(bytes.rfind("PK\x05\x06") != std::string::npos, "ZIP end record");

    FILE* canceled_file = tmpfile();
    check(canceled_file != nullptr, "create canceled export file");
    written = 0;
    canceled = false;
    error.clear();
    const bool canceled_ok = search::write_xlsx(
        canceled_file, "已签收条码", headers, rows.size(),
        [&rows](size_t row, size_t column) -> std::string {
            return rows[row][column];
        },
        [] { return true; }, {}, written, canceled, error);
    fclose(canceled_file);
    check(!canceled_ok, "canceled export should not report success");
    check(canceled, "canceled export should set canceled flag");
    check(written == 0, "canceled export should stop before writing data rows");

    if (argc > 1) {
        const std::string output_path = argv[1];
        const std::wstring wide_output_path(output_path.begin(), output_path.end());
        error.clear();
        check(search::write_xlsx_file(
                  wide_output_path, "已签收条码", headers, rows.size(),
                  [&rows](size_t row, size_t column) -> std::string {
                      return rows[row][column];
                  }, error),
              error.c_str());
    }

    std::cout << "xlsx writer tests passed\n";
    return 0;
}
