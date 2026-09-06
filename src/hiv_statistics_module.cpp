#include "hiv_statistics_module.h"

#ifdef _WIN32

#include "main_app.h"
#include "page_feedback.h"
#include "regular_report_module.h"
#include "resource.h"
#include "search_core.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#include <commctrl.h>
#include <commdlg.h>
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>

#include "tinf.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS = L"HivStatisticsModuleChild";
constexpr const wchar_t* WINDOW_TITLE = L"HIV 抗体检测统计";
constexpr const wchar_t* PROP_STATE = L"HivStatisticsSt";
constexpr const wchar_t* HIV_TEMPLATE_FILE = L"HIVStatisticsTemplate.docx";
constexpr int HIV_TEMPLATE_PLACEHOLDER_COUNT = 46;
constexpr UINT WM_HIV_STATS_LOADED = WM_APP + 0x551;
const COLORREF COLOR_POSITIVE_ROW = RGB(0xFA, 0xC0, 0xCB);
const COLORREF COLOR_TOTAL_ROW = RGB(0xE8, 0xF2, 0xFF);

enum DetailColumn {
    DetailMachine = 0,
    DetailSampleSource,
    DetailMethodology,
    DetailLabDepartment,
    DetailItemCode,
    DetailItemName,
    DetailReportNo,
    DetailBarcode,
    DetailSampleNo,
    DetailPatientNo,
    DetailName,
    DetailCompletedBloodApplyForms,
    DetailPatientType,
    DetailDept,
    DetailResult,
    DetailLowerBound,
    DetailUpperBound,
    DetailPositive,
    DetailReportTime,
};

struct ListColumn {
    const wchar_t* title;
    int width;
};

constexpr ListColumn SUMMARY_COLUMNS[] = {
    {L"样本来源分类", 360},
    {L"初筛检测数", 190},
    {L"初筛阳性数", 190},
    {L"复检数", 160},
    {L"复检阳性数", 190},
    {L"报告疫情检测数", 245},
    {L"报告疫情阳性数", 245},
};

constexpr ListColumn METHODOLOGY_SUMMARY_COLUMNS[] = {
    {L"方法学", 240},
    {L"初筛检测数", 190},
    {L"初筛阳性数", 190},
};

constexpr ListColumn DETAIL_COLUMNS[] = {
    {L"仪器", 240},
    {L"样本来源", 180},
    {L"方法学", 120},
    {L"检验科", 92},
    {L"项目代码", 112},
    {L"项目名称", 300},
    {L"报告号", 130},
    {L"条码号", 150},
    {L"样本号", 118},
    {L"病人号", 130},
    {L"姓名", 120},
    {L"已完结输血单申请号", 190},
    {L"病人类型", 120},
    {L"科室", 220},
    {L"结果", 170},
    {L"下限", 102},
    {L"上限", 102},
    {L"阳性", 82},
    {L"报告时间", 190},
};

constexpr const wchar_t* SUMMARY_ROWS[] = {
    L"术前检测",
    L"受血（制品）前检测",
    L"性病门诊",
    L"其他就诊检测",
    L"婚前检查（含涉外婚检）",
    L"孕产期检查",
    L"检测咨询",
    L"阳性者配偶或性伴检测",
    L"女性阳性者子女检测",
    L"职业暴露检测",
    L"娱乐场所人员检测",
    L"有偿供血（浆）人员检测",
    L"无偿供血人员检测",
    L"出入境人员体检",
    L"新兵体检",
    L"强制／劳教戒毒人员检测",
    L"妇教所／女劳收教人员检测",
    L"其他羁押人员体检",
    L"专题调育或其他",
    L"合计",
};

constexpr int SUMMARY_ROW_PREOPERATIVE = 0;
constexpr int SUMMARY_ROW_TRANSFUSION = 1;
constexpr int SUMMARY_ROW_STI_CLINIC = 2;
constexpr int SUMMARY_ROW_OTHER_VISIT = 3;
constexpr int SUMMARY_ROW_PRENATAL = 5;
constexpr int SUMMARY_ROW_TOTAL = static_cast<int>(std::size(SUMMARY_ROWS)) - 1;

constexpr int SUMMARY_DISPLAY_ROWS[] = {
    SUMMARY_ROW_TOTAL,
    SUMMARY_ROW_PREOPERATIVE,
    SUMMARY_ROW_TRANSFUSION,
    SUMMARY_ROW_STI_CLINIC,
    SUMMARY_ROW_OTHER_VISIT,
    4,
    SUMMARY_ROW_PRENATAL,
    6,
    7,
    8,
    9,
    10,
    11,
    12,
    13,
    14,
    15,
    16,
    17,
    18,
};

enum ControlId {
    IDC_YEAR = 6401,
    IDC_MONTH = 6402,
    IDC_QUERY = 6403,
    IDC_SUMMARY = 6404,
    IDC_DETAILS = 6405,
    IDC_STATUS = 6406,
    IDC_SOURCE = 6407,
    IDC_EXPORT = 6408,
    IDC_UPLOAD_TEMPLATE = 6409,
    IDC_EXPORT_DETAILS = 6410,
    IDC_METHODOLOGY_SUMMARY = 6411,
};

struct HivStatisticsState {
    ModuleContext ctx;
    HWND year = nullptr;
    HWND month = nullptr;
    HWND source = nullptr;
    HWND query = nullptr;
    HWND exportButton = nullptr;
    HWND exportDetailsButton = nullptr;
    HWND uploadTemplateButton = nullptr;
    HWND summary = nullptr;
    HWND methodologySummary = nullptr;
    HWND details = nullptr;
    HWND status = nullptr;
    search::PageFeedback feedback;
    HBRUSH bgBrush = nullptr;
    bool querying = false;
    bool hasLoadedResult = false;
    int detailSortColumn = -1;
    bool detailSortAscending = true;
    search::HivStatSummary statSummary;
    std::vector<search::HivStatDetailRow> rows;
};

struct HivQueryResult {
    bool ok = false;
    std::string error;
    search::HivStatSummary summary;
    std::vector<search::HivStatDetailRow> rows;
};

class ScopedComInit {
public:
    ScopedComInit() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ScopedComInit() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }

    ScopedComInit(const ScopedComInit&) = delete;
    ScopedComInit& operator=(const ScopedComInit&) = delete;

private:
    HRESULT result_ = E_FAIL;
};

int S(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}


HWND label(HWND parent, const wchar_t* text, int x, int y, int w, int h, DWORD align = SS_RIGHT) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | align,
                           x, y, w, h, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

HWND edit(HWND parent, int id, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

HWND combo(HWND parent, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                           x, y, w, h, parent, win32_control_id(id), GetModuleHandleW(nullptr), nullptr);
}

std::wstring textOf(HWND hwnd) {
    wchar_t buf[128]{};
    GetWindowTextW(hwnd, buf, 128);
    return buf;
}

int intText(HWND hwnd, int fallback) {
    const auto text = textOf(hwnd);
    if (text.empty()) return fallback;
    return _wtoi(text.c_str());
}

int selectedMonth(HWND combo) {
    const int idx = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    return idx >= 0 ? idx + 1 : 1;
}

std::wstring selectedComboText(HWND combo) {
    const int idx = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (idx < 0) return {};
    wchar_t buf[64]{};
    SendMessageW(combo, CB_GETLBTEXT, static_cast<WPARAM>(idx), reinterpret_cast<LPARAM>(buf));
    return buf;
}

void setStatus(HivStatisticsState* st, const std::wstring& text) {
    if (st) search::set_page_status(st->feedback, text);
}

int summaryScreeningCount(const search::HivStatSummary& summary, int row) {
    switch (row) {
        case SUMMARY_ROW_PREOPERATIVE: return summary.preoperative_screening_count;
        case SUMMARY_ROW_TRANSFUSION: return summary.transfusion_screening_count;
        case SUMMARY_ROW_STI_CLINIC: return summary.sti_clinic_screening_count;
        case SUMMARY_ROW_OTHER_VISIT: return summary.other_visit_screening_count;
        case SUMMARY_ROW_PRENATAL: return summary.prenatal_screening_count;
        default:
            return row == SUMMARY_ROW_TOTAL ? summary.screening_count : 0;
    }
}

int summaryPositiveCount(const search::HivStatSummary& summary, int row) {
    switch (row) {
        case SUMMARY_ROW_PREOPERATIVE: return summary.preoperative_positive_count;
        case SUMMARY_ROW_TRANSFUSION: return summary.transfusion_positive_count;
        case SUMMARY_ROW_STI_CLINIC: return summary.sti_clinic_positive_count;
        case SUMMARY_ROW_OTHER_VISIT: return summary.other_visit_positive_count;
        case SUMMARY_ROW_PRENATAL: return summary.prenatal_positive_count;
        default:
            return row == SUMMARY_ROW_TOTAL ? summary.positive_count : 0;
    }
}

bool chooseFolder(HWND owner, std::wstring& folder) {
    ScopedComInit com;
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = L"请选择 HIV 统计表导出文件夹";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    wchar_t path[MAX_PATH]{};
    const BOOL ok = SHGetPathFromIDListW(pidl, path);
    CoTaskMemFree(pidl);
    if (!ok || !path[0]) return false;
    folder = path;
    return true;
}

std::wstring moduleDirectory() {
    wchar_t path[MAX_PATH]{};
    const DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    std::wstring dir(path);
    const size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        dir.resize(pos);
    }
    return dir;
}

std::wstring joinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    if (dir.back() == L'\\' || dir.back() == L'/') return dir + name;
    return dir + L"\\" + name;
}

bool fileExists(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring hivTemplateDirectory() {
    return joinPath(moduleDirectory(), L"templates");
}

std::wstring hivDocxTemplatePath() {
    return joinPath(hivTemplateDirectory(), HIV_TEMPLATE_FILE);
}

bool readFileBytes(const std::wstring& path, std::vector<unsigned char>& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 0x7fffffff) {
        CloseHandle(file);
        return false;
    }
    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = bytes.empty() ||
                    ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    return ok && read == bytes.size();
}

bool writeFileBytes(const std::wstring& path, const std::vector<unsigned char>& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = bytes.empty() ||
                    WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == bytes.size();
}

uint16_t readLe16(const std::vector<unsigned char>& bytes, size_t pos) {
    return static_cast<uint16_t>(bytes[pos] | (bytes[pos + 1] << 8));
}

uint32_t readLe32(const std::vector<unsigned char>& bytes, size_t pos) {
    return static_cast<uint32_t>(bytes[pos]) |
           (static_cast<uint32_t>(bytes[pos + 1]) << 8) |
           (static_cast<uint32_t>(bytes[pos + 2]) << 16) |
           (static_cast<uint32_t>(bytes[pos + 3]) << 24);
}

void appendLe16(std::vector<unsigned char>& out, uint16_t value) {
    out.push_back(static_cast<unsigned char>(value & 0xff));
    out.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
}

void appendLe32(std::vector<unsigned char>& out, uint32_t value) {
    appendLe16(out, static_cast<uint16_t>(value & 0xffff));
    appendLe16(out, static_cast<uint16_t>((value >> 16) & 0xffff));
}

uint32_t crc32Bytes(const std::vector<unsigned char>& bytes) {
    uint32_t crc = 0xffffffffu;
    for (unsigned char byte : bytes) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xffffffffu;
}

std::vector<unsigned char> bytesFromString(const std::string& text) {
    return std::vector<unsigned char>(text.begin(), text.end());
}

struct ZipEntry {
    std::string name;
    uint16_t versionMadeBy = 20;
    uint16_t versionNeeded = 20;
    uint16_t flags = 0;
    uint16_t method = 0;
    uint16_t modTime = 0;
    uint16_t modDate = 0;
    uint16_t internalAttr = 0;
    uint32_t crc = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint32_t externalAttr = 0;
    uint32_t localOffset = 0;
    std::vector<unsigned char> data;
};

// Decompress raw DEFLATE data (RFC 1951) using tinf.
// Returns true on success; out receives the uncompressed bytes.
bool tinflateBytes(const unsigned char* src, size_t srcLen, std::vector<unsigned char>& out, size_t expectedSize) {
    if (srcLen == 0) { out.clear(); return true; }
    out.resize(expectedSize > 0 ? expectedSize : srcLen * 4);
    unsigned int dstLen = static_cast<unsigned int>(out.size());
    int rc = tinf_uncompress(out.data(), &dstLen, src, static_cast<unsigned int>(srcLen));
    if (rc == TINF_DATA_ERROR && dstLen > 0 && expectedSize > 0) {
        // Retry with exact size if the buffer was too small
        out.resize(expectedSize);
        dstLen = static_cast<unsigned int>(out.size());
        rc = tinf_uncompress(out.data(), &dstLen, src, static_cast<unsigned int>(srcLen));
    }
    if (rc != TINF_OK) return false;
    out.resize(dstLen);
    return true;
}

bool readZipEntries(const std::vector<unsigned char>& zip, std::vector<ZipEntry>& entries) {
    if (zip.size() < 22) return false;
    size_t eocd = std::string::npos;
    const size_t minPos = zip.size() > 0xffff + 22 ? zip.size() - (0xffff + 22) : 0;
    for (size_t pos = zip.size() - 22; pos >= minPos; --pos) {
        if (readLe32(zip, pos) == 0x06054b50u) {
            eocd = pos;
            break;
        }
        if (pos == 0) break;
    }
    if (eocd == std::string::npos) return false;
    const uint16_t count = readLe16(zip, eocd + 10);
    const uint32_t centralOffset = readLe32(zip, eocd + 16);
    size_t pos = centralOffset;
    for (uint16_t i = 0; i < count; ++i) {
        if (pos + 46 > zip.size() || readLe32(zip, pos) != 0x02014b50u) return false;
        ZipEntry entry;
        entry.versionMadeBy = readLe16(zip, pos + 4);
        entry.versionNeeded = readLe16(zip, pos + 6);
        entry.flags = readLe16(zip, pos + 8) & static_cast<uint16_t>(~0x0008u);
        entry.method = readLe16(zip, pos + 10);
        entry.modTime = readLe16(zip, pos + 12);
        entry.modDate = readLe16(zip, pos + 14);
        entry.crc = readLe32(zip, pos + 16);
        entry.compressedSize = readLe32(zip, pos + 20);
        entry.uncompressedSize = readLe32(zip, pos + 24);
        const uint16_t nameLen = readLe16(zip, pos + 28);
        const uint16_t extraLen = readLe16(zip, pos + 30);
        const uint16_t commentLen = readLe16(zip, pos + 32);
        entry.internalAttr = readLe16(zip, pos + 36);
        entry.externalAttr = readLe32(zip, pos + 38);
        entry.localOffset = readLe32(zip, pos + 42);
        if (pos + 46 + nameLen + extraLen + commentLen > zip.size()) return false;
        entry.name.assign(reinterpret_cast<const char*>(zip.data() + pos + 46), nameLen);

        const size_t local = entry.localOffset;
        if (local + 30 > zip.size() || readLe32(zip, local) != 0x04034b50u) return false;
        const uint16_t localNameLen = readLe16(zip, local + 26);
        const uint16_t localExtraLen = readLe16(zip, local + 28);
        const size_t dataOffset = local + 30 + localNameLen + localExtraLen;
        if (dataOffset + entry.compressedSize > zip.size()) return false;
        if (entry.method == 8 && entry.uncompressedSize > 0) {
            // DEFLATE compressed — decompress to get actual XML content
            std::vector<unsigned char> inflated;
            if (!tinflateBytes(zip.data() + dataOffset, entry.compressedSize,
                               inflated, entry.uncompressedSize)) {
                return false;
            }
            entry.data = std::move(inflated);
            entry.method = 0;
            entry.compressedSize = static_cast<uint32_t>(entry.data.size());
        } else {
            entry.data.assign(zip.begin() + dataOffset, zip.begin() + dataOffset + entry.compressedSize);
        }
        entries.push_back(std::move(entry));
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return !entries.empty();
}

int countSubstrings(const std::string& text, const std::string& needle) {
    if (needle.empty()) return 0;
    int count = 0;
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

bool isUsableHivDocxTemplate(const std::wstring& path, std::wstring* error = nullptr) {
    std::vector<unsigned char> zip;
    if (!readFileBytes(path, zip)) {
        if (error) *error = L"无法读取模版文件。";
        return false;
    }

    std::vector<ZipEntry> entries;
    if (!readZipEntries(zip, entries)) {
        if (error) *error = L"模版不是有效的 DOCX 文件。";
        return false;
    }

    for (const auto& entry : entries) {
        if (entry.name == "word/document.xml") {
            const std::string documentXml(entry.data.begin(), entry.data.end());
            const int placeholders = countSubstrings(documentXml, "{}");
            if (placeholders != HIV_TEMPLATE_PLACEHOLDER_COUNT) {
                if (error) {
                    wchar_t msg[160]{};
                    swprintf(msg, 160, L"模版占位符数量不匹配：当前 %d 个，应为 %d 个。",
                             placeholders, HIV_TEMPLATE_PLACEHOLDER_COUNT);
                    *error = msg;
                }
                return false;
            }
            return true;
        }
    }

    if (error) *error = L"模版缺少 word/document.xml。";
    return false;
}

std::wstring findHivDocxTemplate() {
    const std::wstring path = hivDocxTemplatePath();
    if (fileExists(path) && isUsableHivDocxTemplate(path)) return path;
    return {};
}

void updateExportButton(HivStatisticsState* st) {
    if (!st) return;
    if (st->exportButton) {
        EnableWindow(st->exportButton, st->hasLoadedResult && !findHivDocxTemplate().empty());
    }
    if (st->exportDetailsButton) {
        EnableWindow(st->exportDetailsButton, st->hasLoadedResult && !st->rows.empty());
    }
}

bool ensureDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) {
        return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool chooseDocxTemplate(HWND owner, std::wstring& file) {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Word 模版 (*.docx)\0*.docx\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"请选择 HIV 统计表 DOCX 模版";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;
    file = path;
    return true;
}

void uploadTemplate(HWND hwnd, HivStatisticsState* st) {
    std::wstring source;
    if (!chooseDocxTemplate(hwnd, source)) return;

    std::wstring error;
    if (!isUsableHivDocxTemplate(source, &error)) {
        MessageBoxW(hwnd, error.c_str(), WINDOW_TITLE, MB_ICONERROR);
        return;
    }

    const std::wstring dir = hivTemplateDirectory();
    if (!ensureDirectory(dir)) {
        MessageBoxW(hwnd, L"无法创建 templates 目录，请确认程序目录可写。", WINDOW_TITLE, MB_ICONERROR);
        return;
    }

    const std::wstring target = hivDocxTemplatePath();
    if (source != target && !CopyFileW(source.c_str(), target.c_str(), FALSE)) {
        MessageBoxW(hwnd, L"复制模版失败，请确认程序目录可写。", WINDOW_TITLE, MB_ICONERROR);
        return;
    }

    updateExportButton(st);
    setStatus(st, L"已上传 HIV 统计表模版：" + target);
    MessageBoxW(hwnd, L"模版已上传。", WINDOW_TITLE, MB_ICONINFORMATION);
}

void appendBytes(std::vector<unsigned char>& out, const std::string& text) {
    out.insert(out.end(), text.begin(), text.end());
}

std::string csvEscape(const std::string& text) {
    const bool quote = text.find_first_of(",\"\r\n") != std::string::npos;
    std::string out;
    out.reserve(text.size() + 2);
    if (quote) out.push_back('"');
    for (const char ch : text) {
        if (ch == '"') {
            out += "\"\"";
        } else {
            out.push_back(ch);
        }
    }
    if (quote) out.push_back('"');
    return out;
}

void appendZipLocalHeader(std::vector<unsigned char>& out, const ZipEntry& entry) {
    appendLe32(out, 0x04034b50u);
    appendLe16(out, entry.versionNeeded);
    appendLe16(out, entry.flags);
    appendLe16(out, entry.method);
    appendLe16(out, entry.modTime);
    appendLe16(out, entry.modDate);
    appendLe32(out, entry.crc);
    appendLe32(out, entry.compressedSize);
    appendLe32(out, entry.uncompressedSize);
    appendLe16(out, static_cast<uint16_t>(entry.name.size()));
    appendLe16(out, 0);
    appendBytes(out, entry.name);
    out.insert(out.end(), entry.data.begin(), entry.data.end());
}

void appendZipCentralHeader(std::vector<unsigned char>& out, const ZipEntry& entry) {
    appendLe32(out, 0x02014b50u);
    appendLe16(out, entry.versionMadeBy);
    appendLe16(out, entry.versionNeeded);
    appendLe16(out, entry.flags);
    appendLe16(out, entry.method);
    appendLe16(out, entry.modTime);
    appendLe16(out, entry.modDate);
    appendLe32(out, entry.crc);
    appendLe32(out, entry.compressedSize);
    appendLe32(out, entry.uncompressedSize);
    appendLe16(out, static_cast<uint16_t>(entry.name.size()));
    appendLe16(out, 0);
    appendLe16(out, 0);
    appendLe16(out, 0);
    appendLe16(out, entry.internalAttr);
    appendLe32(out, entry.externalAttr);
    appendLe32(out, entry.localOffset);
    appendBytes(out, entry.name);
}

std::string xmlEscapedUtf8(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string docxCountText(int value, bool forceZero) {
    if (value == 0 && !forceZero) return {};
    return std::to_string(value);
}

std::vector<std::string> buildDocxPlaceholderValues(const HivStatisticsState* st, int year, int month) {
    std::vector<std::string> values;
    values.reserve(46);
    values.push_back(std::to_string(year));
    values.push_back(std::to_string(month));
    values.push_back(search::wide_to_utf8(selectedComboText(st->source)));

    const int totalRow = static_cast<int>(std::size(SUMMARY_ROWS)) - 1;
    for (int i = 0; i <= totalRow; ++i) {
        const bool isTotal = i == totalRow;
        values.push_back(docxCountText(summaryScreeningCount(st->statSummary, i), isTotal));
        values.push_back(docxCountText(summaryPositiveCount(st->statSummary, i), isTotal));
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    values.push_back(std::to_string(now.wYear));
    values.push_back(std::to_string(now.wMonth));
    values.push_back(std::to_string(now.wDay));
    return values;
}

std::string replaceDocxPlaceholders(const std::string& documentXml, const std::vector<std::string>& values) {
    std::string out;
    out.reserve(documentXml.size() + values.size() * 8);
    size_t pos = 0;
    size_t index = 0;
    while (true) {
        const size_t found = documentXml.find("{}", pos);
        if (found == std::string::npos) {
            out.append(documentXml, pos, std::string::npos);
            break;
        }
        out.append(documentXml, pos, found - pos);
        if (index < values.size()) {
            out += xmlEscapedUtf8(values[index]);
        }
        ++index;
        pos = found + 2;
    }
    return out;
}

bool writeDocxFromTemplate(const std::wstring& templatePath, const std::wstring& outputPath,
                           const std::vector<std::string>& placeholderValues) {
    std::vector<unsigned char> zip;
    if (!readFileBytes(templatePath, zip)) return false;

    std::vector<ZipEntry> entries;
    if (!readZipEntries(zip, entries)) return false;

    bool replaced = false;
    for (auto& entry : entries) {
        if (entry.name == "word/document.xml") {
            const std::string documentXml(entry.data.begin(), entry.data.end());
            const auto replacement = bytesFromString(replaceDocxPlaceholders(documentXml, placeholderValues));
            entry.flags = 0;
            entry.method = 0;
            entry.versionNeeded = 20;
            entry.data = replacement;
            entry.compressedSize = static_cast<uint32_t>(entry.data.size());
            entry.uncompressedSize = static_cast<uint32_t>(entry.data.size());
            entry.crc = crc32Bytes(entry.data);
            replaced = true;
            break;
        }
    }
    if (!replaced) return false;

    std::vector<unsigned char> out;
    for (auto& entry : entries) {
        entry.localOffset = static_cast<uint32_t>(out.size());
        appendZipLocalHeader(out, entry);
    }
    const uint32_t centralOffset = static_cast<uint32_t>(out.size());
    for (const auto& entry : entries) {
        appendZipCentralHeader(out, entry);
    }
    const uint32_t centralSize = static_cast<uint32_t>(out.size() - centralOffset);
    appendLe32(out, 0x06054b50u);
    appendLe16(out, 0);
    appendLe16(out, 0);
    appendLe16(out, static_cast<uint16_t>(entries.size()));
    appendLe16(out, static_cast<uint16_t>(entries.size()));
    appendLe32(out, centralSize);
    appendLe32(out, centralOffset);
    appendLe16(out, 0);
    return writeFileBytes(outputPath, out);
}

std::wstring sourceSuffix(HWND source) {
    const std::wstring sourceLabel = selectedComboText(source);
    if (sourceLabel == L"新院") return L" - 新院";
    if (sourceLabel == L"老院") return L" - 老院";
    return L" - 全部";
}

void exportStatistics(HWND hwnd, HivStatisticsState* st) {
    if (!st || !st->hasLoadedResult) {
        MessageBoxW(hwnd, L"请先查询统计数据后再导出。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    std::wstring folder;
    if (!chooseFolder(hwnd, folder)) return;

    const int year = intText(st->year, 0);
    const int month = selectedMonth(st->month);
    wchar_t name[160]{};
    swprintf(name, 160, L"%04d年%d月HIV检测表%s.docx", year, month, sourceSuffix(st->source).c_str());
    std::wstring path = folder;
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') {
        path += L"\\";
    }
    path += name;

    const std::wstring templatePath = findHivDocxTemplate();
    if (templatePath.empty()) {
        MessageBoxW(hwnd, L"未找到匹配的 HIV 统计表 DOCX 模版，请先点击“上传模版”。",
                    WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    const auto placeholderValues = buildDocxPlaceholderValues(st, year, month);
    if (!writeDocxFromTemplate(templatePath, path, placeholderValues)) {
        MessageBoxW(hwnd, L"导出统计表失败，请确认目标文件夹可写。", WINDOW_TITLE, MB_ICONERROR);
        return;
    }
    setStatus(st, L"已导出统计表：" + path);
    MessageBoxW(hwnd, (L"已导出统计表：\n" + path).c_str(), WINDOW_TITLE, MB_ICONINFORMATION);
}

std::wstring defaultDetailCsvFilename(HivStatisticsState* st) {
    const int year = intText(st->year, 0);
    const int month = selectedMonth(st->month);
    wchar_t name[160]{};
    swprintf(name, 160, L"%04d年%d月HIV检测明细表%s.csv", year, month, sourceSuffix(st->source).c_str());
    return name;
}

bool chooseDetailCsvPath(HWND owner, HivStatisticsState* st, std::wstring& path) {
    wchar_t buffer[MAX_PATH]{};
    const std::wstring defaultName = defaultDetailCsvFilename(st);
    lstrcpynW(buffer, defaultName.c_str(), MAX_PATH);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"csv";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return false;
    path = buffer;
    return true;
}

std::string detailCsvValue(const search::HivStatDetailRow& row, int col) {
    switch (col) {
        case DetailMachine: return row.machine_name.empty() ? row.mach_code : row.machine_name;
        case DetailSampleSource: return row.sample_source;
        case DetailMethodology: return row.methodology;
        case DetailLabDepartment: return row.lab_department;
        case DetailItemCode: return row.item_code;
        case DetailItemName: return row.item_name;
        case DetailReportNo: return row.rep_no;
        case DetailBarcode: return row.txm_no;
        case DetailSampleNo: return row.oper_no;
        case DetailPatientNo: return row.patient_no;
        case DetailName: return row.name;
        case DetailCompletedBloodApplyForms: return row.completed_blood_apply_forms;
        case DetailPatientType: return row.patient_type;
        case DetailDept: return row.dept_name;
        case DetailResult: return row.result;
        case DetailLowerBound: return row.lower_bound;
        case DetailUpperBound: return row.upper_bound;
        case DetailPositive: return row.positive;
        case DetailReportTime: return row.report_time;
        default: return {};
    }
}

void exportDetailsCsv(HWND hwnd, HivStatisticsState* st) {
    if (!st || !st->hasLoadedResult) {
        MessageBoxW(hwnd, L"请先查询统计数据后再导出明细。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }
    if (st->rows.empty()) {
        MessageBoxW(hwnd, L"当前没有可导出的 HIV 明细。", WINDOW_TITLE, MB_ICONINFORMATION);
        return;
    }

    std::wstring path;
    if (!chooseDetailCsvPath(hwnd, st, path)) return;

    std::vector<unsigned char> bytes;
    appendBytes(bytes, "\xEF\xBB\xBF");
    for (int col = 0; col < static_cast<int>(std::size(DETAIL_COLUMNS)); ++col) {
        if (col > 0) appendBytes(bytes, ",");
        appendBytes(bytes, csvEscape(search::wide_to_utf8(DETAIL_COLUMNS[col].title)));
    }
    appendBytes(bytes, "\n");
    for (const auto& row : st->rows) {
        for (int col = 0; col < static_cast<int>(std::size(DETAIL_COLUMNS)); ++col) {
            if (col > 0) appendBytes(bytes, ",");
            appendBytes(bytes, csvEscape(detailCsvValue(row, col)));
        }
        appendBytes(bytes, "\n");
    }

    if (!writeFileBytes(path, bytes)) {
        MessageBoxW(hwnd, L"导出明细表失败，请确认目标文件可写。", WINDOW_TITLE, MB_ICONERROR);
        return;
    }
    setStatus(st, L"已导出明细表：" + path);
    MessageBoxW(hwnd, (L"已导出明细表：\n" + path).c_str(), WINDOW_TITLE, MB_ICONINFORMATION);
}

void openRegularReportForRow(HWND owner, HivStatisticsState* st, int index) {
    if (!st) return;
    if (index < 0) {
        index = ListView_GetNextItem(st->details, -1, LVNI_SELECTED);
    }
    if (index < 0 || index >= static_cast<int>(st->rows.size())) return;

    const auto& row = st->rows[static_cast<size_t>(index)];
    if (search::trim(row.rep_no).empty() || search::trim(row.mach_code).empty() ||
        search::trim(row.report_time).empty()) {
        MessageBoxW(owner,
                    L"当前 HIV 明细缺少报告号、检验仪器或报告时间，无法跳转到常规报告。",
                    WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    auto* target = new RegularReportOpenTarget;
    target->rep_no = search::trim(row.rep_no);
    target->oper_no = search::trim(row.oper_no);
    target->inspect_date = search::trim(row.report_time);
    target->mach_code = search::trim(row.mach_code);
    target->mach_name = search::trim(row.machine_name);
    target->room_code = search::trim(row.room_code);

    HWND regular = create_regular_report_module(st->ctx);
    if (!regular || !PostMessageW(regular, WM_REGULAR_OPEN_REPORT, 0, reinterpret_cast<LPARAM>(target))) {
        delete target;
        MessageBoxW(owner, L"常规报告页面打开失败。", WINDOW_TITLE, MB_ICONERROR);
    }
}

template <size_t N>
void addColumns(HWND list, const ListColumn (&columns)[N]) {
    for (int i = 0; i < static_cast<int>(N); ++i) {
        const auto& col = columns[static_cast<size_t>(i)];
        search::add_list_column(list, i, col.title, col.width);
    }
}

void setCell(HWND list, int row, int col, const std::wstring& text) {
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(text.c_str()));
}

void setCellUtf8(HWND list, int row, int col, const std::string& text) {
    setCell(list, row, col, search::utf8_to_wide(text));
}

void populateDetails(HivStatisticsState* st);

std::string detailSortValue(const search::HivStatDetailRow& row, int col) {
    switch (col) {
        case DetailMachine: return row.machine_name.empty() ? row.mach_code : row.machine_name;
        case DetailSampleSource: return row.sample_source;
        case DetailMethodology: return row.methodology;
        case DetailLabDepartment: return row.lab_department;
        case DetailItemCode: return row.item_code;
        case DetailItemName: return row.item_name;
        case DetailReportNo: return row.rep_no;
        case DetailBarcode: return row.txm_no;
        case DetailSampleNo: return row.oper_no;
        case DetailPatientNo: return row.patient_no;
        case DetailName: return row.name;
        case DetailCompletedBloodApplyForms: return row.completed_blood_apply_forms;
        case DetailPatientType: return row.patient_type;
        case DetailDept: return row.dept_name;
        case DetailResult: return row.result;
        case DetailLowerBound: return row.lower_bound;
        case DetailUpperBound: return row.upper_bound;
        case DetailPositive: return row.positive;
        case DetailReportTime: return row.report_time;
        default: return {};
    }
}

bool parseSortNumber(const std::string& value, double& out) {
    const std::string text = search::trim(value);
    if (text.empty()) return false;
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end && *end == '\0';
}

int compareDetailSortValue(const search::HivStatDetailRow& a, const search::HivStatDetailRow& b, int col) {
    const std::string left = search::trim(detailSortValue(a, col));
    const std::string right = search::trim(detailSortValue(b, col));
    double leftNum = 0.0;
    double rightNum = 0.0;
    const bool leftNumeric = parseSortNumber(left, leftNum);
    const bool rightNumeric = parseSortNumber(right, rightNum);
    if (leftNumeric && rightNumeric) {
        if (leftNum < rightNum) return -1;
        if (leftNum > rightNum) return 1;
        return 0;
    }
    if (left < right) return -1;
    if (left > right) return 1;
    return 0;
}

void sortDetailRowsForDisplay(HivStatisticsState* st) {
    if (!st || st->detailSortColumn < 0) return;
    const int col = st->detailSortColumn;
    const bool ascending = st->detailSortAscending;
    std::stable_sort(st->rows.begin(), st->rows.end(),
                     [col, ascending](const search::HivStatDetailRow& a, const search::HivStatDetailRow& b) {
                         const int cmp = compareDetailSortValue(a, b, col);
                         return ascending ? cmp < 0 : cmp > 0;
                     });
}

void sortDetailsByColumn(HivStatisticsState* st, int col) {
    if (!st || !st->details || st->rows.empty() || col < 0 || col >= static_cast<int>(std::size(DETAIL_COLUMNS))) {
        return;
    }
    if (st->detailSortColumn == col) {
        st->detailSortAscending = !st->detailSortAscending;
    } else {
        st->detailSortColumn = col;
        st->detailSortAscending = true;
    }

    std::string selectedKey;
    const int selected = ListView_GetNextItem(st->details, -1, LVNI_SELECTED);
    if (selected >= 0 && selected < static_cast<int>(st->rows.size())) {
        const auto& row = st->rows[static_cast<size_t>(selected)];
        selectedKey = row.rep_no + "|" + row.mach_code + "|" + row.item_code;
    }

    sortDetailRowsForDisplay(st);
    populateDetails(st);

    if (!selectedKey.empty()) {
        for (int i = 0; i < static_cast<int>(st->rows.size()); ++i) {
            const auto& row = st->rows[static_cast<size_t>(i)];
            const std::string key = row.rep_no + "|" + row.mach_code + "|" + row.item_code;
            if (key == selectedKey) {
                ListView_SetItemState(st->details, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(st->details, i, FALSE);
                break;
            }
        }
    }
}

void initSummaryList(HWND list) {
    ListView_DeleteAllItems(list);
    while (ListView_DeleteColumn(list, 0)) {}
    addColumns(list, SUMMARY_COLUMNS);
}

void initMethodologySummaryList(HWND list) {
    ListView_DeleteAllItems(list);
    while (ListView_DeleteColumn(list, 0)) {}
    addColumns(list, METHODOLOGY_SUMMARY_COLUMNS);
}

void initDetailList(HWND list) {
    ListView_DeleteAllItems(list);
    while (ListView_DeleteColumn(list, 0)) {}
    addColumns(list, DETAIL_COLUMNS);
}

void setSummaryCounts(HWND list, int row, int screeningCount, int positiveCount) {
    wchar_t buf[32]{};
    swprintf(buf, 32, L"%d", screeningCount);
    setCell(list, row, 1, buf);
    swprintf(buf, 32, L"%d", positiveCount);
    setCell(list, row, 2, buf);
}

void populateMethodologySummary(HivStatisticsState* st) {
    if (!st || !st->methodologySummary) return;
    ListView_DeleteAllItems(st->methodologySummary);
    struct MethodologyRow {
        const wchar_t* name;
        int screeningCount;
        int positiveCount;
    };
    const MethodologyRow rows[] = {
        {L"化学发光法", st->statSummary.chemiluminescence_screening_count,
         st->statSummary.chemiluminescence_positive_count},
        {L"酶免法", st->statSummary.elisa_screening_count,
         st->statSummary.elisa_positive_count},
    };
    for (int i = 0; i < static_cast<int>(std::size(rows)); ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(rows[i].name);
        ListView_InsertItem(st->methodologySummary, &item);
        setSummaryCounts(st->methodologySummary, i, rows[i].screeningCount, rows[i].positiveCount);
    }
}

void populateSummary(HivStatisticsState* st) {
    if (!st || !st->summary) return;
    ListView_DeleteAllItems(st->summary);
    for (int i = 0; i < static_cast<int>(std::size(SUMMARY_DISPLAY_ROWS)); ++i) {
        const int summaryRow = SUMMARY_DISPLAY_ROWS[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = i;
        item.pszText = const_cast<wchar_t*>(SUMMARY_ROWS[summaryRow]);
        item.lParam = summaryRow;
        ListView_InsertItem(st->summary, &item);
        if (summaryRow == SUMMARY_ROW_PREOPERATIVE) {
            setSummaryCounts(st->summary, i,
                             st->statSummary.preoperative_screening_count,
                             st->statSummary.preoperative_positive_count);
        } else if (summaryRow == SUMMARY_ROW_TRANSFUSION) {
            setSummaryCounts(st->summary, i,
                             st->statSummary.transfusion_screening_count,
                             st->statSummary.transfusion_positive_count);
        } else if (summaryRow == SUMMARY_ROW_STI_CLINIC) {
            setSummaryCounts(st->summary, i,
                             st->statSummary.sti_clinic_screening_count,
                             st->statSummary.sti_clinic_positive_count);
        } else if (summaryRow == SUMMARY_ROW_OTHER_VISIT) {
            setSummaryCounts(st->summary, i,
                             st->statSummary.other_visit_screening_count,
                             st->statSummary.other_visit_positive_count);
        } else if (summaryRow == SUMMARY_ROW_PRENATAL) {
            setSummaryCounts(st->summary, i,
                             st->statSummary.prenatal_screening_count,
                             st->statSummary.prenatal_positive_count);
        } else if (summaryRow == SUMMARY_ROW_TOTAL) {
            setSummaryCounts(st->summary, i,
                             st->statSummary.screening_count,
                             st->statSummary.positive_count);
        }
    }
}

void populateDetails(HivStatisticsState* st) {
    if (!st || !st->details) return;
    ListView_DeleteAllItems(st->details);
    for (int i = 0; i < static_cast<int>(st->rows.size()); ++i) {
        const auto& row = st->rows[static_cast<size_t>(i)];
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        auto first = search::utf8_to_wide(row.machine_name.empty() ? row.mach_code : row.machine_name);
        item.pszText = const_cast<wchar_t*>(first.c_str());
        ListView_InsertItem(st->details, &item);
        setCellUtf8(st->details, i, DetailSampleSource, row.sample_source);
        setCellUtf8(st->details, i, DetailMethodology, row.methodology);
        setCellUtf8(st->details, i, DetailLabDepartment, row.lab_department);
        setCellUtf8(st->details, i, DetailItemCode, row.item_code);
        setCellUtf8(st->details, i, DetailItemName, row.item_name);
        setCellUtf8(st->details, i, DetailReportNo, row.rep_no);
        setCellUtf8(st->details, i, DetailBarcode, row.txm_no);
        setCellUtf8(st->details, i, DetailSampleNo, row.oper_no);
        setCellUtf8(st->details, i, DetailPatientNo, row.patient_no);
        setCellUtf8(st->details, i, DetailName, row.name);
        setCellUtf8(st->details, i, DetailCompletedBloodApplyForms, row.completed_blood_apply_forms);
        setCellUtf8(st->details, i, DetailPatientType, row.patient_type);
        setCellUtf8(st->details, i, DetailDept, row.dept_name);
        setCellUtf8(st->details, i, DetailResult, row.result);
        setCellUtf8(st->details, i, DetailLowerBound, row.lower_bound);
        setCellUtf8(st->details, i, DetailUpperBound, row.upper_bound);
        setCellUtf8(st->details, i, DetailPositive, row.positive);
        setCellUtf8(st->details, i, DetailReportTime, row.report_time);
    }
}

void resizeLayout(HWND hwnd, HivStatisticsState* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int pad = S(hwnd, 10);
    const int topH = S(hwnd, 42);
    const int summaryH = S(hwnd, 232);
    const int methodologyH = S(hwnd, 72);
    const int statusH = S(hwnd, 24);

    MoveWindow(st->summary, pad, topH + pad, w - pad * 2, summaryH, TRUE);
    MoveWindow(st->methodologySummary, pad, topH + summaryH + pad * 2, w - pad * 2, methodologyH, TRUE);
    MoveWindow(st->details, pad, topH + summaryH + methodologyH + pad * 3, w - pad * 2,
               (std::max)(S(hwnd, 80), h - topH - summaryH - methodologyH - pad * 4 - statusH), TRUE);
    MoveWindow(st->status, pad, h - statusH - S(hwnd, 4), w - pad * 2, statusH, TRUE);
    search::layout_page_feedback(st->feedback);
}

void runQuery(HWND hwnd, HivStatisticsState* st) {
    if (!st || st->querying) return;
    const int year = intText(st->year, 0);
    const int month = selectedMonth(st->month);
    if (year < 1900 || year > 9999) {
        MessageBoxW(hwnd, L"请输入有效年份。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    const auto connection = search::build_connection_string_w(st->ctx.dbSettings);
    if (connection.empty()) {
        MessageBoxW(hwnd, L"请先在系统设置中配置数据库连接。", WINDOW_TITLE, MB_ICONWARNING);
        return;
    }

    st->querying = true;
    st->hasLoadedResult = false;
    EnableWindow(st->query, FALSE);
    EnableWindow(st->exportButton, FALSE);
    EnableWindow(st->exportDetailsButton, FALSE);
    setStatus(st, L"正在查询 HIV 抗体检测统计...");
    search::show_page_activity(st->feedback, L"正在查询 HIV 抗体检测统计，请稍候…");

    search::HivStatQuery query;
    query.connection_string = search::wide_to_utf8(connection);
    query.year = year;
    query.month = month;
    query.lab_department = search::wide_to_utf8(selectedComboText(st->source));

    std::thread([hwnd, query]() {
        auto* result = new HivQueryResult();
        result->ok = search::query_hiv_statistics(query, result->summary, result->rows, result->error);
        if (!PostMessageW(hwnd, WM_HIV_STATS_LOADED, 0, reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<HivStatisticsState*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            st = reinterpret_cast<HivStatisticsState*>(mcs->lParam);
            SetPropW(hwnd, PROP_STATE, st);

            st->bgBrush = CreateSolidBrush(RGB(0xF0, 0xF0, 0xF0));
            SYSTEMTIME now{};
            GetLocalTime(&now);

            const int pad = S(hwnd, 10);
            label(hwnd, L"年份：", pad, S(hwnd, 12), S(hwnd, 52), S(hwnd, 24));
            wchar_t yearText[16]{};
            swprintf(yearText, 16, L"%u", now.wYear);
            st->year = edit(hwnd, IDC_YEAR, yearText, S(hwnd, 64), S(hwnd, 10), S(hwnd, 72), S(hwnd, 24));
            label(hwnd, L"月份：", S(hwnd, 148), S(hwnd, 12), S(hwnd, 52), S(hwnd, 24));
            st->month = combo(hwnd, IDC_MONTH, S(hwnd, 202), S(hwnd, 10), S(hwnd, 78), S(hwnd, 160));
            for (int m = 1; m <= 12; ++m) {
                wchar_t text[16]{};
                swprintf(text, 16, L"%d月", m);
                SendMessageW(st->month, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
            }
            SendMessageW(st->month, CB_SETCURSEL, now.wMonth - 1, 0);
            label(hwnd, L"检验科：", S(hwnd, 292), S(hwnd, 12), S(hwnd, 70), S(hwnd, 24));
            st->source = combo(hwnd, IDC_SOURCE, S(hwnd, 364), S(hwnd, 10), S(hwnd, 82), S(hwnd, 160));
            SendMessageW(st->source, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全部"));
            SendMessageW(st->source, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"新院"));
            SendMessageW(st->source, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"老院"));
            SendMessageW(st->source, CB_SETCURSEL, 0, 0);
            st->query = search::create_button(hwnd, IDC_QUERY, L"查询", S(hwnd, 456), S(hwnd, 9), S(hwnd, 86), S(hwnd, 26));
            st->exportButton = search::create_button(hwnd, IDC_EXPORT, L"导出统计表", S(hwnd, 548), S(hwnd, 9), S(hwnd, 112), S(hwnd, 26));
            st->exportDetailsButton = search::create_button(hwnd, IDC_EXPORT_DETAILS, L"导出明细表",
                                                             S(hwnd, 666), S(hwnd, 9), S(hwnd, 112), S(hwnd, 26));
            st->uploadTemplateButton = search::create_button(hwnd, IDC_UPLOAD_TEMPLATE, L"上传模版",
                                                              S(hwnd, 784), S(hwnd, 9), S(hwnd, 112), S(hwnd, 26));
            updateExportButton(st);
            label(hwnd, L"导出统计表使用汇总结果，导出明细表使用下方当前明细。",
                  S(hwnd, 910), S(hwnd, 12), S(hwnd, 520), S(hwnd, 24), SS_LEFT);

            st->summary = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                          WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                          0, 0, 0, 0, hwnd, win32_control_id(IDC_SUMMARY), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->summary, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            initSummaryList(st->summary);

            st->methodologySummary = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                                     WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                                     0, 0, 0, 0, hwnd,
                                                     win32_control_id(IDC_METHODOLOGY_SUMMARY),
                                                     GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->methodologySummary, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            initMethodologySummaryList(st->methodologySummary);

            st->details = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                          WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                          0, 0, 0, 0, hwnd, win32_control_id(IDC_DETAILS), GetModuleHandleW(nullptr), nullptr);
            ListView_SetExtendedListViewStyle(st->details, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
            initDetailList(st->details);

            st->status = label(hwnd, L"请选择年份和月份后查询；未上传匹配模版时统计表导出不可用。", 0, 0, 0, 0, SS_LEFT);
            search::initialize_page_feedback(st->feedback, hwnd, st->status,
                                             st->details, st->ctx.uiFont);
            search::add_page_tooltip(st->feedback, st->query,
                                     L"按年份、月份和检验科查询 HIV 抗体检测统计。");
            search::add_page_tooltip(st->feedback, st->exportButton,
                                     L"使用当前汇总结果生成统计表。");
            search::add_page_tooltip(st->feedback, st->exportDetailsButton,
                                     L"导出当前已加载、已排序的全部检测明细。");
            search::add_page_tooltip(st->feedback, st->uploadTemplateButton,
                                     L"上传与当前统计表格式匹配的 Word 模版。");
            search::add_page_tooltip(st->feedback, st->details,
                                     L"单击列标题排序；双击记录可跳转常规报告。");
            search::apply_font_to_children(hwnd, st->ctx.uiFont);
            populateSummary(st);
            populateMethodologySummary(st);
            resizeLayout(hwnd, st);
            return 0;
        }
        case WM_SIZE:
            resizeLayout(hwnd, st);
            return 0;
        case WM_COMMAND:
            if (st && search::handle_page_feedback_command(st->feedback, lp, WINDOW_TITLE)) return 0;
            if (LOWORD(wp) == IDC_QUERY) {
                runQuery(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_EXPORT) {
                exportStatistics(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_EXPORT_DETAILS) {
                exportDetailsCsv(hwnd, st);
                return 0;
            }
            if (LOWORD(wp) == IDC_UPLOAD_TEMPLATE) {
                uploadTemplate(hwnd, st);
                return 0;
            }
            break;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm->idFrom == IDC_SUMMARY && nm->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    if (static_cast<int>(cd->nmcd.lItemlParam) == SUMMARY_ROW_TOTAL) {
                        cd->clrTextBk = COLOR_TOTAL_ROW;
                    }
                    return CDRF_NEWFONT;
                }
            }
            if (nm->idFrom == IDC_DETAILS && nm->code == NM_CUSTOMDRAW) {
                auto* cd = reinterpret_cast<NMLVCUSTOMDRAW*>(lp);
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const int idx = static_cast<int>(cd->nmcd.dwItemSpec);
                    if (st && idx >= 0 && idx < static_cast<int>(st->rows.size()) &&
                        search::trim(st->rows[static_cast<size_t>(idx)].positive) == "是") {
                        cd->clrTextBk = COLOR_POSITIVE_ROW;
                    }
                    return CDRF_NEWFONT;
                }
            }
            if (st && nm->idFrom == IDC_DETAILS && nm->code == LVN_COLUMNCLICK) {
                auto* lv = reinterpret_cast<NMLISTVIEW*>(lp);
                sortDetailsByColumn(st, lv->iSubItem);
                return 0;
            }
            if (st && nm->idFrom == IDC_DETAILS && nm->code == NM_DBLCLK) {
                auto* item = reinterpret_cast<NMITEMACTIVATE*>(lp);
                openRegularReportForRow(hwnd, st, item ? item->iItem : -1);
                return 0;
            }
            break;
        }
        case WM_HIV_STATS_LOADED: {
            std::unique_ptr<HivQueryResult> result(reinterpret_cast<HivQueryResult*>(lp));
            if (!st) return 0;
            st->querying = false;
            EnableWindow(st->query, TRUE);
            search::hide_page_activity(st->feedback);
            st->hasLoadedResult = false;
            updateExportButton(st);
            if (!result->ok) {
                search::show_page_alert(st->feedback,
                    L"查询失败：" + search::utf8_to_wide(result->error));
                return 0;
            }
            st->hasLoadedResult = true;
            st->statSummary = result->summary;
            st->rows = std::move(result->rows);
            sortDetailRowsForDisplay(st);
            populateSummary(st);
            populateMethodologySummary(st);
            populateDetails(st);
            wchar_t status[160]{};
            swprintf(status, 160, L"查询完成：初筛检测数 %d，初筛阳性数 %d，明细 %d 行。",
                     st->statSummary.screening_count, st->statSummary.positive_count,
                     static_cast<int>(st->rows.size()));
            updateExportButton(st);
            std::wstring statusText = status;
            if (findHivDocxTemplate().empty()) {
                statusText += L" 未上传匹配模版，统计表导出不可用。";
            }
            setStatus(st, statusText);
            return 0;
        }
        case app::WM_APP_SETTINGS_CHANGED:
            if (st) {
                search::apply_font_to_children(hwnd, st->ctx.uiFont);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_CTLCOLORSTATIC:
            if (st) {
                LRESULT result = 0;
                if (search::page_feedback_static_color(
                        st->feedback, reinterpret_cast<HDC>(wp),
                        reinterpret_cast<HWND>(lp), result)) return result;
            }
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<LRESULT>(st ? st->bgBrush : nullptr);
        case WM_ERASEBKGND: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, st ? st->bgBrush : reinterpret_cast<HBRUSH>(GetStockObject(LTGRAY_BRUSH)));
            return 1;
        }
        case WM_DESTROY:
            if (st) {
                search::destroy_page_feedback(st->feedback);
                if (st->bgBrush) DeleteObject(st->bgBrush);
                RemovePropW(hwnd, PROP_STATE);
                delete st;
            }
            return 0;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

void registerClass(HINSTANCE inst) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.lpszClassName = WND_CLASS;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP),
                                               IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
    registered = true;
}

}  // namespace

HWND create_hiv_statistics_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, WINDOW_TITLE)) {
        return existing;
    }

    registerClass(ctx.instance);
    auto* st = new HivStatisticsState();
    st->ctx = ctx;
    MDICREATESTRUCTW mcs{};
    mcs.szClass = WND_CLASS;
    mcs.szTitle = WINDOW_TITLE;
    mcs.hOwner = ctx.instance;
    mcs.x = CW_USEDEFAULT;
    mcs.y = CW_USEDEFAULT;
    mcs.cx = CW_USEDEFAULT;
    mcs.cy = CW_USEDEFAULT;
    mcs.lParam = reinterpret_cast<LPARAM>(st);
    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));
    if (!child) {
        delete st;
        return nullptr;
    }
    SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

#endif
