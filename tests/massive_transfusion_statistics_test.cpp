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
    const char* composition, const char* number, const char* unit) {
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
    return value;
}

}  // namespace

int main() {
    search::MassiveTransfusionStatQuery query;
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
        make_row("7", "C1", "P4", "2026-08-01 12:00:00", "未审核", "72", "机采血小板", "1", "治疗量"),
        make_row("7", "C1", "P4", "2026-08-01 12:00:00", "未审核", "73", "混合冷沉淀凝血因子", "1", "U"),
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
        if (component.composition.find("冷沉淀") != std::string::npos) {
            CHECK(component.conversion_factor == "20");
            CHECK(component.converted_ml == "20");
        }
    }
    CHECK(excluded_count == 2);

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
    std::cout << "massive transfusion aggregation tests passed\n";
    return 0;
}
