#pragma once

#include <functional>
#include <string>
#include <vector>

namespace search {

struct QueryFilters {
    std::string connection_string;
    std::string patient_id;
    std::string barcode;
    std::string patient_name;
    std::string patient_no;
    std::vector<std::string> patient_nos;
    std::string patient_phone;
    std::string oper_no;
    std::string start_date;
    std::string end_date;
    std::string room_code;
    std::string patient_type;
    std::string report_status;
    std::string mach_code;
    std::string group_code;
    std::string item_code;
    std::string lis_abo_codes;
    std::string lis_rhd_codes;
    std::string lis_hgb_codes;
    std::string lis_plt_codes;
    std::string lis_irregular_antibody_codes;
    std::string lis_direct_antiglobulin_codes;
    std::string lis_blood_type_machines;
    std::string lis_cbc_machines;
    std::string lis_blood_exclude_machines;
    int limit = 300;
    bool skip_order_text = false;
};

struct ReportRow {
    std::string id;
    std::string rep_no;
    std::string oper_no;
    std::string name;
    std::string txm_no;
    std::string chk_date;
    std::string sex;
    std::string age;
    std::string bed_code;
    std::string patient_type;
    std::string requester;
    std::string reviewer;
    std::string group_name;
    std::string conf;
    std::string chk_flag;
    std::string zymz_print;
    std::string zzj_print;
    std::string reg_no;
    std::string dept_name;
    std::string order_text;
    std::string sample_name;
    std::string note;
    std::string oper_code;
    std::string collection_time;
    std::string inspect_date;
    std::string rep_time;
    std::string fee;
    std::string dean_oper;
    std::string req_doctor;
    std::string diag_name;
    std::string create_time;
    std::string patient_phone;
    std::string report_type;           // LS_AS_REPORT.assaypat_type: 0=emergency, 9=critical.
    std::string barcode_jz_flag;       // LS_AS_BARCODE.JZ_FLAG, used only for the right-list label.
    std::string mach_code;
    std::string mach_name;
    std::string room_code;
};

struct RoomOption {
    std::string room_code;  // LS_AS_ROOM.ROOM_CODE
    std::string room_name;  // LS_AS_ROOM.ROOM_NAME
    std::string dept_code;  // LS_AS_ROOM.Dept_Code；需要院区联动的查询会回填
};

struct PatientTypeOption {
    std::string type_code;
    std::string type_name;
};

struct MachineOption {
    std::string room_code;
    std::string mach_code;
    std::string mach_name;
    std::string py_code;
    std::string group_code;
    std::string group_name;
    std::string sample_code;
    std::string sample_name;
};

struct ResultRow {
    std::string group_name;
    std::string item_name;
    std::string result;
    std::string downbound;
    std::string upbound;
    std::string unit;
    std::string item_eng;
    std::string normal;
    std::string item_code;
    std::string normal_wj;            // 9=critical result, 0=has critical rule.
    std::string critical_low_bound;   // LS_AS_DEF_ITEMSCOPE.UPBOUND1
    std::string critical_high_bound;  // LS_AS_DEF_ITEMSCOPE.DNBOUND1
};

struct QualityControlLisQuery {
    std::string connection_string;
    std::string start_date;
    std::string end_date;
    std::string mach_code;
    std::string sample_no;
    std::vector<std::string> sample_nos;  // when non-empty, queries r.OPER_NO IN (...)
};

struct QualityControlLisRow {
    std::string entry_id;
    std::string rep_no;
    std::string room_code;
    std::string mach_code;
    std::string mach_name;
    std::string sample_no;
    std::string barcode_no;
    std::string tester_name;
    std::string report_date;
    std::string inspect_date;
    std::string report_time;
    std::string effective_time;
    std::string chk_flag;
    std::string conf;
    std::string item_code;
    std::string item_name;
    std::string item_eng;
    std::string result;
    std::string unit;
    std::string normal;
};

struct QualityControlSampleItemsQuery {
    std::string connection_string;
    std::string inspect_date;
    std::string mach_code;
    std::string sample_no;
};

struct QualityControlSampleItemRow {
    std::string item_code;
    std::string item_name;
    std::string item_eng;
    std::string unit;
    std::string latest_result;
    std::string latest_time;
    std::string latest_rep_no;
    std::string latest_entry_id;
    int point_count = 0;
};

struct LisSummary {
    std::string abo;
    std::string rhd;
    std::string blood_type_date;
    std::string hgb;
    std::string plt;
    std::string cbc_date;
    std::string irregular_antibody;
    std::string irregular_antibody_date;
    std::string direct_antiglobulin;
    std::string direct_antiglobulin_date;
};

struct BloodRequestRow {
    std::string tran_property;   // 输血性质/备血类型（TranProperty）
    std::string patient_name;    // 姓名
    std::string apply_dept;      // 申请科室
    std::string apply_bed_no;    // 备血（床号）
    std::string apply_abo;       // 申请ABO
    std::string apply_rhd;       // 申请RHD
    std::string apply_composition; // 申请成分
    std::string apply_form_no;   // 申请单号
    std::string check_doctor;    // 审核人
    std::string check_date;      // 审核时间
    std::string apply_status;    // 申请状态
    std::string patient_no;      // 病人编号（内部）
    std::string apply_time;      // 申请日期（内部）
    std::string urgency_level;   // 紧急程度（UrgencyLevel，右侧详情用）
    std::string transfusion_history; // 输血史（reactionHistory）
    std::string patient_no_type; // 病人类型（Patient_NOType）
    std::string patient_sex;     // 性别（Patient_Sex）
    std::string patient_age;     // 年龄（Patient_Age + Patient_AgeUnit）
    std::string reaction_history; // 反应史（FYS）
};

struct BloodQueryFilters {
    std::string connection_string;
    std::string patient_no;      // 病人编号
    std::string patient_name;    // 病人姓名
    std::string apply_form_no;   // 申请单号
    std::string apply_status;    // 申请状态
    std::string start_date;      // 开始日期
    std::string end_date;        // 结束日期
    int limit = 500;
};

struct BloodCrossMatchRow {
    std::string blood_out_date;
    std::string blood_out_man;
    std::string blood_bag_no;
    std::string product_code;
    std::string blood_type;
    std::string rhd;
    std::string composition;
    std::string norm;
    std::string unit;
    std::string cross_method;
    std::string main_result;
    std::string second_result;
    std::string match_date;
    std::string match_man;
    std::string source;
};

struct BarcodeQueryRow {
    std::string sample_no;       // 样本号（OPER_NO）
    std::string emergency;       // 急诊
    std::string barcode;         // 条形码
    std::string reg_no;          // 病人号
    std::string type_name;       // 类型
    std::string name;            // 姓名
    std::string sex;             // 性别
    std::string dept_name;       // 申请科室
    std::string bed_no;          // 床号
    std::string receiver;        // 签收人
    std::string receive_time;    // 签收时间
    std::string order_text;      // 医嘱内容
    std::string sample_name;     // 标本
    std::string tester;          // 检验者
    std::string reviewer;        // 审核者
    std::string review_time;     // 审核时间（REP_TIME）
    std::string review_elapsed;  // 签收-审核时间差（C++ 计算）
    long long review_elapsed_seconds = -1; // 时间差排序用
    std::string fee;             // 费用
    std::string request_doctor;  // 申请医生
    std::string status;          // 状态
    std::string note;            // 备注
    std::string reason;          // 原因
    std::string submitter;       // 送检
    std::string submit_time;     // 送检时间
    std::string request_time;    // 申请时间
    std::string cancel_time;     // 取消时间
    std::string cancel_operator; // 取消人
    std::string hzid;            // HZID
    std::string machine_status;  // 上机状态
    std::string report_no;       // 跳转常规报告用 REP_NO
    std::string machine_code;    // 跳转常规报告用 MACH_CODE
    std::string machine_name;    // 跳转常规报告用 MACH_NAME
    std::string room_code;       // 跳转常规报告用 ROOM_CODE
    std::string inspect_date;    // 跳转常规报告用 CHK_DATE
};

struct BarcodeQueryFilters {
    std::string connection_string;
    std::string date_field;      // Apply/Receive/Machine
    std::string start_date;
    std::string end_date;
    std::string barcode;
    std::string patient_name;
    std::string reg_no;
    std::string campus;          // 全部/老院/新院，按申请科室是否包含“滨水”派生
    std::string machine_status;  // 全部/已签收未上机/已上机未审核/已审核未发送/发送完成
    std::string room_code;
    std::vector<std::string> machine_statuses;
    std::vector<std::string> room_codes;
    bool canceled = false;       // true: CANCEL_DATE IS NOT NULL; false: CANCEL_DATE IS NULL
};

struct SpecimenOrderRow {
    std::string barcode;         // 条码号
    std::string room_code;       // 检验室/专业组代码
    std::string order_text;      // 医嘱内容
    std::string sample_name;     // 标本类型
    std::string fee;             // 费用
    std::string request_time;    // 申请时间
    std::string note;            // 备注
};

struct SpecimenBarcodeResult {
    std::string barcode;
    std::string reg_no;
    std::string type_code;
    std::string type_name;
    std::string name;
    std::string sex;
    std::string age;
    std::string dept_name;
    std::string bed_no;
    std::string requester;
    std::string room_code;
    std::string fee;
    std::string signed_time;
    std::string receiver;
    std::string collection_time;
    std::string submit_time;
    std::string jz_flag;
    std::string rep_no;
    std::string oper_no;
    std::string mach_code;
    std::string oper_state;
    std::string group_code;
    std::string chk_flag;
    std::string conf;
    std::string create_time;
    bool has_barcode_rows = false;
    bool has_report_rows = false;
    bool has_outpatient_rows = false;
    bool has_inpatient_rows = false;
    std::vector<SpecimenOrderRow> orders;
};

struct SpecimenBarcodeQuery {
    std::string connection_string;
    std::string barcode;
};

struct SpecimenSignedListRow {
    std::string barcode;         // 条码号
    std::string reg_no;          // 病人号
    std::string type_name;       // 病人类型
    std::string name;            // 姓名
    std::string sex;             // 性别
    std::string dept_name;       // 申请科室
    std::string order_text;      // 医嘱内容
    std::string fee;             // 费用
    std::string request_time;    // 申请时间
    std::string collection_time; // 采集时间
    std::string signed_time;     // 签收时间
    std::string submit_time;     // 送检时间
    std::string age;             // 年龄
    std::string receiver;        // 签收人
    std::string sample_name;     // 标本类型
    std::string room_code;       // 检验室名称/专业组代码
};

struct SpecimenSignedListQuery {
    std::string connection_string;
    bool use_sign_time = true;
    bool use_apply_time = false;
    std::string sign_start;
    std::string sign_end;
    std::string apply_start;
    std::string apply_end;
    std::string room_code;
    std::string patient_name;
};

struct HivStatSummary {
    int screening_count = 0;
    int positive_count = 0;
    int preoperative_screening_count = 0;
    int preoperative_positive_count = 0;
    int transfusion_screening_count = 0;
    int transfusion_positive_count = 0;
    int sti_clinic_screening_count = 0;
    int sti_clinic_positive_count = 0;
    int prenatal_screening_count = 0;
    int prenatal_positive_count = 0;
    int other_visit_screening_count = 0;
    int other_visit_positive_count = 0;
    int chemiluminescence_screening_count = 0;
    int chemiluminescence_positive_count = 0;
    int elisa_screening_count = 0;
    int elisa_positive_count = 0;
};

struct HivStatDetailRow {
    std::string mach_code;
    std::string machine_name;
    std::string sample_source;
    std::string methodology;
    std::string lab_department;
    std::string room_code;
    std::string item_code;
    std::string item_name;
    std::string rep_no;
    std::string txm_no;
    std::string oper_no;
    std::string patient_no;
    std::string name;
    std::string completed_blood_apply_forms;
    std::string patient_type;
    std::string dept_name;
    std::string result;
    std::string lower_bound;
    std::string upper_bound;
    std::string positive;
    std::string report_time;
};

struct HivStatQuery {
    std::string connection_string;
    int year = 0;
    int month = 0;
    std::string lab_department;
};

struct EmergencyStatSummary {
    int emergency_barcode_count = 0;
    int not_loaded_count = 0;
    int loaded_not_reviewed_count = 0;
    int reviewed_count = 0;
    int doctor_viewed_count = 0;
    int sent_count = 0;
    int unfinished_count = 0;
    int report_emergency_count = 0;
    int barcode_emergency_count = 0;
    int both_emergency_count = 0;
};

struct EmergencyStatDetailRow {
    std::string barcode;
    std::string emergency_source;
    std::string barcode_status;
    int min_oper_state = -1;  // Derived display state: REP_NO/CHK_FLAG take priority over delayed OPER_STATE.
    int wait_minutes = 0;
    int wait_seconds = 0;
    std::string sign_oper;
    std::string sign_dept;
    std::string lab_department;
    std::string in_date;
    std::string req_time;
    std::string reg_no;
    std::string type_name;
    std::string name;
    std::string sex;
    std::string age;
    std::string dept_name;
    std::string bed_code;
    std::string order_text;
    std::string sample_name;
    std::string rep_no;
    std::string oper_no;
    std::string mach_code;
    std::string mach_name;
    std::string inspect_date;
    std::string room_code;
    std::string chk_flag;
    std::string conf;
    std::string create_time;
    std::string review_time;
    std::string rep_time;
};

struct EmergencyStatQuery {
    std::string connection_string;
    std::string start_time;
    std::string end_time;
    std::string time_field;      // Sign/Apply
    std::string lab_department;  // 全部/老院/新院
    bool only_unfinished = false;
};

struct TatThresholds {
    int collection_to_receive_minutes = 30;
    int receive_to_machine_minutes = 30;
    int receive_to_review_minutes = 180;
    int collection_to_review_minutes = 240;
};

struct TatStatQuery {
    std::string connection_string;
    std::string start_time;
    std::string end_time;
    std::string room_code;
    std::string department_keyword;
    std::string order_keyword;
    std::string patient_type;  // 全部/住院/门诊
    bool emergency_only = false;
    TatThresholds thresholds;
    std::string current_time;  // Tests may inject a stable clock; empty uses local current time.
};

struct TatStatRawRow {
    std::string barcode;
    std::string patient_type;
    std::string reg_no;
    std::string name;
    std::string sex;
    std::string diagnosis;
    std::string bed_no;
    std::string age;
    std::string sample_name;
    std::string department_name;
    std::string room_code;
    std::string room_name;
    std::string order_text;
    std::string collection_time;
    std::string receive_time;
    std::string receiver;
    std::string report_no;
    std::string oper_no;
    std::string machine_code;
    std::string machine_name;
    std::string inspect_date;
    std::string machine_time;
    std::string review_time;
    std::string reviewer;
    std::string chk_flag;
    std::string conf;
    int barcode_oper_state = -1;
    bool barcode_emergency = false;
    bool report_emergency = false;
    bool has_report = false;
    bool report_reviewed = false;
    bool report_sent = false;
};

struct TatStatDetailRow : TatStatRawRow {
    std::string workflow_status;
    std::string tat_status;
    long long collection_to_receive_seconds = -1;
    long long receive_to_machine_seconds = -1;
    long long receive_to_review_seconds = -1;
    long long collection_to_review_seconds = -1;
    std::string collection_to_receive;
    std::string receive_to_machine;
    std::string receive_to_review;
    std::string collection_to_review;
    bool machine_waiting = false;
    bool review_waiting = false;
    bool time_abnormal = false;
};

struct TatStatSummary {
    int total_count = 0;
    int normal_count = 0;
    int overtime_count = 0;
    int time_abnormal_count = 0;
    int waiting_machine_count = 0;
    int waiting_review_count = 0;
};

struct BackupBloodStatQuery {
    std::string connection_string;
    std::string start_date;
    std::string end_date;
    std::string apply_status;  // 全部/未审核/已审核/已完结/已驳回
    std::string campus;        // 全部/老院/新院，C++ 内存派生后过滤
    bool include_deleted = false;
};

struct BackupBloodStatSummary {
    int total_count = 0;
    int apply_type_count = 0;
    int use_blood_note_count = 0;
    int apply_purpose_count = 0;
    int multiple_match_count = 0;
    int missing_apply_form_no_count = 0;
    int unreviewed_count = 0;
    int reviewed_count = 0;
    int completed_count = 0;
    int rejected_count = 0;
    int deleted_count = 0;
    int other_status_count = 0;
};

struct BackupBloodStatDetailRow {
    std::string campus;        // Apply_Dept 包含“滨水”=新院，否则=老院
    std::string match_source;  // 申请类型/用血备注/输血目的，可组合
    std::string apply_form_no;
    std::string apply_time;
    std::string tran_property;
    std::string use_blood_note;
    std::string apply_purpose;
    std::string apply_status;
    std::string patient_no;
    std::string patient_name;
    std::string apply_dept;
    std::string bed_no;
    bool delete_bit = false;
    bool apply_type_match = false;
    bool use_blood_note_match = false;
    bool apply_purpose_match = false;
};

struct TransfusionOrderStatQuery {
    std::string connection_string;
    std::string start_date;
    std::string end_date;
    std::string campus;  // 全部/老院/新院，C++ 内存派生后过滤
    bool include_rejected = false;
    bool include_deleted = false;
};

enum class TransfusionOrderUrgencyCategory {
    Emergency,
    Routine,
    Backup,
    Other,
};

struct TransfusionOrderStatRawRow {
    std::string apply_form_no;
    std::string apply_time;
    std::string apply_status;
    std::string remark;
    std::string patient_no;
    std::string patient_no_type;
    std::string patient_name;
    std::string apply_dept;
    std::string apply_dept_id;
    std::string bed_no;
    std::string apply_doctor;
    std::string tran_property;
    bool delete_bit = false;
};

struct TransfusionOrderStatDetailRow {
    std::string campus;
    std::string apply_form_no;
    std::string apply_time;
    std::string apply_status;
    std::string remark;
    std::string patient_no;
    std::string patient_no_type;
    std::string patient_name;
    std::string apply_dept;
    std::string apply_dept_id;
    std::string bed_no;
    std::string apply_doctor;
    std::string tran_property;
    std::string data_status;
    bool delete_bit = false;
};

struct TransfusionOrderStatSummary {
    int total_count = 0;
    int unreviewed_count = 0;
    int reviewed_count = 0;
    int completed_count = 0;
    int rejected_count = 0;
    int deleted_count = 0;
    int emergency_count = 0;
    int routine_count = 0;
    int backup_count = 0;
    int other_status_count = 0;
    int missing_apply_form_no_count = 0;
    int conflict_count = 0;
};

struct MassiveTransfusionStatQuery {
    std::string connection_string;
    std::string start_date;
    std::string end_date;
    std::string campus;  // 全部/老院/新院，按事件首张有效申请的申请科室派生
    std::string statistic_basis = "actual";       // actual/application；正式默认实际输血量
    std::string event_time_source = "out";        // match/out/apply/check；仅实际输血口径使用
    bool include_platelet_and_cryoprecipitate = false;
    double threshold_ml = 1600.0;
    bool threshold_inclusive = true;  // true: >= threshold_ml, false: > threshold_ml
};

struct ActualTransfusionRawRow {
    std::string cross_match_id;
    std::string apply_form_no;
    std::string patient_no;
    std::string patient_no_type;
    std::string patient_name;
    std::string verify_state;
    bool cross_match_deleted = false;
    std::string blood_in_id;
    std::string match_date;
    std::string blood_out_date;
    int blood_out_record_count = 0;
    std::string apply_main_id;
    std::string apply_patient_no;
    std::string apply_time;
    std::string check_date;
    std::string apply_status;
    bool apply_deleted = false;
    std::string apply_dept;
    std::string bed_no;
    std::string apply_doctor;
    std::string blood_info_id;
    std::string blood_bag_no;
    std::string product_code;
    std::string composition;
    std::string norm;
    std::string unit;
    std::string composition_type_id;
};

struct MassiveTransfusionRawRow {
    std::string main_id;
    std::string apply_form_no;
    std::string patient_no;
    std::string patient_no_type;
    std::string patient_name;
    std::string patient_sex;
    std::string patient_age;
    std::string apply_time;
    std::string apply_status;
    std::string apply_dept;
    std::string apply_dept_id;
    std::string bed_no;
    std::string apply_doctor;
    std::string son_id;
    std::string son_guid;
    std::string composition;
    std::string apply_num;
    std::string apply_unit;
    std::string composition_big_id;
};

struct MassiveTransfusionComponentDetailRow {
    std::string statistic_basis;
    std::string time_source;
    std::string selected_time;
    std::string event_id;
    std::string campus;
    std::string patient_no;
    std::string patient_name;
    std::string apply_form_no;
    std::string apply_time;
    std::string apply_status;
    std::string apply_dept;
    std::string bed_no;
    std::string apply_doctor;
    std::string composition;
    std::string composition_category_id;
    std::string apply_num;
    std::string apply_unit;
    std::string conversion_factor;
    std::string converted_ml;
    std::string data_status;
    std::string cross_match_id;
    std::string blood_in_id;
    std::string blood_bag_no;
    std::string product_code;
    std::string verify_state;
    std::string match_date;
    std::string blood_out_date;
    std::string check_date;
    int blood_out_record_count = 0;
    bool counted = false;
    bool rejected = false;
    bool excluded_by_component_filter = false;
    bool cross_match_deleted = false;
    bool application_anomaly = false;
};

struct MassiveTransfusionEventRow {
    std::string statistic_basis;
    std::string time_source;
    std::string event_id;
    std::string campus;
    std::string patient_no;
    std::string patient_name;          // 列表姓名；取事件时间顺序中最后出现的非空姓名
    std::string all_patient_names;     // 事件内去重后的全部非空姓名，按首次出现顺序连接
    std::string patient_no_type;
    std::string first_apply_time;
    std::string window_end_time;
    std::string last_apply_time;
    std::string total_ml;
    std::string first_apply_form_no;
    std::string apply_form_nos;
    std::string composition_summary;
    std::string apply_dept;
    std::string bed_no;
    std::string status_summary;
    std::string data_status;
    int application_count = 0;
    int component_count = 0;
    int rejected_application_count = 0;
    int issue_count = 0;
    int audit_count = 0;
    int patient_name_count = 0;
    bool qualifies = false;
    bool complete = true;
    bool cross_department = false;
    bool multiple_patient_names = false;
    std::vector<MassiveTransfusionComponentDetailRow> components;
};

struct MassiveTransfusionStatSummary {
    int raw_record_count = 0;
    int event_count = 0;
    int patient_count = 0;
    int application_count = 0;
    int component_count = 0;
    double total_ml = 0.0;
    int issue_event_count = 0;
    int issue_component_count = 0;
    int rejected_application_count = 0;
    int audit_record_count = 0;
    int missing_patient_no_count = 0;
    int missing_apply_form_no_count = 0;
};

struct ImmuneDuplicateStatQuery {
    std::string connection_string;
    std::string start_time;
    std::string end_time;
};

struct ImmuneDuplicateStatSummary {
    int base_barcode_count = 0;
    int base_patient_count = 0;
    int duplicate_patient_count = 0;
    int duplicate_barcode_count = 0;
    int duplicate_item_count = 0;
    int same_barcode_duplicate_count = 0;
    int cross_barcode_duplicate_count = 0;
    int missing_base_barcode_count = 0;
    int unmatched_inpatient_count = 0;
};

struct ImmuneDuplicateStatDetailRow {
    std::string patient_no;
    std::string name;
    std::string type_name;
    std::string department;
    std::string bed_no;
    std::string base_barcode;
    std::string base_sample_no;
    std::string base_order_text;
    std::string base_sign_time;
    std::string duplicate_barcode;
    std::string duplicate_sample_no;
    std::string duplicate_item_code;
    std::string duplicate_item_name;
    std::string duplicate_category;
    std::string result;
    std::string unit;
    std::string duplicate_sign_time;
    std::string report_time;
    std::string reviewed;
    std::string sent;
    std::string relation;
    std::string report_no;
    std::string machine_code;
    std::string machine_name;
    std::string room_code;
    std::string inspect_date;
};

struct OutpatientChargeQuery {
    std::string connection_string;
    std::string start_time;
    std::string end_time;
    std::string lab_department;  // 全部/老院/新院
    bool include_non_lab = false;
    std::string outpatient_no;
    std::string patient_name;
    std::string id_card;
};

struct OutpatientChargeRow {
    std::string outpatient_no;
    std::string invoice_no;
    std::string card_no;
    std::string name;
    std::string sex;
    std::string age;
    std::string item_name;
    std::string application_department;
    std::string unit_price;
    std::string quantity;
    std::string unit;
    std::string amount;
    std::string charge_time;
    std::string barcode;
    std::string sample_name;
    std::string barcode_print_time;
    std::string lab_department;
};

using LogFn = std::function<void(const std::string&)>;

long long sql_datetime_diff_seconds(const std::string& start, const std::string& end);
std::string format_duration_seconds_zh(long long total_seconds);
std::string employee_display_name(const std::string& employee_code,
                                  const std::string& dictionary_name);

bool query_rooms(const std::string& connection_string, std::vector<RoomOption>& rows, std::string& error, LogFn log = {});
bool query_barcode_rooms(const std::string& connection_string, std::vector<RoomOption>& rows, std::string& error, LogFn log = {});
bool query_report_machine_picker_rooms(const std::string& connection_string, std::vector<RoomOption>& rows, std::string& error, LogFn log = {});
bool query_patient_types(const std::string& connection_string, std::vector<PatientTypeOption>& rows, std::string& error, LogFn log = {});
bool query_machines(const std::string& connection_string, const std::string& room_code, std::vector<MachineOption>& rows, std::string& error, LogFn log = {});
bool query_report_machine_picker_machines(const std::string& connection_string, const std::string& room_code, std::vector<MachineOption>& rows, std::string& error, LogFn log = {});
bool query_reports(const QueryFilters& filters, std::vector<ReportRow>& rows, std::string& error, LogFn log = {});
bool query_blood_lis_reports(const QueryFilters& filters, std::vector<ReportRow>& rows, std::string& error, LogFn log = {});
bool query_latest_report_phone_by_reg_no(const std::string& connection_string, const std::string& reg_no, std::string& phone, std::string& error, LogFn log = {});
bool query_inpatient_nos_by_social_no_from_reg_no(const std::string& connection_string, const std::string& reg_no, std::vector<std::string>& inpatient_nos, std::string& error, LogFn log = {});
bool query_results(const std::string& connection_string, const std::string& rep_no, std::vector<ResultRow>& rows, std::string& error, LogFn log = {});
bool query_quality_control_lis_results(const QualityControlLisQuery& query, std::vector<QualityControlLisRow>& rows, std::string& error, LogFn log = {});
bool query_quality_control_sample_items(const QualityControlSampleItemsQuery& query, std::vector<QualityControlSampleItemRow>& rows, std::string& error, LogFn log = {});
bool query_report_picture(const std::string& connection_string, const std::string& rep_no, std::vector<unsigned char>& picture, std::string& error, LogFn log = {});
bool query_lis_summary(const QueryFilters& filters, LisSummary& summary, std::string& error, LogFn log = {});
bool query_blood_requests(const BloodQueryFilters& filters, std::vector<BloodRequestRow>& rows, std::string& error, LogFn log = {});
bool query_blood_crossmatch_history(const std::string& connection_string, const std::string& patient_no, std::vector<BloodCrossMatchRow>& rows, std::string& error, LogFn log = {});
bool query_barcodes(const BarcodeQueryFilters& filters, std::vector<BarcodeQueryRow>& rows, std::string& error, LogFn log = {});
bool query_specimen_barcode(const SpecimenBarcodeQuery& query, SpecimenBarcodeResult& result, std::string& error, LogFn log = {});
bool query_specimen_signed_list(const SpecimenSignedListQuery& query, std::vector<SpecimenSignedListRow>& rows, std::string& error, LogFn log = {});
bool query_hiv_statistics(const HivStatQuery& query, HivStatSummary& summary, std::vector<HivStatDetailRow>& rows, std::string& error, LogFn log = {});
bool query_emergency_statistics(const EmergencyStatQuery& query, EmergencyStatSummary& summary, std::vector<EmergencyStatDetailRow>& rows, std::string& error, LogFn log = {});
bool build_tat_statistics(const TatStatQuery& query,
                          const std::vector<TatStatRawRow>& raw_rows,
                          TatStatSummary& summary,
                          std::vector<TatStatDetailRow>& rows,
                          std::string& error);
void refresh_tat_statistics(const TatThresholds& thresholds,
                            const std::string& current_time,
                            TatStatSummary& summary,
                            std::vector<TatStatDetailRow>& rows);
bool query_tat_statistics(const TatStatQuery& query, TatStatSummary& summary,
                          std::vector<TatStatDetailRow>& rows,
                          std::string& error, LogFn log = {});
bool query_backup_blood_statistics(const BackupBloodStatQuery& query, BackupBloodStatSummary& summary, std::vector<BackupBloodStatDetailRow>& rows, std::string& error, LogFn log = {});
bool build_transfusion_order_statistics(const TransfusionOrderStatQuery& query,
                                        const std::vector<TransfusionOrderStatRawRow>& raw_rows,
                                        TransfusionOrderStatSummary& summary,
                                        std::vector<TransfusionOrderStatDetailRow>& rows,
                                        std::string& error);
TransfusionOrderUrgencyCategory classify_transfusion_order_urgency(const std::string& value);
bool query_transfusion_order_statistics(const TransfusionOrderStatQuery& query,
                                        TransfusionOrderStatSummary& summary,
                                        std::vector<TransfusionOrderStatDetailRow>& rows,
                                        std::string& error, LogFn log = {});
bool build_massive_transfusion_statistics(const MassiveTransfusionStatQuery& query,
                                          const std::vector<MassiveTransfusionRawRow>& raw_rows,
                                          MassiveTransfusionStatSummary& summary,
                                          std::vector<MassiveTransfusionEventRow>& events,
                                          std::vector<MassiveTransfusionComponentDetailRow>& audit_rows,
                                          std::string& error);
bool build_actual_massive_transfusion_statistics(
    const MassiveTransfusionStatQuery& query,
    const std::vector<ActualTransfusionRawRow>& raw_rows,
    MassiveTransfusionStatSummary& summary,
    std::vector<MassiveTransfusionEventRow>& events,
    std::vector<MassiveTransfusionComponentDetailRow>& audit_rows,
    std::string& error);
bool query_massive_transfusion_statistics(const MassiveTransfusionStatQuery& query,
                                          MassiveTransfusionStatSummary& summary,
                                          std::vector<MassiveTransfusionEventRow>& events,
                                          std::vector<MassiveTransfusionComponentDetailRow>& audit_rows,
                                          std::string& error, LogFn log = {});
bool query_immune_duplicate_statistics(const ImmuneDuplicateStatQuery& query, ImmuneDuplicateStatSummary& summary, std::vector<ImmuneDuplicateStatDetailRow>& rows, std::string& error, LogFn log = {});
bool query_outpatient_charges(const OutpatientChargeQuery& query, std::vector<OutpatientChargeRow>& rows, std::string& error, LogFn log = {});

}  // namespace search
