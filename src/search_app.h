#pragma once

#include "app_settings.h"
#include "search_core.h"

#include <string>

namespace search {

struct QueryInput {
    std::string patient_id;
    std::string barcode;
    std::string patient_name;
    std::string patient_no;
    std::string oper_no;
    std::string start_date;
    std::string end_date;
    std::string room_code;
    std::string patient_type;
    std::string report_status;
    std::string mach_code;
    std::string group_code;
    std::string item_code;
    int limit = 0;
    bool skip_order_text = false;
};

enum class ReportRowTone {
    Printed,
    Reviewed,
    Pending,
};

enum class ResultRowTone {
    Default,
    High,
    Low,
};

QueryFilters make_query_filters(const DbSettings& settings, const QueryInput& input);

std::string display_conf(const std::string& conf);
std::string display_chk_flag(const std::string& chk_flag);
std::string display_binary_print_flag(const std::string& value_text);
std::string make_query_count_status(size_t count);

ReportRowTone report_row_tone(const ReportRow& row);
ResultRowTone result_row_tone(const ResultRow& row);

}  // namespace search
