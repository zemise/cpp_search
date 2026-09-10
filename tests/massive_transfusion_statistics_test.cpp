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

search::MassiveTransfusionRawRow make_row(
    const char* main_id, const char* form, const char* patient,
    const char* time, const char* status, const char* son,
    const char* composition, const char* number, const char* unit,
    const char* composition_big_id = "1") {
    search::MassiveTransfusionRawRow value;
    value.main_id = main_id;
    value.apply_form_no = form;
    value.patient_no = patient;
    value.patient_name = patient;
    value.apply_time = time;
    value.apply_status = status;
    value.apply_dept = "滨水新城输血科";
    value.son_id = son;
    value.composition = composition;
    value.apply_num = number;
    value.apply_unit = unit;
    value.composition_big_id = composition_big_id;
    return value;
}

search::ActualTransfusionRawRow make_actual_row(
    const char* cross_id, const char* form, const char* patient,
    const char* bag_id, const char* match_time,
    const char* composition, const char* norm, const char* unit,
    const char* composition_type_id = "1") {
    search::ActualTransfusionRawRow value;
    value.cross_match_id = cross_id;
    value.apply_form_no = form;
    value.patient_no = patient;
    value.patient_name = patient;
    value.verify_state = "已审核";
    value.blood_in_id = bag_id;
    value.match_date = match_time;
    value.apply_main_id = form;
    value.apply_patient_no = patient;
    value.apply_time = match_time;
    value.check_date = match_time;
    value.apply_status = "已审核";
    value.apply_dept = "滨水新城输血科";
    value.composition = composition;
    value.norm = norm;
    value.unit = unit;
    value.composition_type_id = composition_type_id;
    return value;
}

}  // namespace

int main() {
    search::MassiveTransfusionStatQuery query;
    CHECK(query.statistic_basis == "actual");
    CHECK(query.event_time_source == "out");
    query.start_date = "2026-08-01";
    query.end_date = "2026-08-02";
    query.campus = "新院";

    const std::vector<search::MassiveTransfusionRawRow> raw{
        make_row("1", "A1", "P1", "2026-08-01 08:00:00", "未审核", "11", "红细胞", "4", "U"),
        make_row("2", "A2", "P1", "2026-08-02 07:59:59", "已审核", "21", "其他制品", "2", "治疗量"),
        make_row("2", "A2", "P1", "2026-08-02 07:59:59", "已审核", "22", "血浆", "300", "ML"),
        make_row("3", "A3", "P1", "2026-08-02 08:00:00", "已完结", "31", "血浆", "1600", "ML"),
        make_row("4", "R1", "P1", "2026-08-01 09:00:00", "已驳回", "41", "红细胞", "10", "U"),
        make_row("5", "B1", "P2", "2026-08-01 10:00:00", "未审核", "", "", "", ""),
        make_row("6", "R2", "P3", "2026-08-01 11:00:00", "已驳回", "61", "血浆", "500", "ML"),
    };

    search::MassiveTransfusionStatSummary summary;
    std::vector<search::MassiveTransfusionEventRow> events;
    std::vector<search::MassiveTransfusionComponentDetailRow> rejected;
    std::string error;
    CHECK(search::build_massive_transfusion_statistics(
        query, raw, summary, events, rejected, error));
    CHECK(error.empty());
    CHECK(summary.event_count == 2);
    CHECK(summary.patient_count == 1);
    CHECK(summary.application_count == 3);
    CHECK(summary.component_count == 4);
    CHECK(summary.total_ml == 3200.0);
    CHECK(summary.issue_event_count == 1);
    CHECK(summary.rejected_application_count == 2);
    CHECK(events.size() == 3);
    CHECK(rejected.size() == 1);

    bool saw_boundary_event = false;
    bool saw_rejected = false;
    for (const auto& event : events) {
        if (event.first_apply_form_no == "A3") saw_boundary_event = event.qualifies;
        if (event.first_apply_form_no == "A1") {
            CHECK(event.total_ml == "1600");
            CHECK(event.rejected_application_count == 1);
            for (const auto& component : event.components) {
                if (component.apply_form_no == "R1") {
                    saw_rejected = component.rejected && !component.counted;
                }
            }
        }
    }
    CHECK(saw_boundary_event);
    CHECK(saw_rejected);

    const std::vector<search::MassiveTransfusionRawRow> component_filter_raw{
        make_row("7", "C1", "P4", "2026-08-01 12:00:00", "未审核", "71", "红细胞", "8", "U"),
        make_row("7", "C1", "P4", "2026-08-01 12:00:00", "未审核", "72", "机采血小板", "1", "治疗量", "4"),
        make_row("7", "C1", "P4", "2026-08-01 12:00:00", "未审核", "73", "混合冷沉淀凝血因子", "1", "U", "3"),
    };
    summary = {};
    events.clear();
    rejected.clear();
    CHECK(search::build_massive_transfusion_statistics(
        query, component_filter_raw, summary, events, rejected, error));
    CHECK(events.size() == 1);
    CHECK(events[0].total_ml == "1600");
    CHECK(events[0].component_count == 1);
    int excluded_count = 0;
    for (const auto& component : events[0].components) {
        if (!component.excluded_by_component_filter) continue;
        ++excluded_count;
        CHECK(!component.counted);
        CHECK(component.data_status == "未勾选，不计量");
        if (component.composition_category_id == "3") {
            CHECK(component.conversion_factor == "20");
            CHECK(component.converted_ml == "20");
        }
    }
    CHECK(excluded_count == 2);

    const std::vector<search::MassiveTransfusionRawRow> plasma_name_raw{
        make_row("11", "E1", "P16", "2026-08-01 16:00:00", "已审核", "111",
                 "去冷沉淀冰冻血浆", "1600", "ML", "2"),
    };
    summary = {};
    events.clear();
    rejected.clear();
    CHECK(search::build_massive_transfusion_statistics(
        query, plasma_name_raw, summary, events, rejected, error));
    CHECK(summary.event_count == 1);
    CHECK(events.size() == 1);
    CHECK(events[0].total_ml == "1600");
    CHECK(events[0].components[0].counted);
    CHECK(!events[0].components[0].excluded_by_component_filter);

    const std::vector<search::MassiveTransfusionRawRow> missing_category_raw{
        make_row("12", "E2", "P18", "2026-08-01 17:00:00", "已审核", "121",
                 "冷沉淀凝血因子", "100", "U", ""),
    };
    summary = {};
    events.clear();
    rejected.clear();
    CHECK(search::build_massive_transfusion_statistics(
        query, missing_category_raw, summary, events, rejected, error));
    CHECK(summary.event_count == 0);
    CHECK(events.size() == 1);
    CHECK(events[0].total_ml == "0");
    CHECK(!events[0].components[0].counted);
    CHECK(!events[0].components[0].excluded_by_component_filter);
    CHECK(events[0].components[0].conversion_factor.empty());

    query.include_platelet_and_cryoprecipitate = true;
    summary = {};
    events.clear();
    rejected.clear();
    CHECK(search::build_massive_transfusion_statistics(
        query, component_filter_raw, summary, events, rejected, error));
    CHECK(events.size() == 1);
    CHECK(events[0].total_ml == "1870");
    CHECK(events[0].component_count == 3);
    CHECK(summary.total_ml == 1870.0);
    for (const auto& component : events[0].components) {
        CHECK(!component.excluded_by_component_filter);
        CHECK(component.counted);
    }

    const std::vector<search::MassiveTransfusionRawRow> threshold_raw{
        make_row("8", "D1", "P5", "2026-08-01 13:00:00", "未审核", "81", "血浆", "1599.99", "ML"),
        make_row("9", "D2", "P6", "2026-08-01 14:00:00", "未审核", "91", "血浆", "1600", "ML"),
        make_row("10", "D3", "P7", "2026-08-01 15:00:00", "未审核", "101", "血浆", "1600.01", "ML"),
    };
    query.threshold_ml = 1600.0;
    query.threshold_inclusive = true;
    summary = {};
    events.clear();
    rejected.clear();
    CHECK(search::build_massive_transfusion_statistics(
        query, threshold_raw, summary, events, rejected, error));
    CHECK(summary.event_count == 2);
    CHECK(events.size() == 2);

    query.threshold_inclusive = false;
    summary = {};
    events.clear();
    CHECK(search::build_massive_transfusion_statistics(
        query, threshold_raw, summary, events, rejected, error));
    CHECK(summary.event_count == 1);
    CHECK(events.size() == 1);
    CHECK(events[0].total_ml == "1600.01");

    query.threshold_ml = 0.0;
    CHECK(!search::build_massive_transfusion_statistics(
        query, threshold_raw, summary, events, rejected, error));
    CHECK(!error.empty());

    query = {};
    query.start_date = "2026-08-01";
    query.end_date = "2026-08-03";
    query.campus = "新院";
    query.statistic_basis = "actual";
    query.event_time_source = "match";
    std::vector<search::ActualTransfusionRawRow> actual_raw{
        make_actual_row("C1", "A1", "P8", "B1", "2026-08-01 08:00:00", "红细胞", "4", "U"),
        make_actual_row("C2", "A2", "P8", "B2", "2026-08-02 07:59:59", "血浆", "800", "ML"),
        make_actual_row("C3", "A3", "P8", "B3", "2026-08-02 08:00:00", "血浆", "1600", "ML"),
        make_actual_row("C4", "A4", "P9", "B4", "2026-08-01 09:00:00", "冷沉淀", "10", "U", "3"),
    };
    auto unreviewed = make_actual_row("C5", "A5", "P8", "B5", "2026-08-01 10:00:00", "红细胞", "10", "U");
    unreviewed.verify_state = "未审核";
    actual_raw.push_back(unreviewed);
    auto deleted = make_actual_row("C6", "A6", "P8", "B6", "2026-08-01 11:00:00", "红细胞", "10", "U");
    deleted.cross_match_deleted = true;
    actual_raw.push_back(deleted);
    auto app_anomaly = make_actual_row("C7", "A7", "P10", "B7", "2026-08-01 12:00:00", "血浆", "1600", "ML");
    app_anomaly.apply_status = "已驳回";
    app_anomaly.apply_patient_no = "OTHER";
    actual_raw.push_back(app_anomaly);

    summary = {};
    events.clear();
    rejected.clear();
    CHECK(search::build_actual_massive_transfusion_statistics(
        query, actual_raw, summary, events, rejected, error));
    CHECK(summary.event_count == 3);
    CHECK(summary.patient_count == 2);
    CHECK(summary.total_ml == 4800.0);
    CHECK(events.size() == 3);
    CHECK(rejected.size() == 3);
    bool saw_first_window = false;
    bool saw_boundary = false;
    bool saw_application_anomaly = false;
    for (const auto& event : events) {
        if (event.first_apply_form_no == "A1") {
            saw_first_window = event.total_ml == "1600" && event.component_count == 2;
            CHECK(event.composition_summary.find("红细胞 800ml") != std::string::npos);
            CHECK(event.composition_summary.find("血浆 800ml") != std::string::npos);
        }
        if (event.first_apply_form_no == "A3") saw_boundary = event.total_ml == "1600";
        if (event.first_apply_form_no == "A7") {
            saw_application_anomaly = event.qualifies && event.audit_count == 1;
        }
    }
    CHECK(saw_first_window);
    CHECK(saw_boundary);
    CHECK(saw_application_anomaly);

    auto actual_plasma_name = make_actual_row(
        "C15", "A15", "P17", "B15", "2026-08-01 17:00:00",
        "去冷沉淀冰冻血浆", "1600", "ML", "2");
    summary = {};
    events.clear();
    rejected.clear();
    CHECK(search::build_actual_massive_transfusion_statistics(
        query, {actual_plasma_name}, summary, events, rejected, error));
    CHECK(summary.event_count == 1);
    CHECK(events.size() == 1);
    CHECK(events[0].total_ml == "1600");
    CHECK(events[0].components[0].counted);
    CHECK(!events[0].components[0].excluded_by_component_filter);

    auto actual_without_name = make_actual_row(
        "C17", "A17", "P20", "B17", "2026-08-01 17:30:00",
        "", "1600", "ML", "2");
    summary = {};
    events.clear();
    rejected.clear();
    CHECK(search::build_actual_massive_transfusion_statistics(
        query, {actual_without_name}, summary, events, rejected, error));
    CHECK(summary.event_count == 1);
    CHECK(events.size() == 1);
    CHECK(events[0].total_ml == "1600");
    CHECK(events[0].components[0].counted);
    CHECK(events[0].composition_summary.find("未命名制品 1600ml") != std::string::npos);

    auto actual_missing_category = make_actual_row(
        "C16", "A16", "P19", "B16", "2026-08-01 18:00:00",
        "冷沉淀凝血因子", "100", "U", "");
    summary = {};
    events.clear();
    rejected.clear();
    CHECK(search::build_actual_massive_transfusion_statistics(
        query, {actual_missing_category}, summary, events, rejected, error));
    CHECK(summary.event_count == 0);
    CHECK(events.size() == 1);
    CHECK(events[0].total_ml == "0");
    CHECK(!events[0].components[0].counted);
    CHECK(!events[0].components[0].excluded_by_component_filter);
    CHECK(events[0].components[0].conversion_factor.empty());

    auto duplicate = make_actual_row("C8", "A8", "P11", "B8", "2026-08-01 13:00:00", "血浆", "1600", "ML");
    auto duplicate_again = duplicate;
    duplicate_again.cross_match_id = "C9";
    std::vector<search::ActualTransfusionRawRow> duplicate_rows{duplicate, duplicate_again};
    summary = {};
    events.clear();
    rejected.clear();
    CHECK(search::build_actual_massive_transfusion_statistics(
        query, duplicate_rows, summary, events, rejected, error));
    CHECK(summary.event_count == 0);
    CHECK(events.empty());
    CHECK(rejected.size() == 2);

    auto missing_time = make_actual_row("C10", "A10", "P12", "B10", "", "血浆", "1600", "ML");
    missing_time.apply_time = "2026-08-01 14:00:00";
    summary = {};
    rejected.clear();
    CHECK(search::build_actual_massive_transfusion_statistics(
        query, {missing_time}, summary, events, rejected, error));
    CHECK(events.empty());
    CHECK(rejected.size() == 1);
    CHECK(rejected[0].data_status.find("配血时间缺失") != std::string::npos);

    auto scoped = make_actual_row(
        "C12", "A12", "P14", "B12", "2026-08-01 15:00:00", "血浆", "1600", "ML");
    auto outside_duplicate = scoped;
    outside_duplicate.cross_match_id = "C13";
    outside_duplicate.match_date = "2026-08-10 15:00:00";
    outside_duplicate.apply_time = outside_duplicate.match_date;
    outside_duplicate.check_date = outside_duplicate.match_date;
    auto outside_missing_time = make_actual_row(
        "C14", "A14", "P15", "B14", "", "血浆", "1600", "ML");
    outside_missing_time.apply_time = "2026-08-10 16:00:00";
    outside_missing_time.check_date = outside_missing_time.apply_time;
    summary = {};
    events.clear();
    rejected.clear();
    CHECK(search::build_actual_massive_transfusion_statistics(
        query, {scoped, outside_duplicate, outside_missing_time},
        summary, events, rejected, error));
    CHECK(summary.raw_record_count == 1);
    CHECK(summary.event_count == 1);
    CHECK(events.size() == 1);
    CHECK(rejected.empty());

    auto time_choice = make_actual_row(
        "C11", "A11", "P13", "B11", "2026-08-01 08:00:00", "血浆", "1600", "ML");
    time_choice.blood_out_date = "2026-08-02 08:00:00";
    time_choice.apply_time = "2026-08-03 08:00:00";
    time_choice.check_date = "2026-08-03 09:00:00";
    time_choice.blood_out_record_count = 2;
    query.event_time_source = "out";
    summary = {};
    events.clear();
    rejected.clear();
    CHECK(search::build_actual_massive_transfusion_statistics(
        query, {time_choice}, summary, events, rejected, error));
    CHECK(events.size() == 1);
    CHECK(events[0].first_apply_time == "2026-08-02 08:00:00");
    CHECK(events[0].time_source == "出库时间");
    CHECK(events[0].audit_count == 1);
    CHECK(rejected.size() == 1);
    CHECK(rejected[0].data_status.find("最早出库时间") != std::string::npos);

    query.event_time_source = "check";
    summary = {};
    events.clear();
    rejected.clear();
    CHECK(search::build_actual_massive_transfusion_statistics(
        query, {time_choice}, summary, events, rejected, error));
    CHECK(events.size() == 1);
    CHECK(events[0].first_apply_time == "2026-08-03 09:00:00");
    CHECK(events[0].time_source == "血库审核时间");
    std::cout << "massive transfusion aggregation tests passed\n";
    return 0;
}
