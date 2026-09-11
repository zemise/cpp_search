#ifdef _WIN32

#include "phone_directory_module.h"

#include "app_settings_io.h"
#include "main_app.h"
#include "resource.h"
#include "search_ui_layout.h"

#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kPhoneDirectoryClass[] = L"LISWorkbenchPhoneDirectoryWindow";
constexpr wchar_t kPhoneDirectoryTitle[] = L"常用电话";
constexpr wchar_t kDefaultFileName[] = L"常用电话.rtf";
constexpr wchar_t kConfigSection[] = L"PhoneDirectory";
constexpr wchar_t kConfigSelectedFile[] = L"SelectedFile";
constexpr int kMaxPhoneFileBytes = 16 * 1024 * 1024;

constexpr int IDC_FILE_COMBO = 17001;
constexpr int IDC_UPLOAD = 17002;
constexpr int IDC_RELOAD = 17003;
constexpr int IDC_FIND_EDIT = 17005;
constexpr int IDC_VIEWER = 17007;
constexpr int IDT_AUTO_SEARCH = 17020;
constexpr UINT WM_PHONE_FOCUS_SEARCH = WM_APP + 71;
constexpr int VIEWER_PAGE_MAX_W = 880;
const COLORREF kPhoneBg = RGB(243, 246, 250);
const COLORREF kPhonePanel = RGB(255, 255, 255);
const COLORREF kPhoneBorder = RGB(229, 234, 241);
const COLORREF kPhoneSubtleBorder = RGB(229, 234, 241);

struct PhoneDirectoryState {
    ModuleContext ctx;
    HWND fileCombo = nullptr;
    HWND uploadButton = nullptr;
    HWND findEdit = nullptr;
    HWND reloadButton = nullptr;
    HWND viewer = nullptr;
    HFONT font = nullptr;
    bool ownedByWindow = false;
    RECT topCardRect{};
    RECT viewerCardRect{};
    std::wstring folderPath;
    std::wstring filePath;
    std::wstring selectedFileName;
    std::vector<std::wstring> fileNames;
};

struct StreamInState {
    const BYTE* data = nullptr;
    DWORD size = 0;
    DWORD offset = 0;
};

HMODULE g_richEditModule = nullptr;

bool g_isWindows7 = []() -> bool {
    OSVERSIONINFOW vi = { sizeof(vi) };
    return GetVersionExW(&vi) && vi.dwMajorVersion == 6 && vi.dwMinorVersion == 1;
}();

DWORD CALLBACK streamInCallback(DWORD_PTR cookie, LPBYTE buffer, LONG cb, LONG* pcb) {
    auto* state = reinterpret_cast<StreamInState*>(cookie);
    if (!state || !buffer || !pcb || cb <= 0) {
        if (pcb) *pcb = 0;
        return 1;
    }
    const DWORD remaining = state->size - state->offset;
    const DWORD toCopy = std::min<DWORD>(remaining, static_cast<DWORD>(cb));
    if (toCopy > 0) {
        CopyMemory(buffer, state->data + state->offset, toCopy);
        state->offset += toCopy;
    }
    *pcb = static_cast<LONG>(toCopy);
    return 0;
}

std::wstring joinPath(const std::wstring& dir, const wchar_t* name) {
    if (dir.empty()) return name ? name : L"";
    if (dir.back() == L'\\' || dir.back() == L'/') return dir + name;
    return dir + L"\\" + name;
}

std::wstring joinPath(const std::wstring& dir, const std::wstring& name) {
    return joinPath(dir, name.c_str());
}

std::wstring executableDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (len >= buffer.size() - 1) {
        buffer.resize(buffer.size() * 2, L'\0');
        len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    buffer.resize(len);
    const auto pos = buffer.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return buffer.substr(0, pos);
}

std::wstring windowText(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return {};
    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(hwnd, text.data(), len + 1);
    text.resize(len);
    return text;
}

std::wstring fileNameFromPath(const std::wstring& path) {
    const auto pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return path;
    return path.substr(pos + 1);
}

std::wstring toLower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return text;
}

std::wstring trimWide(std::wstring text) {
    auto isSpace = [](wchar_t ch) {
        return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
    };
    while (!text.empty() && isSpace(text.front())) {
        text.erase(text.begin());
    }
    while (!text.empty() && isSpace(text.back())) {
        text.pop_back();
    }
    return text;
}

bool ensureDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES) {
        return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool fileExists(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool chooseRtfFile(HWND owner, std::wstring& file) {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"RTF 文件 (*.rtf)\0*.rtf\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"请选择常用电话 RTF 文件";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return false;
    file = path;
    return true;
}

bool hasRtfExtension(const std::wstring& path) {
    const std::wstring lower = toLower(fileNameFromPath(path));
    return lower.size() >= 4 && lower.substr(lower.size() - 4) == L".rtf";
}

std::vector<std::wstring> listRtfFiles(const std::wstring& folder) {
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW fd{};
    const std::wstring pattern = joinPath(folder, L"*.rtf");
    HANDLE find = FindFirstFileW(pattern.c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return files;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            files.emplace_back(fd.cFileName);
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    std::sort(files.begin(), files.end(), [](const std::wstring& a, const std::wstring& b) {
        return toLower(a) < toLower(b);
    });
    return files;
}

bool nameInList(const std::vector<std::wstring>& files, const std::wstring& name) {
    return std::find(files.begin(), files.end(), name) != files.end();
}

void selectFileName(PhoneDirectoryState* st, const std::wstring& fileName, bool persist) {
    if (!st) return;
    st->selectedFileName = fileName;
    st->filePath = st->selectedFileName.empty()
        ? joinPath(st->folderPath, kDefaultFileName)
        : joinPath(st->folderPath, st->selectedFileName);
    if (persist && !st->selectedFileName.empty()) {
        search::save_module_str(kConfigSection, kConfigSelectedFile, st->selectedFileName);
    }
}

void applyViewerPageRect(PhoneDirectoryState* st) {
    if (!st || !st->viewer) return;
    RECT client{};
    GetClientRect(st->viewer, &client);
    const int scale = static_cast<int>(search::dpi_scale_factor(st->viewer) * 1.0f);
    const int clientW = static_cast<int>(client.right - client.left);
    const int clientH = static_cast<int>(client.bottom - client.top);
    const int pageW = std::min(clientW, VIEWER_PAGE_MAX_W * scale);
    const int left = std::max(0, (clientW - pageW) / 2);
    RECT formatRect{left, 0, left + pageW, clientH};
    SendMessageW(st->viewer, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&formatRect));
}

bool isRectVisible(const RECT& rc) {
    return rc.right > rc.left && rc.bottom > rc.top;
}

void fillRoundRect(HDC dc, const RECT& rc, int radius, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(dc, brush));
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(dc, pen));
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void strokeRoundRect(HDC dc, const RECT& rc, int radius, COLORREF color) {
    HBRUSH oldBrush = reinterpret_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN oldPen = reinterpret_cast<HPEN>(SelectObject(dc, pen));
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
}

void drawPhoneDirectoryChrome(HWND hwnd, PhoneDirectoryState* st, HDC dc) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    HBRUSH bg = CreateSolidBrush(kPhoneBg);
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    const int scale = std::max(1, static_cast<int>(search::dpi_scale_factor(hwnd) * 1.0f));
    const int radius = 8 * scale;
    if (st && isRectVisible(st->viewerCardRect)) {
        fillRoundRect(dc, st->viewerCardRect, radius, kPhonePanel);
        strokeRoundRect(dc, st->viewerCardRect, radius, kPhoneBorder);
    }
    if (st && isRectVisible(st->topCardRect)) {
        fillRoundRect(dc, st->topCardRect, radius, kPhonePanel);
        strokeRoundRect(dc, st->topCardRect, radius, kPhoneSubtleBorder);
    }
}

void setViewerPlainText(PhoneDirectoryState* st, const std::wstring& text) {
    if (!st || !st->viewer) return;
    SendMessageW(st->viewer, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(st->viewer, text.c_str());
    applyViewerPageRect(st);
    SendMessageW(st->viewer, EM_SETSEL, 0, 0);
    SendMessageW(st->viewer, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->viewer, nullptr, TRUE);
}

bool readFileBytes(const std::wstring& path, std::vector<BYTE>& bytes, std::wstring& error) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = L"找不到文件：" + path;
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > kMaxPhoneFileBytes) {
        CloseHandle(file);
        error = L"文件大小异常或超过 16MB：" + path;
        return false;
    }
    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const BOOL ok = bytes.empty() ||
                    ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok || read != static_cast<DWORD>(bytes.size())) {
        error = L"读取文件失败：" + path;
        return false;
    }
    return true;
}

std::wstring bytesToWideText(const std::vector<BYTE>& bytes) {
    if (bytes.empty()) return {};
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        const auto chars = (bytes.size() - 2) / sizeof(wchar_t);
        return std::wstring(reinterpret_cast<const wchar_t*>(bytes.data() + 2), chars);
    }
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        const char* ptr = reinterpret_cast<const char*>(bytes.data() + 3);
        const int count = MultiByteToWideChar(CP_UTF8, 0, ptr, static_cast<int>(bytes.size() - 3), nullptr, 0);
        std::wstring out(count, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, ptr, static_cast<int>(bytes.size() - 3), out.data(), count);
        return out;
    }
    const char* ptr = reinterpret_cast<const char*>(bytes.data());
    int count = MultiByteToWideChar(CP_ACP, 0, ptr, static_cast<int>(bytes.size()), nullptr, 0);
    if (count <= 0) return L"";
    std::wstring out(count, L'\0');
    MultiByteToWideChar(CP_ACP, 0, ptr, static_cast<int>(bytes.size()), out.data(), count);
    return out;
}

bool streamRtf(PhoneDirectoryState* st, const std::vector<BYTE>& bytes) {
    if (!st || !st->viewer) return false;
    StreamInState stream{bytes.data(), static_cast<DWORD>(bytes.size()), 0};
    EDITSTREAM es{};
    es.dwCookie = reinterpret_cast<DWORD_PTR>(&stream);
    es.pfnCallback = streamInCallback;
    SendMessageW(st->viewer, WM_SETREDRAW, FALSE, 0);
    SendMessageW(st->viewer, EM_SETSEL, 0, -1);
    SendMessageW(st->viewer, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
    const LRESULT result = SendMessageW(st->viewer, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));
    applyViewerPageRect(st);
    SendMessageW(st->viewer, EM_SETSEL, 0, 0);
    SendMessageW(st->viewer, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(st->viewer, nullptr, TRUE);
    return result > 0 && es.dwError == 0;
}

void loadPhoneFile(PhoneDirectoryState* st) {
    if (!st) return;
    std::vector<BYTE> bytes;
    std::wstring error;
    if (!readFileBytes(st->filePath, bytes, error)) {
        setViewerPlainText(st,
            L"尚未找到常用电话文件。\r\n\r\n请将 RTF 文件放到以下路径后点击“刷新”：\r\n" + st->filePath +
            L"\r\n\r\n建议用 Word/WPS/写字板维护电话表，并另存为 RTF 格式。");
        return;
    }

    if (streamRtf(st, bytes)) return;

    setViewerPlainText(st, bytesToWideText(bytes));
}

void refreshFileCombo(PhoneDirectoryState* st, const std::wstring& preferred, bool persistSelection) {
    if (!st || !st->fileCombo) return;
    st->fileNames = listRtfFiles(st->folderPath);
    SendMessageW(st->fileCombo, CB_RESETCONTENT, 0, 0);
    for (const auto& name : st->fileNames) {
        SendMessageW(st->fileCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
    }

    std::wstring chosen = preferred;
    if (!chosen.empty() && !nameInList(st->fileNames, chosen)) {
        chosen.clear();
    }
    if (chosen.empty() && nameInList(st->fileNames, kDefaultFileName)) {
        chosen = kDefaultFileName;
    }
    if (chosen.empty() && !st->fileNames.empty()) {
        chosen = st->fileNames.front();
    }

    if (!chosen.empty()) {
        const LRESULT idx = SendMessageW(st->fileCombo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
                                         reinterpret_cast<LPARAM>(chosen.c_str()));
        if (idx >= 0) {
            SendMessageW(st->fileCombo, CB_SETCURSEL, static_cast<WPARAM>(idx), 0);
        }
    }
    selectFileName(st, chosen, persistSelection);
}

void uploadPhoneFile(HWND hwnd, PhoneDirectoryState* st) {
    if (!st) return;
    std::wstring source;
    if (!chooseRtfFile(hwnd, source)) return;
    if (!hasRtfExtension(source)) {
        MessageBoxW(hwnd, L"请选择 RTF 文件。", kPhoneDirectoryTitle, MB_ICONERROR);
        return;
    }
    if (!ensureDirectory(st->folderPath)) {
        MessageBoxW(hwnd, L"无法创建 documents 目录，请确认程序目录可写。", kPhoneDirectoryTitle, MB_ICONERROR);
        return;
    }

    const std::wstring targetName = fileNameFromPath(source);
    const std::wstring target = joinPath(st->folderPath, targetName);
    if (source != target && fileExists(target)) {
        const int choice = MessageBoxW(hwnd, L"documents 目录中已存在同名文件，是否覆盖？",
                                       kPhoneDirectoryTitle, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2);
        if (choice != IDYES) return;
    }
    if (source != target && !CopyFileW(source.c_str(), target.c_str(), FALSE)) {
        MessageBoxW(hwnd, L"上传失败，请确认程序目录可写。", kPhoneDirectoryTitle, MB_ICONERROR);
        return;
    }
    refreshFileCombo(st, targetName, true);
    loadPhoneFile(st);
}

void focusFindEdit(PhoneDirectoryState* st, bool selectAll = true) {
    if (!st || !st->findEdit) return;
    SetFocus(st->findEdit);
    if (selectAll) {
        SendMessageW(st->findEdit, EM_SETSEL, 0, -1);
    }
}

void forceViewerRepaint(HWND viewer, bool includeFrame) {
    if (!viewer) return;
    UINT flags = RDW_INVALIDATE | RDW_ALLCHILDREN;
    if (includeFrame) flags |= RDW_FRAME;
    RedrawWindow(viewer, nullptr, nullptr, flags);
}

void scrollViewerToMatch(PhoneDirectoryState* st, LONG charPos) {
    if (!st || !st->viewer || charPos < 0) return;
    const LONG matchLine = static_cast<LONG>(
        SendMessageW(st->viewer, EM_EXLINEFROMCHAR, 0, static_cast<LPARAM>(charPos)));
    if (matchLine < 0) return;

    RECT rc{};
    GetClientRect(st->viewer, &rc);
    const int scale = std::max(1, static_cast<int>(search::dpi_scale_factor(st->viewer) * 1.0f));
    const int lineH = std::max(12 * scale, 18 * scale);
    const int visibleLines = std::max(3, static_cast<int>(rc.bottom - rc.top) / lineH);
    const LONG targetFirstLine = std::max<LONG>(0, matchLine - std::max<LONG>(1, visibleLines / 3));
    const LONG currentFirstLine = static_cast<LONG>(SendMessageW(st->viewer, EM_GETFIRSTVISIBLELINE, 0, 0));
    const LONG delta = targetFirstLine - currentFirstLine;
    if (delta != 0) {
        SendMessageW(st->viewer, EM_LINESCROLL, 0, static_cast<LPARAM>(delta));
    }
}

bool findText(PhoneDirectoryState* st, bool fromStart, bool beepOnMissing) {
    if (!st || !st->viewer || !st->findEdit) return false;
    const std::wstring keyword = trimWide(windowText(st->findEdit));
    if (keyword.empty()) {
        focusFindEdit(st, false);
        return false;
    }

    CHARRANGE current{};
    SendMessageW(st->viewer, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&current));
    const LONG textLength = static_cast<LONG>(SendMessageW(st->viewer, WM_GETTEXTLENGTH, 0, 0));

    FINDTEXTEXW ft{};
    ft.chrg.cpMin = fromStart ? 0 : std::max<LONG>(current.cpMax, 0);
    ft.chrg.cpMax = -1;
    ft.lpstrText = const_cast<LPWSTR>(keyword.c_str());
    LRESULT pos = SendMessageW(st->viewer, EM_FINDTEXTEXW, FR_DOWN, reinterpret_cast<LPARAM>(&ft));

    if (!fromStart && pos < 0 && ft.chrg.cpMin > 0) {
        ft.chrg.cpMin = 0;
        ft.chrg.cpMax = textLength;
        pos = SendMessageW(st->viewer, EM_FINDTEXTEXW, FR_DOWN, reinterpret_cast<LPARAM>(&ft));
    }

    if (pos >= 0) {
        scrollViewerToMatch(st, ft.chrgText.cpMin);
        SendMessageW(st->viewer, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&ft.chrgText));
        forceViewerRepaint(st->viewer, false);
        focusFindEdit(st, false);
        return true;
    }

    if (beepOnMissing) MessageBeep(MB_ICONINFORMATION);
    focusFindEdit(st, false);
    return false;
}

bool findNext(PhoneDirectoryState* st) {
    return findText(st, false, true);
}

void runAutoSearch(HWND hwnd, PhoneDirectoryState* st) {
    KillTimer(hwnd, IDT_AUTO_SEARCH);
    if (!st) return;
    if (!trimWide(windowText(st->findEdit)).empty()) {
        findText(st, true, false);
    } else {
        focusFindEdit(st, false);
    }
}

PhoneDirectoryState* stateFrom(HWND hwnd) {
    return reinterpret_cast<PhoneDirectoryState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void layoutPhoneDirectory(HWND hwnd, PhoneDirectoryState* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int scale = std::max(1, static_cast<int>(search::dpi_scale_factor(hwnd) * 1.0f));
    const int margin = 14 * scale;
    const int topH = 76 * scale;
    const int buttonW = 56 * scale;
    const int uploadW = 56 * scale;
    const int controlGap = 10 * scale;
    const int rowGap = 8 * scale;
    const int rowH = 28 * scale;
    const int cardPadX = 12 * scale;
    const int cardPadY = 10 * scale;
    const int viewerPad = 10 * scale;
    const int topMaxW = 420 * scale;
    const int topMinW = 300 * scale;
    const int sideGap = 8 * scale;
    const int comboMinW = 120 * scale;
    const int contentW = static_cast<int>(rc.right - rc.left) - margin * 2;
    const int topW = std::max(0, std::min(contentW, topMaxW));
    const int topLeft = std::max(margin, static_cast<int>(rc.right) - margin - topW);
    st->topCardRect = {topLeft, margin, topLeft + topW, margin + topH};
    if (topW >= topMinW && topLeft - sideGap > margin + 260 * scale) {
        st->viewerCardRect = {margin, margin, topLeft - sideGap, rc.bottom - margin};
    } else {
        st->viewerCardRect = {margin, margin + topH + sideGap, rc.right - margin, rc.bottom - margin};
    }

    const int innerW = std::max(0, topW - cardPadX * 2);
    const int controlX = st->topCardRect.left + cardPadX;
    const int searchY = st->topCardRect.top + cardPadY;
    const int docY = searchY + rowH + rowGap;
    const int searchW = std::max(0, innerW);
    int comboW = std::max(comboMinW, innerW - uploadW - buttonW - controlGap * 2);
    if (innerW < comboW + uploadW + buttonW + controlGap * 2) {
        comboW = std::max(0, innerW - uploadW - buttonW - controlGap * 2);
    }

    MoveWindow(st->findEdit, controlX, searchY, searchW, rowH, TRUE);

    int x = controlX;
    MoveWindow(st->fileCombo, x, docY, comboW, 300 * scale, TRUE);
    x += comboW + controlGap;
    MoveWindow(st->uploadButton, x, docY, uploadW, rowH, TRUE);
    x += uploadW + controlGap;
    MoveWindow(st->reloadButton, x, docY, buttonW, rowH, TRUE);

    const int viewerW = std::max(0, static_cast<int>(st->viewerCardRect.right - st->viewerCardRect.left) - viewerPad * 2);
    const int viewerH = std::max(0, static_cast<int>(st->viewerCardRect.bottom - st->viewerCardRect.top) - viewerPad * 2);
    MoveWindow(st->viewer, st->viewerCardRect.left + viewerPad, st->viewerCardRect.top + viewerPad,
               viewerW, viewerH, TRUE);
    SetWindowPos(st->viewer, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(st->fileCombo, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(st->uploadButton, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(st->reloadButton, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(st->findEdit, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    applyViewerPageRect(st);
    InvalidateRect(hwnd, nullptr, TRUE);
}

void applyPhoneDirectoryFont(HWND hwnd, PhoneDirectoryState* st, HFONT font) {
    if (!st || !font) return;
    st->ctx.uiFont = font;
    st->font = font;
    search::apply_font_to_children(hwnd, font);
    layoutPhoneDirectory(hwnd, st);
}

LRESULT CALLBACK findEditSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                  UINT_PTR, DWORD_PTR refData) {
    auto* st = reinterpret_cast<PhoneDirectoryState*>(refData);
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        findNext(st);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_F3) {
        findNext(st);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK viewerSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                UINT_PTR, DWORD_PTR refData) {
    auto* st = reinterpret_cast<PhoneDirectoryState*>(refData);
    if (msg == WM_ERASEBKGND) {
        return TRUE;
    }
    if (g_isWindows7 && (msg == WM_VSCROLL || msg == WM_HSCROLL ||
        msg == WM_MOUSEHWHEEL)) {
        const LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
        InvalidateRect(hwnd, nullptr, FALSE);
        return result;
    }
    if (g_isWindows7 && msg == WM_MOUSEWHEEL) {
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        const int lines = delta / WHEEL_DELTA * 3;
        const WPARAM cmd = (delta > 0) ? SB_LINEUP : SB_LINEDOWN;
        for (int i = 0; i < abs(lines); ++i)
            SendMessageW(hwnd, EM_SCROLL, cmd, 0);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    if (msg == WM_SETFOCUS && st && st->findEdit) {
        PostMessageW(GetParent(hwnd), WM_PHONE_FOCUS_SEARCH, 0, 0);
    }
    if (msg == WM_KEYDOWN && wp == VK_F3) {
        findNext(st);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == L'F' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        focusFindEdit(st);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK phoneDirectoryWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs ? cs->lpCreateParams : nullptr);
        auto* st = mcs ? reinterpret_cast<PhoneDirectoryState*>(mcs->lParam) : nullptr;
        if (!st) {
            MessageBoxW(hwnd, L"常用电话窗口初始化参数无效。", L"常用电话", MB_ICONERROR);
            return -1;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        st->ownedByWindow = true;
        if (!g_richEditModule) {
            g_richEditModule = LoadLibraryW(L"Msftedit.dll");
        }
        if (!g_richEditModule) {
            MessageBoxW(hwnd, L"无法加载系统 RichEdit 组件 Msftedit.dll。", L"常用电话", MB_ICONERROR);
            return -1;
        }

        st->folderPath = joinPath(executableDirectory(), L"documents");
        ensureDirectory(st->folderPath);
        const std::wstring savedFile = search::load_module_str(kConfigSection, kConfigSelectedFile, kDefaultFileName);

        st->font = st->ctx.uiFont ? st->ctx.uiFont : search::create_ui_font(st->ctx.fontSize);
        st->fileCombo = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
                                        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                        0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_FILE_COMBO),
                                        st->ctx.instance, nullptr);
        st->uploadButton = search::create_button(hwnd, IDC_UPLOAD, L"上传", 0, 0, 0, 0);
        st->findEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                       0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_FIND_EDIT),
                                       st->ctx.instance, nullptr);
        SendMessageW(st->findEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"输入科室或电话查询"));
        st->reloadButton = search::create_button(hwnd, IDC_RELOAD, L"刷新", 0, 0, 0, 0);
        st->viewer = CreateWindowExW(0, MSFTEDIT_CLASS, L"",
                                     WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                                     WS_CLIPSIBLINGS |
                                     ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL |
                                     ES_READONLY | ES_NOHIDESEL,
                                     0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(IDC_VIEWER),
                                     st->ctx.instance, nullptr);
        if (!st->fileCombo || !st->uploadButton || !st->findEdit || !st->reloadButton || !st->viewer) {
            return -1;
        }

        SendMessageW(st->viewer, EM_SETREADONLY, TRUE, 0);
        SendMessageW(st->viewer, EM_SETEVENTMASK, 0, ENM_KEYEVENTS);
        SendMessageW(st->viewer, EM_HIDESELECTION, FALSE, 0);
        SendMessageW(st->viewer, EM_SETBKGNDCOLOR, FALSE, RGB(255, 255, 255));
        SetWindowSubclass(st->findEdit, findEditSubclass, 1, reinterpret_cast<DWORD_PTR>(st));
        SetWindowSubclass(st->viewer, viewerSubclass, 1, reinterpret_cast<DWORD_PTR>(st));
        applyPhoneDirectoryFont(hwnd, st, st->font);
        refreshFileCombo(st, savedFile, false);
        layoutPhoneDirectory(hwnd, st);
        loadPhoneFile(st);
        PostMessageW(hwnd, WM_PHONE_FOCUS_SEARCH, 0, 0);
        return 0;
    }
    case WM_SIZE:
        layoutPhoneDirectory(hwnd, stateFrom(hwnd));
        return 0;
    case WM_ERASEBKGND: {
        drawPhoneDirectoryChrome(hwnd, stateFrom(hwnd), reinterpret_cast<HDC>(wp));
        return TRUE;
    }
    case WM_PHONE_FOCUS_SEARCH:
        focusFindEdit(stateFrom(hwnd));
        return 0;
    case WM_MDIACTIVATE:
        if (reinterpret_cast<HWND>(lp) == hwnd) {
            PostMessageW(hwnd, WM_PHONE_FOCUS_SEARCH, 0, 0);
        }
        break;
    case app::WM_APP_FONT_CHANGED: {
        auto* st = stateFrom(hwnd);
        if (st && lp) {
            applyPhoneDirectoryFont(hwnd, st, reinterpret_cast<HFONT>(lp));
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == IDT_AUTO_SEARCH) {
            runAutoSearch(hwnd, stateFrom(hwnd));
            return 0;
        }
        break;
    case WM_COMMAND: {
        auto* st = stateFrom(hwnd);
        const int id = LOWORD(wp);
        if (id == IDC_FIND_EDIT && HIWORD(wp) == EN_CHANGE) {
            runAutoSearch(hwnd, st);
            return 0;
        }
        if (id == IDC_FILE_COMBO && HIWORD(wp) == CBN_SELCHANGE && st) {
            const LRESULT idx = SendMessageW(st->fileCombo, CB_GETCURSEL, 0, 0);
            if (idx >= 0) {
                wchar_t name[MAX_PATH]{};
                SendMessageW(st->fileCombo, CB_GETLBTEXT, static_cast<WPARAM>(idx), reinterpret_cast<LPARAM>(name));
                selectFileName(st, name, true);
                loadPhoneFile(st);
                runAutoSearch(hwnd, st);
            }
            return 0;
        }
        if (id == IDC_UPLOAD) {
            uploadPhoneFile(hwnd, st);
            runAutoSearch(hwnd, st);
            return 0;
        }
        if (id == IDC_RELOAD) {
            const std::wstring current = st ? st->selectedFileName : L"";
            refreshFileCombo(st, current, false);
            loadPhoneFile(st);
            runAutoSearch(hwnd, st);
            return 0;
        }
        break;
    }
    case WM_NOTIFY: {
        auto* st = stateFrom(hwnd);
        auto* hdr = reinterpret_cast<NMHDR*>(lp);
        if (st && hdr && hdr->hwndFrom == st->viewer && hdr->code == EN_MSGFILTER) {
            auto* filter = reinterpret_cast<MSGFILTER*>(lp);
            if (filter->msg == WM_KEYDOWN) {
                if (filter->wParam == VK_F3) {
                    findNext(st);
                    return 1;
                }
                if (filter->wParam == L'F' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                    focusFindEdit(st);
                    return 1;
                }
            }
        }
        break;
    }
    case WM_KEYDOWN: {
        auto* st = stateFrom(hwnd);
        if (wp == VK_F3) {
            findNext(st);
            return 0;
        }
        if (wp == L'F' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            focusFindEdit(st);
            return 0;
        }
        break;
    }
    case WM_DESTROY: {
        auto* st = stateFrom(hwnd);
        KillTimer(hwnd, IDT_AUTO_SEARCH);
        if (st) {
            if (st->findEdit) RemoveWindowSubclass(st->findEdit, findEditSubclass, 1);
            if (st->viewer) RemoveWindowSubclass(st->viewer, viewerSubclass, 1);
            if (!st->ctx.uiFont && st->font) DeleteObject(st->font);
            delete st;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        }
        break;
    }
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

HWND create_phone_directory_module(const ModuleContext& ctx) {
    if (HWND existing = activate_existing_mdi_child_by_title(ctx.mdiClient, kPhoneDirectoryTitle)) {
        return existing;
    }

    REGISTER_MDI_CHILD_CLASS(ctx.instance, phoneDirectoryWndProc, kPhoneDirectoryClass,
                             reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

    auto* st = new PhoneDirectoryState{};
    st->ctx = ctx;

    MDICREATESTRUCTW mcs{};
    mcs.szTitle = kPhoneDirectoryTitle;
    mcs.szClass = kPhoneDirectoryClass;
    mcs.hOwner = ctx.instance;
    mcs.x = CW_USEDEFAULT;
    mcs.y = CW_USEDEFAULT;
    mcs.cx = 860;
    mcs.cy = 620;
    mcs.style = WS_VISIBLE | WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    mcs.lParam = reinterpret_cast<LPARAM>(st);

    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0, reinterpret_cast<LPARAM>(&mcs)));
    if (!child) {
        if (!st->ownedByWindow) delete st;
        MessageBoxW(ctx.mdiClient, L"无法打开常用电话窗口。", L"常用电话", MB_ICONERROR);
    } else {
        SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    }
    return child;
}

#endif
