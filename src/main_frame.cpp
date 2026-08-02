#include <winsock2.h>   // must come before windows.h
#include "main_app.h"
#include "resource.h"
#include "search_text.h"

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <iphlpapi.h>
#include <commctrl.h>
#include <shellapi.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#include "app_settings.h"
#include "app_settings_io.h"
#include "backup_blood_statistics_module.h"
#include "crash_handler.h"
#include "emergency_statistics_module.h"
#include "log.h"
#include "search_ui_layout.h"
#include "barcode_module.h"
#include "blood_module.h"
#include "hiv_statistics_module.h"
#include "immune_duplicate_statistics_module.h"
#include "mchc_correction_module.h"
#include "menu_toolbar.h"
#include "module_registry.h"
#include "outpatient_query_module.h"
#include "phone_directory_module.h"
#include "quality_control_module.h"
#include "query_module.h"
#include "regular_report_module.h"
#include "settings_module.h"
#include "specimen_sign_module.h"
#include "update_config.h"
#include "update_source.h"
#include "version.h"
#include "win32_control_id.h"
namespace {

constexpr int IDM_QUERY        = 1001;
constexpr int IDM_BLOOD        = 1002;
constexpr int IDM_QCONTROL     = 1011;
constexpr int IDM_SETTINGS     = 2001;
constexpr int IDM_EXIT         = 2002;
constexpr int IDM_ABOUT        = 2003;
constexpr int IDM_CHECK_UPDATE = 2004;
constexpr int IDM_CASCADE      = 2101;
constexpr int IDM_TILE_H       = 2102;
constexpr int IDM_TILE_V       = 2103;
constexpr int IDM_ARRANGE      = 2104;
constexpr int IDM_CLOSE_ACTIVE = 2105;
constexpr int ID_TOOLBAR       = 3100;
constexpr int ID_BTNCLOSE      = 3101;
constexpr int IDM_TOOL1        = 3011;
constexpr int IDM_TOOL2        = 3012;
constexpr int IDM_TOOL3        = 3013;
constexpr int IDM_TOOL4        = 3014;
constexpr int IDM_TOOL5        = 3015;
constexpr int IDM_TOOL6        = 3016;
constexpr int IDM_STAT1        = 3021;
constexpr int IDM_STAT2        = 3022;
constexpr int IDM_STAT3        = 3023;
constexpr int IDM_STAT4        = 3024;
constexpr int IDM_STAT5        = 3025;
constexpr int ID_STATUS        = 4001;
constexpr int ID_TIMER         = 5001;
constexpr int ID_AUTO_UPDATE_TIMER = 5002;
constexpr UINT WM_AUTO_UPDATE_CHECK_DONE = WM_APP + 31;
constexpr UINT WM_MANUAL_UPDATE_CHECK_DONE = WM_APP + 32;

constexpr const wchar_t* MDI_CHILD_CLASS = L"MdiPlaceholderChild";
constexpr const wchar_t* UPDATE_CACHE_DIR_NAME = L"LISWorkbench\\UpdateCache";
constexpr const wchar_t* UPDATE_APP_EXE_NAME = L"lis_workbench.exe";
constexpr const wchar_t* UPDATE_UPDATER_EXE_NAME = L"Updater.exe";

app::Context g_ctx;
bool g_manualUpdateChecking = false;

struct MenuDrawText {
    std::wstring text;
    bool topLevel = false;
};

struct AutoUpdateCheckDone {
    bool ok = false;
    bool updateAvailable = false;
    std::string version;
    std::string error;
};

struct ManualUpdateCheckDone {
    bool ok = false;
    lis_update::UpdateCheckResult result;
    std::string error;
};

struct UpdateSourceConfig {
    std::wstring sourceType;
    std::wstring manifestUrl;
    std::wstring folderPath;
};

std::vector<std::unique_ptr<MenuDrawText>> g_menuDrawTexts;

MenuDrawText* addMenuDrawText(const wchar_t* text, bool topLevel) {
    g_menuDrawTexts.push_back(std::make_unique<MenuDrawText>(MenuDrawText{text ? text : L"", topLevel}));
    return g_menuDrawTexts.back().get();
}

HFONT createMenuFont(int pointSize) {
    NONCLIENTMETRICSW nm{};
    nm.cbSize = sizeof(nm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(nm), &nm, 0);
    LOGFONTW lf = nm.lfMenuFont;
    HDC screen = GetDC(nullptr);
    lf.lfHeight = -MulDiv(pointSize, GetDeviceCaps(screen, LOGPIXELSY), 72);
    ReleaseDC(nullptr, screen);
    return CreateFontIndirectW(&lf);
}

void applyFontToChildren(HWND root, HFONT font) {
    if (!root || !font) return;
    EnumChildWindows(root, [](HWND child, LPARAM p) -> BOOL {
        if (GetDlgCtrlID(child) == ID_STATUS) return TRUE;
        SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(p), TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(font));
    InvalidateRect(root, nullptr, TRUE);
}

void broadcastFontChanged() {
    const LPARAM fontParam = reinterpret_cast<LPARAM>(g_ctx.uiFont);
    if (g_ctx.mdiClient) {
        HWND child = GetWindow(g_ctx.mdiClient, GW_CHILD);
        while (child) {
            SendMessageW(child, app::WM_APP_FONT_CHANGED, 0, fontParam);
            child = GetWindow(child, GW_HWNDNEXT);
        }
    }
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM p) -> BOOL {
        SendMessageW(hwnd, app::WM_APP_FONT_CHANGED, 0, p);
        return TRUE;
    }, fontParam);
}

void broadcastSettingsChangedToMdiChildren() {
    if (!g_ctx.mdiClient) return;
    HWND child = GetWindow(g_ctx.mdiClient, GW_CHILD);
    while (child) {
        SendMessageW(child, app::WM_APP_SETTINGS_CHANGED, 0, 0);
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

void rebuildUiFont(int fontSize) {
    g_ctx.fontSize = search::clamp_font_size(fontSize);
    HFONT newFont = search::create_ui_font(g_ctx.fontSize);
    HFONT newMenuFont = createMenuFont(g_ctx.fontSize);
    if (!newFont || !newMenuFont) {
        if (newFont) DeleteObject(newFont);
        if (newMenuFont) DeleteObject(newMenuFont);
        return;
    }
    HFONT oldFont = g_ctx.uiFont;
    HFONT oldMenuFont = g_ctx.menuFont;
    g_ctx.uiFont = newFont;
    g_ctx.menuFont = newMenuFont;
    applyFontToChildren(g_ctx.mainWindow, g_ctx.uiFont);
    HWND tb = GetDlgItem(g_ctx.mainWindow, ID_TOOLBAR);
    if (tb) {
        mtSetFont(tb, g_ctx.menuFont);
        RECT rc{};
        GetClientRect(g_ctx.mainWindow, &rc);
        SendMessageW(g_ctx.mainWindow, WM_SIZE, 0, MAKELPARAM(rc.right - rc.left, rc.bottom - rc.top));
    }
    if (g_ctx.mainWindow) DrawMenuBar(g_ctx.mainWindow);
    broadcastFontChanged();
    if (oldFont) DeleteObject(oldFont);
    if (oldMenuFont) DeleteObject(oldMenuFont);
}

LRESULT CALLBACK mdiChildProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            wchar_t title[256];
            GetWindowTextW(hwnd, title, 256);
            CreateWindowExW(0, L"STATIC", title,
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 0, 0, 0, hwnd, nullptr, g_ctx.instance, nullptr);
            return 0;
        }
        case WM_SIZE: {
            HWND label = GetWindow(hwnd, GW_CHILD);
            if (label) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                MoveWindow(label, 0, 0, rc.right, rc.bottom, TRUE);
            }
            return 0;
        }
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

HWND createMdiChild(const wchar_t* title) {
    MDICREATESTRUCTW mcs{};
    mcs.szClass = MDI_CHILD_CLASS;
    mcs.szTitle = title;
    mcs.hOwner = g_ctx.instance;
    mcs.x = CW_USEDEFAULT;
    mcs.y = CW_USEDEFAULT;
    mcs.cx = CW_USEDEFAULT;
    mcs.cy = CW_USEDEFAULT;
    HWND child = reinterpret_cast<HWND>(SendMessageW(g_ctx.mdiClient, WM_MDICREATE, 0,
        reinterpret_cast<LPARAM>(&mcs)));
    SendMessageW(g_ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    return child;
}

HWND getActiveMdiChild() {
    if (!g_ctx.mdiClient) return nullptr;
    return reinterpret_cast<HWND>(SendMessageW(g_ctx.mdiClient, WM_MDIGETACTIVE, 0, 0));
}

int toolbarCommandForMdiChild(HWND child) {
    if (!child) return 0;
    wchar_t title[256]{};
    if (!GetWindowTextW(child, title, 256)) return 0;
    if (lstrcmpW(title, L"标本签收中心") == 0) return IDM_TOOL3;
    if (lstrcmpW(title, L"常规报告") == 0) return IDM_TOOL2;
    if (lstrcmpW(title, L"输血结果查询") == 0) return IDM_BLOOD;
    if (lstrcmpW(title, L"检验结果查询") == 0) return IDM_QUERY;
    if (lstrcmpW(title, L"质控分析") == 0) return IDM_QCONTROL;
    if (lstrcmpW(title, L"门诊查询") == 0) return IDM_TOOL5;
    if (lstrcmpW(title, L"常用电话") == 0) return IDM_TOOL6;
    return 0;
}

void updateToolbarState() {
    HWND tb = GetDlgItem(g_ctx.mainWindow, ID_TOOLBAR);
    if (!tb) return;
    HWND active = getActiveMdiChild();
    mtSetActiveButton(tb, toolbarCommandForMdiChild(active));
    mtEnableButton(tb, ID_BTNCLOSE, active != nullptr);
}

void closeActiveMdiChild() {
    HWND active = getActiveMdiChild();
    if (active) SendMessageW(g_ctx.mdiClient, WM_MDIDESTROY, reinterpret_cast<WPARAM>(active), 0);
    updateToolbarState();
}

// ── placeholder factories (to be replaced with real modules) ────

HWND create_tool4_placeholder(const ModuleContext&) { return createMdiChild(L"工具4"); }
HWND create_stat5_placeholder(const ModuleContext&) { return createMdiChild(L"统计分析5"); }

// ── module registry ─────────────────────────────────────────────

const ModuleDef g_modules[] = {
    { L"Query",    L"检验管理", L"检验结果查询(&Q)...", IDM_QUERY,    create_query_module    },
    { L"Blood",    L"检验管理", L"输血结果查询(&B)...", IDM_BLOOD,    create_blood_module },
    { L"QualityControl", L"检验管理", L"质控分析(&C)", IDM_QCONTROL, create_quality_control_module },
    { L"Barcode",  L"工具",     L"已签收条码查询(&1)...", IDM_TOOL1,   create_barcode_module },
    { L"MchcCorrection", L"工具", L"脂血MCHC校正(&2)", IDM_TOOL4, create_mchc_correction_module },
    { L"OutpatientQuery", L"工具", L"门诊查询(&3)", IDM_TOOL5, create_outpatient_query_module },
    { L"PhoneDirectory", L"工具", L"常用电话(&4)",       IDM_TOOL6,   create_phone_directory_module },
    { L"HivStatistics", L"统计分析管理", L"HIV 抗体检测统计(&1)", IDM_STAT1, create_hiv_statistics_module },
    { L"EmergencyStatistics", L"统计分析管理", L"急诊样本统计(&2)", IDM_STAT2, create_emergency_statistics_module },
    { L"ImmuneDuplicateStatistics", L"统计分析管理", L"免疫重复项目统计(&3)", IDM_STAT3, create_immune_duplicate_statistics_module },
    { L"BackupBloodStatistics", L"统计分析管理", L"备血统计(&4)", IDM_STAT4, create_backup_blood_statistics_module },
    { L"Stat5",    L"统计分析管理", L"统计分析5(&5)",    IDM_STAT5,   create_stat5_placeholder },
    { L"Settings", L"系统",     L"系统设置(&S)...",     IDM_SETTINGS, create_settings_module  },
};
constexpr int g_moduleCount = sizeof(g_modules) / sizeof(g_modules[0]);

// ── helpers ─────────────────────────────────────────────────────

void updateStatusBarParts(HWND sb, int clientWidth) {
    int ipW  = (std::max)(220, clientWidth * 22 / 100);
    int timeW = (std::max)(300, clientWidth * 26 / 100);
    int parts[] = {clientWidth * 30 / 100, clientWidth - ipW - timeW, clientWidth - timeW, clientWidth};
    SendMessageW(sb, SB_SETPARTS, 4, (LPARAM)parts);
    SendMessageW(sb, WM_SIZE, 0, 0);
}

ModuleContext makeCtx() {
    return { g_ctx.mdiClient, g_ctx.instance, g_ctx.uiFont,
             g_ctx.dbSettings, g_ctx.fontSize, &g_ctx };
}

bool dispatchToolbarOnlyModule(int id) {
    switch (id) {
        case IDM_TOOL2:
            create_regular_report_module(makeCtx());
            updateToolbarState();
            return true;
        case IDM_TOOL3:
            create_specimen_sign_module(makeCtx());
            updateToolbarState();
            return true;
        default:
            return false;
    }
}

void setMainStatusText(const wchar_t* text) {
    HWND sb = GetDlgItem(g_ctx.mainWindow, ID_STATUS);
    if (sb) SendMessageW(sb, SB_SETTEXT, 0, reinterpret_cast<LPARAM>(text ? text : L""));
}

UpdateSourceConfig loadUpdateSourceConfig() {
    UpdateSourceConfig cfg;
    cfg.sourceType = search::load_module_str(
        lis_update::kConfigSection, L"SourceType", lis_update::kSourceFolder);
    if (cfg.sourceType != lis_update::kSourceHttp) {
        cfg.sourceType = lis_update::kSourceFolder;
    }

    cfg.manifestUrl = search::load_module_str(
        lis_update::kConfigSection, L"ManifestUrl", lis_update::kDefaultGithubManifestUrl);
    if (cfg.manifestUrl.empty()) {
        cfg.manifestUrl = lis_update::kDefaultGithubManifestUrl;
    }
    cfg.folderPath = search::load_module_str(lis_update::kConfigSection, L"FolderPath", L"");
    return cfg;
}

bool localFileExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring quoteProcessArg(const std::wstring& value) {
    std::wstring out = L"\"";
    for (wchar_t ch : value) {
        if (ch == L'"') out += L'\\';
        out += ch;
    }
    out += L"\"";
    return out;
}

std::wstring programDataUpdateCacheDir() {
    DWORD needed = GetEnvironmentVariableW(L"ProgramData", nullptr, 0);
    std::wstring base;
    if (needed > 0) {
        std::wstring buffer(static_cast<size_t>(needed), L'\0');
        DWORD written = GetEnvironmentVariableW(L"ProgramData", buffer.data(), needed);
        if (written > 0 && written < needed) {
            buffer.resize(static_cast<size_t>(written));
            base = buffer;
        }
    }
    if (base.empty()) {
        base = search::module_dir();
    }
    return lis_update::join_update_path(base, UPDATE_CACHE_DIR_NAME);
}

bool launchUpdaterForPackage(HWND hwnd, const std::wstring& package_path) {
    const std::wstring app_dir = search::module_dir();
    const std::wstring updater_path = lis_update::join_update_path(app_dir, UPDATE_UPDATER_EXE_NAME);
    if (!localFileExists(updater_path)) {
        MessageBoxW(hwnd, L"安装目录中未找到 Updater.exe。", L"安装更新", MB_ICONERROR);
        return false;
    }

    std::wstring args;
    args += L"--app-dir " + quoteProcessArg(app_dir);
    args += L" --app-exe " + quoteProcessArg(UPDATE_APP_EXE_NAME);
    args += L" --package-file " + quoteProcessArg(package_path);
    args += L" --pid " + std::to_wstring(GetCurrentProcessId());

    HINSTANCE started = ShellExecuteW(nullptr, L"open", updater_path.c_str(), args.c_str(),
                                      app_dir.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(started) <= 32) {
        MessageBoxW(hwnd, L"启动 Updater.exe 失败。", L"安装更新", MB_ICONERROR);
        return false;
    }

    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    return true;
}

void startManualUpdateCheck(HWND hwnd) {
    if (g_manualUpdateChecking) {
        MessageBoxW(hwnd, L"正在检查更新，请稍候。", L"检查更新", MB_ICONINFORMATION);
        return;
    }

    const UpdateSourceConfig cfg = loadUpdateSourceConfig();
    if (cfg.sourceType == lis_update::kSourceHttp) {
        if (cfg.manifestUrl.empty()) {
            MessageBoxW(hwnd, L"请先在系统设置中填写 HTTP manifest URL。", L"检查更新", MB_ICONWARNING);
            return;
        }
    } else if (cfg.folderPath.empty()) {
        MessageBoxW(hwnd, L"请先在系统设置中填写共享文件夹路径。", L"检查更新", MB_ICONWARNING);
        return;
    }

    g_manualUpdateChecking = true;
    setMainStatusText(L"正在检查更新...");

    const std::wstring cache_dir = programDataUpdateCacheDir();
    std::thread([hwnd, cfg, cache_dir]() {
        auto* done = new ManualUpdateCheckDone;
        std::unique_ptr<lis_update::IUpdateSource> source;
        if (cfg.sourceType == lis_update::kSourceHttp) {
            source = std::make_unique<lis_update::HttpUpdateSource>(cfg.manifestUrl);
        } else {
            source = std::make_unique<lis_update::FolderUpdateSource>(cfg.folderPath);
        }

        done->ok = lis_update::check_and_fetch_update(*source, search::kVersion,
                                                      cache_dir, done->result, done->error);
        if (!PostMessageW(hwnd, WM_MANUAL_UPDATE_CHECK_DONE, 0, reinterpret_cast<LPARAM>(done))) {
            delete done;
        }
    }).detach();
}

void showManualUpdateCheckResult(HWND hwnd, const ManualUpdateCheckDone& done) {
    g_manualUpdateChecking = false;
    setMainStatusText(L"就绪");

    if (!done.ok) {
        MessageBoxW(hwnd, search::utf8_to_wide(done.error).c_str(), L"检查更新失败", MB_ICONERROR);
        return;
    }

    if (!done.result.update_available) {
        const std::wstring message = L"当前已是最新版本。\r\n当前版本：" +
                                     search::utf8_to_wide(search::kVersion);
        MessageBoxW(hwnd, message.c_str(), L"检查更新", MB_ICONINFORMATION);
        return;
    }

    const std::wstring message =
        L"发现新版本：" + search::utf8_to_wide(done.result.manifest.version) +
        L"\r\n更新包已缓存到：\r\n" + done.result.package_path +
        L"\r\n\r\n是否立即安装并重启程序？";
    const int answer = MessageBoxW(hwnd, message.c_str(), L"检查更新",
                                   MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2);
    if (answer == IDYES) {
        launchUpdaterForPackage(hwnd, done.result.package_path);
    }
}

std::wstring todayUpdateStamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[16]{};
    swprintf(buf, 16, L"%04d%02d%02d", st.wYear, st.wMonth, st.wDay);
    return buf;
}

bool shouldAutoCheckUpdateToday() {
    if (!search::load_module_int(lis_update::kConfigSection, L"AutoCheck", 0)) {
        return false;
    }
    const std::wstring today = todayUpdateStamp();
    const std::wstring last = search::load_module_str(lis_update::kConfigSection, L"LastCheckTime", L"");
    return last != today;
}

void markAutoCheckUpdateToday() {
    search::save_module_str(lis_update::kConfigSection, L"LastCheckTime", todayUpdateStamp());
}

void startAutoUpdateCheck(HWND hwnd) {
    const UpdateSourceConfig cfg = loadUpdateSourceConfig();

    if (cfg.sourceType == lis_update::kSourceHttp && cfg.manifestUrl.empty()) return;
    if (cfg.sourceType != lis_update::kSourceHttp && cfg.folderPath.empty()) return;

    std::thread([hwnd, cfg]() {
        auto* done = new AutoUpdateCheckDone;
        std::unique_ptr<lis_update::IUpdateSource> source;
        if (cfg.sourceType == lis_update::kSourceHttp) {
            source = std::make_unique<lis_update::HttpUpdateSource>(cfg.manifestUrl);
        } else {
            source = std::make_unique<lis_update::FolderUpdateSource>(cfg.folderPath);
        }

        lis_update::UpdateManifest manifest;
        done->ok = source->fetch_manifest(manifest, done->error);
        if (done->ok && lis_update::compare_version_strings(manifest.version, search::kVersion) > 0) {
            done->updateAvailable = true;
            done->version = manifest.version;
        }
        if (!PostMessageW(hwnd, WM_AUTO_UPDATE_CHECK_DONE, 0, reinterpret_cast<LPARAM>(done))) {
            delete done;
        }
    }).detach();
}

void showAutoUpdateCheckResult(HWND hwnd, const AutoUpdateCheckDone& done) {
    if (!done.ok || !done.updateAvailable) return;
    const std::wstring message =
        L"发现新版本：" + search::utf8_to_wide(done.version) +
        L"\r\n是否现在下载并安装更新？";
    const int answer = MessageBoxW(hwnd, message.c_str(), L"自动检查更新",
                                   MB_ICONINFORMATION | MB_YESNO | MB_DEFBUTTON2);
    if (answer == IDYES) {
        startManualUpdateCheck(hwnd);
    }
}

void appendOwnerDrawItem(HMENU menu, UINT_PTR id, const wchar_t* text, bool enabled = true) {
    UINT flags = MF_OWNERDRAW | (enabled ? MF_ENABLED : MF_GRAYED);
    AppendMenuW(menu, flags, id, reinterpret_cast<LPCWSTR>(addMenuDrawText(text, false)));
}

// ── menu setup ──────────────────────────────────────────────────

HMENU setupMenus(HWND hwnd) {
    HMENU bar = CreateMenu();
    HMENU subMenus[8]{};
    const wchar_t* subNames[8]{};
    int subCount = 0;

    // Auto-generate menus from g_modules[], grouped by menuParent
    for (int i = 0; i < g_moduleCount; i++) {
        const auto& m = g_modules[i];
        int idx = -1;
        for (int j = 0; j < subCount; j++) {
            if (wcscmp(subNames[j], m.menuParent) == 0) { idx = j; break; }
        }
        if (idx < 0) {
            subMenus[subCount] = CreatePopupMenu();
            subNames[subCount] = m.menuParent;
            AppendMenuW(bar, MF_POPUP | MF_OWNERDRAW, (UINT_PTR)subMenus[subCount],
                        reinterpret_cast<LPCWSTR>(addMenuDrawText(m.menuParent, true)));
            idx = subCount++;
        }
        appendOwnerDrawItem(subMenus[idx], static_cast<UINT_PTR>(m.menuId), m.menuLabel);
    }

    // Fixed: window menu
    HMENU windowMenu = CreatePopupMenu();
    appendOwnerDrawItem(windowMenu, IDM_CASCADE, L"层叠(&C)");
    appendOwnerDrawItem(windowMenu, IDM_TILE_H, L"水平平铺(&H)");
    appendOwnerDrawItem(windowMenu, IDM_TILE_V, L"垂直平铺(&V)");
    AppendMenuW(windowMenu, MF_SEPARATOR, 0, nullptr);
    appendOwnerDrawItem(windowMenu, IDM_ARRANGE, L"排列图标(&A)");
    AppendMenuW(windowMenu, MF_SEPARATOR, 0, nullptr);
    appendOwnerDrawItem(windowMenu, IDM_CLOSE_ACTIVE, L"关闭当前(&L)");
    AppendMenuW(bar, MF_POPUP | MF_OWNERDRAW, (UINT_PTR)windowMenu,
                reinterpret_cast<LPCWSTR>(addMenuDrawText(L"窗口(&W)", true)));

    // Fixed items appended to last menu (L"系统")
    for (int j = 0; j < subCount; j++) {
        if (wcscmp(subNames[j], L"系统") == 0) {
            appendOwnerDrawItem(subMenus[j], IDM_CHECK_UPDATE, L"检查更新(&U)...");
            AppendMenuW(subMenus[j], MF_SEPARATOR, 0, nullptr);
            appendOwnerDrawItem(subMenus[j], IDM_ABOUT, L"关于(&A)...");
            appendOwnerDrawItem(subMenus[j], IDM_EXIT, L"退出(&X)");
            break;
        }
    }

    SetMenu(hwnd, bar);
    return windowMenu;
}

void updateTimePane(HWND hwnd) {
    HWND sb = GetDlgItem(hwnd, ID_STATUS);
    if (!sb) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[64];
    swprintf(buf, 64, L"当前时间：%d年%d月%d日 %d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    SendMessageW(sb, SB_SETTEXT, MAKEWPARAM(3, SBT_NOBORDERS), (LPARAM)buf);
}

std::wstring getLocalIp() {
    ULONG bufLen = 0;
    GetAdaptersInfo(nullptr, &bufLen);
    if (bufLen == 0) return L"0.0.0.0";
    std::vector<BYTE> buf(bufLen);
    auto* p = reinterpret_cast<PIP_ADAPTER_INFO>(buf.data());
    if (GetAdaptersInfo(p, &bufLen) != ERROR_SUCCESS) return L"0.0.0.0";
    while (p) {
        if (p->IpAddressList.IpAddress.String[0] != '0' &&
            strcmp(p->IpAddressList.IpAddress.String, "127.0.0.1") != 0) {
            return search::utf8_to_wide(p->IpAddressList.IpAddress.String);
        }
        p = p->Next;
    }
    return L"0.0.0.0";
}

void setupStatusBar(HWND hwnd) {
    HWND sb = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE,
                              0, 0, 0, 0, hwnd, win32_control_id(ID_STATUS),
                              g_ctx.instance, nullptr);
    RECT rc;
    GetClientRect(hwnd, &rc);
    updateStatusBarParts(sb, rc.right - rc.left);
    SendMessageW(sb, SB_SETTEXT, 0, (LPARAM)L"就绪");
    std::wstring ip = L"本机：" + getLocalIp();
    SendMessageW(sb, SB_SETTEXT, MAKEWPARAM(2, SBT_NOBORDERS), (LPARAM)ip.c_str());
}

bool measureMenuItem(LPMEASUREITEMSTRUCT mi) {
    if (!mi || mi->CtlType != ODT_MENU || !mi->itemData) return false;
    auto* data = reinterpret_cast<MenuDrawText*>(mi->itemData);
    HDC dc = GetDC(g_ctx.mainWindow);
    HFONT old = g_ctx.menuFont ? static_cast<HFONT>(SelectObject(dc, g_ctx.menuFont)) : nullptr;
    RECT textRc{0, 0, 0, 0};
    DrawTextW(dc, data->text.c_str(), -1, &textRc, DT_SINGLELINE | DT_CALCRECT);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    if (old) SelectObject(dc, old);
    ReleaseDC(g_ctx.mainWindow, dc);
    const int horizontalPad = data->topLevel ? 22 : 42;
    mi->itemWidth = (std::max)(textRc.right - textRc.left + horizontalPad, tm.tmAveCharWidth * 4);
    mi->itemHeight = tm.tmHeight + 8;
    return true;
}

bool drawMenuItem(LPDRAWITEMSTRUCT di) {
    if (!di || di->CtlType != ODT_MENU || !di->itemData) return false;
    auto* data = reinterpret_cast<MenuDrawText*>(di->itemData);
    const bool selected = (di->itemState & ODS_SELECTED) != 0;
    HBRUSH bg = GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_MENU);
    FillRect(di->hDC, &di->rcItem, bg);

    HFONT old = g_ctx.menuFont ? static_cast<HFONT>(SelectObject(di->hDC, g_ctx.menuFont)) : nullptr;
    SetBkMode(di->hDC, TRANSPARENT);
    const bool disabled = (di->itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
    SetTextColor(di->hDC, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : (disabled ? COLOR_GRAYTEXT : COLOR_MENUTEXT)));
    RECT textRc = di->rcItem;
    textRc.left += data->topLevel ? 10 : 24;
    textRc.right -= data->topLevel ? 10 : 16;
    DrawTextW(di->hDC, data->text.c_str(), -1, &textRc,
              DT_SINGLELINE | DT_VCENTER | (data->topLevel ? DT_CENTER : DT_LEFT));
    if (old) SelectObject(di->hDC, old);
    return true;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            HMENU windowMenu = setupMenus(hwnd);

            WNDCLASSEXW childWc{};
            childWc.cbSize = sizeof(childWc);
            childWc.lpfnWndProc = mdiChildProc;
            childWc.hInstance = g_ctx.instance;
            childWc.hIcon = LoadIconW(g_ctx.instance, MAKEINTRESOURCEW(IDI_APP));
            childWc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            childWc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            childWc.lpszClassName = MDI_CHILD_CLASS;
            RegisterClassExW(&childWc);

            CLIENTCREATESTRUCT ccs{};
            ccs.hWindowMenu = windowMenu;
            ccs.idFirstChild = 5000;
            g_ctx.mdiClient = CreateWindowExW(WS_EX_CLIENTEDGE, L"MDICLIENT", L"",
                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL,
                0, 0, 0, 0, hwnd, nullptr, g_ctx.instance, &ccs);

            HWND tb = mtCreate(hwnd, g_ctx.instance, g_ctx.menuFont, ID_TOOLBAR);
            mtAddButton(tb, L"标本签收中心", IDM_TOOL3);
            mtAddButton(tb, L"常规报告", IDM_TOOL2);
            mtAddButton(tb, L"输血查询", IDM_BLOOD);
            mtAddButton(tb, L"结果查询", IDM_QUERY);
            mtAddButton(tb, L"质控分析", IDM_QCONTROL);
            mtAddButton(tb, L"门诊查询", IDM_TOOL5);
            mtAddButton(tb, L"常用电话", IDM_TOOL6);
            mtAddStretch(tb);
            mtAddCloseButton(tb, L"关闭当前", ID_BTNCLOSE, false);

            setupStatusBar(hwnd);
            updateTimePane(hwnd);
            SetTimer(hwnd, ID_TIMER, 1000, nullptr);
            if (shouldAutoCheckUpdateToday()) {
                markAutoCheckUpdateToday();
                SetTimer(hwnd, ID_AUTO_UPDATE_TIMER, 15000, nullptr);
            }
            return 0;
        }
        case WM_MDIACTIVATE:
            updateToolbarState();
            break;
        case WM_TIMER: {
            if (wp == ID_TIMER) {
                updateTimePane(hwnd);
            } else if (wp == ID_AUTO_UPDATE_TIMER) {
                KillTimer(hwnd, ID_AUTO_UPDATE_TIMER);
                startAutoUpdateCheck(hwnd);
            }
            return 0;
        }
        case WM_AUTO_UPDATE_CHECK_DONE: {
            std::unique_ptr<AutoUpdateCheckDone> done(
                reinterpret_cast<AutoUpdateCheckDone*>(lp));
            if (done) {
                showAutoUpdateCheckResult(hwnd, *done);
            }
            return 0;
        }
        case WM_MANUAL_UPDATE_CHECK_DONE: {
            std::unique_ptr<ManualUpdateCheckDone> done(
                reinterpret_cast<ManualUpdateCheckDone*>(lp));
            if (done) {
                showManualUpdateCheckResult(hwnd, *done);
            }
            return 0;
        }
        case WM_SIZE: {
            HWND tb = GetDlgItem(hwnd, ID_TOOLBAR);
            int tbH = tb ? mtGetHeight(tb) : 28;
            if (tb) MoveWindow(tb, 0, 0, LOWORD(lp), tbH, TRUE);

            RECT sbRc{};
            HWND sb = GetDlgItem(hwnd, ID_STATUS);
            if (sb) GetWindowRect(sb, &sbRc);
            int sbH = sb ? (sbRc.bottom - sbRc.top) : 24;
            if (g_ctx.mdiClient)
                MoveWindow(g_ctx.mdiClient, 0, tbH, LOWORD(lp), HIWORD(lp) - tbH - sbH, TRUE);

            if (sb) updateStatusBarParts(sb, LOWORD(lp));
            return 0;
        }
        case WM_MEASUREITEM:
            if (measureMenuItem(reinterpret_cast<LPMEASUREITEMSTRUCT>(lp))) return TRUE;
            break;
        case WM_DRAWITEM:
            if (drawMenuItem(reinterpret_cast<LPDRAWITEMSTRUCT>(lp))) return TRUE;
            break;
        case WM_COMMAND: {
            int id = LOWORD(wp);

            // Module registry dispatch
            for (int i = 0; i < g_moduleCount; i++) {
                if (g_modules[i].menuId == id) {
                    g_modules[i].create(makeCtx());
                    updateToolbarState();
                    return 0;
                }
            }

            if (dispatchToolbarOnlyModule(id)) return 0;

            // Fixed items
            switch (id) {
                case IDM_ABOUT:
                {
                    const std::wstring aboutText =
                        L"LIS 工作台\n版本 " + search::utf8_to_wide(search::kVersion) +
                        L"\n\n作者：Zhao Wang";
                    MessageBoxW(hwnd,
                        aboutText.c_str(),
                        L"关于", MB_ICONINFORMATION);
                    return 0;
                }
                case ID_BTNCLOSE:        closeActiveMdiChild(); return 0;
                case IDM_CHECK_UPDATE:   startManualUpdateCheck(hwnd); return 0;
                case IDM_EXIT:           DestroyWindow(hwnd); return 0;
                case IDM_CASCADE:        SendMessageW(g_ctx.mdiClient, WM_MDICASCADE, 0, 0); return 0;
                case IDM_TILE_H:         SendMessageW(g_ctx.mdiClient, WM_MDITILE, MDITILE_HORIZONTAL, 0); return 0;
                case IDM_TILE_V:         SendMessageW(g_ctx.mdiClient, WM_MDITILE, MDITILE_VERTICAL, 0); return 0;
                case IDM_ARRANGE:        SendMessageW(g_ctx.mdiClient, WM_MDIICONARRANGE, 0, 0); return 0;
                case IDM_CLOSE_ACTIVE:   closeActiveMdiChild(); return 0;
            }
            break;
        }
        case app::WM_APP_SETTINGS_CHANGED:
            rebuildUiFont(g_ctx.fontSize);
            broadcastSettingsChangedToMdiChildren();
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, ID_TIMER);
            KillTimer(hwnd, ID_AUTO_UPDATE_TIMER);
            PostQuitMessage(0);
            return 0;
    }
    return DefFrameProcW(hwnd, g_ctx.mdiClient, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    install_crash_handler();
    applog::init(L"log");
    LOG_INFO("LIS 工作台 starting");

    g_ctx.instance = instance;
    auto iniSettings = search::load_settings(search::default_ini_path());
    g_ctx.dbSettings = iniSettings.db;
    g_ctx.fontSize = search::clamp_font_size(iniSettings.ui.font_size);
    g_ctx.uiFont = search::create_ui_font(g_ctx.fontSize);
    g_ctx.menuFont = createMenuFont(g_ctx.fontSize);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_APPWORKSPACE + 1);
    wc.lpszClassName = L"LISWorkbenchMainWindow";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName,
        L"LIS 工作台",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 800,
        nullptr, nullptr, instance, nullptr);

    g_ctx.mainWindow = hwnd;
    ShowWindow(hwnd, SW_MAXIMIZE);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!TranslateMDISysAccel(g_ctx.mdiClient, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (g_ctx.uiFont) DeleteObject(g_ctx.uiFont);
    if (g_ctx.menuFont) DeleteObject(g_ctx.menuFont);
    return 0;
}
