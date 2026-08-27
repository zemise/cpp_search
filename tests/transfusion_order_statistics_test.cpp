#include "search_core.h"

#include <iostream>
#include <vector>

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "check failed at line " << __LINE__ << ": " #condition "\n"; \
            return 1; \
        } \
    } while (false)

namespace {

search::TransfusionOrderStatRawRow make_row(
    const char* form, const char* time, const char* status,
    const char* patient, const char* dept, bool deleted = false) {
    search::TransfusionOrderStatRawRow row;
    row.apply_form_no = form;
    row.apply_time = time;
    row.apply_status = status;
    row.patient_no = patient;
    row.patient_no_type = "住院";
    row.patient_name = patient;
    row.apply_dept = dept;
    row.bed_no = "01";
    row.apply_doctor = "测试医生";
    row.tran_property = "输血";
    row.delete_bit = deleted;
    return row;
}

}  // namespace

int main() {
    search::TransfusionOrderStatQuery query;
    query.start_date = "2026-08-11";
    query.end_date = "2026-08-11";
    query.campus = "全部";

    const std::vector<search::TransfusionOrderStatRawRow> raw{
        make_row("A1", "2026-08-11 08:00:00", "未审核", "P1", "心内科"),
        make_row("A2", "2026-08-11 09:00:00", "已审核", "P2", "滨水新城心内科"),
        make_row("A3", "2026-08-11 10:00:00", "已完结", "P3", "血液内科"),
        make_row("A4", "2026-08-11 11:00:00", "已驳回", "P4", "血液内科"),
        make_row("A5", "2026-08-11 12:00:00", "已删除", "P5", "滨水新城产科", true),
        make_row("A6", "2026-08-11 13:00:00", "未知状态", "P6", "血液内科"),
        make_row("", "2026-08-11 14:00:00", "已审核", "P7", "血液内科"),
    };

    search::TransfusionOrderStatSummary summary;
    std::vector<search::TransfusionOrderStatDetailRow> rows;
    std::string error;
    CHECK(search::build_transfusion_order_statistics(query, raw, summary, rows, error));
    CHECK(error.empty());
    CHECK(summary.total_count == 3);
    CHECK(summary.unreviewed_count == 1);
    CHECK(summary.reviewed_count == 1);
    CHECK(summary.completed_count == 1);
    CHECK(summary.rejected_count == 0);
    CHECK(summary.deleted_count == 0);
    CHECK(summary.other_status_count == 1);
    CHECK(summary.missing_apply_form_no_count == 1);
    CHECK(rows.size() == 3);

    query.include_rejected = true;
    query.include_deleted = true;
    CHECK(search::build_transfusion_order_statistics(query, raw, summary, rows, error));
    CHECK(summary.total_count == 5);
    CHECK(summary.rejected_count == 1);
    CHECK(summary.deleted_count == 1);
    CHECK(rows.size() == 5);

    query.campus = "新院";
    CHECK(search::build_transfusion_order_statistics(query, raw, summary, rows, error));
    CHECK(summary.total_count == 2);
    CHECK(summary.reviewed_count == 1);
    CHECK(summary.deleted_count == 1);
    CHECK(summary.other_status_count == 0);
    CHECK(summary.missing_apply_form_no_count == 0);

    query.campus = "全部";
    query.include_rejected = false;
    query.include_deleted = false;
    auto conflict_a = make_row("C1", "2026-08-11 08:00:00", "未审核", "P8", "");
    auto conflict_b = make_row("C1", "2026-08-11 09:00:00", "已审核", "P9", "血液内科");
    const std::vector<search::TransfusionOrderStatRawRow> conflicts{conflict_a, conflict_b};
    CHECK(search::build_transfusion_order_statistics(query, conflicts, summary, rows, error));
    CHECK(summary.total_count == 1);
    CHECK(summary.reviewed_count == 1);
    CHECK(summary.conflict_count == 1);
    CHECK(rows.size() == 1);
    CHECK(rows[0].apply_status == "已审核");
    CHECK(rows[0].apply_time == "2026-08-11 09:00:00");
    CHECK(rows[0].data_status.find("状态冲突") != std::string::npos);
    CHECK(rows[0].data_status.find("病人号冲突") != std::string::npos);

    std::cout << "transfusion order aggregation tests passed\n";
    return 0;
}
