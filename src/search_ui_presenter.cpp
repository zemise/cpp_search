#include "search_ui_presenter.h"

#ifdef _WIN32

#include "search_app.h"
#include "search_text.h"
#include "search_ui_columns.h"

#include <commctrl.h>

namespace search {
namespace {

void set_cell(HWND list, int row, int col, const std::string& text) {
    const auto wide = utf8_to_wide(text);
    ListView_SetItemText(list, row, col, const_cast<wchar_t*>(wide.c_str()));
}

void insert_report_row(HWND list, int index, const ReportRow& row) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = index;
    std::wstring first = utf8_to_wide(row.oper_no);
    item.pszText = first.data();
    ListView_InsertItem(list, &item);
    set_cell(list, index, report_columns::Name, row.name);
    set_cell(list, index, report_columns::Barcode, row.txm_no);
    set_cell(list, index, report_columns::ReportTime, row.chk_date);
    set_cell(list, index, report_columns::Sex, row.sex);
    set_cell(list, index, report_columns::Age, row.age);
    set_cell(list, index, report_columns::Bed, row.bed_code);
    set_cell(list, index, report_columns::PatientType, row.patient_type);
    set_cell(list, index, report_columns::Requester, row.requester);
    set_cell(list, index, report_columns::Reviewer, row.reviewer);
    set_cell(list, index, report_columns::GroupName, row.group_name);
    set_cell(list, index, report_columns::ReviewStatus, display_conf(row.conf));
    set_cell(list, index, report_columns::ConfirmStatus, display_chk_flag(row.chk_flag));
    set_cell(list, index, report_columns::PrintStatus, display_binary_print_flag(row.zymz_print));
    set_cell(list, index, report_columns::SelfServicePrintStatus, display_binary_print_flag(row.zzj_print));
}

void insert_result_row(HWND list, int index, const ResultRow& row) {
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = index;
    std::wstring first = utf8_to_wide(row.item_name);
    item.pszText = first.data();
    ListView_InsertItem(list, &item);
    set_cell(list, index, result_columns::Result, row.result);
    set_cell(list, index, result_columns::LowerBound, row.downbound);
    set_cell(list, index, result_columns::UpperBound, row.upbound);
    set_cell(list, index, result_columns::Unit, row.unit);
    set_cell(list, index, result_columns::EnglishName, row.item_eng);
}

}  // namespace

void set_status_text(const MainUiHandles& ui, const std::wstring& text) {
    SetWindowTextW(ui.status, text.c_str());
}

void clear_result_lists(const MainUiHandles& ui) {
    ListView_DeleteAllItems(ui.reports);
    ListView_DeleteAllItems(ui.results);
}

void present_report_rows(const MainUiHandles& ui, const std::vector<ReportRow>& rows) {
    SendMessageW(ui.reports, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(ui.reports);
    for (size_t i = 0; i < rows.size(); ++i) {
        insert_report_row(ui.reports, static_cast<int>(i), rows[i]);
    }
    SendMessageW(ui.reports, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(ui.reports, nullptr, TRUE);
}

void present_result_rows(const MainUiHandles& ui, const std::vector<ResultRow>& rows) {
    SendMessageW(ui.results, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(ui.results);
    for (size_t i = 0; i < rows.size(); ++i) {
        insert_result_row(ui.results, static_cast<int>(i), rows[i]);
    }
    SendMessageW(ui.results, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(ui.results, nullptr, TRUE);
}

}  // namespace search

#endif
