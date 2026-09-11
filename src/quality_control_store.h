#pragma once

#ifdef _WIN32

#include <string>
#include <vector>

namespace qc {

struct Config {
    int id = 0;
    int sample_config_id = 0;
    int sample_item_id = 0;
    int lot_id = 0;
    bool enabled = true;
    std::string room_code;
    std::string mach_code;
    std::string mach_name;
    std::string sample_no;
    std::string qc_name;
    std::string level;
    std::string item_code;
    std::string item_name;
    std::string item_eng;
    std::string unit;
    std::string lot_no;
    std::string lot_valid_from;
    std::string lot_valid_to;
    std::string target_mean;
    std::string target_sd;
};

struct SampleConfig {
    int id = 0;
    bool enabled = true;
    std::string room_code;
    std::string mach_code;
    std::string mach_name;
    std::string sample_no;
    std::string qc_name;
    std::string level;
};

struct SampleItem {
    int id = 0;
    int sample_config_id = 0;
    bool enabled = true;
    std::string item_code;
    std::string item_name;
    std::string item_eng;
    std::string unit;
    std::string qc_name;
    std::string level;
    int sort_order = 0;
};

struct Lot {
    int id = 0;
    int config_id = 0;
    int sample_config_id = 0;
    bool enabled = true;
    std::string lot_no;
    std::string target_mean;
    std::string target_sd;
    std::string valid_from;
    std::string valid_to;
    std::string note;
};

struct LotItemTarget {
    int id = 0;
    int lot_id = 0;
    int sample_item_id = 0;
    std::string target_mean;
    std::string target_sd;
};

struct Result {
    int id = 0;
    std::string source_rep_no;
    std::string source_entry_key;
    std::string room_code;
    std::string mach_code;
    std::string mach_name;
    std::string sample_no;
    std::string tester_name;
    std::string report_date;
    std::string inspect_date;
    std::string report_time;
    std::string effective_time;
    std::string item_code;
    std::string item_name;
    std::string item_eng;
    std::string result_text;
    double result_value = 0.0;
    bool has_numeric_value = false;
    std::string unit;
    std::string normal;
    std::string qc_name;
    std::string level;
    std::string lot_no;
    std::string lot_valid_from;
    std::string lot_valid_to;
    std::string target_mean;
    std::string target_sd;
    double qc_mean = 0.0;
    double qc_sd = 0.0;
    double qc_z = 0.0;
    bool has_qc_stats = false;
    bool has_qc_z = false;
    std::string qc_status;
    std::string qc_rules;
    std::string data_source;
    std::string cached_at;
};

struct Query {
    std::string start_date;
    std::string end_date;
    std::string mach_code;
    std::string item_code;
    std::string level;
};

struct CacheMeta {
    bool found = false;
    int row_count = 0;
    std::string cached_at;
    std::string refreshed_at;
    std::string latest_effective_time;
    std::string max_entry_id;
};

std::wstring default_db_path();

bool ensure_store(std::string& error);
bool load_configs(std::vector<Config>& rows, std::string& error);
bool load_analysis_configs(std::vector<Config>& rows, std::string& error);
bool save_config(Config& row, std::string& error);
bool delete_config(int id, std::string& error);
bool load_sample_configs(std::vector<SampleConfig>& rows, std::string& error);
bool save_sample_config(SampleConfig& row, std::string& error);
bool delete_sample_config(int id, std::string& error);
bool load_sample_items(int sample_config_id, std::vector<SampleItem>& rows, std::string& error);
bool save_sample_item(SampleItem& row, std::string& error);
bool delete_sample_item(int id, std::string& error);
bool load_lots(std::vector<Lot>& rows, std::string& error);
bool load_lots_for_config(int config_id, std::vector<Lot>& rows, std::string& error);
bool load_lots_for_sample_config(int sample_config_id, std::vector<Lot>& rows, std::string& error);
bool save_lot(Lot& row, std::string& error);
bool delete_lot(int id, std::string& error);
bool load_lot_item_targets(int lot_id, std::vector<LotItemTarget>& rows, std::string& error);
bool save_lot_item_target(LotItemTarget& row, std::string& error);
bool load_result_cache(const Query& query, std::vector<Result>& rows, CacheMeta& meta, std::string& error);
bool save_result_cache(const Query& query, const std::string& sample_scope,
                       const std::string& item_scope, const std::vector<Result>& rows, std::string& error);

}  // namespace qc

#endif
