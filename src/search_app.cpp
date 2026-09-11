#include "search_app.h"
#include "search_text.h"

namespace search {

QueryFilters make_query_filters(const DbSettings& settings, const QueryInput& input) {
    QueryFilters filters;
    filters.connection_string = wide_to_utf8(build_connection_string_w(settings));
    filters.patient_id = input.patient_id;
    filters.barcode = input.barcode;
    filters.patient_name = input.patient_name;
    filters.patient_no = input.patient_no;
    filters.oper_no = input.oper_no;
    filters.start_date = input.start_date;
    filters.end_date = input.end_date;
    filters.room_code = input.room_code;
    filters.patient_type = input.patient_type;
    filters.report_status = input.report_status;
    filters.mach_code = input.mach_code;
    filters.group_code = input.group_code;
    filters.item_code = input.item_code;
    filters.limit = input.limit;
    filters.skip_order_text = input.skip_order_text;
    return filters;
}

std::string display_conf(const std::string& conf) {
    const auto value = trim(conf);
    if (value == "T" || value == "S") {
        return "已审核";
    }
    return "未审核";
}

std::string display_chk_flag(const std::string& chk_flag) {
    return trim(chk_flag) == "T" ? "已发送" : "未发送";
}

std::string display_binary_print_flag(const std::string& value_text) {
    return trim(value_text) == "1" ? "已打印" : "未打印";
}

std::string make_query_count_status(size_t count) {
    return "当前查询总数：" + std::to_string(count) + " 条";
}

ReportRowTone report_row_tone(const ReportRow& row) {
    if (trim(row.zymz_print) == "1") {
        return ReportRowTone::Printed;
    }
    const auto value = trim(row.conf);
    if (value == "T" || value == "S") {
        return ReportRowTone::Reviewed;
    }
    return ReportRowTone::Pending;
}

ResultRowTone result_row_tone(const ResultRow& row) {
    // Empirical mapping from live LIS data: 1=high, 5=low, 3/NULL=default.
    const auto value = trim(row.normal);
    if (value == "1") {
        return ResultRowTone::High;
    }
    if (value == "5") {
        return ResultRowTone::Low;
    }
    return ResultRowTone::Default;
}

}  // namespace search
