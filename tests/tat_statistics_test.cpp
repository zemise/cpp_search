#include "search_core.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

search::TatStatRawRow base_row() {
    search::TatStatRawRow row;
    row.barcode = "904117569";
    row.patient_type = "门诊";
    row.reg_no = "2026083001";
    row.name = "测试病人";
    row.collection_time = "2026-09-02 08:24:00";
    row.receive_time = "2026-09-02 08:26:00";
    row.machine_time = "2026-09-02 08:31:00";
    row.review_time = "2026-09-02 10:26:00";
    row.order_text = "肝功能";
    row.report_no = "1001";
    row.has_report = true;
    row.report_reviewed = true;
    row.report_sent = true;
    row.barcode_oper_state = 2;
    return row;
}

}  // namespace

int main() {
    search::TatStatQuery query;
    query.patient_type = "全部";
    query.current_time = "2026-09-02 12:00:00";
    query.thresholds = {30, 30, 180, 240};

    auto first = base_row();
    auto second = first;
    second.order_text = "肾功能";
    std::vector<search::TatStatRawRow> raw{first, second};
    search::TatStatSummary summary;
    std::vector<search::TatStatDetailRow> rows;
    std::string error;
    check(search::build_tat_statistics(query, raw, summary, rows, error), "build should succeed");
    check(rows.size() == 1, "same barcode should aggregate to one row");
    check(rows[0].order_text == "肝功能/肾功能", "orders should be de-duplicated and joined");
    check(rows[0].collection_to_receive_seconds == 120, "collection to receive duration");
    check(rows[0].receive_to_machine_seconds == 300, "receive to machine duration");
    check(rows[0].receive_to_review_seconds == 7200, "receive to review duration");
    check(rows[0].collection_to_review_seconds == 7320, "collection to review duration");
    check(rows[0].tat_status == "正常", "completed row within thresholds should be normal");
    check(rows[0].workflow_status == "发送完成", "sent workflow status");

    auto waiting = base_row();
    waiting.barcode = "waiting";
    waiting.machine_time.clear();
    waiting.review_time.clear();
    waiting.report_no.clear();
    waiting.has_report = false;
    waiting.report_reviewed = false;
    waiting.report_sent = false;
    waiting.barcode_oper_state = 0;
    raw = {waiting};
    rows.clear();
    check(search::build_tat_statistics(query, raw, summary, rows, error), "waiting build should succeed");
    check(rows[0].receive_to_machine_seconds == 12840, "waiting machine duration uses current time");
    check(rows[0].machine_waiting && rows[0].review_waiting, "waiting flags");
    check(rows[0].tat_status == "超时", "unfinished overdue row should be overtime");
    check(rows[0].workflow_status == "已签收未上机", "not loaded workflow status");

    auto missing_collection = base_row();
    missing_collection.barcode = "missing";
    missing_collection.collection_time.clear();
    raw = {missing_collection};
    rows.clear();
    check(search::build_tat_statistics(query, raw, summary, rows, error), "missing collection build");
    check(rows[0].collection_to_receive_seconds < 0, "missing collection must not fall back");
    check(rows[0].collection_to_review_seconds < 0, "missing collection review must not calculate");
    check(rows[0].collection_to_receive.empty(), "missing collection display remains empty");

    auto boundary = base_row();
    boundary.barcode = "boundary";
    boundary.machine_time = "2026-09-02 08:56:00";
    raw = {boundary};
    rows.clear();
    check(search::build_tat_statistics(query, raw, summary, rows, error), "boundary build");
    check(rows[0].tat_status == "正常", "equal threshold should be normal");
    rows[0].machine_time = "2026-09-02 08:56:01";
    search::refresh_tat_statistics(query.thresholds, query.current_time, summary, rows);
    check(rows[0].tat_status == "超时", "one second over threshold should be overtime");

    auto abnormal = base_row();
    abnormal.barcode = "abnormal";
    abnormal.machine_time = "2026-09-02 08:25:00";
    raw = {abnormal};
    rows.clear();
    check(search::build_tat_statistics(query, raw, summary, rows, error), "abnormal build");
    check(rows[0].tat_status == "时间异常", "negative duration should be abnormal");
    check(summary.time_abnormal_count == 1, "abnormal summary count");

    std::cout << "tat statistics tests passed\n";
    return 0;
}
