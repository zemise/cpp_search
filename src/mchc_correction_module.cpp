#include "mchc_correction_module.h"

#ifdef _WIN32

#include "main_app.h"
#include "resource.h"
#include "search_text.h"
#include "search_ui_layout.h"
#include "win32_control_id.h"

#include <commctrl.h>
#include <windows.h>
#include <windowsx.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr const wchar_t* WND_CLASS     = L"MchcCorrectionModuleChild";
constexpr const wchar_t* WINDOW_TITLE  = L"脂血 MCHC 校正";
constexpr const wchar_t* PROP_STATE    = L"MchcCorrectionSt";

// ── card header accent colors ────────────────────────────────────

constexpr COLORREF kBeforeHead  = RGB(0x3B, 0x82, 0xF6);  // blue
constexpr COLORREF kAfterHead   = RGB(0xF5, 0x9E, 0x0B);  // amber
constexpr COLORREF kResultHead  = RGB(0x10, 0xB9, 0x81);  // green
constexpr COLORREF kHeadText    = RGB(0xFF, 0xFF, 0xFF);

// ── control ids ──────────────────────────────────────────────────

enum {
    IDC_RAW_HGB      = 4701,
    IDC_RAW_HCT      = 4702,
    IDC_RAW_RBC      = 4703,
    IDC_PLASMA_HGB   = 4704,
    IDC_CORRECTED_HGB  = 4705,
    IDC_CORRECTED_MCH  = 4706,
    IDC_CORRECTED_MCHC = 4707,
    IDC_CLEAR        = 4709,
    IDC_STATUS       = 4711,
};

constexpr UINT_PTR IDT_AUTO_CALC = 4720;
constexpr UINT_PTR SUBCLASS_INPUT = 1;

// ── card layout ──────────────────────────────────────────────────

struct CardSlot {
    const wchar_t* label;
    HWND hwnd;
};

struct Card {
    RECT rect;
    const wchar_t* title;
    COLORREF headColor;
    std::vector<CardSlot> slots;
};

struct State {
    ModuleContext ctx;
    HWND rawHgb  = nullptr;
    HWND rawHct  = nullptr;
    HWND rawRbc  = nullptr;
    HWND plasmaHgb = nullptr;
    HWND correctedHgb  = nullptr;
    HWND correctedMch  = nullptr;
    HWND correctedMchc = nullptr;
    HWND clearBtn = nullptr;
    HWND status = nullptr;
    HBRUSH bgBrush = nullptr;
    std::vector<Card> cards;
    RECT   referenceRect{};   // updated during WM_PAINT, used for hit-test+hand cursor
};

// ── input cue-banner subclass ────────────────────────────────────

struct InputCueBanner {
    const wchar_t* cue = nullptr;
};

LRESULT CALLBACK inputCueProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                              UINT_PTR /*subclassId*/, DWORD_PTR refData) {
    auto* cue = reinterpret_cast<InputCueBanner*>(refData);
    switch (msg) {
        case WM_PAINT: {
            // Let EDIT draw normally first
            DefSubclassProc(hwnd, msg, wp, lp);
            // Draw cue only when empty and unfocused
            if (!cue || !cue->cue) break;
            int len = GetWindowTextLengthW(hwnd);
            if (len > 0) break;
            if (GetFocus() == hwnd) break;
            HDC dc = GetDC(hwnd);
            if (dc) {
                RECT rc{};
                GetClientRect(hwnd, &rc);
                InflateRect(&rc, -4, -1);
                HGDIOBJ oldFont = SelectObject(dc, reinterpret_cast<HFONT>(
                    SendMessageW(hwnd, WM_GETFONT, 0, 0)));
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, RGB(0xB0, 0xB8, 0xC4));
                DrawTextW(dc, cue->cue, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(dc, oldFont);
                ReleaseDC(hwnd, dc);
            }
            return 0;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(hwnd, nullptr, TRUE);
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, inputCueProc, SUBCLASS_INPUT);
            delete cue;
            break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

// ── helpers ──────────────────────────────────────────────────────

int S(HWND hwnd, int value) {
    return static_cast<int>(value * search::dpi_scale_factor(hwnd));
}

void setStatus(State* st, const wchar_t* text) {
    if (st && st->status) SetWindowTextW(st->status, text);
}

std::string editText(HWND hwnd) {
    wchar_t buf[64]{};
    GetWindowTextW(hwnd, buf, 64);
    return search::wide_to_utf8(buf);
}

void setEdit(HWND hwnd, const std::string& text) {
    SetWindowTextW(hwnd, search::utf8_to_wide(text).c_str());
}

void setEdit(HWND hwnd, const wchar_t* text) {
    SetWindowTextW(hwnd, text);
}

// ── toast popup ("已复制") ──────────────────────────────────────

LRESULT CALLBACK toastWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            SetTimer(hwnd, 1, 1400, nullptr);
            return 0;
        }
        case WM_TIMER:
            if (wp == 1) {
                KillTimer(hwnd, 1);
                DestroyWindow(hwnd);
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc{};
            GetClientRect(hwnd, &rc);
            // dark rounded bg
            HBRUSH bg = CreateSolidBrush(RGB(0x30, 0x30, 0x30));
            HPEN   pn = CreatePen(PS_SOLID, 1, RGB(0x30, 0x30, 0x30));
            HGDIOBJ oldBr = SelectObject(dc, bg);
            HGDIOBJ oldPn = SelectObject(dc, pn);
            RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 8, 8);
            SelectObject(dc, oldBr);
            SelectObject(dc, oldPn);
            DeleteObject(bg);
            DeleteObject(pn);
            // white text
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(0xFF, 0xFF, 0xFF));
            InflateRect(&rc, -10, -4);
            DrawTextW(dc, L"已复制", -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void showCopyToast(HWND parent, HWND nearCtrl) {
    // register once
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = toastWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        wc.lpszClassName = L"MchcCopyToast";
        RegisterClassExW(&wc);
        registered = true;
    }
    HWND toast = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        L"MchcCopyToast", L"", WS_POPUP,
        0, 0, 96, 32, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!toast) return;

    // position near the clicked control
    RECT crc{};
    if (nearCtrl) {
        GetWindowRect(nearCtrl, &crc);
    } else {
        GetWindowRect(parent, &crc);
    }
    const int tx = (crc.left + crc.right) / 2 - 48;
    const int ty = crc.top - 40;
    SetWindowPos(toast, nullptr, tx, ty, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetLayeredWindowAttributes(toast, 0, 220, LWA_ALPHA);  // ~85% opaque
}

// ── painting ─────────────────────────────────────────────────────

void fillSolid(HDC dc, const RECT& rc, COLORREF color) {
    HBRUSH br = CreateSolidBrush(color);
    FillRect(dc, &rc, br);
    DeleteObject(br);
}

void drawCard(HDC dc, HWND hwnd, const Card& card, HFONT uiFont) {
    const auto s = search::dpi_scale_factor(hwnd);
    auto S = [s](int v) { return static_cast<int>(v * s); };
    const int headH = S(36);

    // shadow
    RECT shadow = card.rect;
    InflateRect(&shadow, 2, 2);
    fillSolid(dc, shadow, RGB(0xDD, 0xE2, 0xEB));

    // body — use system window color, not white
    fillSolid(dc, card.rect, GetSysColor(COLOR_WINDOW));

    // header bar
    RECT head = {card.rect.left, card.rect.top, card.rect.right, card.rect.top + headH};
    fillSolid(dc, head, card.headColor);

    // header title
    if (card.title) {
        HGDIOBJ oldFont = SelectObject(dc, uiFont ? uiFont : GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kHeadText);
        RECT titleRc = {head.left + S(14), head.top, head.right - S(10), head.bottom};
        DrawTextW(dc, card.title, -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, oldFont);
    }
}

// ── calculation ──────────────────────────────────────────────────

bool tryParseDouble(const std::string& text, double& out) {
    const auto s = search::trim(text);
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end != s.c_str() && *end == '\0';
}

void doCalculate(State* st) {
    double rawHgb = 0.0, rawHctPct = 0.0, plasmaHgb = 0.0;
    double rawRbc = 0.0;
    bool hasRbc = false;

    const bool hgbOk  = tryParseDouble(editText(st->rawHgb), rawHgb) && rawHgb > 0.0;
    const bool hctOk  = tryParseDouble(editText(st->rawHct), rawHctPct) && rawHctPct > 0.0 && rawHctPct < 100.0;
    const bool plasOk = tryParseDouble(editText(st->plasmaHgb), plasmaHgb) && plasmaHgb >= 0.0;
    const bool allOk  = hgbOk && hctOk && plasOk;

    const std::string rbcText = search::trim(editText(st->rawRbc));
    if (!rbcText.empty()) {
        if (tryParseDouble(rbcText, rawRbc) && rawRbc > 0.0) hasRbc = true;
    }

    if (!allOk) {
        setEdit(st->correctedHgb,  L"--");
        setEdit(st->correctedMch,  L"--");
        setEdit(st->correctedMchc, L"--");
        setStatus(st, L"请正确填写：校正前HGB、校正前HCT、脂血血浆HGB");
        return;
    }

    const double hctRatio = rawHctPct / 100.0;
    const double hgbCorrected = rawHgb - plasmaHgb * (1.0 - hctRatio);
    const double mchcCorrected = hgbCorrected / hctRatio;

    char buf[64]{};
    std::snprintf(buf, sizeof(buf), "%.2f g/L", hgbCorrected);
    setEdit(st->correctedHgb, buf);
    std::snprintf(buf, sizeof(buf), "%.2f g/L", mchcCorrected);
    setEdit(st->correctedMchc, buf);

    if (hasRbc) {
        const double mchCorrected = hgbCorrected / rawRbc;
        std::snprintf(buf, sizeof(buf), "%.2f pg", mchCorrected);
        setEdit(st->correctedMch, buf);
        std::snprintf(buf, sizeof(buf),
            "计算完成：HGB=%.2f g/L，MCH=%.2f pg，MCHC=%.2f g/L",
            hgbCorrected, mchCorrected, mchcCorrected);
    } else {
        setEdit(st->correctedMch, L"--");
        std::snprintf(buf, sizeof(buf),
            "计算完成：HGB=%.2f g/L，MCHC=%.2f g/L",
            hgbCorrected, mchcCorrected);
    }
    setStatus(st, search::utf8_to_wide(buf).c_str());
}

void doClear(State* st) {
    setEdit(st->rawHgb,    L"");
    setEdit(st->rawHct,    L"");
    setEdit(st->rawRbc,    L"");
    setEdit(st->plasmaHgb, L"");
    setEdit(st->correctedHgb,  L"--");
    setEdit(st->correctedMch,  L"--");
    setEdit(st->correctedMchc, L"--");
    setStatus(st, L"已清空输入与结果");
    SetFocus(st->rawHgb);
}

void showReferenceDialog(HWND parent) {
    const wchar_t* text =
        L"计算公式:\r\n"
        L"1) 校正值HGB = 校正前HGB - 脂血血浆HGB × (1 - 校正前HCT)\r\n"
        L"2) 校正值MCH = HGB校正值 / 校正前RBC\r\n"
        L"3) 校正值MCHC = HGB校正值 / 校正前HCT\r\n\r\n"
        L"输入说明:\r\n"
        L"- 校正前HCT 单位为 %，计算时会自动转换为小数。\r\n"
        L"- 校正前RBC 可不填；不填时仅输出 HGB、MCHC。\r\n\r\n"
        L"参考文献:\r\n"
        L"[1] 曾素根等. 不同脂血浓度对血液分析仪的影响[J]. 检验医学, 2013.\r\n"
        L"[2] 2021欧洲动脉硬化学会(EAS)共识声明. Eur Heart J, 2021.\r\n"
        L"[3] 管仕毅等. 乳糜血经不同条件血浆置换后对血常规检测的影响[J]. 2019.\r\n"
        L"[4] 曾素根等. Sysmex公司血液分析仪的干扰因素分析判断及处理程序[J]. 2010.";
    MessageBoxW(parent, text, L"计算说明与参考文献", MB_ICONINFORMATION);
}

// ── layout ───────────────────────────────────────────────────────

void resizeLayout(HWND hwnd, State* st) {
    if (!st) return;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const float s = search::dpi_scale_factor(hwnd);
    auto S = [s](int v) { return static_cast<int>(v * s); };

    const int margin   = S(14);
    const int gap      = S(12);
    const int headH    = S(36);
    const int rowH     = S(28);
    const int lblW     = S(186);
    const int editW    = S(118);
    const int cardPad  = S(14);
    const int slotPad  = S(6);

    // three cards vertically stacked on the left side;
    // right side is reserved for future use
    const int cardW = lblW + editW + S(8) + cardPad * 2;
    const int cardX = margin;

    int cardY = margin + S(86);  // below instruction area

    st->cards.clear();

    // Card 1 — 离心前 (3 rows)
    {
        const int cardH = headH + cardPad + 3 * (rowH + slotPad) + cardPad;
        Card c;
        c.rect = {cardX, cardY, cardX + cardW, cardY + cardH};
        c.title = L"离心前";
        c.headColor = kBeforeHead;
        c.slots.push_back({L"校正前HGB (g/L):",             st->rawHgb});
        c.slots.push_back({L"校正前HCT (%):",               st->rawHct});
        c.slots.push_back({L"校正前RBC (×10¹²/L):",  st->rawRbc});
        st->cards.push_back(c);
        cardY += cardH + gap;
    }

    // Card 2 — 离心后 (1 row)
    {
        const int cardH = headH + cardPad + 1 * (rowH + slotPad) + cardPad;
        Card c;
        c.rect = {cardX, cardY, cardX + cardW, cardY + cardH};
        c.title = L"离心后";
        c.headColor = kAfterHead;
        c.slots.push_back({L"脂血血浆HGB (g/L):", st->plasmaHgb});
        st->cards.push_back(c);
        cardY += cardH + gap;
    }

    // Card 3 — 计算结果 (3 rows)
    {
        const int cardH = headH + cardPad + 3 * (rowH + slotPad) + cardPad;
        Card c;
        c.rect = {cardX, cardY, cardX + cardW, cardY + cardH};
        c.title = L"计算结果";
        c.headColor = kResultHead;
        c.slots.push_back({L"HGB校正值:",   st->correctedHgb});
        c.slots.push_back({L"MCH校正值:",   st->correctedMch});
        c.slots.push_back({L"MCHC校正值:",  st->correctedMchc});
        st->cards.push_back(c);
    }

    // position edit controls inside each card
    for (const auto& card : st->cards) {
        int slotY = card.rect.top + headH + cardPad;
        for (const auto& slot : card.slots) {
            if (slot.hwnd) {
                const int ctrlW = card.rect.right - card.rect.left - cardPad * 2 - lblW - S(4);
                MoveWindow(slot.hwnd,
                    card.rect.left + cardPad + lblW + S(4),
                    slotY, ctrlW, rowH, TRUE);
            }
            slotY += rowH + slotPad;
        }
    }

    // buttons right below the last card, right-aligned to card border
    const int btnY = (st->cards.empty() ? cardY : st->cards.back().rect.bottom + gap);
    const int btnH = S(32);
    const int btnW = S(90);
    const int cardR = cardX + cardW;
    MoveWindow(st->clearBtn,     cardR - btnW,           btnY, btnW, btnH, TRUE);

    // status bar at window bottom
    MoveWindow(st->status, margin, h - S(22), w - margin * 2, S(20), TRUE);

    InvalidateRect(hwnd, nullptr, TRUE);
}

// ── window proc ──────────────────────────────────────────────────

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<State*>(GetPropW(hwnd, PROP_STATE));
    switch (msg) {
        case WM_CREATE: {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            auto* mcs = reinterpret_cast<MDICREATESTRUCTW*>(cs->lpCreateParams);
            st = reinterpret_cast<State*>(mcs->lParam);
            SetPropW(hwnd, PROP_STATE, reinterpret_cast<HANDLE>(st));
            st->bgBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));

            // input fields — right-aligned numeric entry with cue banners
            st->rawHgb    = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_RIGHT | ES_AUTOHSCROLL,
                0, 0, 10, 24, hwnd, win32_control_id(IDC_RAW_HGB),
                GetModuleHandleW(nullptr), nullptr);
            st->rawHct    = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_RIGHT | ES_AUTOHSCROLL,
                0, 0, 10, 24, hwnd, win32_control_id(IDC_RAW_HCT),
                GetModuleHandleW(nullptr), nullptr);
            st->rawRbc    = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_RIGHT | ES_AUTOHSCROLL,
                0, 0, 10, 24, hwnd, win32_control_id(IDC_RAW_RBC),
                GetModuleHandleW(nullptr), nullptr);
            st->plasmaHgb = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_RIGHT | ES_AUTOHSCROLL,
                0, 0, 10, 24, hwnd, win32_control_id(IDC_PLASMA_HGB),
                GetModuleHandleW(nullptr), nullptr);

            // output fields (read-only)
            st->correctedHgb  = CreateWindowExW(0, L"EDIT", L"--",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY | ES_CENTER,
                0, 0, 10, 24, hwnd, win32_control_id(IDC_CORRECTED_HGB),
                GetModuleHandleW(nullptr), nullptr);
            st->correctedMch  = CreateWindowExW(0, L"EDIT", L"--",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY | ES_CENTER,
                0, 0, 10, 24, hwnd, win32_control_id(IDC_CORRECTED_MCH),
                GetModuleHandleW(nullptr), nullptr);
            st->correctedMchc = CreateWindowExW(0, L"EDIT", L"--",
                WS_CHILD | WS_VISIBLE | WS_BORDER | ES_READONLY | ES_CENTER,
                0, 0, 10, 24, hwnd, win32_control_id(IDC_CORRECTED_MCHC),
                GetModuleHandleW(nullptr), nullptr);

            // buttons
            st->clearBtn = search::create_button(hwnd, IDC_CLEAR, L"清空",
                0, 0, 90, 32);

            st->status = CreateWindowExW(0, L"STATIC", L"输入参数后将自动计算",
                WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                0, 0, 0, 0, hwnd, win32_control_id(IDC_STATUS),
                GetModuleHandleW(nullptr), nullptr);

            search::apply_font_to_children(hwnd, st->ctx.uiFont);

            // Light-grey cue banners via custom painting (lighter than system default)
            SetWindowSubclass(st->rawHgb,    inputCueProc, SUBCLASS_INPUT,
                reinterpret_cast<DWORD_PTR>(new InputCueBanner{L"例如 180"}));
            SetWindowSubclass(st->rawHct,    inputCueProc, SUBCLASS_INPUT,
                reinterpret_cast<DWORD_PTR>(new InputCueBanner{L"例如 45"}));
            SetWindowSubclass(st->rawRbc,    inputCueProc, SUBCLASS_INPUT,
                reinterpret_cast<DWORD_PTR>(new InputCueBanner{L"可选，例如 5.8"}));
            SetWindowSubclass(st->plasmaHgb, inputCueProc, SUBCLASS_INPUT,
                reinterpret_cast<DWORD_PTR>(new InputCueBanner{L"例如 25"}));

            SetFocus(st->rawHgb);
            return 0;
        }
        case WM_SIZE:
            resizeLayout(hwnd, st);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            if (st && st->bgBrush) return reinterpret_cast<INT_PTR>(st->bgBrush);
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            const int w = client.right - client.left;
            const int h = client.bottom - client.top;

            HDC memDc = CreateCompatibleDC(dc);
            HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
            HGDIOBJ oldBmp = SelectObject(memDc, bmp);

            // background — system BTNFACE
            fillSolid(memDc, client, GetSysColor(COLOR_BTNFACE));

            // instruction text
            HGDIOBJ oldFont = SelectObject(memDc, st ? st->ctx.uiFont : GetStockObject(DEFAULT_GUI_FONT));
            SetBkMode(memDc, TRANSPARENT);
            const COLORREF instrColor = RGB(0x47, 0x55, 0x69);
            SetTextColor(memDc, instrColor);

            const int margin = S(hwnd, 14);
            const int instrH = S(hwnd, 18);

            // line 0: "校正方法："
            const wchar_t* methodTitle = L"校正方法：";
            int iy = S(hwnd, 8);
            RECT titleRc = {margin, iy, w - margin, iy + instrH};
            DrawTextW(memDc, methodTitle, -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            iy += instrH;

            // lines 1–3: steps
            const wchar_t* instrLines[] = {
                L"1. 将原血常规标本上机检测，得到校正前HGB、校正前HCT、校正前RBC；",
                L"2. 再将血常规标本低速离心（离心力310-550g）2~3分钟，轻轻取出，吸出脂血浆；",
                L"3. 再将脂血浆进行全血细胞计数（CBC），检测出脂血浆中假的HGB浓度（脂血血浆HGB）。",
            };
            SetTextColor(memDc, instrColor);
            for (const auto* line : instrLines) {
                RECT ir = {margin, iy, w - margin, iy + instrH};
                DrawTextW(memDc, line, -1, &ir, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                iy += instrH;
            }

            // reference link on its own line below the instruction steps
            const wchar_t* linkText = L"计算说明与参考文献 →";
            const int linkW = S(hwnd, 220);
            iy += S(hwnd, 2);
            RECT linkRc = {margin, iy, margin + linkW, iy + instrH};
            {
                LOGFONTW lf{};
                GetObjectW(st ? st->ctx.uiFont : GetStockObject(DEFAULT_GUI_FONT), sizeof(lf), &lf);
                lf.lfUnderline = TRUE;
                lf.lfWeight = FW_NORMAL;
                HFONT linkFont = CreateFontIndirectW(&lf);
                HGDIOBJ linkOldFont = SelectObject(memDc, linkFont);
                SetTextColor(memDc, RGB(0x1A, 0x73, 0xE8));
                DrawTextW(memDc, linkText, -1, &linkRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(memDc, linkOldFont);
                DeleteObject(linkFont);
            }
            st->referenceRect = linkRc;
            SelectObject(memDc, oldFont);

            // draw cards
            for (const auto& card : st->cards) {
                drawCard(memDc, hwnd, card, st ? st->ctx.uiFont : nullptr);

                // slot labels
                oldFont = SelectObject(memDc, st ? st->ctx.uiFont : GetStockObject(DEFAULT_GUI_FONT));
                SetBkMode(memDc, TRANSPARENT);
                SetTextColor(memDc, GetSysColor(COLOR_WINDOWTEXT));
                const int cardPad = S(hwnd, 14);
                const int headH = S(hwnd, 36);
                const int rowH = S(hwnd, 28);
                const int lblW = S(hwnd, 186);
                int slotY = card.rect.top + headH + cardPad;
                for (const auto& slot : card.slots) {
                    RECT lr = {card.rect.left + cardPad,
                               slotY,
                               card.rect.left + cardPad + lblW,
                               slotY + rowH};
                    DrawTextW(memDc, slot.label, -1, &lr,
                              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                    slotY += rowH + S(hwnd, 6);
                }
                SelectObject(memDc, oldFont);
            }

            BitBlt(dc, 0, 0, w, h, memDc, 0, 0, SRCCOPY);
            SelectObject(memDc, oldBmp);
            DeleteObject(bmp);
            DeleteDC(memDc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (st) {
                POINT pt{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
                if (PtInRect(&st->referenceRect, pt)) {
                    showReferenceDialog(hwnd);
                    return 0;
                }
            }
            break;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            // Auto-calculate on input change with 250ms debounce
            if (HIWORD(wp) == EN_CHANGE &&
                (id == IDC_RAW_HGB || id == IDC_RAW_HCT ||
                 id == IDC_RAW_RBC || id == IDC_PLASMA_HGB)) {
                KillTimer(hwnd, IDT_AUTO_CALC);
                SetTimer(hwnd, IDT_AUTO_CALC, 250, nullptr);
                return 0;
            }
            if (id == IDC_CLEAR)          { doClear(st); return 0; }
            // Click-to-copy output fields (numeric value only, no unit)
            if (id == IDC_CORRECTED_HGB || id == IDC_CORRECTED_MCH || id == IDC_CORRECTED_MCHC) {
                HWND ctrl = reinterpret_cast<HWND>(lp);
                if (HIWORD(wp) == EN_SETFOCUS && ctrl) {
                    wchar_t buf[64]{};
                    GetWindowTextW(ctrl, buf, 64);
                    if (buf[0] && wcscmp(buf, L"--") != 0) {
                        // Strip unit: copy only characters before the first space
                        std::wstring text(buf);
                        size_t sp = text.find(L' ');
                        if (sp != std::wstring::npos) text.resize(sp);
                        if (!text.empty()) {
                            if (OpenClipboard(hwnd)) {
                                EmptyClipboard();
                                size_t len = text.size() + 1;
                                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len * sizeof(wchar_t));
                                if (hMem) {
                                    wcscpy_s(reinterpret_cast<wchar_t*>(GlobalLock(hMem)), len, text.c_str());
                                    GlobalUnlock(hMem);
                                    SetClipboardData(CF_UNICODETEXT, hMem);
                                }
                                CloseClipboard();
                            }
                            showCopyToast(hwnd, ctrl);
                        }
                    }
                }
                return 0;
            }
            break;
        }
        case WM_TIMER:
            if (wp == IDT_AUTO_CALC) {
                KillTimer(hwnd, IDT_AUTO_CALC);
                doCalculate(st);
                return 0;
            }
            break;
        case WM_SETCURSOR: {
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (st && PtInRect(&st->referenceRect, pt)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }
        case WM_DESTROY:
            KillTimer(hwnd, IDT_AUTO_CALC);
            RemovePropW(hwnd, PROP_STATE);
            if (st) {
                if (st->bgBrush) { DeleteObject(st->bgBrush); st->bgBrush = nullptr; }
                delete st;
            }
            return 0;
        default:
            break;
    }
    return DefMDIChildProcW(hwnd, msg, wp, lp);
}

}  // namespace

// ── public factory ────────────────────────────────────────────────

HWND create_mchc_correction_module(const ModuleContext& ctx) {
    static bool registered = false;
    if (!registered) {
        REGISTER_MDI_CHILD_CLASS(
            ctx.instance, wndProc, WND_CLASS,
            reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1));
        registered = true;
    }

    auto* st = new State;
    st->ctx = ctx;

    MDICREATESTRUCTW mcs{};
    mcs.szClass = WND_CLASS;
    mcs.szTitle = WINDOW_TITLE;
    mcs.hOwner = ctx.instance;
    mcs.x = mcs.y = mcs.cx = mcs.cy = CW_USEDEFAULT;
    mcs.lParam = reinterpret_cast<LPARAM>(st);

    HWND child = reinterpret_cast<HWND>(SendMessageW(ctx.mdiClient, WM_MDICREATE, 0,
                                                      reinterpret_cast<LPARAM>(&mcs)));
    if (child) {
        SendMessageW(ctx.mdiClient, WM_MDIMAXIMIZE, reinterpret_cast<WPARAM>(child), 0);
    }
    return child;
}

#endif  // _WIN32
