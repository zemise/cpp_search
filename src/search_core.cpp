#include "search_core.h"
#include "search_text.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <sqlext.h>
#endif

namespace search {
namespace {

constexpr const char* kRedCellCompositionTypeId = "1";
constexpr const char* kPlasmaCompositionTypeId = "2";
constexpr const char* kCryoprecipitateCompositionTypeId = "3";
constexpr const char* kPlateletCompositionTypeId = "4";

bool is_known_composition_type_id(const std::string& value) {
    return value == kRedCellCompositionTypeId ||
           value == kPlasmaCompositionTypeId ||
           value == kCryoprecipitateCompositionTypeId ||
           value == kPlateletCompositionTypeId;
}

void collect_event_patient_name(const std::string& value,
                                std::vector<std::string>& ordered_names,
                                std::set<std::string>& seen_names) {
    const std::string name = trim(value);
    if (!name.empty() && seen_names.insert(name).second) {
        ordered_names.push_back(name);
    }
}

void finalize_event_patient_names(MassiveTransfusionEventRow& event,
                                  const std::vector<std::string>& ordered_names) {
    event.all_patient_names.clear();
    for (const auto& name : ordered_names) {
        if (!event.all_patient_names.empty()) event.all_patient_names += " → ";
        event.all_patient_names += name;
    }
    event.patient_name_count = static_cast<int>(ordered_names.size());
    event.multiple_patient_names = ordered_names.size() > 1;
    event.patient_name = ordered_names.empty() ? std::string() : ordered_names.back();
}

std::string sql_escape(std::string value) {
    size_t pos = 0;
    while ((pos = value.find('\'', pos)) != std::string::npos) {
        value.insert(pos, "'");
        pos += 2;
    }
    return value;
}

std::string sql_string_list(const std::vector<std::string>& values) {
    std::ostringstream out;
    bool first = true;
    for (const auto& value : values) {
        const std::string trimmed = trim(value);
        if (trimmed.empty()) continue;
        if (!first) out << ",";
        out << "'" << sql_escape(trimmed) << "'";
        first = false;
    }
    return out.str();
}

std::string local_datetime_text() {
    const std::time_t value = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
                  local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                  local.tm_hour, local.tm_min, local.tm_sec);
    return buffer;
}

bool parse_sql_datetime(const std::string& value, std::tm& out) {
    const std::string text = trim(value);
    if (text.size() < 10) return false;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) < 3) {
        return false;
    }
    out = std::tm{};
    out.tm_year = year - 1900;
    out.tm_mon = month - 1;
    out.tm_mday = day;
    out.tm_hour = hour;
    out.tm_min = minute;
    out.tm_sec = second;
    out.tm_isdst = -1;
    return true;
}

bool valid_date_parts(int year, int month, int day) {
    if (year < 1900 || year > 9999 || month < 1 || month > 12 || day < 1) return false;
    static const int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int max_day = days_in_month[month - 1];
    const bool leap_year = (year % 400 == 0) || (year % 4 == 0 && year % 100 != 0);
    if (month == 2 && leap_year) {
        max_day = 29;
    }
    return day <= max_day;
}

bool parse_sql_datetime_seconds(const std::string& value, long long& seconds) {
    const std::string text = trim(value);
    if (text.size() != 19 || text[4] != '-' || text[7] != '-' || text[10] != ' ' ||
        text[13] != ':' || text[16] != ':') {
        return false;
    }
    const auto digit = [&text](size_t index) {
        const unsigned char ch = static_cast<unsigned char>(text[index]);
        return std::isdigit(ch) ? static_cast<int>(ch - '0') : -1;
    };
    const auto two_digits = [&digit](size_t index) {
        const int high = digit(index);
        const int low = digit(index + 1);
        return high < 0 || low < 0 ? -1 : high * 10 + low;
    };

    const int y0 = digit(0);
    const int y1 = digit(1);
    const int y2 = digit(2);
    const int y3 = digit(3);
    if (y0 < 0 || y1 < 0 || y2 < 0 || y3 < 0) return false;
    int year = y0 * 1000 + y1 * 100 + y2 * 10 + y3;
    const int month = two_digits(5);
    const int day = two_digits(8);
    const int hour = two_digits(11);
    const int minute = two_digits(14);
    const int second = two_digits(17);
    if (!valid_date_parts(year, month, day) || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59) {
        return false;
    }

    // Convert a civil date to a day number using Gregorian calendar arithmetic.
    // The constant epoch is irrelevant for differences, and no timezone lookup is needed.
    year -= month <= 2;
    const int era = year / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned adjusted_month = static_cast<unsigned>(month + (month > 2 ? -3 : 9));
    const unsigned day_of_year = (153 * adjusted_month + 2) / 5 + static_cast<unsigned>(day) - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    const long long days = static_cast<long long>(era) * 146097 + day_of_era;
    seconds = days * 86400 + hour * 3600 + minute * 60 + second;
    return true;
}

std::string date_from_yyyymmdd_prefix(const std::string& value) {
    const std::string text = trim(value);
    if (text.size() < 8) return "";
    if (!std::all_of(text.begin(), text.begin() + 8, [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        return "";
    }
    const int year = std::atoi(text.substr(0, 4).c_str());
    const int month = std::atoi(text.substr(4, 2).c_str());
    const int day = std::atoi(text.substr(6, 2).c_str());
    if (!valid_date_parts(year, month, day)) return "";
    return text.substr(0, 4) + "-" + text.substr(4, 2) + "-" + text.substr(6, 2);
}

std::string date_from_sql_datetime(const std::string& value) {
    std::tm parsed{};
    if (!parse_sql_datetime(value, parsed)) return "";
    char buffer[11] = {};
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d",
                  parsed.tm_year + 1900, parsed.tm_mon + 1, parsed.tm_mday);
    return buffer;
}

long long seconds_between_sql_datetimes(const std::string& start, const std::string& end) {
    long long start_seconds = 0;
    long long end_seconds = 0;
    if (!parse_sql_datetime_seconds(start, start_seconds) ||
        !parse_sql_datetime_seconds(end, end_seconds) || end_seconds < start_seconds) {
        return -1;
    }
    return end_seconds - start_seconds;
}

std::string age_from_birthdate(const std::string& birthdate) {
    std::tm birth_tm{};
    if (!parse_sql_datetime(birthdate, birth_tm)) {
        return "";
    }
    std::time_t now_time = std::time(nullptr);
    if (now_time == static_cast<std::time_t>(-1)) {
        return "";
    }
    std::tm* now_tm = std::localtime(&now_time);
    if (!now_tm) {
        return "";
    }

    int years = now_tm->tm_year - birth_tm.tm_year;
    if (now_tm->tm_mon < birth_tm.tm_mon ||
        (now_tm->tm_mon == birth_tm.tm_mon && now_tm->tm_mday < birth_tm.tm_mday)) {
        --years;
    }
    if (years < 0) {
        return "";
    }
    return std::to_string(years) + "岁";
}

std::vector<std::string> split(const std::string& text, char delimiter) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string part;
    while (std::getline(ss, part, delimiter)) {
        out.push_back(part);
    }
    return out;
}

std::string upper_ascii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return text;
}

bool contains_text(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

void add_count(int& screening_count, int& positive_count, bool is_positive) {
    ++screening_count;
    if (is_positive) {
        ++positive_count;
    }
}

int non_negative_difference(int total, int used) {
    return total > used ? total - used : 0;
}

bool is_hiv_sti_clinic_dept(const std::string& dept_name) {
    return contains_text(dept_name, "皮肤科门诊");
}

bool is_hiv_other_visit_dept(const std::string& dept_name) {
    const std::string normalized = trim(dept_name);
    return normalized.empty() || normalized == "0" ||
           contains_text(normalized, "体检") ||
           contains_text(normalized, "儿童保健") ||
           contains_text(normalized, "健康管理") ||
           contains_text(normalized, "GCP");
}

bool is_hiv_prenatal_dept(const std::string& dept_name) {
    return contains_text(dept_name, "产科门诊") ||
           contains_text(dept_name, "早孕关爱门诊");
}

std::string sql_item_code_list(const std::string& text, const char* fallback) {
    const std::string source = trim(text).empty() ? fallback : text;
    std::vector<std::string> codes;
    std::set<std::string> seen;
    std::string token;
    auto flush = [&]() {
        token = trim(token);
        if (!token.empty() &&
            std::all_of(token.begin(), token.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; }) &&
            seen.insert(token).second) {
            codes.push_back(token);
        }
        token.clear();
    };

    for (unsigned char ch : source) {
        if (std::isdigit(ch)) {
            token.push_back(static_cast<char>(ch));
            continue;
        }
        flush();
    }
    flush();

    if (codes.empty() && source != fallback) {
        return sql_item_code_list(fallback, fallback);
    }

    std::ostringstream out;
    for (size_t i = 0; i < codes.size(); ++i) {
        if (i > 0) out << ",";
        out << codes[i];
    }
    return out.str();
}

std::string sql_room_machine_filter(const std::string& text, const char* report_alias) {
    std::vector<std::pair<std::string, std::vector<std::string>>> groups;
    std::set<std::string> seen_pairs;
    for (const auto& raw_group : split(text, ';')) {
        const std::string group = trim(raw_group);
        const auto colon = group.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string room = trim(group.substr(0, colon));
        if (room.empty() || !std::all_of(room.begin(), room.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
            continue;
        }
        std::vector<std::string> machines;
        for (const auto& raw_machine : split(group.substr(colon + 1), ',')) {
            const std::string machine = trim(raw_machine);
            if (machine.empty() ||
                !std::all_of(machine.begin(), machine.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
                continue;
            }
            const std::string key = room + ":" + machine;
            if (seen_pairs.insert(key).second) {
                machines.push_back(machine);
            }
        }
        if (!machines.empty()) {
            groups.push_back({room, machines});
        }
    }
    if (groups.empty()) {
        return "";
    }

    std::ostringstream sql;
    sql << " AND (";
    for (size_t i = 0; i < groups.size(); ++i) {
        if (i > 0) sql << " OR ";
        sql << "(" << report_alias << ".ROOM_CODE=" << groups[i].first
            << " AND " << report_alias << ".MACH_CODE";
        if (groups[i].second.size() == 1) {
            sql << "=" << groups[i].second.front();
        } else {
            sql << " IN (";
            for (size_t j = 0; j < groups[i].second.size(); ++j) {
                if (j > 0) sql << ",";
                sql << groups[i].second[j];
            }
            sql << ")";
        }
        sql << ")";
    }
    sql << ")";
    return sql.str();
}

std::string sql_room_machine_exclude_filter(const std::string& text, const char* report_alias) {
    std::set<std::string> rooms;
    std::vector<std::pair<std::string, std::vector<std::string>>> groups;
    std::set<std::string> seen_pairs;
    for (const auto& raw_group : split(text, ';')) {
        const std::string group = trim(raw_group);
        const auto colon = group.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string room = trim(group.substr(0, colon));
        if (room.empty() || !std::all_of(room.begin(), room.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
            continue;
        }
        const std::string machine_text = trim(group.substr(colon + 1));
        if (machine_text.empty()) {
            rooms.insert(room);
            continue;
        }
        std::vector<std::string> machines;
        for (const auto& raw_machine : split(machine_text, ',')) {
            const std::string machine = trim(raw_machine);
            if (machine.empty() ||
                !std::all_of(machine.begin(), machine.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
                continue;
            }
            const std::string key = room + ":" + machine;
            if (seen_pairs.insert(key).second) {
                machines.push_back(machine);
            }
        }
        if (!machines.empty()) {
            groups.push_back({room, machines});
        }
    }
    if (rooms.empty() && groups.empty()) {
        return "";
    }

    std::ostringstream sql;
    sql << " AND NOT (";
    bool need_or = false;
    if (!rooms.empty()) {
        sql << "isnull(" << report_alias << ".ROOM_CODE,-1)";
        if (rooms.size() == 1) {
            sql << "=" << *rooms.begin();
        } else {
            sql << " IN (";
            size_t idx = 0;
            for (const auto& room : rooms) {
                if (idx++ > 0) sql << ",";
                sql << room;
            }
            sql << ")";
        }
        need_or = true;
    }
    for (const auto& group : groups) {
        if (need_or) sql << " OR ";
        sql << "(isnull(" << report_alias << ".ROOM_CODE,-1)=" << group.first
            << " AND isnull(" << report_alias << ".MACH_CODE,-1)";
        if (group.second.size() == 1) {
            sql << "=" << group.second.front();
        } else {
            sql << " IN (";
            for (size_t i = 0; i < group.second.size(); ++i) {
                if (i > 0) sql << ",";
                sql << group.second[i];
            }
            sql << ")";
        }
        sql << ")";
        need_or = true;
    }
    sql << ")";
    return sql.str();
}

std::map<std::string, std::string> parse_connection_kv(const std::string& text) {
    std::map<std::string, std::string> values;
    for (const auto& part : split(text, ';')) {
        const auto pos = part.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        values[upper_ascii(trim(part.substr(0, pos)))] = trim(part.substr(pos + 1));
    }
    return values;
}

std::string candidate_driver_name(const std::string& candidate) {
    const auto upper = upper_ascii(candidate);
    const auto pos = upper.find("DRIVER={");
    if (pos == std::string::npos) {
        return "manual";
    }
    const auto start = pos + 8;
    const auto end = candidate.find('}', start);
    if (end == std::string::npos) {
        return "manual";
    }
    return candidate.substr(start, end - start);
}

std::vector<std::string> odbc_candidates(const std::string& input) {
    if (input.find("DRIVER=") != std::string::npos || input.find("Driver=") != std::string::npos) {
        return {input};
    }
    const auto kv = parse_connection_kv(input);
    const auto server_it = kv.find("DATA SOURCE");
    const auto db_it = kv.find("INITIAL CATALOG");
    const auto user_it = kv.find("USER ID");
    const auto pass_it = kv.find("PASSWORD");
    if (server_it == kv.end() || db_it == kv.end() || user_it == kv.end() || pass_it == kv.end()) {
        return {input};
    }

    return {
        "DRIVER={ODBC Driver 18 for SQL Server};SERVER=" + server_it->second +
            ";DATABASE=" + db_it->second +
            ";UID=" + user_it->second +
            ";PWD=" + pass_it->second +
            ";TrustServerCertificate=Yes;",
        "DRIVER={ODBC Driver 17 for SQL Server};SERVER=" + server_it->second +
            ";DATABASE=" + db_it->second +
            ";UID=" + user_it->second +
            ";PWD=" + pass_it->second +
            ";TrustServerCertificate=Yes;",
        "DRIVER={SQL Server};SERVER=" + server_it->second +
            ";DATABASE=" + db_it->second +
            ";UID=" + user_it->second +
            ";PWD=" + pass_it->second + ";",
    };
}

#ifdef _WIN32
constexpr SQLULEN kLoginTimeoutSeconds = 5;

std::mutex& preferred_odbc_candidate_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, std::string>& preferred_odbc_candidates() {
    static std::map<std::string, std::string> cache;
    return cache;
}

std::string cached_odbc_candidate(const std::string& connection_string) {
    std::lock_guard<std::mutex> lock(preferred_odbc_candidate_mutex());
    const auto it = preferred_odbc_candidates().find(connection_string);
    return it == preferred_odbc_candidates().end() ? std::string{} : it->second;
}

void remember_odbc_candidate(const std::string& connection_string, const std::string& candidate) {
    std::lock_guard<std::mutex> lock(preferred_odbc_candidate_mutex());
    preferred_odbc_candidates()[connection_string] = candidate;
}

std::vector<std::string> prioritized_odbc_candidates(const std::string& connection_string,
                                                     std::string& cached_candidate) {
    auto candidates = odbc_candidates(connection_string);
    cached_candidate = cached_odbc_candidate(connection_string);
    if (cached_candidate.empty()) {
        return candidates;
    }
    candidates.erase(std::remove(candidates.begin(), candidates.end(), cached_candidate), candidates.end());
    candidates.insert(candidates.begin(), cached_candidate);
    return candidates;
}

void enable_odbc_connection_pooling_once(LogFn log) {
    static std::once_flag flag;
    std::call_once(flag, [log]() {
        const SQLRETURN rc = SQLSetEnvAttr(
            SQL_NULL_HANDLE,
            SQL_ATTR_CONNECTION_POOLING,
            reinterpret_cast<SQLPOINTER>(SQL_CP_ONE_PER_HENV),
            0);
        if (log) {
            log(std::string("db odbc pooling ") +
                (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO ? "enabled" : "not enabled") + "\n");
        }
    });
}

struct DbContext {
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;

    ~DbContext() {
        if (dbc != SQL_NULL_HDBC) {
            SQLDisconnect(dbc);
            SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        }
        if (env != SQL_NULL_HENV) {
            SQLFreeHandle(SQL_HANDLE_ENV, env);
        }
    }

    // Non-copyable
    DbContext() = default;
    DbContext(const DbContext&) = delete;
    DbContext& operator=(const DbContext&) = delete;
};

std::string collect_diag(SQLSMALLINT handle_type, SQLHANDLE handle);

std::string fetch_column(SQLHSTMT stmt, SQLUSMALLINT col) {
    std::wstring buffer(2048, L'\0');
    SQLLEN indicator = 0;
    const SQLRETURN rc = SQLGetData(stmt, col, SQL_C_WCHAR, buffer.data(),
                                    static_cast<SQLLEN>(buffer.size() * sizeof(wchar_t)), &indicator);
    if (!(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) || indicator == SQL_NULL_DATA) {
        return "";
    }
    buffer.resize(wcslen(buffer.c_str()));
    return trim(wide_to_utf8(buffer));
}

bool fetch_binary_column(SQLHSTMT stmt, SQLUSMALLINT col, std::vector<unsigned char>& out, std::string& error) {
    out.clear();
    unsigned char buffer[8192]{};
    while (true) {
        SQLLEN indicator = 0;
        const SQLRETURN rc = SQLGetData(stmt, col, SQL_C_BINARY, buffer, sizeof(buffer), &indicator);
        if (rc == SQL_NO_DATA) {
            return true;
        }
        if (indicator == SQL_NULL_DATA) {
            out.clear();
            return true;
        }
        if (!(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO)) {
            error = "SQLGetData binary failed: " + collect_diag(SQL_HANDLE_STMT, stmt);
            out.clear();
            return false;
        }

        SQLLEN bytes_read = 0;
        if (indicator == SQL_NO_TOTAL) {
            bytes_read = rc == SQL_SUCCESS_WITH_INFO ? static_cast<SQLLEN>(sizeof(buffer)) : 0;
        } else if (rc == SQL_SUCCESS_WITH_INFO && indicator > static_cast<SQLLEN>(sizeof(buffer))) {
            bytes_read = static_cast<SQLLEN>(sizeof(buffer));
        } else {
            bytes_read = std::min<SQLLEN>(indicator, static_cast<SQLLEN>(sizeof(buffer)));
        }
        if (bytes_read > 0) {
            out.insert(out.end(), buffer, buffer + bytes_read);
        }
        if (rc == SQL_SUCCESS) {
            return true;
        }
    }
}

std::string collect_diag(SQLSMALLINT handle_type, SQLHANDLE handle) {
    std::ostringstream oss;
    SQLSMALLINT rec = 1;
    while (true) {
        SQLWCHAR state[16] = {};
        SQLWCHAR message[1024] = {};
        SQLINTEGER native_error = 0;
        SQLSMALLINT text_len = 0;
        const SQLRETURN rc = SQLGetDiagRecW(handle_type, handle, rec, state, &native_error,
                                            message, 1024, &text_len);
        if (!(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO)) {
            break;
        }
        if (rec > 1) {
            oss << " | ";
        }
        oss << wide_to_utf8(state) << ":" << native_error << ":" << wide_to_utf8(message);
        ++rec;
    }
    return oss.str();
}

void disconnect(DbContext& db) {
    if (db.dbc != SQL_NULL_HDBC) {
        SQLDisconnect(db.dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, db.dbc);
        db.dbc = SQL_NULL_HDBC;
    }
    if (db.env != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_ENV, db.env);
        db.env = SQL_NULL_HENV;
    }
}

bool connect(const std::string& connection_string, DbContext& db, std::string& error, LogFn log) {
    if (connection_string.empty()) {
        error = "missing connection string";
        return false;
    }
    enable_odbc_connection_pooling_once(log);
    if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &db.env) != SQL_SUCCESS) {
        error = "SQLAllocHandle ENV failed";
        return false;
    }
    SQLSetEnvAttr(db.env, SQL_ATTR_ODBC_VERSION, reinterpret_cast<void*>(SQL_OV_ODBC3), 0);
    SQLSetEnvAttr(db.env, SQL_ATTR_CP_MATCH, reinterpret_cast<void*>(SQL_CP_STRICT_MATCH), 0);
    if (SQLAllocHandle(SQL_HANDLE_DBC, db.env, &db.dbc) != SQL_SUCCESS) {
        error = "SQLAllocHandle DBC failed";
        disconnect(db);
        return false;
    }
    SQLSetConnectAttr(db.dbc,
                      SQL_ATTR_LOGIN_TIMEOUT,
                      reinterpret_cast<SQLPOINTER>(kLoginTimeoutSeconds),
                      0);
    if (log) {
        log("db login timeout seconds=" + std::to_string(kLoginTimeoutSeconds) + "\n");
    }

    std::string cached_candidate;
    const auto candidates = prioritized_odbc_candidates(connection_string, cached_candidate);
    std::vector<std::string> failed_attempt_logs;

    for (const auto& candidate : candidates) {
        const auto driver_name = candidate_driver_name(candidate);
        const auto wide = utf8_to_wide(candidate);
        SQLWCHAR out_conn[2048] = {};
        SQLSMALLINT out_len = 0;
        const SQLRETURN rc = SQLDriverConnectW(
            db.dbc, nullptr,
            reinterpret_cast<SQLWCHAR*>(const_cast<wchar_t*>(wide.c_str())), SQL_NTS,
            out_conn, 2048, &out_len, SQL_DRIVER_NOPROMPT);
        if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) {
            remember_odbc_candidate(connection_string, candidate);
            if (log) {
                log(std::string("db connect ok driver=") + driver_name +
                    (candidate == cached_candidate ? " cached" : "") + "\n");
            }
            return true;
        }
        if (log) {
            failed_attempt_logs.push_back(
                "db connect failed driver=" + driver_name +
                (candidate == cached_candidate ? " cached" : "") +
                " diag=" + collect_diag(SQL_HANDLE_DBC, db.dbc) + "\n");
        }
    }

    error = "SQLDriverConnect failed: " + collect_diag(SQL_HANDLE_DBC, db.dbc);
    if (log) {
        for (const auto& entry : failed_attempt_logs) {
            log(entry);
        }
        log(error + "\n");
    }
    disconnect(db);
    return false;
}

bool exec_query(SQLHDBC dbc, const std::string& sql, SQLHSTMT& stmt, std::string& error) {
    stmt = SQL_NULL_HSTMT;
    if (SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt) != SQL_SUCCESS) {
        error = "SQLAllocHandle STMT failed";
        return false;
    }
    const auto wide = utf8_to_wide(sql);
    const SQLRETURN rc = SQLExecDirectW(stmt, reinterpret_cast<SQLWCHAR*>(const_cast<wchar_t*>(wide.c_str())), SQL_NTS);
    if (!(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO)) {
        error = "SQLExecDirect failed: " + collect_diag(SQL_HANDLE_STMT, stmt);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        stmt = SQL_NULL_HSTMT;
        return false;
    }
    return true;
}

using EmployeeNameMap = std::unordered_map<std::string, std::string>;

struct BarcodeEmployeeCache {
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<const EmployeeNameMap>> by_connection;
};

BarcodeEmployeeCache& barcode_employee_cache() {
    static BarcodeEmployeeCache cache;
    return cache;
}

bool load_barcode_employee_names(SQLHDBC dbc,
                                 const std::string& connection_string,
                                 std::shared_ptr<const EmployeeNameMap>& names,
                                 bool& cache_hit,
                                 std::string& error,
                                 LogFn log) {
    auto& cache = barcode_employee_cache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    const auto cached = cache.by_connection.find(connection_string);
    if (cached != cache.by_connection.end()) {
        names = cached->second;
        cache_hit = true;
        return true;
    }

    cache_hit = false;
    const std::string sql =
        "SELECT EMPLOYEE_ID,NAME FROM JC_EMPLOYEE_PROPERTY WITH (NOLOCK)";
    if (log) log("exec employee dictionary sql: " + sql + "\n");

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(dbc, sql, stmt, error)) return false;

    auto loaded = std::make_shared<EmployeeNameMap>();
    while (SQLFetch(stmt) == SQL_SUCCESS) {
        const std::string code = trim(fetch_column(stmt, 1));
        const std::string name = trim(fetch_column(stmt, 2));
        if (!code.empty() && !name.empty()) {
            (*loaded)[code] = name;
        }
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);

    names = loaded;
    cache.by_connection[connection_string] = std::move(loaded);
    error.clear();
    return true;
}

void add_eq(std::ostringstream& sql, const char* col, const std::string& value) {
    if (!trim(value).empty()) {
        sql << " AND " << col << "='" << sql_escape(trim(value)) << "'";
    }
}

void add_like(std::ostringstream& sql, const char* col, const std::string& value) {
    if (!trim(value).empty()) {
        sql << " AND " << col << " LIKE '%" << sql_escape(trim(value)) << "%'";
    }
}

void add_in(std::ostringstream& sql, const char* col, const std::vector<std::string>& values) {
    const std::string list = sql_string_list(values);
    if (!list.empty()) {
        sql << " AND " << col << " IN (" << list << ")";
    }
}

void add_report_status(std::ostringstream& sql, const std::string& value) {
    const auto status = trim(value);
    if (status.empty() || status == "全部") {
        return;
    }
    if (status == "已审核") {
        sql << " AND r.CONF='T'";
        return;
    }
    if (status == "未审核") {
        sql << " AND isnull(r.CONF,'')<>'T' AND isnull(r.CONF,'')<>'S'";
        return;
    }
    if (status == "已发送") {
        sql << " AND r.CHK_FLAG='T'";
        return;
    }
    if (status == "未发送") {
        sql << " AND isnull(r.CHK_FLAG,'')<>'T'";
    }
}

void add_lis_patient_filters(std::ostringstream& sql, const QueryFilters& filters, const char* report_alias) {
    if (!trim(filters.patient_name).empty()) {
        sql << " AND " << report_alias << ".NAME LIKE '%" << sql_escape(trim(filters.patient_name)) << "%'";
    }
    if (!trim(filters.patient_no).empty()) {
        sql << " AND " << report_alias << ".REG_NO='" << sql_escape(trim(filters.patient_no)) << "'";
    }
    const std::string patient_no_list = sql_string_list(filters.patient_nos);
    if (!patient_no_list.empty()) {
        sql << " AND " << report_alias << ".REG_NO IN (" << patient_no_list << ")";
    }
    if (!trim(filters.patient_phone).empty()) {
        sql << " AND LTRIM(RTRIM(isnull(" << report_alias << ".PAT_PHONE,'')))='"
            << sql_escape(trim(filters.patient_phone)) << "'";
    }
    if (!trim(filters.start_date).empty()) {
        sql << " AND " << report_alias << ".CHK_DATE >= '" << sql_escape(trim(filters.start_date)) << "'";
    }
    if (!trim(filters.end_date).empty()) {
        sql << " AND " << report_alias << ".CHK_DATE < DATEADD(day,1,'" << sql_escape(trim(filters.end_date)) << "')";
    }
}

void add_blood_apply_status(std::ostringstream& sql, const std::string& value) {
    const auto status = trim(value);
    if (status.empty() || status == "全部") {
        return;
    }

    sql << " AND LTRIM(RTRIM(a.ApplyForm_Statue))='" << sql_escape(status) << "'";
}

std::string barcode_machine_status_condition(const std::string& value) {
    const auto status = trim(value);
    if (status.empty() || status == "全部") {
        return {};
    }
    if (status == "已签收未上机") {
        return "isnull(rs.REPORT_SENT,0)=0"
               " AND isnull(rs.REPORT_REVIEWED,0)=0"
               " AND isnull(rs.HAS_REPORT,0)=0"
               " AND isnull(b.OPER_STATE,0)=0";
    } else if (status == "已上机未审核") {
        return "isnull(rs.REPORT_SENT,0)=0"
               " AND isnull(rs.REPORT_REVIEWED,0)=0"
               " AND (isnull(rs.HAS_REPORT,0)=1 OR isnull(b.OPER_STATE,0)>=1)";
    } else if (status == "已审核未发送") {
        return "isnull(rs.REPORT_SENT,0)=0"
               " AND isnull(rs.REPORT_REVIEWED,0)=1";
    } else if (status == "发送完成") {
        return "isnull(rs.REPORT_SENT,0)=1";
    }
    return {};
}

void add_barcode_machine_status(std::ostringstream& sql, const std::string& value) {
    const auto condition = barcode_machine_status_condition(value);
    if (!condition.empty()) {
        sql << " AND " << condition;
    }
}

void add_barcode_machine_statuses(std::ostringstream& sql, const std::vector<std::string>& values) {
    std::vector<std::string> conditions;
    for (const auto& value : values) {
        auto condition = barcode_machine_status_condition(value);
        if (!condition.empty()) conditions.push_back(std::move(condition));
    }
    if (conditions.empty()) return;
    sql << " AND (";
    for (size_t i = 0; i < conditions.size(); ++i) {
        if (i > 0) sql << " OR ";
        sql << "(" << conditions[i] << ")";
    }
    sql << ")";
}

const char* barcode_machine_status_sql() {
    return "CASE"
           " WHEN isnull(rs.REPORT_SENT,0)=1 THEN '发送完成'"
           " WHEN isnull(rs.REPORT_REVIEWED,0)=1 THEN '已审核未发送'"
           " WHEN isnull(rs.HAS_REPORT,0)=1 OR isnull(b.OPER_STATE,0)>=1 THEN '已上机未审核'"
           " WHEN isnull(b.OPER_STATE,0)=0 THEN '已签收未上机'"
           " ELSE '' END";
}

void fill_if_empty(std::string& target, const std::string& value) {
    if (trim(target).empty() && !trim(value).empty()) {
        target = value;
    }
}

bool exec_optional_query(SQLHDBC dbc, const std::string& sql, SQLHSTMT& stmt, LogFn log) {
    std::string ignored_error;
    if (log) log("exec optional sql: " + sql + "\n");
    return exec_query(dbc, sql, stmt, ignored_error);
}

std::string specimen_order_key(const SpecimenOrderRow& row) {
    return trim(row.barcode) + "\n" + trim(row.room_code) + "\n" + trim(row.order_text) + "\n" +
           trim(row.sample_name) + "\n" + trim(row.fee) + "\n" +
           trim(row.request_time);
}

void add_unique_order(std::vector<SpecimenOrderRow>& rows, const SpecimenOrderRow& row) {
    const auto key = specimen_order_key(row);
    for (const auto& existing : rows) {
        if (specimen_order_key(existing) == key) {
            return;
        }
    }
    rows.push_back(row);
}

bool should_add_supplemental_orders(const SpecimenBarcodeResult& result) {
    return !result.has_barcode_rows || result.orders.empty();
}

#endif

}  // namespace

long long sql_datetime_diff_seconds(const std::string& start, const std::string& end) {
    return seconds_between_sql_datetimes(start, end);
}

std::string format_duration_seconds_zh(long long total_seconds) {
    if (total_seconds < 0) return "";
    const long long hours = total_seconds / 3600;
    const long long minutes = (total_seconds % 3600) / 60;
    const long long seconds = total_seconds % 60;
    if (hours > 0) {
        return std::to_string(hours) + "小时" + std::to_string(minutes) +
            "分钟" + std::to_string(seconds) + "秒";
    }
    if (minutes > 0) {
        return std::to_string(minutes) + "分钟" + std::to_string(seconds) + "秒";
    }
    return std::to_string(seconds) + "秒";
}

std::string employee_display_name(const std::string& employee_code,
                                  const std::string& dictionary_name) {
    const std::string name = trim(dictionary_name);
    return name.empty() ? trim(employee_code) : name;
}

bool query_rooms(const std::string& connection_string, std::vector<RoomOption>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)connection_string;
    (void)log;
    error = "query_rooms is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(connection_string, db, error, log)) {
        return false;
    }

    const std::string sql =
        "SELECT CAST(ROOM_CODE AS varchar(20)), isnull(RTRIM(ROOM_NAME),'')"
        " FROM LS_AS_ROOM WHERE DELETE_BIT=0 ORDER BY ROOM_CODE";
    if (log) {
        log("exec sql: " + sql + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql, stmt, error)) {
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        RoomOption row;
        row.room_code = fetch_column(stmt, 1);
        row.room_name = fetch_column(stmt, 2);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_barcode_rooms(const std::string& connection_string, std::vector<RoomOption>& rows,
                         std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)connection_string;
    (void)log;
    error = "query_barcode_rooms is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(connection_string, db, error, log)) {
        return false;
    }

    const std::string sql =
        "SELECT CAST(ROOM_CODE AS varchar(20)),isnull(RTRIM(ROOM_NAME),''),"
        "isnull(LTRIM(RTRIM(CONVERT(varchar(20),Dept_Code))),'')"
        " FROM LS_AS_ROOM WHERE DELETE_BIT=0"
        " AND CONVERT(varchar(20),Dept_Code) IN ('102','401')"
        " ORDER BY Dept_Code,ROOM_CODE";
    if (log) {
        log("exec sql: " + sql + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql, stmt, error)) {
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        RoomOption row;
        row.room_code = fetch_column(stmt, 1);
        row.room_name = fetch_column(stmt, 2);
        row.dept_code = fetch_column(stmt, 3);
        rows.push_back(std::move(row));
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_report_machine_picker_rooms(const std::string& connection_string, std::vector<RoomOption>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)connection_string;
    (void)log;
    error = "query_report_machine_picker_rooms is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(connection_string, db, error, log)) {
        return false;
    }

    const std::string sql =
        "SELECT CAST(ROOM_CODE AS varchar(20)), isnull(RTRIM(ROOM_NAME),'')"
        " FROM LS_AS_ROOM WHERE DELETE_BIT=0 AND Dept_Code IN (102,401)"
        " ORDER BY ROOM_CODE";
    if (log) {
        log("exec sql: " + sql + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql, stmt, error)) {
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        RoomOption row;
        row.room_code = fetch_column(stmt, 1);
        row.room_name = fetch_column(stmt, 2);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_patient_types(const std::string& connection_string, std::vector<PatientTypeOption>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)connection_string;
    (void)log;
    error = "query_patient_types is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(connection_string, db, error, log)) {
        return false;
    }

    const std::string sql =
        "SELECT isnull(TYPE,''), isnull(RTRIM(TYPE_NAME),'')"
        " FROM LS_AS_PATTYPE WHERE DELETE_BIT=0 ORDER BY TYPE";
    if (log) {
        log("exec sql: " + sql + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql, stmt, error)) {
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        PatientTypeOption row;
        row.type_code = fetch_column(stmt, 1);
        row.type_name = fetch_column(stmt, 2);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_machines(const std::string& connection_string, const std::string& room_code, std::vector<MachineOption>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)connection_string;
    (void)room_code;
    (void)log;
    error = "query_machines is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(connection_string, db, error, log)) {
        return false;
    }

    std::ostringstream sql;
    sql << "SELECT CAST(ROOM_CODE AS varchar(20)), CAST(MACH_CODE AS varchar(20)),"
        << " isnull(RTRIM(MACH_NAME),''), isnull(RTRIM(PY_CODE),'')"
        << " FROM LS_AS_MACHINE WHERE DELETE_BIT=0 AND isnull(RTRIM(RUL),'')='启用'";
    add_eq(sql, "ROOM_CODE", room_code);
    sql << " ORDER BY MACH_CODE";
    if (log) {
        log("exec sql: " + sql.str() + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        MachineOption row;
        row.room_code = fetch_column(stmt, 1);
        row.mach_code = fetch_column(stmt, 2);
        row.mach_name = fetch_column(stmt, 3);
        row.py_code = fetch_column(stmt, 4);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_report_machine_picker_machines(const std::string& connection_string, const std::string& room_code, std::vector<MachineOption>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)connection_string;
    (void)room_code;
    (void)log;
    error = "query_report_machine_picker_machines is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(connection_string, db, error, log)) {
        return false;
    }

    std::ostringstream sql;
    sql << "SELECT CAST(m.ROOM_CODE AS varchar(20)), CAST(m.MACH_CODE AS varchar(20)),"
        << " isnull(RTRIM(m.MACH_NAME),''), isnull(RTRIM(m.PY_CODE),''),"
        << " isnull(CAST(main_group.GROUP_CODE AS varchar(20)),''),"
        << " isnull(RTRIM(item.ITEM_NAME),''),"
        << " isnull(RTRIM(main_group.SAMP_CODE),''),"
        << " isnull(RTRIM(samp.SAMP_NAME),'')"
        << " FROM LS_AS_MACHINE m"
        << " OUTER APPLY (SELECT TOP 1 g.GROUP_CODE, g.SAMP_CODE"
        << " FROM LS_AS_GROUP g"
        << " WHERE g.DELETE_BIT=0 AND isnull(RTRIM(g.REP_STYLE),'')='M'"
        << " AND g.MACH_CODE=m.MACH_CODE"
        << " ORDER BY isnull(g.orderby,2147483647), g.GROUP_CODE) main_group"
        << " LEFT JOIN LS_CODE_ITEM item ON RTRIM(item.ITEM_CODE)=CAST(main_group.GROUP_CODE AS varchar(10))"
        << " LEFT JOIN LS_AS_SAMPLE samp ON CAST(samp.SAMP_CODE AS varchar(4))=RTRIM(main_group.SAMP_CODE)"
        << " AND samp.DELETE_BIT=0"
        << " WHERE m.DELETE_BIT=0 AND isnull(RTRIM(m.RUL),'')='启用'"
        << " AND EXISTS (SELECT 1 FROM LS_AS_ROOM r"
        << " WHERE r.DELETE_BIT=0 AND r.ROOM_CODE=m.ROOM_CODE"
        << " AND r.Dept_Code IN (102,401))";
    add_eq(sql, "m.ROOM_CODE", room_code);
    sql << " ORDER BY m.ROOM_CODE, m.MACH_CODE";
    if (log) {
        log("exec sql: " + sql.str() + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        MachineOption row;
        row.room_code = fetch_column(stmt, 1);
        row.mach_code = fetch_column(stmt, 2);
        row.mach_name = fetch_column(stmt, 3);
        row.py_code = fetch_column(stmt, 4);
        row.group_code = fetch_column(stmt, 5);
        row.group_name = fetch_column(stmt, 6);
        row.sample_code = fetch_column(stmt, 7);
        row.sample_name = fetch_column(stmt, 8);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_reports(const QueryFilters& filters, std::vector<ReportRow>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)filters;
    (void)log;
    error = "query_reports is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(filters.connection_string, db, error, log)) {
        return false;
    }

    std::ostringstream sql;
    sql << "SELECT ";
    if (filters.limit > 0) {
        sql << "TOP " << filters.limit << " ";
    }
    sql << "CAST(r.ID AS varchar(20)),CAST(r.REP_NO AS varchar(20)),"
        << " isnull(r.OPER_NO,''),isnull(r.NAME,''),isnull(r.TXM_NO,''),"
        << " isnull(CONVERT(varchar(19),r.REP_DATE,120),''),isnull(RTRIM(sx.SEX_NAME),''),isnull(r.AGE,''),"
        << " isnull(r.BED_CODE,''),isnull(RTRIM(p.TYPE_NAME),''),isnull(RTRIM(emp_oper.NAME),''),"
        << " isnull(RTRIM(emp_rep.NAME),''),isnull(r.GROUP_NO,''),"
        << " isnull(r.CONF,''),isnull(r.CHK_FLAG,''),cast(isnull(r.ZYMZ_PRINT,0) as varchar(20)),"
        << " cast(isnull(r.ZZJ_PRINT,0) as varchar(20)),isnull(r.REG_NO,''),"
        << " isnull(LTRIM(RTRIM(bar.DEPT_NAME)),''),"
        << (filters.skip_order_text ? "''" : "isnull(ord.ORDER_TEXT,'')") << ","
        << " isnull(LTRIM(RTRIM(samp.SAMP_NAME)),''),isnull(r.NOTE,''),"
        << " isnull(cast(r.OPER_CODE as varchar(20)),''),isnull(CONVERT(varchar(19),bar.IN_DATE,120),''),"
        << " isnull(CONVERT(varchar(19),r.CHK_DATE,120),''),isnull(CONVERT(varchar(19),r.REP_TIME,120),''),"
        << " isnull(cast(r.FY as varchar(32)),''),"
        << " isnull(NULLIF(LTRIM(RTRIM(emp_dean.NAME)),''),isnull(cast(r.DEAN_OPER as varchar(20)),'')),"
        << " isnull(LTRIM(RTRIM(emp_req.NAME)),''),"
        << " isnull(r.DIAG_NAME,''),isnull(r.CREATE_TIME,''),isnull(r.PAT_PHONE,''),"
        << " isnull(cast(r.assaypat_type as varchar(20)),''),"
        << " isnull(cast(bar.JZ_FLAG as varchar(20)),'') ,"
        << " isnull(cast(r.MACH_CODE as varchar(20)),''),"
        << " isnull(nullif(LTRIM(RTRIM(mach.MACH_NAME)),''),isnull(cast(r.MACH_CODE as varchar(20)),'')),"
        << " isnull(cast(r.ROOM_CODE as varchar(20)),'')"
        << " FROM LS_AS_REPORT r"
        << " LEFT JOIN LS_AS_PATTYPE p ON r.TYPE = p.TYPE AND p.DELETE_BIT=0"
        << " LEFT JOIN LS_AS_SEX sx ON sx.SEX_CODE = r.SEX"
        << " LEFT JOIN LS_AS_SAMPLE samp ON samp.SAMP_CODE=r.SAMP_CODE AND samp.DELETE_BIT=0"
        << " LEFT JOIN LS_AS_MACHINE mach ON r.MACH_CODE=mach.MACH_CODE AND mach.DELETE_BIT=0"
        << " LEFT JOIN JC_EMPLOYEE_PROPERTY emp_oper ON emp_oper.EMPLOYEE_ID = r.OPER_CODE"
        << " LEFT JOIN JC_EMPLOYEE_PROPERTY emp_rep ON emp_rep.EMPLOYEE_ID = r.REP_OPER"
        << " LEFT JOIN JC_EMPLOYEE_PROPERTY emp_dean ON emp_dean.EMPLOYEE_ID = r.DEAN_OPER"
        << " LEFT JOIN JC_EMPLOYEE_PROPERTY emp_req ON emp_req.EMPLOYEE_ID = r.REQ_DR"
        << " OUTER APPLY (SELECT TOP 1 b.DEPT_NAME,b.IN_DATE,b.JZ_FLAG FROM LS_AS_BARCODE b WITH (NOLOCK)"
        << " WHERE isnull(b.DELETE_BIT,0)=0 AND b.BARCODE=r.TXM_NO ORDER BY b.ID DESC) bar";
    if (!filters.skip_order_text) {
        sql << " OUTER APPLY (SELECT STUFF(("
            << " SELECT '/' + LTRIM(RTRIM(b2.ORDER_TEXT))"
            << " FROM LS_AS_BARCODE b2 WITH (NOLOCK)"
            << " WHERE isnull(b2.DELETE_BIT,0)=0 AND b2.BARCODE=r.TXM_NO"
            << " AND NULLIF(LTRIM(RTRIM(b2.ORDER_TEXT)),'') IS NOT NULL"
            << " ORDER BY b2.ID"
            << " FOR XML PATH(''),TYPE).value('.','varchar(max)'),1,1,'') AS ORDER_TEXT) ord";
    }
    sql << " WHERE r.DELETE_BIT=0";

    add_eq(sql, "r.REG_NO", filters.patient_id);
    add_eq(sql, "r.TXM_NO", filters.barcode);
    add_like(sql, "r.NAME", filters.patient_name);
    if (!trim(filters.patient_no).empty()) {
        sql << " AND EXISTS (SELECT 1 FROM LS_AS_BARCODE b"
            << " WHERE isnull(b.DELETE_BIT,0)=0"
            << " AND b.REG_NO='" << sql_escape(trim(filters.patient_no)) << "'"
            << " AND b.BARCODE = r.TXM_NO)";
    }
    add_eq(sql, "r.OPER_NO", filters.oper_no);
    add_eq(sql, "r.ROOM_CODE", filters.room_code);
    add_eq(sql, "r.TYPE", filters.patient_type);
    add_report_status(sql, filters.report_status);
    add_eq(sql, "r.MACH_CODE", filters.mach_code);
    add_eq(sql, "r.GROUP_CODE", filters.group_code);
    if (!trim(filters.start_date).empty()) {
        sql << " AND r.CHK_DATE >= '" << sql_escape(trim(filters.start_date)) << "'";
    }
    if (!trim(filters.end_date).empty()) {
        sql << " AND r.CHK_DATE < DATEADD(day,1,'" << sql_escape(trim(filters.end_date)) << "')";
    }
    if (!trim(filters.item_code).empty()) {
        sql << " AND EXISTS (SELECT 1 FROM LS_AS_REPENTRY e WHERE e.REP_NO=r.REP_NO"
            << " AND e.DELETE_BIT=0 AND e.ITEM_CODE=" << sql_escape(trim(filters.item_code)) << ")";
    }
    sql << " ORDER BY r.CHK_DATE DESC, r.REP_NO DESC";

    if (log) {
        log("exec sql: " + sql.str() + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        ReportRow row;
        row.id = fetch_column(stmt, 1);
        row.rep_no = fetch_column(stmt, 2);
        row.oper_no = fetch_column(stmt, 3);
        row.name = fetch_column(stmt, 4);
        row.txm_no = fetch_column(stmt, 5);
        row.chk_date = fetch_column(stmt, 6);
        row.sex = fetch_column(stmt, 7);
        row.age = fetch_column(stmt, 8);
        row.bed_code = fetch_column(stmt, 9);
        row.patient_type = fetch_column(stmt, 10);
        row.requester = fetch_column(stmt, 11);
        row.reviewer = fetch_column(stmt, 12);
        row.group_name = fetch_column(stmt, 13);
        row.conf = fetch_column(stmt, 14);
        row.chk_flag = fetch_column(stmt, 15);
        row.zymz_print = fetch_column(stmt, 16);
        row.zzj_print = fetch_column(stmt, 17);
        row.reg_no = fetch_column(stmt, 18);
        row.dept_name = fetch_column(stmt, 19);
        row.order_text = fetch_column(stmt, 20);
        row.sample_name = fetch_column(stmt, 21);
        row.note = fetch_column(stmt, 22);
        row.oper_code = fetch_column(stmt, 23);
        row.collection_time = fetch_column(stmt, 24);
        row.inspect_date = fetch_column(stmt, 25);
        row.rep_time = fetch_column(stmt, 26);
        row.fee = fetch_column(stmt, 27);
        row.dean_oper = fetch_column(stmt, 28);
        row.req_doctor = fetch_column(stmt, 29);
        row.diag_name = fetch_column(stmt, 30);
        row.create_time = fetch_column(stmt, 31);
        row.patient_phone = fetch_column(stmt, 32);
        row.report_type = fetch_column(stmt, 33);
        row.barcode_jz_flag = fetch_column(stmt, 34);
        row.mach_code = fetch_column(stmt, 35);
        row.mach_name = fetch_column(stmt, 36);
        row.room_code = fetch_column(stmt, 37);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_blood_lis_reports(const QueryFilters& filters, std::vector<ReportRow>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)filters;
    (void)log;
    error = "query_blood_lis_reports is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(filters.connection_string, db, error, log)) {
        return false;
    }

    std::ostringstream sql;
    sql << "SELECT ";
    if (filters.limit > 0) {
        sql << "TOP " << filters.limit << " ";
    }
    sql << "CAST(r.REP_NO AS varchar(20)),"
        << " isnull(r.OPER_NO,''),"
        << " isnull(CONVERT(varchar(19),r.CHK_DATE,120),''),"
        << " isnull(r.GROUP_NO,''),"
        << " isnull(r.TXM_NO,''),"
        << " isnull(RTRIM(emp_oper.NAME),''),"
        << " isnull(RTRIM(emp_rep.NAME),''),"
        << " isnull(r.AGE,''),"
        << " isnull(RTRIM(sx.SEX_NAME),''),"
        << " isnull(cast(r.ROOM_CODE as varchar(20)),''),"
        << " isnull(cast(r.MACH_CODE as varchar(20)),''),"
        << " isnull(LTRIM(RTRIM(r.PAT_PHONE)),'')"
        << " FROM LS_AS_REPORT r WITH (NOLOCK)"
        << " LEFT JOIN LS_AS_SEX sx WITH (NOLOCK) ON sx.SEX_CODE=r.SEX"
        << " LEFT JOIN JC_EMPLOYEE_PROPERTY emp_oper WITH (NOLOCK) ON emp_oper.EMPLOYEE_ID=r.OPER_CODE"
        << " LEFT JOIN JC_EMPLOYEE_PROPERTY emp_rep WITH (NOLOCK) ON emp_rep.EMPLOYEE_ID=r.REP_OPER"
        << " WHERE r.DELETE_BIT=0";

    add_like(sql, "r.NAME", filters.patient_name);
    if (!trim(filters.patient_no).empty()) {
        sql << " AND r.REG_NO='" << sql_escape(trim(filters.patient_no)) << "'";
    }
    const std::string patient_no_list = sql_string_list(filters.patient_nos);
    if (!patient_no_list.empty()) {
        sql << " AND r.REG_NO IN (" << patient_no_list << ")";
    }
    if (!trim(filters.patient_phone).empty()) {
        sql << " AND LTRIM(RTRIM(isnull(r.PAT_PHONE,'')))='" << sql_escape(trim(filters.patient_phone)) << "'";
    }
    if (!trim(filters.start_date).empty()) {
        sql << " AND r.CHK_DATE >= '" << sql_escape(trim(filters.start_date)) << "'";
    }
    if (!trim(filters.end_date).empty()) {
        sql << " AND r.CHK_DATE < DATEADD(day,1,'" << sql_escape(trim(filters.end_date)) << "')";
    }
    sql << sql_room_machine_exclude_filter(filters.lis_blood_exclude_machines, "r");
    sql << " ORDER BY r.CHK_DATE DESC,r.REP_NO DESC";

    if (log) {
        log("exec sql: " + sql.str() + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        ReportRow row;
        row.rep_no = fetch_column(stmt, 1);
        row.oper_no = fetch_column(stmt, 2);
        row.chk_date = fetch_column(stmt, 3);
        row.group_name = fetch_column(stmt, 4);
        row.txm_no = fetch_column(stmt, 5);
        row.requester = fetch_column(stmt, 6);
        row.reviewer = fetch_column(stmt, 7);
        row.age = fetch_column(stmt, 8);
        row.sex = fetch_column(stmt, 9);
        row.room_code = fetch_column(stmt, 10);
        row.mach_code = fetch_column(stmt, 11);
        row.patient_phone = fetch_column(stmt, 12);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_latest_report_phone_by_reg_no(const std::string& connection_string, const std::string& reg_no,
                                         std::string& phone, std::string& error, LogFn log) {
    phone.clear();
#ifndef _WIN32
    (void)connection_string;
    (void)reg_no;
    (void)log;
    error = "query_latest_report_phone_by_reg_no is only available on Windows";
    return false;
#else
    const std::string trimmed_reg_no = trim(reg_no);
    if (trimmed_reg_no.empty()) {
        error.clear();
        return true;
    }

    DbContext db;
    if (!connect(connection_string, db, error, log)) {
        return false;
    }

    std::ostringstream sql;
    sql << "SELECT TOP 1 LTRIM(RTRIM(isnull(PAT_PHONE,'')))"
        << " FROM LS_AS_REPORT WITH (NOLOCK)"
        << " WHERE DELETE_BIT=0"
        << " AND REG_NO='" << sql_escape(trimmed_reg_no) << "'"
        << " AND NULLIF(LTRIM(RTRIM(isnull(PAT_PHONE,''))),'') IS NOT NULL"
        << " ORDER BY CHK_DATE DESC,REP_TIME DESC,REP_NO DESC";

    if (log) {
        log("exec sql: " + sql.str() + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        return false;
    }

    if (SQLFetch(stmt) == SQL_SUCCESS) {
        phone = fetch_column(stmt, 1);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_inpatient_nos_by_social_no_from_reg_no(const std::string& connection_string, const std::string& reg_no,
                                                  std::vector<std::string>& inpatient_nos, std::string& error,
                                                  LogFn log) {
    inpatient_nos.clear();
#ifndef _WIN32
    (void)connection_string;
    (void)reg_no;
    (void)log;
    error = "query_inpatient_nos_by_social_no_from_reg_no is only available on Windows";
    return false;
#else
    const std::string trimmed_reg_no = trim(reg_no);
    if (trimmed_reg_no.empty()) {
        error.clear();
        return true;
    }

    DbContext db;
    if (!connect(connection_string, db, error, log)) {
        return false;
    }

    std::ostringstream sql;
    sql << "SELECT DISTINCT LTRIM(RTRIM(isnull(z2.INPATIENT_NO,'')))"
        << " FROM ZY_INPATIENT z1 WITH (NOLOCK)"
        << " INNER JOIN ZY_INPATIENT z2 WITH (NOLOCK)"
        << " ON LTRIM(RTRIM(isnull(z2.SOCIAL_NO,'')))=LTRIM(RTRIM(isnull(z1.SOCIAL_NO,'')))"
        << " WHERE LTRIM(RTRIM(isnull(z1.INPATIENT_NO,'')))='" << sql_escape(trimmed_reg_no) << "'"
        << " AND NULLIF(LTRIM(RTRIM(isnull(z1.SOCIAL_NO,''))),'') IS NOT NULL"
        << " AND NULLIF(LTRIM(RTRIM(isnull(z2.INPATIENT_NO,''))),'') IS NOT NULL"
        << " ORDER BY LTRIM(RTRIM(isnull(z2.INPATIENT_NO,'')))";

    if (log) {
        log("exec sql: " + sql.str() + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        const std::string inpatient_no = trim(fetch_column(stmt, 1));
        if (!inpatient_no.empty()) {
            inpatient_nos.push_back(inpatient_no);
        }
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_results(const std::string& connection_string, const std::string& rep_no, std::vector<ResultRow>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)connection_string;
    (void)rep_no;
    (void)log;
    error = "query_results is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(connection_string, db, error, log)) {
        return false;
    }

    std::ostringstream sql;
    sql << "SELECT isnull(lm.GROUP_NAME,''),isnull(i.ITEM_NAME,e.ITEM_NAME),isnull(e.RESULT,''),isnull(e.UPBOUND,''),"
        << " isnull(e.DOWNBOUND,''),isnull(RTRIM(i.UNIT),''),isnull(i.ENG_NAME,''),"
        << " isnull(e.NORMAL,''),CAST(e.ITEM_CODE AS varchar(20)),"
        << " isnull(CAST(e.NORMAL_WJ AS varchar(20)),''),"
        << " isnull(scope.UPBOUND1,''),isnull(scope.DNBOUND1,'')"
        << " FROM LS_AS_REPENTRY e"
        << " LEFT JOIN LS_AS_ITEM i ON e.ITEM_CODE = i.ITEM_CODE AND i.DELETE_BIT=0"
        << " OUTER APPLY ("
        << " SELECT TOP 1 m.GROUP_NAME"
        << " FROM LS_AS_LABMATCH m"
        << " WHERE m.GROUP_CODE=e.GROUP_CODE"
        << " AND NULLIF(LTRIM(RTRIM(isnull(m.GROUP_NAME,''))),'') IS NOT NULL"
        << " ORDER BY CASE WHEN isnull(m.DELETE_BIT,0)=0 AND isnull(m.USE_FLAG,0)=0 THEN 0 ELSE 1 END,m.ID"
        << " ) lm"
        << " OUTER APPLY ("
        << " SELECT TOP 1 LTRIM(RTRIM(isnull(sx.SEX_NAME,''))) AS SEX_NAME"
        << " FROM LS_AS_REPORT rr WITH (NOLOCK)"
        << " LEFT JOIN LS_AS_SEX sx WITH (NOLOCK) ON sx.SEX_CODE=rr.SEX"
        << " WHERE rr.REP_NO=e.REP_NO ORDER BY rr.ID"
        << " ) report_sex"
        << " OUTER APPLY ("
        << " SELECT TOP 1 s.UPBOUND1,s.DNBOUND1"
        << " FROM LS_AS_DEF_ITEMSCOPE s WITH (NOLOCK)"
        << " WHERE s.ITEM_CODE=e.ITEM_CODE"
        << " AND (LTRIM(RTRIM(isnull(s.SEX,'')))='通用'"
        << " OR (NULLIF(report_sex.SEX_NAME,'') IS NOT NULL"
        << " AND LTRIM(RTRIM(isnull(s.SEX,'')))=report_sex.SEX_NAME))"
        << " ORDER BY CASE"
        << " WHEN NULLIF(report_sex.SEX_NAME,'') IS NOT NULL"
        << " AND LTRIM(RTRIM(isnull(s.SEX,'')))=report_sex.SEX_NAME THEN 0"
        << " WHEN LTRIM(RTRIM(isnull(s.SEX,'')))='通用' THEN 1"
        << " ELSE 2 END,"
        << " CASE WHEN isnull(s.DEF_FLAG,'')='1' THEN 0 ELSE 1 END,s.ID"
        << " ) scope"
        << " WHERE e.DELETE_BIT=0 AND e.REP_NO=" << sql_escape(rep_no)
        << " ORDER BY e.GROUP_CODE ASC,e.ITEM_CODE ASC,e.ID ASC";

    if (log) {
        log("exec sql: " + sql.str() + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        ResultRow row;
        row.group_name = fetch_column(stmt, 1);
        row.item_name = fetch_column(stmt, 2);
        row.result = fetch_column(stmt, 3);
        row.downbound = fetch_column(stmt, 4);
        row.upbound = fetch_column(stmt, 5);
        row.unit = fetch_column(stmt, 6);
        row.item_eng = fetch_column(stmt, 7);
        row.normal = fetch_column(stmt, 8);
        row.item_code = fetch_column(stmt, 9);
        row.normal_wj = fetch_column(stmt, 10);
        row.critical_low_bound = fetch_column(stmt, 11);
        row.critical_high_bound = fetch_column(stmt, 12);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_quality_control_lis_results(const QualityControlLisQuery& query, std::vector<QualityControlLisRow>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)query;
    (void)log;
    error = "query_quality_control_lis_results is only available on Windows";
    return false;
#else
    if (trim(query.mach_code).empty()) {
        error = "missing quality control machine code";
        return false;
    }
    if (!query.sample_nos.empty()) {
        // batch query: all sample numbers share one connection
    } else if (trim(query.sample_no).empty()) {
        error = "missing quality control sample number";
        return false;
    }

    // Collect unique sample numbers for SQL IN clause
    std::vector<std::string> all_samples;
    if (!query.sample_nos.empty()) {
        all_samples = query.sample_nos;
    } else {
        all_samples.push_back(trim(query.sample_no));
    }

    DbContext db;
    if (!connect(query.connection_string, db, error, log)) {
        return false;
    }

    // Preload dictionary lookups to avoid SQL JOINs
    std::map<std::string, std::string> item_names, item_engs, item_units;
    std::map<std::string, std::string> mach_names, employee_names;
    {
        auto load_map = [&](const std::string& map_sql, std::map<std::string, std::string>& out) {
            if (log) log("exec sql: " + map_sql + "\n");
            SQLHSTMT ms = SQL_NULL_HSTMT;
            if (!exec_query(db.dbc, map_sql, ms, error)) return false;
            while (SQLFetch(ms) == SQL_SUCCESS) {
                const std::string k = trim(fetch_column(ms, 1));
                const std::string v = trim(fetch_column(ms, 2));
                if (!k.empty() && !v.empty()) out[k] = v;
            }
            SQLFreeHandle(SQL_HANDLE_STMT, ms);
            return true;
        };
        if (!load_map(
                "SELECT isnull(LTRIM(RTRIM(CONVERT(varchar(20),MACH_CODE))),''),"
                " isnull(LTRIM(RTRIM(MACH_NAME)),'')"
                " FROM LS_AS_MACHINE WITH (NOLOCK)"
                " WHERE isnull(DELETE_BIT,0)=0",
                mach_names) ||
            !load_map(
                "SELECT isnull(LTRIM(RTRIM(CONVERT(varchar(20),ITEM_CODE))),''),"
                " isnull(LTRIM(RTRIM(ITEM_NAME)),'')"
                " FROM LS_AS_ITEM WITH (NOLOCK)"
                " WHERE isnull(DELETE_BIT,0)=0",
                item_names) ||
            !load_map(
                "SELECT isnull(LTRIM(RTRIM(CONVERT(varchar(20),ITEM_CODE))),''),"
                " isnull(LTRIM(RTRIM(ENG_NAME)),'')"
                " FROM LS_AS_ITEM WITH (NOLOCK)"
                " WHERE isnull(DELETE_BIT,0)=0",
                item_engs) ||
            !load_map(
                "SELECT isnull(LTRIM(RTRIM(CONVERT(varchar(20),ITEM_CODE))),''),"
                " isnull(RTRIM(UNIT),'')"
                " FROM LS_AS_ITEM WITH (NOLOCK)"
                " WHERE isnull(DELETE_BIT,0)=0",
                item_units) ||
            !load_map(
                "SELECT isnull(LTRIM(RTRIM(CONVERT(varchar(20),EMPLOYEE_ID))),''),"
                " isnull(LTRIM(RTRIM(NAME)),'')"
                " FROM JC_EMPLOYEE_PROPERTY WITH (NOLOCK)",
                employee_names)) {
            return false;
        }
    }

    // Build IN-list for sample numbers
    std::ostringstream sample_list;
    for (size_t si = 0; si < all_samples.size(); ++si) {
        if (si > 0) sample_list << ",";
        sample_list << "'" << sql_escape(all_samples[si]) << "'";
    }

    std::ostringstream sql;
    sql << "SELECT "
        << " CAST(e.ID AS varchar(30)),"
        << " CAST(r.REP_NO AS varchar(30)),"
        << " isnull(CAST(r.ROOM_CODE AS varchar(20)),''),"
        << " isnull(CAST(r.MACH_CODE AS varchar(20)),''),"
        << " isnull(r.OPER_NO,''),"
        << " isnull(r.TXM_NO,''),"
        << " isnull(CAST(r.OPER_CODE AS varchar(20)),''),"
        << " isnull(CONVERT(varchar(19),r.REP_DATE,120),''),"
        << " isnull(CONVERT(varchar(19),r.CHK_DATE,120),''),"
        << " isnull(CONVERT(varchar(19),r.REP_TIME,120),''),"
        << " isnull(CONVERT(varchar(19),r.CHK_DATE,120),''),"
        << " isnull(r.CHK_FLAG,''),"
        << " isnull(r.CONF,''),"
        << " isnull(CAST(e.ITEM_CODE AS varchar(20)),''),"
        << " isnull(e.RESULT,''),"
        << " isnull(e.NORMAL,'')"
        << " FROM LS_AS_REPORT r WITH (NOLOCK)"
        << " INNER JOIN LS_AS_REPENTRY e WITH (NOLOCK) ON e.REP_NO=r.REP_NO AND isnull(e.DELETE_BIT,0)=0"
        << " WHERE isnull(r.DELETE_BIT,0)=0"
        << " AND r.MACH_CODE='" << sql_escape(trim(query.mach_code)) << "'"
        << " AND r.OPER_NO IN (" << sample_list.str() << ")";
    if (!trim(query.start_date).empty()) {
        sql << " AND r.CHK_DATE >= '" << sql_escape(trim(query.start_date)) << "'";
    }
    if (!trim(query.end_date).empty()) {
        sql << " AND r.CHK_DATE < DATEADD(day,1,'" << sql_escape(trim(query.end_date)) << "')";
    }
    sql << " ORDER BY r.CHK_DATE ASC,r.REP_NO ASC,e.GROUP_CODE ASC,e.ITEM_CODE ASC,e.ID ASC";

    if (log) {
        log("exec sql: " + sql.str() + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        QualityControlLisRow row;
        row.entry_id = fetch_column(stmt, 1);
        row.rep_no = fetch_column(stmt, 2);
        row.room_code = fetch_column(stmt, 3);
        row.mach_code = fetch_column(stmt, 4);
        row.sample_no = fetch_column(stmt, 5);
        row.barcode_no = fetch_column(stmt, 6);
        {
            const auto eit = employee_names.find(trim(fetch_column(stmt, 7)));
            row.tester_name = eit != employee_names.end() ? eit->second : "";
        }
        row.report_date = fetch_column(stmt, 8);
        row.inspect_date = fetch_column(stmt, 9);
        row.report_time = fetch_column(stmt, 10);
        row.effective_time = fetch_column(stmt, 11);
        row.chk_flag = fetch_column(stmt, 12);
        row.conf = fetch_column(stmt, 13);
        {
            const std::string ic = trim(fetch_column(stmt, 14));
            row.item_code = ic;
            auto nit = item_names.find(ic);
            row.item_name = nit != item_names.end() ? nit->second : ic;
            auto eit = item_engs.find(ic);
            row.item_eng = eit != item_engs.end() ? eit->second : "";
            auto uit = item_units.find(ic);
            row.unit = uit != item_units.end() ? uit->second : "";
        }
        row.result = fetch_column(stmt, 15);
        row.normal = fetch_column(stmt, 16);
        {
            auto mit = mach_names.find(trim(row.mach_code));
            row.mach_name = mit != mach_names.end() ? mit->second : row.mach_code;
        }
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_quality_control_sample_items(const QualityControlSampleItemsQuery& query, std::vector<QualityControlSampleItemRow>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)query;
    (void)log;
    error = "query_quality_control_sample_items is only available on Windows";
    return false;
#else
    if (trim(query.mach_code).empty() || trim(query.sample_no).empty() || trim(query.inspect_date).empty()) {
        error = "missing quality control machine code, sample number, or inspect date";
        return false;
    }

    DbContext db;
    if (!connect(query.connection_string, db, error, log)) {
        return false;
    }

    const std::string mach = sql_escape(trim(query.mach_code));
    const std::string sample = sql_escape(trim(query.sample_no));
    const std::string date = sql_escape(trim(query.inspect_date));
    std::ostringstream sql;
    sql << "WITH src AS ("
        << " SELECT"
        << " CAST(e.ID AS varchar(30)) AS entry_id,"
        << " CAST(r.REP_NO AS varchar(30)) AS rep_no,"
        << " isnull(CAST(e.ITEM_CODE AS varchar(20)),'') AS item_code,"
        << " isnull(i.ITEM_NAME,e.ITEM_NAME) AS item_name,"
        << " isnull(nullif(RTRIM(i.ENG_NAME),''),isnull(e.ITEM_ENG,'')) AS item_eng,"
        << " isnull(RTRIM(i.UNIT),'') AS unit,"
        << " isnull(e.RESULT,'') AS result_text,"
        << " isnull(CONVERT(varchar(19),COALESCE(r.REP_TIME,r.CHK_DATE,r.REP_DATE),120),'') AS effective_time,"
        << " ROW_NUMBER() OVER (PARTITION BY e.ITEM_CODE ORDER BY COALESCE(r.REP_TIME,r.CHK_DATE,r.REP_DATE) DESC,r.REP_NO DESC,e.ID DESC) AS rn,"
        << " COUNT(*) OVER (PARTITION BY e.ITEM_CODE) AS point_count"
        << " FROM LS_AS_REPORT r WITH (NOLOCK)"
        << " INNER JOIN LS_AS_REPENTRY e WITH (NOLOCK) ON e.REP_NO=r.REP_NO AND isnull(e.DELETE_BIT,0)=0"
        << " LEFT JOIN LS_AS_ITEM i WITH (NOLOCK) ON e.ITEM_CODE=i.ITEM_CODE AND isnull(i.DELETE_BIT,0)=0"
        << " WHERE isnull(r.DELETE_BIT,0)=0"
        << " AND r.MACH_CODE='" << mach << "'"
        << " AND r.OPER_NO='" << sample << "'"
        << " AND r.CHK_DATE>='" << date << "'"
        << " AND r.CHK_DATE<DATEADD(day,1,'" << date << "')"
        << ")"
        << " SELECT item_code,item_name,item_eng,unit,result_text,effective_time,rep_no,entry_id,point_count"
        << " FROM src WHERE rn=1 ORDER BY item_code";

    if (log) {
        log("exec sql: " + sql.str() + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        return false;
    }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        QualityControlSampleItemRow row;
        row.item_code = fetch_column(stmt, 1);
        row.item_name = fetch_column(stmt, 2);
        row.item_eng = fetch_column(stmt, 3);
        row.unit = fetch_column(stmt, 4);
        row.latest_result = fetch_column(stmt, 5);
        row.latest_time = fetch_column(stmt, 6);
        row.latest_rep_no = fetch_column(stmt, 7);
        row.latest_entry_id = fetch_column(stmt, 8);
        const std::string count = fetch_column(stmt, 9);
        row.point_count = count.empty() ? 0 : std::atoi(count.c_str());
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_report_picture(const std::string& connection_string, const std::string& rep_no,
                          std::vector<unsigned char>& picture, std::string& error, LogFn log) {
    picture.clear();
#ifndef _WIN32
    (void)connection_string;
    (void)rep_no;
    (void)log;
    error = "query_report_picture is only available on Windows";
    return false;
#else
    const std::string trimmed_rep_no = trim(rep_no);
    if (trimmed_rep_no.empty()) {
        error.clear();
        return true;
    }

    DbContext db;
    if (!connect(connection_string, db, error, log)) {
        return false;
    }

    std::ostringstream sql;
    sql << "SELECT TOP 1 PICTURE"
        << " FROM LS_AS_ITEMPICTURE WITH (NOLOCK)"
        << " WHERE isnull(DELETE_BIT,0)=0"
        << " AND REP_NO='" << sql_escape(trimmed_rep_no) << "'"
        << " AND PICTURE IS NOT NULL"
        << " AND DATALENGTH(PICTURE)>0"
        << " ORDER BY PIC_NO,ID";

    if (log) {
        log("exec sql: " + sql.str() + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        return false;
    }

    if (SQLFetch(stmt) == SQL_SUCCESS) {
        if (!fetch_binary_column(stmt, 1, picture, error)) {
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
            return false;
        }
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_lis_summary(const QueryFilters& filters, LisSummary& summary, std::string& error, LogFn log) {
    summary = {};
#ifndef _WIN32
    (void)filters;
    (void)log;
    error = "query_lis_summary is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(filters.connection_string, db, error, log)) {
        return false;
    }

    auto exec_one = [&](const std::ostringstream& sql, std::vector<std::string>& cols) -> bool {
        if (log) {
            log("exec sql: " + sql.str() + "\n");
        }
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (!exec_query(db.dbc, sql.str(), stmt, error)) {
            return false;
        }
        if (SQLFetch(stmt) == SQL_SUCCESS) {
            for (SQLUSMALLINT i = 1; i <= cols.size(); ++i) {
                cols[static_cast<size_t>(i - 1)] = fetch_column(stmt, i);
            }
        }
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return true;
    };

    const std::string abo_codes = sql_item_code_list(filters.lis_abo_codes, "91962;11101;91963;11102");
    const std::string rhd_codes = sql_item_code_list(filters.lis_rhd_codes, "91964;11103");
    const std::string hgb_codes = sql_item_code_list(filters.lis_hgb_codes, "91672;90891;1013;92563;90943;89786");
    const std::string plt_codes = sql_item_code_list(filters.lis_plt_codes, "91678;90897;1019;92569;90949");
    const std::string irregular_antibody_codes = sql_item_code_list(filters.lis_irregular_antibody_codes, "11106;91966");
    const std::string direct_antiglobulin_codes = sql_item_code_list(filters.lis_direct_antiglobulin_codes, "11105;91965");
    const std::string blood_codes = abo_codes + "," + rhd_codes;
    const std::string cbc_codes = hgb_codes + "," + plt_codes;
    const std::string blood_machine_filter = sql_room_machine_filter(filters.lis_blood_type_machines, "r");
    const std::string cbc_machine_filter = sql_room_machine_filter(filters.lis_cbc_machines, "r");

    // Single round-trip via OUTER APPLY: each summary branch is independent but
    // shares the same patient/date filters to avoid repeated network round-trips.
    std::ostringstream sql;
    sql
        << "SELECT b.abo,b.rhd,b.blood_type_date,c.hgb,c.plt,c.cbc_date,"
        << "ia.irregular_antibody,ia.irregular_antibody_date,d.direct_antiglobulin,d.direct_antiglobulin_date"
        << " FROM (VALUES(1)) AS _(d)"
        << " OUTER APPLY ("
        << "SELECT TOP 1"
        << " isnull(MAX(CASE WHEN e.ITEM_CODE IN (" << abo_codes << ") THEN nullif(LTRIM(RTRIM(e.RESULT)),'') END),'') AS abo,"
        << " isnull(MAX(CASE WHEN e.ITEM_CODE IN (" << rhd_codes << ") THEN nullif(LTRIM(RTRIM(e.RESULT)),'') END),'') AS rhd,"
        << " isnull(CONVERT(varchar(10),r.CHK_DATE,120),'') AS blood_type_date"
        << " FROM LS_AS_REPORT r WITH (NOLOCK)"
        << " INNER JOIN LS_AS_REPENTRY e WITH (NOLOCK) ON e.REP_NO=r.REP_NO AND isnull(e.DELETE_BIT,0)=0"
        << " WHERE isnull(r.DELETE_BIT,0)=0"
        << " AND e.ITEM_CODE IN (" << blood_codes << ")"
        << blood_machine_filter;
    add_lis_patient_filters(sql, filters, "r");
    sql
        << " GROUP BY r.REP_NO,r.CHK_DATE"
        << " HAVING MAX(CASE WHEN e.ITEM_CODE IN (" << abo_codes << ")"
        << " AND nullif(LTRIM(RTRIM(e.RESULT)),'') IS NOT NULL THEN 1 ELSE 0 END)=1"
        << " AND MAX(CASE WHEN e.ITEM_CODE IN (" << rhd_codes << ")"
        << " AND nullif(LTRIM(RTRIM(e.RESULT)),'') IS NOT NULL THEN 1 ELSE 0 END)=1"
        << " ORDER BY r.CHK_DATE DESC,r.REP_NO DESC"
        << ") b"
        << " OUTER APPLY ("
        << "SELECT TOP 1"
        << " isnull(MAX(CASE WHEN e.ITEM_CODE IN (" << hgb_codes << ") THEN nullif(LTRIM(RTRIM(e.RESULT)),'') END),'') AS hgb,"
        << " isnull(MAX(CASE WHEN e.ITEM_CODE IN (" << plt_codes << ") THEN nullif(LTRIM(RTRIM(e.RESULT)),'') END),'') AS plt,"
        << " isnull(CONVERT(varchar(10),r.CHK_DATE,120),'') AS cbc_date"
        << " FROM LS_AS_REPORT r WITH (NOLOCK)"
        << " INNER JOIN LS_AS_REPENTRY e WITH (NOLOCK) ON e.REP_NO=r.REP_NO AND isnull(e.DELETE_BIT,0)=0"
        << " WHERE isnull(r.DELETE_BIT,0)=0"
        << " AND e.ITEM_CODE IN (" << cbc_codes << ")"
        << cbc_machine_filter;
    add_lis_patient_filters(sql, filters, "r");
    sql
        << " GROUP BY r.REP_NO,r.CHK_DATE"
        << " HAVING MAX(CASE WHEN e.ITEM_CODE IN (" << hgb_codes << ")"
        << " AND nullif(LTRIM(RTRIM(e.RESULT)),'') IS NOT NULL THEN 1 ELSE 0 END)=1"
        << " AND MAX(CASE WHEN e.ITEM_CODE IN (" << plt_codes << ")"
        << " AND nullif(LTRIM(RTRIM(e.RESULT)),'') IS NOT NULL THEN 1 ELSE 0 END)=1"
        << " ORDER BY r.CHK_DATE DESC,r.REP_NO DESC"
        << ") c"
        << " OUTER APPLY ("
        << "SELECT TOP 1"
        << " isnull(nullif(LTRIM(RTRIM(e.RESULT)),''),'') AS irregular_antibody,"
        << " isnull(CONVERT(varchar(10),r.CHK_DATE,120),'') AS irregular_antibody_date"
        << " FROM LS_AS_REPORT r WITH (NOLOCK)"
        << " INNER JOIN LS_AS_REPENTRY e WITH (NOLOCK) ON e.REP_NO=r.REP_NO AND isnull(e.DELETE_BIT,0)=0"
        << " WHERE isnull(r.DELETE_BIT,0)=0"
        << " AND nullif(LTRIM(RTRIM(e.RESULT)),'') IS NOT NULL"
        << " AND e.ITEM_CODE IN (" << irregular_antibody_codes << ")";
    add_lis_patient_filters(sql, filters, "r");
    sql
        << " ORDER BY r.CHK_DATE DESC,r.REP_NO DESC,e.ID DESC"
        << ") ia"
        << " OUTER APPLY ("
        << "SELECT TOP 1"
        << " isnull(nullif(LTRIM(RTRIM(e.RESULT)),''),'') AS direct_antiglobulin,"
        << " isnull(CONVERT(varchar(10),r.CHK_DATE,120),'') AS direct_antiglobulin_date"
        << " FROM LS_AS_REPORT r WITH (NOLOCK)"
        << " INNER JOIN LS_AS_REPENTRY e WITH (NOLOCK) ON e.REP_NO=r.REP_NO AND isnull(e.DELETE_BIT,0)=0"
        << " WHERE isnull(r.DELETE_BIT,0)=0"
        << " AND nullif(LTRIM(RTRIM(e.RESULT)),'') IS NOT NULL"
        << " AND e.ITEM_CODE IN (" << direct_antiglobulin_codes << ")";
    add_lis_patient_filters(sql, filters, "r");
    sql
        << " ORDER BY r.CHK_DATE DESC,r.REP_NO DESC,e.ID DESC"
        << ") d";

    std::vector<std::string> cols(10);
    if (!exec_one(sql, cols)) {
        return false;
    }
    summary.abo = cols[0];
    summary.rhd = cols[1];
    summary.blood_type_date = cols[2];
    summary.hgb = cols[3];
    summary.plt = cols[4];
    summary.cbc_date = cols[5];
    summary.irregular_antibody = cols[6];
    summary.irregular_antibody_date = cols[7];
    summary.direct_antiglobulin = cols[8];
    summary.direct_antiglobulin_date = cols[9];

    error.clear();
    return true;
#endif
}

bool query_blood_requests(const BloodQueryFilters& filters, std::vector<BloodRequestRow>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)filters; (void)log;
    error = "query_blood_requests is only available on Windows";
    return false;
#else
    DbContext db;
    if (!connect(filters.connection_string, db, error, log)) return false;

    std::ostringstream sql;
    sql << "SELECT ";
    if (filters.limit > 0) {
        sql << "TOP " << filters.limit << " ";
    }
    sql << "isnull(LTRIM(RTRIM(a.TranProperty)),''),"
        << "isnull(LTRIM(RTRIM(a.Patient_Name)),''),"
        << "isnull(LTRIM(RTRIM(a.Apply_Dept)),''),isnull(LTRIM(RTRIM(a.Apply_BedNo)),''),"
        << "isnull(LTRIM(RTRIM(a.BloodType)),''),isnull(LTRIM(RTRIM(a.RHD)),''),"
        << "isnull(("
        << "SELECT "
        << "isnull(LTRIM(RTRIM(s.ApplyComposition)),'') + "
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),s.ApplyNum))),'') + "
        << "isnull(LTRIM(RTRIM(s.ApplyUnit)),'') + ';'"
        << " FROM LS_XK_BloodRequestApplySon s"
        << " WHERE s.ApplyFormNO=a.ApplyFormNO"
        << " ORDER BY s.ID"
        << " FOR XML PATH(''),TYPE).value('.','varchar(max)'),'')"
        << ",isnull(LTRIM(RTRIM(a.Patient_NO)),''),isnull(LTRIM(RTRIM(a.ApplyFormNO)),''),"
        << "isnull(LTRIM(RTRIM(a.Check_Doctor)),''),isnull(CONVERT(varchar(19),a.Check_Date,120),''),"
        << "isnull(LTRIM(RTRIM(a.ApplyForm_Statue)),''),isnull(CONVERT(varchar(19),a.Apply_Time,120),''),"
        << "isnull(LTRIM(RTRIM(a.UrgencyLevel)),''),isnull(LTRIM(RTRIM(a.reactionHistory)),''),"
        << "isnull(LTRIM(RTRIM(a.Patient_NOType)),''),isnull(LTRIM(RTRIM(a.Patient_Sex)),''),"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(20),a.Patient_Age))),'') + isnull(LTRIM(RTRIM(a.Patient_AgeUnit)),''),"
        << "isnull(LTRIM(RTRIM(a.FYS)),'')"
        << " FROM LS_XK_BloodRequestApply a"
        << " WHERE a.Delete_Bit=0";

    if (!trim(filters.patient_no).empty()) {
        sql << " AND a.Patient_NO='" << sql_escape(trim(filters.patient_no)) << "'";
    }
    if (!trim(filters.patient_name).empty()) {
        sql << " AND a.Patient_Name LIKE '%" << sql_escape(trim(filters.patient_name)) << "%'";
    }
    if (!trim(filters.apply_form_no).empty()) {
        sql << " AND a.ApplyFormNO='" << sql_escape(trim(filters.apply_form_no)) << "'";
    }
    add_blood_apply_status(sql, filters.apply_status);
    if (!trim(filters.start_date).empty()) {
        sql << " AND a.Apply_Time >= '" << sql_escape(trim(filters.start_date)) << "'";
    }
    if (!trim(filters.end_date).empty()) {
        sql << " AND a.Apply_Time < DATEADD(day,1,'" << sql_escape(trim(filters.end_date)) << "')";
    }

    sql << " ORDER BY a.Apply_Time DESC";

    if (log) log("exec sql: " + sql.str() + "\n");

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) { return false; }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        BloodRequestRow row;
        row.tran_property     = fetch_column(stmt, 1);
        row.patient_name      = fetch_column(stmt, 2);
        row.apply_dept        = fetch_column(stmt, 3);
        row.apply_bed_no      = fetch_column(stmt, 4);
        row.apply_abo         = fetch_column(stmt, 5);
        row.apply_rhd         = fetch_column(stmt, 6);
        row.apply_composition = fetch_column(stmt, 7);
        row.patient_no        = fetch_column(stmt, 8);
        row.apply_form_no     = fetch_column(stmt, 9);
        row.check_doctor      = fetch_column(stmt, 10);
        row.check_date        = fetch_column(stmt, 11);
        row.apply_status      = fetch_column(stmt, 12);
        row.apply_time        = fetch_column(stmt, 13);
        row.urgency_level     = fetch_column(stmt, 14);
        row.transfusion_history = fetch_column(stmt, 15);
        row.patient_no_type   = fetch_column(stmt, 16);
        row.patient_sex       = fetch_column(stmt, 17);
        row.patient_age       = fetch_column(stmt, 18);
        row.reaction_history  = fetch_column(stmt, 19);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_blood_crossmatch_history(const std::string& connection_string, const std::string& patient_no,
                                    std::vector<BloodCrossMatchRow>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)connection_string;
    (void)patient_no;
    (void)log;
    error = "query_blood_crossmatch_history is only available on Windows";
    return false;
#else
    const std::string no = trim(patient_no);
    if (no.empty()) {
        error.clear();
        return true;
    }

    DbContext db;
    if (!connect(connection_string, db, error, log)) return false;

    std::ostringstream sql;
    sql << "SELECT "
        << "isnull(CONVERT(varchar(19),bo.BloodOut_Date,120),''),"
        << "isnull(LTRIM(RTRIM(bo.BloodOut_Man)),''),"
        << "isnull(LTRIM(RTRIM(bi.BloodBagNO)),''),"
        << "isnull(LTRIM(RTRIM(bi.CmpProductCode)),''),"
        << "isnull(LTRIM(RTRIM(bt.Blood_Type)),''),"
        << "isnull(LTRIM(RTRIM(rh.Blood_RH)),''),"
        << "isnull(LTRIM(RTRIM(comp.Blood_Composition)),''),"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),comp.Norm))),''),"
        << "isnull(LTRIM(RTRIM(comp.Unit)),''),"
        << "isnull(LTRIM(RTRIM(cm.CrossMethed)),''),"
        << "isnull(LTRIM(RTRIM(cm.MainCrossResult)),''),"
        << "isnull(LTRIM(RTRIM(cm.SecondCrossResult)),''),"
        << "isnull(CONVERT(varchar(19),cm.Match_Date,120),''),"
        << "isnull(LTRIM(RTRIM(cm.Match_Man)),''),"
        << "isnull(LTRIM(RTRIM(src.Sources_Blood)),'')"
        << " FROM LS_XK_BloodCrossMatch cm WITH (NOLOCK)"
        << " LEFT JOIN LS_XK_BloodOutInfo bo WITH (NOLOCK) ON bo.BloodInID=cm.BloodInID"
        << " LEFT JOIN LS_XK_BloodInfo bi WITH (NOLOCK) ON bi.ID=cm.BloodInID"
        << " LEFT JOIN LS_XK_B_TypeInfo bt WITH (NOLOCK) ON bt.ID=bi.BloodTypeID"
        << " LEFT JOIN LS_XK_B_RhInfo rh WITH (NOLOCK) ON rh.ID=bi.RhD_ID"
        << " LEFT JOIN LS_XK_B_CompositionInfo comp WITH (NOLOCK) ON comp.ID=bi.CompositionID"
        << " LEFT JOIN LS_XK_B_SourceInfo src WITH (NOLOCK) ON src.ID=bi.SourceID"
        << " WHERE cm.Delete_Bit=0"
        << " AND cm.Patient_NO='" << sql_escape(no) << "'"
        << " ORDER BY bo.BloodOut_Date DESC,cm.Match_Date DESC,cm.ID DESC";

    if (log) log("exec sql: " + sql.str() + "\n");

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) return false;

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        BloodCrossMatchRow row;
        row.blood_out_date = fetch_column(stmt, 1);
        row.blood_out_man = fetch_column(stmt, 2);
        row.blood_bag_no = fetch_column(stmt, 3);
        row.product_code = fetch_column(stmt, 4);
        row.blood_type = fetch_column(stmt, 5);
        row.rhd = fetch_column(stmt, 6);
        row.composition = fetch_column(stmt, 7);
        row.norm = fetch_column(stmt, 8);
        row.unit = fetch_column(stmt, 9);
        row.cross_method = fetch_column(stmt, 10);
        row.main_result = fetch_column(stmt, 11);
        row.second_result = fetch_column(stmt, 12);
        row.match_date = fetch_column(stmt, 13);
        row.match_man = fetch_column(stmt, 14);
        row.source = fetch_column(stmt, 15);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_barcodes(const BarcodeQueryFilters& filters, std::vector<BarcodeQueryRow>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)filters; (void)log;
    error = "query_barcodes is only available on Windows";
    return false;
#else
    const auto query_started = std::chrono::steady_clock::now();
    DbContext db;
    if (!connect(filters.connection_string, db, error, log)) return false;

    std::shared_ptr<const EmployeeNameMap> employee_names;
    bool employee_cache_hit = false;
    std::string employee_error;
    const auto employee_dictionary_started = std::chrono::steady_clock::now();
    if (!load_barcode_employee_names(db.dbc, filters.connection_string, employee_names,
                                     employee_cache_hit, employee_error, log)) {
        if (log) log("barcode employee dictionary unavailable: " + employee_error + "\n");
        employee_names = std::make_shared<const EmployeeNameMap>();
    }
    const auto employee_dictionary_finished = std::chrono::steady_clock::now();

    const std::string requested_campus = trim(filters.campus);
    const auto campus_matches = [&requested_campus](const std::string& dept_name) {
        if (requested_campus != "老院" && requested_campus != "新院") return true;
        const std::string campus = contains_text(dept_name, "滨水") ? "新院" : "老院";
        return campus == requested_campus;
    };

    const std::string date_col =
        trim(filters.date_field) == "Receive" ? "b.IN_DATE" :
        trim(filters.date_field) == "Machine" ? "b.IN_DATE" :
        "b.REQ_TIME";

    std::ostringstream where;
    where << " WHERE b.CANCEL_DATE IS " << (filters.canceled ? "NOT NULL" : "NULL");

    if (!trim(filters.start_date).empty()) {
        where << " AND " << date_col << " >= '" << sql_escape(trim(filters.start_date)) << "'";
    }
    if (!trim(filters.end_date).empty()) {
        where << " AND " << date_col << " < DATEADD(minute,1,'" << sql_escape(trim(filters.end_date)) << "')";
    }
    add_like(where, "b.BARCODE", filters.barcode);
    add_like(where, "b.NAME", filters.patient_name);
    add_like(where, "b.REG_NO", filters.reg_no);
    add_eq(where, "CONVERT(varchar(20),b.ROOM_CODE)", filters.room_code);
    add_in(where, "CONVERT(varchar(20),b.ROOM_CODE)", filters.room_codes);

    if (!filters.machine_statuses.empty()) {
        add_barcode_machine_statuses(where, filters.machine_statuses);
    } else {
        add_barcode_machine_status(where, filters.machine_status);
    }

    const std::string order_expr = "b.IN_DATE ASC,b.ID ASC";

    std::ostringstream sql;
    sql << "SELECT "
        << "isnull(LTRIM(RTRIM(rd.OPER_NO)),'')"
        << ",isnull(CONVERT(varchar(10),b.JZ_FLAG),'') AS emergency,"
        << "isnull(LTRIM(RTRIM(b.BARCODE)),'') AS barcode,"
        << "isnull(LTRIM(RTRIM(b.REG_NO)),'') AS reg_no,"
        << "isnull(LTRIM(RTRIM(b.TYPENAME)),'') AS type_name,"
        << "isnull(LTRIM(RTRIM(b.NAME)),'') AS patient_name,"
        << "isnull(LTRIM(RTRIM(b.SEX)),'') AS sex,"
        << "isnull(LTRIM(RTRIM(b.DEPT_NAME)),'') AS dept_name,"
        << "isnull(LTRIM(RTRIM(b.BEDNO)),'') AS bed_no,"
        << "isnull(LTRIM(RTRIM(b.OPER_CODE)),'') AS receiver,"
        << "isnull(CONVERT(varchar(19),b.IN_DATE,120),'') AS receive_time,"
        << "isnull(LTRIM(RTRIM(b.ORDER_TEXT)),'') AS order_text,"
        << "isnull(LTRIM(RTRIM(b.SAMP_NAME)),'') AS sample_name,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(50),rd.OPER_CODE))),'') AS tester_code,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(50),rd.REP_OPER))),'') AS reviewer_code,"
        << "isnull(CONVERT(varchar(19),rd.REP_TIME,120),'') AS review_time,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),b.FY))),'') AS fee,"
        << "isnull(LTRIM(RTRIM(b.REQ_DRN)),'') AS request_doctor,"
        << "isnull(CONVERT(varchar(10),b.ZT_FLAG),'') AS status,"
        << "isnull(LTRIM(RTRIM(b.NOTE)),'') AS note,"
        << "isnull(LTRIM(RTRIM(b.REASON)),'') AS reason,"
        << "isnull(LTRIM(RTRIM(b.sjyq_qsr)),'') AS submitter,"
        << "isnull(nullif(LTRIM(RTRIM(b.COLLECTION_TIME)),''),isnull(CONVERT(varchar(19),b.SUB_DATE,120),'')) AS submit_time,"
        << "isnull(CONVERT(varchar(19),b.REQ_TIME,120),'') AS request_time,"
        << "isnull(CONVERT(varchar(19),b.CANCEL_DATE,120),'') AS cancel_time,"
        << "isnull(LTRIM(RTRIM(b.CANCEL_OPER)),'') AS cancel_operator,"
        << "isnull(CONVERT(varchar(30),b.HZID),'') AS hzid,"
        << barcode_machine_status_sql() << " AS machine_status,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(30),rd.REP_NO))),'') AS report_no,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(20),rd.MACH_CODE))),'') AS machine_code,"
        << "isnull(nullif(LTRIM(RTRIM(rd.MACH_NAME)),''),isnull(LTRIM(RTRIM(CONVERT(varchar(20),rd.MACH_CODE))),'')) AS machine_name,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(20),rd.ROOM_CODE))),'') AS room_code,"
        << "isnull(CONVERT(varchar(19),rd.CHK_DATE,120),'') AS inspect_date"
        << " FROM LS_AS_BARCODE b WITH (NOLOCK)"
        << " OUTER APPLY (SELECT"
        << " MAX(CASE WHEN NULLIF(LTRIM(RTRIM(CONVERT(varchar(30),r.REP_NO))),'') IS NOT NULL THEN 1 ELSE 0 END) AS HAS_REPORT,"
        << " MAX(CASE WHEN LTRIM(RTRIM(isnull(r.CHK_FLAG,'')))='T' THEN 1 ELSE 0 END) AS REPORT_REVIEWED,"
        << " MAX(CASE WHEN LTRIM(RTRIM(isnull(r.CONF,'')))='S' THEN 1 ELSE 0 END) AS REPORT_SENT"
        << " FROM LS_AS_REPORT r WITH (NOLOCK)"
        << " WHERE isnull(r.DELETE_BIT,0)=0"
        << " AND r.TXM_NO=b.BARCODE) rs"
        << " OUTER APPLY (SELECT TOP 1"
        << " r.REP_NO,r.OPER_NO,r.CHK_DATE,r.REP_TIME,r.MACH_CODE,r.ROOM_CODE,mach.MACH_NAME,"
        << " r.OPER_CODE,r.REP_OPER"
        << " FROM LS_AS_REPORT r WITH (NOLOCK)"
        << " LEFT JOIN LS_AS_MACHINE mach WITH (NOLOCK)"
        << " ON r.MACH_CODE=mach.MACH_CODE AND r.ROOM_CODE=mach.ROOM_CODE AND mach.DELETE_BIT=0"
        << " WHERE isnull(r.DELETE_BIT,0)=0"
        << " AND r.TXM_NO=b.BARCODE"
        << " AND NULLIF(LTRIM(RTRIM(CONVERT(varchar(30),r.REP_NO))),'') IS NOT NULL"
        << " ORDER BY r.CHK_DATE DESC,r.REP_TIME DESC,r.REP_NO DESC) rd"
        << where.str()
        << " ORDER BY " << order_expr;

    if (log) log("exec sql: " + sql.str() + "\n");

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) { return false; }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        BarcodeQueryRow row;
        row.sample_no      = fetch_column(stmt, 1);
        row.emergency      = fetch_column(stmt, 2);
        row.barcode        = fetch_column(stmt, 3);
        row.reg_no         = fetch_column(stmt, 4);
        row.type_name      = fetch_column(stmt, 5);
        row.name           = fetch_column(stmt, 6);
        row.sex            = fetch_column(stmt, 7);
        row.dept_name      = fetch_column(stmt, 8);
        row.bed_no         = fetch_column(stmt, 9);
        row.receiver       = fetch_column(stmt, 10);
        row.receive_time   = fetch_column(stmt, 11);
        row.order_text     = fetch_column(stmt, 12);
        row.sample_name    = fetch_column(stmt, 13);
        const std::string tester_code = fetch_column(stmt, 14);
        const std::string reviewer_code = fetch_column(stmt, 15);
        const auto tester_name = employee_names->find(tester_code);
        const auto reviewer_name = employee_names->find(reviewer_code);
        row.tester         = employee_display_name(
            tester_code, tester_name != employee_names->end() ? tester_name->second : "");
        row.reviewer       = employee_display_name(
            reviewer_code, reviewer_name != employee_names->end() ? reviewer_name->second : "");
        row.review_time    = fetch_column(stmt, 16);
        row.fee            = fetch_column(stmt, 17);
        row.request_doctor = fetch_column(stmt, 18);
        row.status         = fetch_column(stmt, 19);
        row.note           = fetch_column(stmt, 20);
        row.reason         = fetch_column(stmt, 21);
        row.submitter      = fetch_column(stmt, 22);
        row.submit_time    = fetch_column(stmt, 23);
        row.request_time   = fetch_column(stmt, 24);
        row.cancel_time    = fetch_column(stmt, 25);
        row.cancel_operator = fetch_column(stmt, 26);
        row.hzid           = fetch_column(stmt, 27);
        row.machine_status = fetch_column(stmt, 28);
        row.report_no      = fetch_column(stmt, 29);
        row.machine_code   = fetch_column(stmt, 30);
        row.machine_name   = fetch_column(stmt, 31);
        row.room_code      = fetch_column(stmt, 32);
        row.inspect_date   = fetch_column(stmt, 33);
        if (!campus_matches(row.dept_name)) continue;
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    const auto database_finished = std::chrono::steady_clock::now();

    struct CachedReviewElapsed {
        long long seconds = -1;
        std::string text;
    };
    std::unordered_map<std::string, CachedReviewElapsed> elapsed_cache;
    elapsed_cache.reserve(rows.size());
    for (auto& row : rows) {
        if (row.receive_time.empty() || row.review_time.empty()) continue;
        const std::string cache_key = row.receive_time + '\x1f' + row.review_time;
        auto found = elapsed_cache.find(cache_key);
        if (found == elapsed_cache.end()) {
            CachedReviewElapsed elapsed;
            elapsed.seconds = sql_datetime_diff_seconds(row.receive_time, row.review_time);
            elapsed.text = format_duration_seconds_zh(elapsed.seconds);
            found = elapsed_cache.emplace(cache_key, std::move(elapsed)).first;
        }
        row.review_elapsed_seconds = found->second.seconds;
        row.review_elapsed = found->second.text;
    }
    const auto calculation_finished = std::chrono::steady_clock::now();
    if (log) {
        const auto database_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            database_finished - query_started).count();
        const auto calculation_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            calculation_finished - database_finished).count();
        const auto employee_dictionary_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            employee_dictionary_finished - employee_dictionary_started).count();
        const auto main_query_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            database_finished - employee_dictionary_finished).count();
        log("barcode query timing: database_and_fetch_ms=" + std::to_string(database_ms) +
            ", main_query_and_fetch_ms=" + std::to_string(main_query_ms) +
            ", employee_dictionary_ms=" + std::to_string(employee_dictionary_ms) +
            ", employee_cache_hit=" + std::to_string(employee_cache_hit ? 1 : 0) +
            ", employee_cache_entries=" + std::to_string(employee_names->size()) +
            ", duration_ms=" + std::to_string(calculation_ms) +
            ", rows=" + std::to_string(rows.size()) +
            ", duration_cache_entries=" + std::to_string(elapsed_cache.size()) + "\n");
    }
    error.clear();
    return true;
#endif
}

bool query_specimen_signed_list(const SpecimenSignedListQuery& query, std::vector<SpecimenSignedListRow>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)query; (void)log;
    error = "query_specimen_signed_list is only available on Windows";
    return false;
#else
    if (!query.use_sign_time && !query.use_apply_time) {
        error = "missing date filter";
        return false;
    }

    DbContext db;
    if (!connect(query.connection_string, db, error, log)) return false;

    std::ostringstream where;
    where << " WHERE b.IN_DATE IS NOT NULL"
          << " AND (b.DELETE_BIT IS NULL OR b.DELETE_BIT=0)"
          << " AND (b.ZT_FLAG IS NULL OR b.ZT_FLAG<>9)";

    if (query.use_sign_time) {
        if (!trim(query.sign_start).empty()) {
            where << " AND b.IN_DATE >= '" << sql_escape(trim(query.sign_start)) << "'";
        }
        if (!trim(query.sign_end).empty()) {
            where << " AND b.IN_DATE < DATEADD(minute,1,'" << sql_escape(trim(query.sign_end)) << "')";
        }
    }
    if (query.use_apply_time) {
        if (!trim(query.apply_start).empty()) {
            where << " AND b.REQ_TIME >= '" << sql_escape(trim(query.apply_start)) << "'";
        }
        if (!trim(query.apply_end).empty()) {
            where << " AND b.REQ_TIME < DATEADD(minute,1,'" << sql_escape(trim(query.apply_end)) << "')";
        }
    }
    add_eq(where, "CONVERT(varchar(20),b.ROOM_CODE)", query.room_code);
    add_like(where, "b.NAME", query.patient_name);

    std::ostringstream sql;
    sql << "SELECT "
        << "isnull(LTRIM(RTRIM(b.BARCODE)),''),"
        << "isnull(LTRIM(RTRIM(b.REG_NO)),''),"
        << "isnull(LTRIM(RTRIM(b.TYPENAME)),''),"
        << "isnull(LTRIM(RTRIM(b.NAME)),''),"
        << "isnull(LTRIM(RTRIM(b.SEX)),''),"
        << "isnull(LTRIM(RTRIM(b.DEPT_NAME)),''),"
        << "isnull(LTRIM(RTRIM(b.ORDER_TEXT)),''),"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),b.FY))),''),"
        << "isnull(CONVERT(varchar(19),b.REQ_TIME,120),''),"
        << "isnull(nullif(LTRIM(RTRIM(b.COLLECTION_TIME)),''),isnull(CONVERT(varchar(19),b.SUB_DATE,120),'')),"
        << "isnull(CONVERT(varchar(19),b.IN_DATE,120),''),"
        << "isnull(CONVERT(varchar(19),b.SUB_DATE,120),''),"
        << "isnull(LTRIM(RTRIM(b.AGE)),''),"
        << "isnull(LTRIM(RTRIM(b.OPER_CODE)),''),"
        << "isnull(LTRIM(RTRIM(b.SAMP_NAME)),''),"
        << "isnull(nullif(LTRIM(RTRIM(room.ROOM_NAME)),''),isnull(LTRIM(RTRIM(CONVERT(varchar(20),b.ROOM_CODE))),''))"
        << " FROM LS_AS_BARCODE b WITH (NOLOCK)"
        << " LEFT JOIN LS_AS_ROOM room WITH (NOLOCK) ON b.ROOM_CODE=room.ROOM_CODE AND room.DELETE_BIT=0"
        << where.str()
        << " ORDER BY b.IN_DATE ASC,b.BARCODE ASC,b.ID ASC";

    if (log) log("exec sql: " + sql.str() + "\n");

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) { return false; }

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        SpecimenSignedListRow row;
        row.barcode = fetch_column(stmt, 1);
        row.reg_no = fetch_column(stmt, 2);
        row.type_name = fetch_column(stmt, 3);
        row.name = fetch_column(stmt, 4);
        row.sex = fetch_column(stmt, 5);
        row.dept_name = fetch_column(stmt, 6);
        row.order_text = fetch_column(stmt, 7);
        row.fee = fetch_column(stmt, 8);
        row.request_time = fetch_column(stmt, 9);
        row.collection_time = fetch_column(stmt, 10);
        row.signed_time = fetch_column(stmt, 11);
        row.submit_time = fetch_column(stmt, 12);
        row.age = fetch_column(stmt, 13);
        row.receiver = fetch_column(stmt, 14);
        row.sample_name = fetch_column(stmt, 15);
        row.room_code = fetch_column(stmt, 16);
        rows.push_back(std::move(row));
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

bool query_specimen_barcode(const SpecimenBarcodeQuery& query, SpecimenBarcodeResult& result, std::string& error, LogFn log) {
    result = SpecimenBarcodeResult{};
#ifndef _WIN32
    (void)query; (void)log;
    error = "query_specimen_barcode is only available on Windows";
    return false;
#else
    const auto barcode = trim(query.barcode);
    if (barcode.empty()) {
        error = "empty barcode";
        return false;
    }

    DbContext db;
    if (!connect(query.connection_string, db, error, log)) return false;

    result.barcode = barcode;
    const auto escaped = sql_escape(barcode);

    {
        std::ostringstream sql;
        sql << "SELECT TOP 200 "
            << "isnull(LTRIM(RTRIM(b.BARCODE)),''),"
            << "isnull(LTRIM(RTRIM(b.REG_NO)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(20),b.TYPE))),''),"
            << "isnull(LTRIM(RTRIM(b.TYPENAME)),''),"
            << "isnull(LTRIM(RTRIM(b.NAME)),''),"
            << "isnull(LTRIM(RTRIM(b.SEX)),''),"
            << "isnull(LTRIM(RTRIM(b.AGE)),''),"
            << "isnull(LTRIM(RTRIM(b.DEPT_NAME)),''),"
            << "isnull(LTRIM(RTRIM(b.BEDNO)),''),"
            << "isnull(LTRIM(RTRIM(b.REQ_DRN)),''),"
            << "isnull(nullif(LTRIM(RTRIM(room.ROOM_NAME)),''),isnull(LTRIM(RTRIM(CONVERT(varchar(20),b.ROOM_CODE))),'')),"
            << "isnull(LTRIM(RTRIM(b.SAMP_NAME)),''),"
            << "isnull(LTRIM(RTRIM(b.ORDER_TEXT)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),b.FY))),''),"
            << "isnull(CONVERT(varchar(19),b.REQ_TIME,120),''),"
            << "isnull(CONVERT(varchar(19),b.IN_DATE,120),''),"
            << "isnull(LTRIM(RTRIM(b.OPER_CODE)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(10),b.JZ_FLAG))),''),"
            << "isnull(nullif(LTRIM(RTRIM(CONVERT(varchar(32),b.COLLECTION_TIME))),''),isnull(CONVERT(varchar(19),b.SUB_DATE,120),'')),"
            << "isnull(CONVERT(varchar(19),b.SUB_DATE,120),''),"
            << "isnull(LTRIM(RTRIM(b.NOTE)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(10),b.OPER_STATE))),'')"
            << " FROM LS_AS_BARCODE b WITH (NOLOCK)"
            << " LEFT JOIN LS_AS_ROOM room WITH (NOLOCK) ON b.ROOM_CODE=room.ROOM_CODE AND room.DELETE_BIT=0"
            << " WHERE b.BARCODE='" << escaped << "'"
            << " AND (b.DELETE_BIT IS NULL OR b.DELETE_BIT=0)"
            << " AND (b.ZT_FLAG IS NULL OR b.ZT_FLAG<>9)"
            << " ORDER BY b.ID";
        if (log) log("exec sql: " + sql.str() + "\n");

        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (!exec_query(db.dbc, sql.str(), stmt, error)) { return false; }
        while (SQLFetch(stmt) == SQL_SUCCESS) {
            const auto col_barcode = fetch_column(stmt, 1);
            const auto col_reg_no = fetch_column(stmt, 2);
            const auto col_type_code = fetch_column(stmt, 3);
            const auto col_type_name = fetch_column(stmt, 4);
            const auto col_name = fetch_column(stmt, 5);
            const auto col_sex = fetch_column(stmt, 6);
            const auto col_age = fetch_column(stmt, 7);
            const auto col_dept_name = fetch_column(stmt, 8);
            const auto col_bed_no = fetch_column(stmt, 9);
            const auto col_requester = fetch_column(stmt, 10);
            const auto col_room_code = fetch_column(stmt, 11);
            const auto col_sample_name = fetch_column(stmt, 12);
            const auto col_order_text = fetch_column(stmt, 13);
            const auto col_fee = fetch_column(stmt, 14);
            const auto col_request_time = fetch_column(stmt, 15);
            const auto col_signed_time = fetch_column(stmt, 16);
            const auto col_receiver = fetch_column(stmt, 17);
            const auto col_jz_flag = fetch_column(stmt, 18);
            const auto col_collection_time = fetch_column(stmt, 19);
            const auto col_submit_time = fetch_column(stmt, 20);
            const auto col_note = fetch_column(stmt, 21);
            const auto col_oper_state = fetch_column(stmt, 22);

            result.has_barcode_rows = true;
            fill_if_empty(result.barcode, col_barcode);
            fill_if_empty(result.reg_no, col_reg_no);
            fill_if_empty(result.type_code, col_type_code);
            fill_if_empty(result.type_name, col_type_name);
            fill_if_empty(result.name, col_name);
            fill_if_empty(result.sex, col_sex);
            fill_if_empty(result.age, col_age);
            fill_if_empty(result.dept_name, col_dept_name);
            fill_if_empty(result.bed_no, col_bed_no);
            fill_if_empty(result.requester, col_requester);
            fill_if_empty(result.room_code, col_room_code);

            SpecimenOrderRow order;
            order.barcode = col_barcode;
            order.room_code = col_room_code;
            order.sample_name = col_sample_name;
            order.order_text = col_order_text;
            order.fee = col_fee;
            order.request_time = col_request_time;
            fill_if_empty(result.fee, order.fee);
            fill_if_empty(result.signed_time, col_signed_time);
            fill_if_empty(result.receiver, col_receiver);
            fill_if_empty(result.jz_flag, col_jz_flag);
            fill_if_empty(result.collection_time, col_collection_time);
            fill_if_empty(result.submit_time, col_submit_time);
            if (trim(col_oper_state) == "0" || trim(result.oper_state).empty()) {
                result.oper_state = col_oper_state;
            }
            order.note = col_note;
            if (!trim(order.order_text).empty() || !trim(order.sample_name).empty()) {
                add_unique_order(result.orders, order);
            }
        }
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    }

    {
        std::ostringstream sql;
        sql << "SELECT TOP 20 "
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(20),r.TYPE))),''),"
            << "isnull(LTRIM(RTRIM(pt.TYPE_NAME)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(30),r.REP_NO))),''),"
            << "isnull(LTRIM(RTRIM(r.OPER_NO)),''),"
            << "isnull(LTRIM(RTRIM(r.REG_NO)),''),"
            << "isnull(LTRIM(RTRIM(r.NAME)),''),"
            << "CASE r.SEX WHEN '1' THEN '男' WHEN '2' THEN '女' ELSE isnull(LTRIM(RTRIM(r.SEX)),'') END,"
            << "isnull(LTRIM(RTRIM(r.AGE)),''),"
            << "isnull(LTRIM(RTRIM(r.BED_CODE)),''),"
            << "isnull(LTRIM(RTRIM(dept.NAME)),''),"
            << "isnull(nullif(LTRIM(RTRIM(mach.MACH_NAME)),''),isnull(LTRIM(RTRIM(CONVERT(varchar(20),r.MACH_CODE))),'')),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(20),r.GROUP_CODE))),''),"
            << "isnull(LTRIM(RTRIM(r.CHK_FLAG)),''),"
            << "isnull(LTRIM(RTRIM(r.CONF)),''),"
            << "isnull(CONVERT(varchar(19),r.CREATE_TIME,120),'')"
            << " FROM LS_AS_REPORT r WITH (NOLOCK)"
            << " LEFT JOIN LS_AS_PATTYPE pt WITH (NOLOCK) ON r.TYPE=pt.TYPE AND pt.DELETE_BIT=0"
            << " LEFT JOIN JC_DEPT_PROPERTY dept WITH (NOLOCK) ON r.DEPT_CODE=dept.DEPT_ID"
            << " LEFT JOIN LS_AS_MACHINE mach WITH (NOLOCK) ON r.MACH_CODE=mach.MACH_CODE AND mach.DELETE_BIT=0"
            << " WHERE r.TXM_NO='" << escaped << "'"
            << " AND (r.DELETE_BIT IS NULL OR r.DELETE_BIT=0)"
            << " ORDER BY r.CHK_DATE DESC,r.REP_NO DESC";
        if (log) log("exec sql: " + sql.str() + "\n");

        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (!exec_query(db.dbc, sql.str(), stmt, error)) { return false; }
        while (SQLFetch(stmt) == SQL_SUCCESS) {
            const auto col_type_code = fetch_column(stmt, 1);
            const auto col_type_name = fetch_column(stmt, 2);
            const auto col_rep_no = fetch_column(stmt, 3);
            const auto col_oper_no = fetch_column(stmt, 4);
            const auto col_reg_no = fetch_column(stmt, 5);
            const auto col_name = fetch_column(stmt, 6);
            const auto col_sex = fetch_column(stmt, 7);
            const auto col_age = fetch_column(stmt, 8);
            const auto col_bed_no = fetch_column(stmt, 9);
            const auto col_dept_name = fetch_column(stmt, 10);
            const auto col_mach_code = fetch_column(stmt, 11);
            const auto col_group_code = fetch_column(stmt, 12);
            const auto col_chk_flag = fetch_column(stmt, 13);
            const auto col_conf = fetch_column(stmt, 14);
            const auto col_create_time = fetch_column(stmt, 15);

            result.has_report_rows = true;
            fill_if_empty(result.type_code, col_type_code);
            fill_if_empty(result.type_name, col_type_name);
            fill_if_empty(result.rep_no, col_rep_no);
            fill_if_empty(result.oper_no, col_oper_no);
            fill_if_empty(result.reg_no, col_reg_no);
            fill_if_empty(result.name, col_name);
            fill_if_empty(result.sex, col_sex);
            fill_if_empty(result.age, col_age);
            fill_if_empty(result.bed_no, col_bed_no);
            fill_if_empty(result.dept_name, col_dept_name);
            fill_if_empty(result.mach_code, col_mach_code);
            fill_if_empty(result.group_code, col_group_code);
            fill_if_empty(result.chk_flag, col_chk_flag);
            fill_if_empty(result.conf, col_conf);
            fill_if_empty(result.create_time, col_create_time);
        }
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    }

    {
        std::ostringstream sql;
        sql << "SELECT TOP 50 "
            << "isnull(LTRIM(RTRIM(TXM)),''),"
            << "isnull(LTRIM(RTRIM(blh)),''),"
            << "isnull(LTRIM(RTRIM(hzxm)),''),"
            << "CASE CONVERT(varchar(10),xb) WHEN '1' THEN '男' WHEN '2' THEN '女' ELSE isnull(LTRIM(RTRIM(CONVERT(varchar(10),xb))),'') END,"
            << "isnull(LTRIM(RTRIM(BBMC)),''),"
            << "isnull(LTRIM(RTRIM(ksname)),''),"
            << "isnull(LTRIM(RTRIM(kdys)),''),"
            << "isnull(LTRIM(RTRIM(sqnr)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),ZJE))),''),"
            << "isnull(CONVERT(varchar(19),lrrq,120),''),"
            << "isnull(CONVERT(varchar(19),BBCJSJ,120),'')"
            << " FROM V_lis_mzinfo_txm"
            << " WHERE TXM='" << escaped << "'";
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (exec_optional_query(db.dbc, sql.str(), stmt, log)) {
            while (SQLFetch(stmt) == SQL_SUCCESS) {
                const auto col_txm = fetch_column(stmt, 1);
                const auto col_blh = fetch_column(stmt, 2);
                const auto col_name = fetch_column(stmt, 3);
                const auto col_sex = fetch_column(stmt, 4);
                const auto col_sample_name = fetch_column(stmt, 5);
                const auto col_dept_name = fetch_column(stmt, 6);
                const auto col_requester = fetch_column(stmt, 7);
                const auto col_order_text = fetch_column(stmt, 8);
                const auto col_fee = fetch_column(stmt, 9);
                const auto col_request_time = fetch_column(stmt, 10);
                const auto col_collection_time = fetch_column(stmt, 11);

                result.has_outpatient_rows = true;
                fill_if_empty(result.barcode, col_txm);
                fill_if_empty(result.reg_no, col_blh);
                fill_if_empty(result.name, col_name);
                fill_if_empty(result.sex, col_sex);
                fill_if_empty(result.type_name, "门诊");
                fill_if_empty(result.dept_name, col_dept_name);
                fill_if_empty(result.requester, col_requester);
                SpecimenOrderRow order;
                order.barcode = col_txm;
                order.sample_name = col_sample_name;
                order.order_text = col_order_text;
                order.fee = col_fee;
                order.request_time = col_request_time;
                fill_if_empty(result.fee, order.fee);
                fill_if_empty(result.collection_time, col_collection_time);
                if (should_add_supplemental_orders(result) &&
                    (!trim(order.order_text).empty() || !trim(order.sample_name).empty())) {
                    add_unique_order(result.orders, order);
                }
            }
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        }
    }

    {
        std::ostringstream sql;
        sql << "SELECT TOP 50 "
            << "isnull(LTRIM(RTRIM(TXM)),''),"
            << "isnull(LTRIM(RTRIM(BLH)),''),"
            << "isnull(LTRIM(RTRIM(SQNR)),''),"
            << "isnull(LTRIM(RTRIM(BBMC)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),JE))),''),"
            << "isnull(CONVERT(varchar(19),SQRQ,120),''),"
            << "isnull(CONVERT(varchar(19),BBCJSJ,120),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(20),ZXKS))),'')"
            << " FROM YJ_MZSQ WITH (NOLOCK)"
            << " WHERE TXM='" << escaped << "'"
            << " AND (BSCBZ IS NULL OR BSCBZ=0)"
            << " ORDER BY YJSQID";
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (exec_optional_query(db.dbc, sql.str(), stmt, log)) {
            while (SQLFetch(stmt) == SQL_SUCCESS) {
                const auto col_txm = fetch_column(stmt, 1);
                const auto col_blh = fetch_column(stmt, 2);
                const auto col_order_text = fetch_column(stmt, 3);
                const auto col_sample_name = fetch_column(stmt, 4);
                const auto col_fee = fetch_column(stmt, 5);
                const auto col_request_time = fetch_column(stmt, 6);
                const auto col_collection_time = fetch_column(stmt, 7);
                const auto col_room_code = fetch_column(stmt, 8);

                result.has_outpatient_rows = true;
                fill_if_empty(result.barcode, col_txm);
                fill_if_empty(result.reg_no, col_blh);
                fill_if_empty(result.type_name, "门诊");
                SpecimenOrderRow order;
                order.barcode = col_txm;
                order.room_code = col_room_code;
                order.order_text = col_order_text;
                order.sample_name = col_sample_name;
                order.fee = col_fee;
                order.request_time = col_request_time;
                fill_if_empty(result.room_code, order.room_code);
                fill_if_empty(result.fee, order.fee);
                fill_if_empty(result.collection_time, col_collection_time);
                if (should_add_supplemental_orders(result) &&
                    (!trim(order.order_text).empty() || !trim(order.sample_name).empty())) {
                    add_unique_order(result.orders, order);
                }
            }
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        }
    }

    {
        std::ostringstream sql;
        sql << "SELECT TOP 50 "
            << "isnull(LTRIM(RTRIM(TXM)),''),"
            << "isnull(LTRIM(RTRIM(SQNR)),''),"
            << "isnull(LTRIM(RTRIM(BBMC)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),JE))),''),"
            << "isnull(CONVERT(varchar(19),SQRQ,120),''),"
            << "isnull(CONVERT(varchar(19),BBCJSJ,120),''),"
            << "isnull(CONVERT(varchar(19),JSSJ,120),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(20),JSKS))),'')"
            << " FROM YJ_ZYSQ WITH (NOLOCK)"
            << " WHERE TXM='" << escaped << "'"
            << " AND (BSCBZ IS NULL OR BSCBZ=0)"
            << " ORDER BY YJSQID";
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (exec_optional_query(db.dbc, sql.str(), stmt, log)) {
            while (SQLFetch(stmt) == SQL_SUCCESS) {
                const auto col_txm = fetch_column(stmt, 1);
                const auto col_order_text = fetch_column(stmt, 2);
                const auto col_sample_name = fetch_column(stmt, 3);
                const auto col_fee = fetch_column(stmt, 4);
                const auto col_request_time = fetch_column(stmt, 5);
                const auto col_collection_time = fetch_column(stmt, 6);
                const auto col_signed_time = fetch_column(stmt, 7);
                const auto col_room_code = fetch_column(stmt, 8);

                result.has_inpatient_rows = true;
                fill_if_empty(result.barcode, col_txm);
                fill_if_empty(result.type_name, "住院");
                SpecimenOrderRow order;
                order.barcode = col_txm;
                order.order_text = col_order_text;
                order.sample_name = col_sample_name;
                order.fee = col_fee;
                order.request_time = col_request_time;
                fill_if_empty(result.fee, order.fee);
                fill_if_empty(result.collection_time, col_collection_time);
                fill_if_empty(result.signed_time, col_signed_time);
                order.room_code = col_room_code;
                fill_if_empty(result.room_code, order.room_code);
                if (should_add_supplemental_orders(result) &&
                    (!trim(order.order_text).empty() || !trim(order.sample_name).empty())) {
                    add_unique_order(result.orders, order);
                }
            }
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        }
    }

    error.clear();
    return true;
#endif
}

bool query_hiv_statistics(const HivStatQuery& query, HivStatSummary& summary, std::vector<HivStatDetailRow>& rows, std::string& error, LogFn log) {
    summary = HivStatSummary{};
    rows.clear();
#ifndef _WIN32
    (void)query;
    (void)log;
    error = "query_hiv_statistics is only available on Windows";
    return false;
#else
    if (query.year < 1900 || query.year > 9999 || query.month < 1 || query.month > 12) {
        error = "invalid year or month";
        return false;
    }

    char start_date[16]{};
    char end_date[16]{};
    const int next_year = query.month == 12 ? query.year + 1 : query.year;
    const int next_month = query.month == 12 ? 1 : query.month + 1;
    sprintf_s(start_date, "%04d-%02d-01", query.year, query.month);
    sprintf_s(end_date, "%04d-%02d-01", next_year, next_month);
    const std::string lab_department = trim(query.lab_department);

    DbContext db;
    if (!connect(query.connection_string, db, error, log)) {
        return false;
    }

    auto load_lookup = [&](const std::string& lookup_sql, std::map<std::string, std::string>& out) {
        if (log) log("exec sql: " + lookup_sql + "\n");
        SQLHSTMT lookup_stmt = SQL_NULL_HSTMT;
        if (!exec_query(db.dbc, lookup_sql, lookup_stmt, error)) {
            return false;
        }
        while (SQLFetch(lookup_stmt) == SQL_SUCCESS) {
            const std::string code = fetch_column(lookup_stmt, 1);
            const std::string name = fetch_column(lookup_stmt, 2);
            if (!code.empty()) {
                out[code] = name;
            }
        }
        SQLFreeHandle(SQL_HANDLE_STMT, lookup_stmt);
        return true;
    };

    std::map<std::string, std::string> machine_names;
    std::map<std::string, std::string> patient_type_names;
    std::map<std::string, std::string> dept_names;

    if (!load_lookup(
            "SELECT isnull(LTRIM(RTRIM(CONVERT(varchar(20),MACH_CODE))),''),"
            " isnull(LTRIM(RTRIM(MACH_NAME)),'')"
            " FROM LS_AS_MACHINE WITH (NOLOCK)"
            " WHERE isnull(DELETE_BIT,0)=0",
            machine_names) ||
        !load_lookup(
            "SELECT isnull(LTRIM(RTRIM(TYPE)),''),"
            " isnull(LTRIM(RTRIM(TYPE_NAME)),'')"
            " FROM LS_AS_PATTYPE WITH (NOLOCK)"
            " WHERE isnull(DELETE_BIT,0)=0",
            patient_type_names) ||
        !load_lookup(
            "SELECT isnull(LTRIM(RTRIM(CONVERT(varchar(20),DEPT_ID))),''),"
            " isnull(LTRIM(RTRIM(NAME)),'')"
            " FROM JC_DEPT_PROPERTY WITH (NOLOCK)"
            " WHERE isnull(DELETED,0)=0",
            dept_names)) {
        return false;
    }

    const auto lookup_name = [](const std::map<std::string, std::string>& values,
                                const std::string& code,
                                const std::string& fallback) {
        const auto it = values.find(code);
        if (it != values.end() && !trim(it->second).empty()) {
            return it->second;
        }
        return fallback;
    };

    const auto lab_department_for_dept = [](const std::string& dept_name) {
        return contains_text(dept_name, "滨水新城") ? std::string("新院") : std::string("老院");
    };

    std::vector<std::string> lab_department_dept_codes;
    std::vector<std::string> new_lab_department_dept_codes;
    if (lab_department == "新院" || lab_department == "老院") {
        for (const auto& [dept_code, dept_name] : dept_names) {
            if (!std::all_of(dept_code.begin(), dept_code.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
                continue;
            }
            if (lab_department_for_dept(dept_name) == "新院") {
                new_lab_department_dept_codes.push_back(dept_code);
            }
            if (lab_department_for_dept(dept_name) == lab_department) {
                lab_department_dept_codes.push_back(dept_code);
            }
        }
    }

    const auto append_dept_code_filter = [&](std::ostringstream& target) {
        if (lab_department != "新院" && lab_department != "老院") {
            return;
        }
        if (lab_department == "老院") {
            if (new_lab_department_dept_codes.empty()) {
                return;
            }
            target << " AND (NULLIF(LTRIM(RTRIM(CONVERT(varchar(20),r.DEPT_CODE))),'') IS NULL"
                   << " OR r.DEPT_CODE NOT IN (";
            for (size_t i = 0; i < new_lab_department_dept_codes.size(); ++i) {
                if (i > 0) {
                    target << ",";
                }
                target << new_lab_department_dept_codes[i];
            }
            target << "))";
            return;
        }
        if (lab_department_dept_codes.empty()) {
            target << " AND 1=0";
            return;
        }
        target << " AND r.DEPT_CODE IN (";
        for (size_t i = 0; i < lab_department_dept_codes.size(); ++i) {
            if (i > 0) {
                target << ",";
            }
            target << lab_department_dept_codes[i];
        }
        target << ")";
    };

    struct HivReportCandidate {
        std::string rep_no;
        std::string mach_code;
        std::string txm_no;
        std::string oper_no;
        std::string patient_no;
        std::string name;
        std::string patient_type_code;
        std::string dept_code;
        std::string room_code;
        std::string report_time;
    };

    std::ostringstream report_sql;
    auto append_report_branch = [&](int mach_code, bool first) {
        if (!first) {
            report_sql << " UNION ALL ";
        }
        report_sql << " SELECT"
            << " CONVERT(varchar(30),r.REP_NO),"
            << " CONVERT(varchar(20),r.MACH_CODE),"
            << " isnull(LTRIM(RTRIM(r.TXM_NO)),'') AS TXM_NO,"
            << " isnull(LTRIM(RTRIM(r.OPER_NO)),'') AS OPER_NO,"
            << " isnull(LTRIM(RTRIM(r.REG_NO)),'') AS PATIENT_NO,"
            << " isnull(LTRIM(RTRIM(r.NAME)),'') AS NAME,"
            << " isnull(LTRIM(RTRIM(CONVERT(varchar(20),r.TYPE))),'') AS PAT_TYPE_CODE,"
            << " isnull(LTRIM(RTRIM(CONVERT(varchar(20),r.DEPT_CODE))),'') AS DEPT_CODE,"
            << " isnull(LTRIM(RTRIM(CONVERT(varchar(20),r.ROOM_CODE))),'') AS ROOM_CODE,"
            << " isnull(CONVERT(varchar(19),r.REP_TIME,120),'') AS REP_TIME_TEXT"
            << " FROM LS_AS_REPORT r WITH (NOLOCK)"
            << " WHERE isnull(r.DELETE_BIT,0)=0"
            << " AND r.REP_TIME>='" << start_date << "'"
            << " AND r.REP_TIME<'" << end_date << "'"
            << " AND r.CHK_FLAG='T'"
            << " AND r.CONF='S'"
            << " AND r.MACH_CODE=" << mach_code
            << " AND NULLIF(LTRIM(RTRIM(r.NAME)),'') IS NOT NULL";
        append_dept_code_filter(report_sql);
    };

    append_report_branch(4005, true);
    append_report_branch(914, false);
    append_report_branch(4008, false);
    report_sql << " ORDER BY 2,1";

    if (log) log("exec sql: " + report_sql.str() + "\n");

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, report_sql.str(), stmt, error)) {
        return false;
    }

    std::map<std::string, HivReportCandidate> reports_by_rep_no;
    std::vector<std::string> rep_nos;
    while (SQLFetch(stmt) == SQL_SUCCESS) {
        HivReportCandidate report;
        report.rep_no = fetch_column(stmt, 1);
        report.mach_code = fetch_column(stmt, 2);
        report.txm_no = fetch_column(stmt, 3);
        report.oper_no = fetch_column(stmt, 4);
        report.patient_no = fetch_column(stmt, 5);
        report.name = fetch_column(stmt, 6);
        report.patient_type_code = fetch_column(stmt, 7);
        report.dept_code = fetch_column(stmt, 8);
        report.room_code = fetch_column(stmt, 9);
        report.report_time = fetch_column(stmt, 10);
        if (report.rep_no.empty() || reports_by_rep_no.find(report.rep_no) != reports_by_rep_no.end()) {
            continue;
        }
        rep_nos.push_back(report.rep_no);
        reports_by_rep_no[report.rep_no] = std::move(report);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);

    const auto expected_hiv_item_code = [](const std::string& mach_code) -> const char* {
        if (mach_code == "4005") return "91593";
        if (mach_code == "914") return "93053";
        if (mach_code == "4008") return "91442";
        return "";
    };
    const auto hiv_methodology = [](const std::string& mach_code) -> const char* {
        if (mach_code == "4005" || mach_code == "914") return "化学发光法";
        if (mach_code == "4008") return "酶免法";
        return "";
    };
    const auto max_text = [](const std::string& a, const std::string& b) {
        if (a.empty()) return b;
        if (b.empty()) return a;
        return (std::max)(a, b);
    };
    const auto padded_number = [](std::string value) {
        value = trim(value);
        if (value.size() >= 20) {
            return value;
        }
        return std::string(20 - value.size(), '0') + value;
    };
    const auto grouped_key = [&](const HivReportCandidate& report, const std::string& item_code) {
        return padded_number(report.mach_code) + "\x1f" + padded_number(item_code) + "\x1f" + padded_number(report.rep_no);
    };

    std::map<std::string, HivStatDetailRow> grouped_rows;
    constexpr size_t REP_NO_BATCH_SIZE = 500;
    for (size_t offset = 0; offset < rep_nos.size(); offset += REP_NO_BATCH_SIZE) {
        const size_t end = (std::min)(rep_nos.size(), offset + REP_NO_BATCH_SIZE);
        std::ostringstream entry_sql;
        entry_sql
            << "SELECT"
            << " CONVERT(varchar(30),e.REP_NO),"
            << " CONVERT(varchar(20),e.ITEM_CODE),"
            << " isnull(LTRIM(RTRIM(e.ITEM_NAME)),''),"
            << " isnull(LTRIM(RTRIM(e.RESULT)),''),"
            << " isnull(LTRIM(RTRIM(e.UPBOUND)),''),"
            << " isnull(LTRIM(RTRIM(e.DOWNBOUND)),'')"
            << " FROM LS_AS_REPENTRY e WITH (NOLOCK)"
            << " WHERE isnull(e.DELETE_BIT,0)=0"
            << " AND e.ITEM_CODE IN (91593,93053,91442)"
            << " AND e.REP_NO IN (";
        bool has_rep_no = false;
        for (size_t i = offset; i < end; ++i) {
            const std::string rep_no = trim(rep_nos[i]);
            if (!std::all_of(rep_no.begin(), rep_no.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
                continue;
            }
            if (has_rep_no) {
                entry_sql << ",";
            }
            entry_sql << rep_no;
            has_rep_no = true;
        }
        if (!has_rep_no) {
            continue;
        }
        entry_sql << ") ORDER BY e.REP_NO,e.ITEM_CODE";

        if (log) log("exec sql: " + entry_sql.str() + "\n");

        SQLHSTMT entry_stmt = SQL_NULL_HSTMT;
        if (!exec_query(db.dbc, entry_sql.str(), entry_stmt, error)) {
            return false;
        }

        while (SQLFetch(entry_stmt) == SQL_SUCCESS) {
            const std::string rep_no = fetch_column(entry_stmt, 1);
            const std::string item_code = fetch_column(entry_stmt, 2);
            const auto report_it = reports_by_rep_no.find(rep_no);
            if (report_it == reports_by_rep_no.end()) {
                continue;
            }
            const HivReportCandidate& report = report_it->second;
            if (item_code != expected_hiv_item_code(report.mach_code)) {
                continue;
            }

            HivStatDetailRow& row = grouped_rows[grouped_key(report, item_code)];
            if (row.rep_no.empty()) {
                row.mach_code = report.mach_code;
                row.machine_name = lookup_name(machine_names, report.mach_code, report.mach_code);
                row.methodology = hiv_methodology(report.mach_code);
                row.lab_department = lab_department_for_dept(lookup_name(dept_names, report.dept_code, report.dept_code));
                row.room_code = report.room_code;
                row.item_code = item_code;
                row.rep_no = report.rep_no;
                row.txm_no = report.txm_no;
                row.oper_no = report.oper_no;
                row.patient_no = report.patient_no;
                row.name = report.name;
                row.patient_type = lookup_name(patient_type_names, report.patient_type_code, report.patient_type_code);
                row.dept_name = lookup_name(dept_names, report.dept_code, report.dept_code);
                row.report_time = report.report_time;
                row.positive = "否";
            }

            const std::string item_name = fetch_column(entry_stmt, 3);
            const std::string result = fetch_column(entry_stmt, 4);
            const std::string lower_bound = fetch_column(entry_stmt, 5);
            const std::string upper_bound = fetch_column(entry_stmt, 6);
            row.item_name = max_text(row.item_name, item_name);
            row.result = max_text(row.result, result);
            row.lower_bound = max_text(row.lower_bound, lower_bound);
            row.upper_bound = max_text(row.upper_bound, upper_bound);
            if (contains_text(result, "待确认") || contains_text(result, "阳性") || contains_text(result, "+")) {
                row.positive = "是";
            }
        }

        SQLFreeHandle(SQL_HANDLE_STMT, entry_stmt);
    }

    for (auto& [_, row] : grouped_rows) {
        ++summary.screening_count;
        const bool is_positive = trim(row.positive) == "是";
        if (is_positive) {
            ++summary.positive_count;
        }
        if (is_hiv_sti_clinic_dept(row.dept_name)) {
            add_count(summary.sti_clinic_screening_count, summary.sti_clinic_positive_count, is_positive);
        }
        if (is_hiv_other_visit_dept(row.dept_name)) {
            add_count(summary.other_visit_screening_count, summary.other_visit_positive_count, is_positive);
        }
        if (is_hiv_prenatal_dept(row.dept_name)) {
            add_count(summary.prenatal_screening_count, summary.prenatal_positive_count, is_positive);
        }
        rows.push_back(std::move(row));
    }

    if (!rows.empty()) {
        std::ostringstream completed_apply_sql;
        completed_apply_sql
            << ";WITH completed_apply_distinct AS ("
            << " SELECT DISTINCT"
            << " LTRIM(RTRIM(a.Patient_NO)) AS PATIENT_NO,"
            << " LTRIM(RTRIM(a.ApplyFormNO)) AS ApplyFormNO"
            << " FROM LS_XK_BloodRequestApply a WITH (NOLOCK)"
            << " WHERE isnull(a.Delete_Bit,0)=0"
            << " AND a.Apply_Time>='" << start_date << "'"
            << " AND a.Apply_Time<'" << end_date << "'"
            << " AND LTRIM(RTRIM(a.ApplyForm_Statue))='已完结'"
            << " AND NULLIF(LTRIM(RTRIM(a.Patient_NO)),'') IS NOT NULL"
            << " AND NULLIF(LTRIM(RTRIM(a.ApplyFormNO)),'') IS NOT NULL"
            << "), completed_apply_forms AS ("
            << " SELECT bd.PATIENT_NO,"
            << " STUFF(("
            << " SELECT ';' + x.ApplyFormNO"
            << " FROM completed_apply_distinct x"
            << " WHERE x.PATIENT_NO=bd.PATIENT_NO"
            << " ORDER BY x.ApplyFormNO"
            << " FOR XML PATH(''),TYPE).value('.','varchar(max)'),1,1,'') AS COMPLETED_APPLY_FORMS"
            << " FROM completed_apply_distinct bd"
            << " GROUP BY bd.PATIENT_NO"
            << ")"
            << " SELECT PATIENT_NO,COMPLETED_APPLY_FORMS FROM completed_apply_forms";

        if (log) log("exec sql: " + completed_apply_sql.str() + "\n");

        SQLHSTMT completed_apply_stmt = SQL_NULL_HSTMT;
        if (!exec_query(db.dbc, completed_apply_sql.str(), completed_apply_stmt, error)) {
            return false;
        }

        std::map<std::string, std::string> completed_forms_by_patient;
        while (SQLFetch(completed_apply_stmt) == SQL_SUCCESS) {
            const std::string patient_no = fetch_column(completed_apply_stmt, 1);
            completed_forms_by_patient[patient_no] = fetch_column(completed_apply_stmt, 2);
        }
        SQLFreeHandle(SQL_HANDLE_STMT, completed_apply_stmt);

        for (auto& row : rows) {
            const auto it = completed_forms_by_patient.find(row.patient_no);
            if (it != completed_forms_by_patient.end()) {
                row.completed_blood_apply_forms = it->second;
            }
        }
    }

    const auto hiv_sample_source = [](const HivStatDetailRow& row) {
        if (!trim(row.completed_blood_apply_forms).empty()) {
            return std::string("受血（制品）前检测");
        }
        if (is_hiv_sti_clinic_dept(row.dept_name)) {
            return std::string("性病门诊");
        }
        if (is_hiv_other_visit_dept(row.dept_name)) {
            return std::string("其他就诊检测");
        }
        if (is_hiv_prenatal_dept(row.dept_name)) {
            return std::string("孕产期检查");
        }
        return std::string("术前检测");
    };

    for (auto& row : rows) {
        row.sample_source = hiv_sample_source(row);
    }

    for (const auto& row : rows) {
        const bool is_positive = trim(row.positive) == "是";
        if (row.methodology == "化学发光法") {
            add_count(summary.chemiluminescence_screening_count,
                      summary.chemiluminescence_positive_count,
                      is_positive);
        } else if (row.methodology == "酶免法") {
            add_count(summary.elisa_screening_count,
                      summary.elisa_positive_count,
                      is_positive);
        }
        if (!trim(row.completed_blood_apply_forms).empty()) {
            add_count(summary.transfusion_screening_count,
                      summary.transfusion_positive_count,
                      is_positive);
        }
    }

    const int classified_screening_count =
        summary.transfusion_screening_count +
        summary.sti_clinic_screening_count +
        summary.other_visit_screening_count +
        summary.prenatal_screening_count;
    const int classified_positive_count =
        summary.transfusion_positive_count +
        summary.sti_clinic_positive_count +
        summary.other_visit_positive_count +
        summary.prenatal_positive_count;
    summary.preoperative_screening_count =
        non_negative_difference(summary.screening_count, classified_screening_count);
    summary.preoperative_positive_count =
        non_negative_difference(summary.positive_count, classified_positive_count);

    error.clear();
    return true;
#endif
}

bool query_emergency_statistics(const EmergencyStatQuery& query, EmergencyStatSummary& summary, std::vector<EmergencyStatDetailRow>& rows, std::string& error, LogFn log) {
    summary = EmergencyStatSummary{};
    rows.clear();
#ifndef _WIN32
    (void)query;
    (void)log;
    error = "query_emergency_statistics is only available on Windows";
    return false;
#else
    const std::string start_time = trim(query.start_time);
    const std::string end_time = trim(query.end_time);
    if (start_time.empty() || end_time.empty()) {
        error = "start_time and end_time are required";
        return false;
    }

    const std::string time_field = trim(query.time_field);
    const char* date_column = time_field == "Apply" ? "b.REQ_TIME" : "b.IN_DATE";
    const std::string lab_department = trim(query.lab_department);

    DbContext db;
    if (!connect(query.connection_string, db, error, log)) {
        return false;
    }

    // Load machine name lookup once to avoid LS_AS_MACHINE JOIN in the main query
    std::map<std::string, std::string> machine_names;
    {
        std::ostringstream mSql;
        mSql << "SELECT isnull(LTRIM(RTRIM(CONVERT(varchar(20),MACH_CODE))),''),"
             << " isnull(LTRIM(RTRIM(MACH_NAME)),'')"
             << " FROM LS_AS_MACHINE WITH (NOLOCK)"
             << " WHERE isnull(DELETE_BIT,0)=0";
        if (log) log("exec sql: " + mSql.str() + "\n");
        SQLHSTMT mStmt = SQL_NULL_HSTMT;
        if (exec_query(db.dbc, mSql.str(), mStmt, error)) {
            while (SQLFetch(mStmt) == SQL_SUCCESS) {
                const std::string code = fetch_column(mStmt, 1);
                const std::string name = fetch_column(mStmt, 2);
                if (!code.empty()) machine_names[code] = name;
            }
            SQLFreeHandle(SQL_HANDLE_STMT, mStmt);
        }
    }

    std::ostringstream sql;
    sql
        << "SELECT"
        << " LTRIM(RTRIM(b.BARCODE)) AS BARCODE,"
        << " CAST(CASE WHEN isnull(b.JZ_FLAG,0)=1 THEN 1 ELSE 0 END AS varchar(10)) AS BARCODE_EMERGENCY,"
        << " CAST(CASE WHEN r.assaypat_type='0' THEN 1 ELSE 0 END AS varchar(10)) AS REPORT_EMERGENCY,"
        << " isnull(CONVERT(varchar(19),b.IN_DATE,120),'') AS IN_DATE,"
        << " isnull(CONVERT(varchar(19),r.REP_DATE,120),'') AS REQ_TIME,"
        << " isnull(CONVERT(varchar(20),b.OPER_STATE),'') AS OPER_STATE,"
        << " CAST(CASE WHEN b.CANCEL_DATE IS NOT NULL THEN 1 ELSE 0 END AS varchar(10)) AS HAS_CANCEL,"
        << " isnull(LTRIM(RTRIM(b.REG_NO)),'') AS REG_NO,"
        << " isnull(LTRIM(RTRIM(b.TYPENAME)),'') AS TYPE_NAME,"
        << " isnull(LTRIM(RTRIM(b.NAME)),'') AS NAME,"
        << " isnull(LTRIM(RTRIM(b.SEX)),'') AS SEX,"
        << " isnull(LTRIM(RTRIM(b.AGE)),'') AS AGE,"
        << " isnull(LTRIM(RTRIM(b.DEPT_NAME)),'') AS DEPT_NAME,"
        << " isnull(LTRIM(RTRIM(b.BEDNO)),'') AS BED_NO,"
        << " isnull(LTRIM(RTRIM(b.OPER_CODE)),'') AS SIGN_OPER,"
        << " isnull(LTRIM(RTRIM(CONVERT(varchar(20),b.sign_dept))),'') AS SIGN_DEPT,"
        << " isnull(LTRIM(RTRIM(b.SAMP_NAME)),'') AS SAMPLE_NAME,"
        << " isnull(LTRIM(RTRIM(b.ORDER_TEXT)),'') AS ORDER_TEXT,"
        << " isnull(CONVERT(varchar(20),r.REP_NO),'') AS REP_NO,"
        << " isnull(LTRIM(RTRIM(r.OPER_NO)),'') AS OPER_NO,"
        << " isnull(CONVERT(varchar(20),r.MACH_CODE),'') AS MACH_CODE,"
        << " isnull(CONVERT(varchar(19),r.CHK_DATE,120),'') AS INSPECT_DATE,"
        << " isnull(CONVERT(varchar(20),r.ROOM_CODE),'') AS ROOM_CODE,"
        << " isnull(r.CHK_FLAG,'') AS CHK_FLAG,"
        << " isnull(r.CONF,'') AS CONF,"
        << " CAST(CASE WHEN LTRIM(RTRIM(isnull(r.CHK_FLAG,'')))='T' THEN 1 ELSE 0 END AS varchar(10)) AS REPORT_REVIEWED,"
        << " CAST(CASE WHEN LTRIM(RTRIM(isnull(r.CONF,'')))='S' THEN 1 ELSE 0 END AS varchar(10)) AS REPORT_SENT,"
        << " isnull(r.CREATE_TIME,'') AS CREATE_TIME,"
        << " isnull(CONVERT(varchar(19),r.REP_TIME,120),'') AS REVIEW_TIME,"
        << " isnull(CONVERT(varchar(19),r.REP_TIME,120),'') AS REP_TIME"
        << " FROM LS_AS_BARCODE b WITH (NOLOCK)"
        << " LEFT JOIN LS_AS_REPORT r WITH (NOLOCK)"
        << " ON r.TXM_NO=b.BARCODE AND isnull(r.DELETE_BIT,0)=0"
        << " WHERE isnull(b.DELETE_BIT,0)=0"
        << " AND NULLIF(LTRIM(RTRIM(b.BARCODE)),'') IS NOT NULL"
        << " AND " << date_column << ">='" << sql_escape(start_time) << "'"
        << " AND " << date_column << "<DATEADD(minute,1,'" << sql_escape(end_time) << "')"
        << " AND b.CANCEL_DATE IS NULL"
        << " AND (r.assaypat_type='0' OR EXISTS ("
        << " SELECT 1 FROM LS_AS_BARCODE be WITH (NOLOCK)"
        << " WHERE isnull(be.DELETE_BIT,0)=0 AND be.BARCODE=b.BARCODE AND isnull(be.JZ_FLAG,0)=1"
        << " ))"
        << " ORDER BY LTRIM(RTRIM(b.BARCODE)), b.ID";

    if (log) {
        log("exec sql: " + sql.str() + "\n");
    }

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        return false;
    }

    const auto to_int = [](const std::string& value, int fallback = 0) {
        const std::string text = trim(value);
        if (text.empty()) {
            return fallback;
        }
        return std::atoi(text.c_str());
    };
    const auto barcode_status = [](int state, bool report_sent) {
        switch (state) {
            case 0: return std::string("未上机");
            case 1: return std::string("已上机未审核");
            case 2: return report_sent ? std::string("审核完成已发送") : std::string("审核完成未发送");
            case 3: return std::string("医生已查看");
            default: return std::string("未知");
        }
    };
    const auto effective_state = [](int barcode_state, bool has_report, bool report_reviewed) {
        if (report_reviewed && barcode_state == 3) return 3;
        if (report_reviewed) return 2;
        if (has_report || barcode_state >= 1) return 1;
        if (barcode_state == 0) return 0;
        return -1;
    };
    const auto derived_lab_department = [](const std::string& sign_dept, const std::string& dept_name) {
        const std::string dept = trim(sign_dept);
        if (dept == "102") return std::string("老院");
        if (dept == "401") return std::string("新院");
        return contains_text(dept_name, "滨水") ? std::string("新院") : std::string("老院");
    };
    struct AggregatedEmergencyRow {
        EmergencyStatDetailRow row;
        int barcode_oper_state = -1;
        bool has_cancel = false;
        bool barcode_emergency = false;
        bool report_emergency = false;
        bool report_reviewed = false;
        bool report_sent = false;
        std::set<std::string> order_texts;
    };
    std::vector<AggregatedEmergencyRow> aggregated;
    std::map<std::string, size_t> barcode_index;
    const auto assign_if_not_empty = [](std::string& target, const std::string& value) {
        if (!trim(value).empty()) target = value;
    };
    while (SQLFetch(stmt) == SQL_SUCCESS) {
        const std::string barcode = fetch_column(stmt, 1);
        if (trim(barcode).empty()) continue;
        auto found = barcode_index.find(barcode);
        if (found == barcode_index.end()) {
            barcode_index[barcode] = aggregated.size();
            aggregated.push_back(AggregatedEmergencyRow{});
            found = barcode_index.find(barcode);
            aggregated.back().row.barcode = barcode;
        }
        auto& agg = aggregated[found->second];
        auto& row = agg.row;
        const bool barcode_emergency = to_int(fetch_column(stmt, 2)) == 1;
        const bool report_emergency = to_int(fetch_column(stmt, 3)) == 1;
        agg.barcode_emergency = agg.barcode_emergency || barcode_emergency;
        agg.report_emergency = agg.report_emergency || report_emergency;
        const std::string in_date = fetch_column(stmt, 4);
        if (!trim(in_date).empty() && (trim(row.in_date).empty() || in_date < row.in_date)) {
            row.in_date = in_date;
        }
        assign_if_not_empty(row.req_time, fetch_column(stmt, 5));
        const int barcode_oper_state = to_int(fetch_column(stmt, 6), -1);
        if (barcode_oper_state >= 0 && (agg.barcode_oper_state < 0 || barcode_oper_state < agg.barcode_oper_state)) {
            agg.barcode_oper_state = barcode_oper_state;
        }
        agg.has_cancel = agg.has_cancel || to_int(fetch_column(stmt, 7)) == 1;
        assign_if_not_empty(row.reg_no, fetch_column(stmt, 8));
        assign_if_not_empty(row.type_name, fetch_column(stmt, 9));
        assign_if_not_empty(row.name, fetch_column(stmt, 10));
        assign_if_not_empty(row.sex, fetch_column(stmt, 11));
        assign_if_not_empty(row.age, fetch_column(stmt, 12));
        assign_if_not_empty(row.dept_name, fetch_column(stmt, 13));
        assign_if_not_empty(row.bed_code, fetch_column(stmt, 14));
        assign_if_not_empty(row.sign_oper, fetch_column(stmt, 15));
        assign_if_not_empty(row.sign_dept, fetch_column(stmt, 16));
        assign_if_not_empty(row.sample_name, fetch_column(stmt, 17));
        const std::string order_text = trim(fetch_column(stmt, 18));
        if (!order_text.empty() && agg.order_texts.insert(order_text).second) {
            if (!row.order_text.empty()) row.order_text += "/";
            row.order_text += order_text;
        }
        assign_if_not_empty(row.rep_no, fetch_column(stmt, 19));
        assign_if_not_empty(row.oper_no, fetch_column(stmt, 20));
        assign_if_not_empty(row.mach_code, fetch_column(stmt, 21));
        {
            const auto mit = machine_names.find(row.mach_code);
            row.mach_name = mit != machine_names.end() && !trim(mit->second).empty()
                ? mit->second : row.mach_code;
        }
        assign_if_not_empty(row.inspect_date, fetch_column(stmt, 22));
        assign_if_not_empty(row.room_code, fetch_column(stmt, 23));
        assign_if_not_empty(row.chk_flag, fetch_column(stmt, 24));
        assign_if_not_empty(row.conf, fetch_column(stmt, 25));
        const bool report_reviewed = to_int(fetch_column(stmt, 26)) == 1;
        const bool report_sent = to_int(fetch_column(stmt, 27)) == 1;
        agg.report_reviewed = agg.report_reviewed || report_reviewed;
        agg.report_sent = agg.report_sent || report_sent;
        assign_if_not_empty(row.create_time, fetch_column(stmt, 28));
        assign_if_not_empty(row.review_time, fetch_column(stmt, 29));
        assign_if_not_empty(row.rep_time, fetch_column(stmt, 30));
    }

    for (auto& agg : aggregated) {
        auto& row = agg.row;
        if (agg.barcode_emergency && agg.report_emergency) {
            row.emergency_source = "报告+条码";
        } else if (agg.barcode_emergency) {
            row.emergency_source = "条码急诊";
        } else {
            row.emergency_source = "报告急诊";
        }
        row.lab_department = derived_lab_department(row.sign_dept, row.dept_name);
        const bool has_report = !trim(row.rep_no).empty();
        row.min_oper_state = effective_state(agg.barcode_oper_state, has_report, agg.report_reviewed);
        row.wait_seconds = agg.report_reviewed ? seconds_between_sql_datetimes(row.in_date, row.review_time) : -1;
        row.wait_minutes = row.wait_seconds < 0 ? 0 : row.wait_seconds / 60;
        row.barcode_status = agg.has_cancel ? "取消签收" : barcode_status(row.min_oper_state, agg.report_sent);

        if ((lab_department == "老院" || lab_department == "新院") && row.lab_department != lab_department) {
            continue;
        }
        if (query.only_unfinished && agg.report_sent) {
            continue;
        }

        ++summary.emergency_barcode_count;
        if (row.min_oper_state == 0) ++summary.not_loaded_count;
        if (row.min_oper_state == 1) ++summary.loaded_not_reviewed_count;
        if (row.min_oper_state == 2) ++summary.reviewed_count;
        if (row.min_oper_state == 3) ++summary.doctor_viewed_count;
        if (agg.report_sent) ++summary.sent_count;
        if (!agg.report_sent) ++summary.unfinished_count;
        if (agg.barcode_emergency) ++summary.barcode_emergency_count;
        if (agg.report_emergency) ++summary.report_emergency_count;
        if (agg.barcode_emergency && agg.report_emergency) ++summary.both_emergency_count;

        rows.push_back(std::move(row));
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

void refresh_tat_statistics(const TatThresholds& thresholds,
                            const std::string& current_time,
                            TatStatSummary& summary,
                            std::vector<TatStatDetailRow>& rows) {
    summary = TatStatSummary{};
    const std::string now = trim(current_time).empty() ? local_datetime_text() : trim(current_time);
    const auto threshold_seconds = [](int minutes) -> long long {
        return static_cast<long long>((std::max)(1, minutes)) * 60LL;
    };
    for (auto& row : rows) {
        row.machine_waiting = trim(row.machine_time).empty();
        row.review_waiting = trim(row.review_time).empty();
        row.time_abnormal = false;

        const auto duration = [&row, &now](const std::string& start,
                                           const std::string& actual_end,
                                           bool waiting,
                                           long long& seconds,
                                           std::string& text) {
            seconds = -1;
            text.clear();
            if (trim(start).empty()) return;
            const std::string end = waiting ? now : actual_end;
            if (trim(end).empty()) return;
            seconds = sql_datetime_diff_seconds(start, end);
            if (seconds < 0) {
                row.time_abnormal = true;
                text = "时间异常";
                return;
            }
            text = format_duration_seconds_zh(seconds);
            if (waiting) text += "（等待中）";
        };

        duration(row.collection_time, row.receive_time, false,
                 row.collection_to_receive_seconds, row.collection_to_receive);
        duration(row.receive_time, row.machine_time, row.machine_waiting,
                 row.receive_to_machine_seconds, row.receive_to_machine);
        duration(row.receive_time, row.review_time, row.review_waiting,
                 row.receive_to_review_seconds, row.receive_to_review);
        duration(row.collection_time, row.review_time, row.review_waiting,
                 row.collection_to_review_seconds, row.collection_to_review);

        const bool overtime =
            (row.collection_to_receive_seconds >= 0 &&
             row.collection_to_receive_seconds > threshold_seconds(thresholds.collection_to_receive_minutes)) ||
            (row.receive_to_machine_seconds >= 0 &&
             row.receive_to_machine_seconds > threshold_seconds(thresholds.receive_to_machine_minutes)) ||
            (row.receive_to_review_seconds >= 0 &&
             row.receive_to_review_seconds > threshold_seconds(thresholds.receive_to_review_minutes)) ||
            (row.collection_to_review_seconds >= 0 &&
             row.collection_to_review_seconds > threshold_seconds(thresholds.collection_to_review_minutes));

        if (row.time_abnormal) {
            row.tat_status = "时间异常";
            ++summary.time_abnormal_count;
        } else if (overtime) {
            row.tat_status = "超时";
            ++summary.overtime_count;
        } else {
            row.tat_status = "正常";
            ++summary.normal_count;
        }
        if (row.machine_waiting) ++summary.waiting_machine_count;
        if (row.review_waiting) ++summary.waiting_review_count;
        ++summary.total_count;
    }
}

bool build_tat_statistics(const TatStatQuery& query,
                          const std::vector<TatStatRawRow>& raw_rows,
                          TatStatSummary& summary,
                          std::vector<TatStatDetailRow>& rows,
                          std::string& error) {
    summary = TatStatSummary{};
    rows.clear();
    std::unordered_map<std::string, size_t> indexes;
    std::vector<std::set<std::string>> order_sets;

    const auto assign_if_empty = [](std::string& target, const std::string& value) {
        if (trim(target).empty() && !trim(value).empty()) target = trim(value);
    };
    for (const auto& raw : raw_rows) {
        const std::string barcode = trim(raw.barcode);
        if (barcode.empty()) continue;
        auto found = indexes.find(barcode);
        if (found == indexes.end()) {
            TatStatDetailRow row;
            static_cast<TatStatRawRow&>(row) = raw;
            row.barcode = barcode;
            row.order_text.clear();
            rows.push_back(std::move(row));
            order_sets.emplace_back();
            const size_t index = rows.size() - 1;
            indexes[barcode] = index;
            found = indexes.find(barcode);
        }
        const size_t index = found->second;
        auto& row = rows[index];
        const std::string order = trim(raw.order_text);
        if (!order.empty() && order_sets[index].insert(order).second) {
            if (!row.order_text.empty()) row.order_text += "/";
            row.order_text += order;
        }
        assign_if_empty(row.patient_type, raw.patient_type);
        assign_if_empty(row.reg_no, raw.reg_no);
        assign_if_empty(row.name, raw.name);
        assign_if_empty(row.sex, raw.sex);
        assign_if_empty(row.diagnosis, raw.diagnosis);
        assign_if_empty(row.bed_no, raw.bed_no);
        assign_if_empty(row.age, raw.age);
        assign_if_empty(row.sample_name, raw.sample_name);
        assign_if_empty(row.department_name, raw.department_name);
        assign_if_empty(row.room_code, raw.room_code);
        assign_if_empty(row.room_name, raw.room_name);
        assign_if_empty(row.collection_time, raw.collection_time);
        assign_if_empty(row.receive_time, raw.receive_time);
        assign_if_empty(row.receiver, raw.receiver);
        assign_if_empty(row.report_no, raw.report_no);
        assign_if_empty(row.oper_no, raw.oper_no);
        assign_if_empty(row.machine_code, raw.machine_code);
        assign_if_empty(row.machine_name, raw.machine_name);
        assign_if_empty(row.inspect_date, raw.inspect_date);
        assign_if_empty(row.machine_time, raw.machine_time);
        assign_if_empty(row.review_time, raw.review_time);
        assign_if_empty(row.reviewer, raw.reviewer);
        assign_if_empty(row.chk_flag, raw.chk_flag);
        assign_if_empty(row.conf, raw.conf);
        if (raw.barcode_oper_state >= 0 &&
            (row.barcode_oper_state < 0 || raw.barcode_oper_state > row.barcode_oper_state)) {
            row.barcode_oper_state = raw.barcode_oper_state;
        }
        row.barcode_emergency = row.barcode_emergency || raw.barcode_emergency;
        row.report_emergency = row.report_emergency || raw.report_emergency;
        row.has_report = row.has_report || raw.has_report;
        row.report_reviewed = row.report_reviewed || raw.report_reviewed;
        row.report_sent = row.report_sent || raw.report_sent;
    }

    const std::string patient_type = trim(query.patient_type);
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const TatStatDetailRow& row) {
        if (query.emergency_only && !row.barcode_emergency && !row.report_emergency) return true;
        if (patient_type == "住院" && !contains_text(row.patient_type, "住院")) return true;
        if (patient_type == "门诊" && !contains_text(row.patient_type, "门诊")) return true;
        return false;
    }), rows.end());

    for (auto& row : rows) {
        if (row.report_sent) row.workflow_status = "发送完成";
        else if (row.report_reviewed) row.workflow_status = "已审核未发送";
        else if (row.has_report || row.barcode_oper_state >= 1) row.workflow_status = "已上机未审核";
        else row.workflow_status = "已签收未上机";
    }
    refresh_tat_statistics(query.thresholds, query.current_time, summary, rows);
    error.clear();
    return true;
}

bool query_tat_statistics(const TatStatQuery& query, TatStatSummary& summary,
                          std::vector<TatStatDetailRow>& rows,
                          std::string& error, LogFn log) {
    summary = TatStatSummary{};
    rows.clear();
#ifndef _WIN32
    (void)query;
    (void)log;
    error = "query_tat_statistics is only available on Windows";
    return false;
#else
    const std::string start_time = trim(query.start_time);
    const std::string end_time = trim(query.end_time);
    if (start_time.empty() || end_time.empty() || start_time > end_time) {
        error = "valid start_time and end_time are required";
        return false;
    }
    DbContext db;
    if (!connect(query.connection_string, db, error, log)) return false;

    std::shared_ptr<const EmployeeNameMap> employee_names;
    bool cache_hit = false;
    std::string employee_error;
    if (!load_barcode_employee_names(db.dbc, query.connection_string, employee_names,
                                     cache_hit, employee_error, log)) {
        if (log) log("tat employee dictionary unavailable: " + employee_error + "\n");
        employee_names = std::make_shared<const EmployeeNameMap>();
    }

    std::ostringstream where;
    where << " WHERE isnull(b.DELETE_BIT,0)=0"
          << " AND b.CANCEL_DATE IS NULL"
          << " AND b.IN_DATE>='" << sql_escape(start_time) << "'"
          << " AND b.IN_DATE<DATEADD(minute,1,'" << sql_escape(end_time) << "')"
          << " AND NULLIF(LTRIM(RTRIM(b.BARCODE)),'') IS NOT NULL";
    add_eq(where, "CONVERT(varchar(20),b.ROOM_CODE)", query.room_code);
    add_like(where, "b.DEPT_NAME", query.department_keyword);
    add_like(where, "b.ORDER_TEXT", query.order_keyword);

    std::ostringstream sql;
    sql << "SELECT"
        << " isnull(LTRIM(RTRIM(b.BARCODE)),''),"
        << " isnull(LTRIM(RTRIM(b.TYPENAME)),''),"
        << " isnull(LTRIM(RTRIM(b.REG_NO)),''),"
        << " isnull(LTRIM(RTRIM(b.NAME)),''),"
        << " isnull(LTRIM(RTRIM(b.SEX)),''),"
        << " isnull(LTRIM(RTRIM(rd.DIAG_NAME)),''),"
        << " isnull(LTRIM(RTRIM(b.BEDNO)),''),"
        << " isnull(LTRIM(RTRIM(b.AGE)),''),"
        << " isnull(LTRIM(RTRIM(b.SAMP_NAME)),''),"
        << " isnull(LTRIM(RTRIM(b.DEPT_NAME)),''),"
        << " isnull(CONVERT(varchar(20),COALESCE(rd.ROOM_CODE,b.ROOM_CODE)),''),"
        << " isnull(nullif(LTRIM(RTRIM(room.ROOM_NAME)),''),isnull(CONVERT(varchar(20),COALESCE(rd.ROOM_CODE,b.ROOM_CODE)),'')),"
        << " isnull(LTRIM(RTRIM(b.ORDER_TEXT)),''),"
        << " isnull(LTRIM(RTRIM(CONVERT(varchar(32),b.COLLECTION_TIME))),''),"
        << " isnull(CONVERT(varchar(19),b.IN_DATE,120),''),"
        << " isnull(LTRIM(RTRIM(CONVERT(varchar(50),b.OPER_CODE))),''),"
        << " isnull(CONVERT(varchar(30),rd.REP_NO),''),"
        << " isnull(LTRIM(RTRIM(rd.OPER_NO)),''),"
        << " isnull(CONVERT(varchar(20),rd.MACH_CODE),''),"
        << " isnull(nullif(LTRIM(RTRIM(rd.MACH_NAME)),''),isnull(CONVERT(varchar(20),rd.MACH_CODE),'')),"
        << " isnull(CONVERT(varchar(19),rd.CHK_DATE,120),''),"
        << " isnull(CONVERT(varchar(19),rd.CREATE_TIME,120),''),"
        << " isnull(CONVERT(varchar(19),rd.REP_TIME,120),''),"
        << " isnull(LTRIM(RTRIM(CONVERT(varchar(50),rd.REP_OPER))),''),"
        << " isnull(rd.CHK_FLAG,''),isnull(rd.CONF,''),"
        << " isnull(CONVERT(varchar(20),b.OPER_STATE),''),"
        << " CASE WHEN isnull(b.JZ_FLAG,0)=1 THEN '1' ELSE '0' END,"
        << " isnull(CONVERT(varchar(10),rs.REPORT_EMERGENCY),'0'),"
        << " isnull(CONVERT(varchar(10),rs.HAS_REPORT),'0'),"
        << " isnull(CONVERT(varchar(10),rs.REPORT_REVIEWED),'0'),"
        << " isnull(CONVERT(varchar(10),rs.REPORT_SENT),'0')"
        << " FROM LS_AS_BARCODE b WITH (NOLOCK)"
        << " OUTER APPLY (SELECT"
        << " MAX(CASE WHEN NULLIF(LTRIM(RTRIM(CONVERT(varchar(30),r.REP_NO))),'') IS NOT NULL THEN 1 ELSE 0 END) HAS_REPORT,"
        << " MAX(CASE WHEN r.assaypat_type='0' THEN 1 ELSE 0 END) REPORT_EMERGENCY,"
        << " MAX(CASE WHEN LTRIM(RTRIM(isnull(r.CHK_FLAG,'')))='T' THEN 1 ELSE 0 END) REPORT_REVIEWED,"
        << " MAX(CASE WHEN LTRIM(RTRIM(isnull(r.CONF,'')))='S' THEN 1 ELSE 0 END) REPORT_SENT"
        << " FROM LS_AS_REPORT r WITH (NOLOCK)"
        << " WHERE isnull(r.DELETE_BIT,0)=0 AND r.TXM_NO=b.BARCODE) rs"
        << " OUTER APPLY (SELECT TOP 1 r.REP_NO,r.OPER_NO,r.MACH_CODE,r.ROOM_CODE,"
        << " r.CHK_DATE,r.CREATE_TIME,r.REP_TIME,r.REP_OPER,r.CHK_FLAG,r.CONF,r.DIAG_NAME,"
        << " mach.MACH_NAME"
        << " FROM LS_AS_REPORT r WITH (NOLOCK)"
        << " LEFT JOIN LS_AS_MACHINE mach WITH (NOLOCK)"
        << " ON mach.MACH_CODE=r.MACH_CODE AND mach.ROOM_CODE=r.ROOM_CODE AND isnull(mach.DELETE_BIT,0)=0"
        << " WHERE isnull(r.DELETE_BIT,0)=0 AND r.TXM_NO=b.BARCODE"
        << " ORDER BY r.CHK_DATE DESC,r.REP_TIME DESC,r.REP_NO DESC) rd"
        << " LEFT JOIN LS_AS_ROOM room WITH (NOLOCK)"
        << " ON room.ROOM_CODE=COALESCE(rd.ROOM_CODE,b.ROOM_CODE) AND isnull(room.DELETE_BIT,0)=0"
        << where.str()
        << " ORDER BY b.IN_DATE,b.BARCODE,b.ID";
    if (log) log("exec sql: " + sql.str() + "\n");

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) return false;
    std::vector<TatStatRawRow> raw_rows;
    while (SQLFetch(stmt) == SQL_SUCCESS) {
        TatStatRawRow row;
        row.barcode = fetch_column(stmt, 1);
        row.patient_type = fetch_column(stmt, 2);
        row.reg_no = fetch_column(stmt, 3);
        row.name = fetch_column(stmt, 4);
        row.sex = fetch_column(stmt, 5);
        row.diagnosis = fetch_column(stmt, 6);
        row.bed_no = fetch_column(stmt, 7);
        row.age = fetch_column(stmt, 8);
        row.sample_name = fetch_column(stmt, 9);
        row.department_name = fetch_column(stmt, 10);
        row.room_code = fetch_column(stmt, 11);
        row.room_name = fetch_column(stmt, 12);
        row.order_text = fetch_column(stmt, 13);
        row.collection_time = fetch_column(stmt, 14);
        row.receive_time = fetch_column(stmt, 15);
        row.receiver = fetch_column(stmt, 16);
        row.report_no = fetch_column(stmt, 17);
        row.oper_no = fetch_column(stmt, 18);
        row.machine_code = fetch_column(stmt, 19);
        row.machine_name = fetch_column(stmt, 20);
        row.inspect_date = fetch_column(stmt, 21);
        row.machine_time = fetch_column(stmt, 22);
        row.review_time = fetch_column(stmt, 23);
        const std::string reviewer_code = fetch_column(stmt, 24);
        const auto reviewer = employee_names->find(trim(reviewer_code));
        row.reviewer = employee_display_name(
            reviewer_code, reviewer != employee_names->end() ? reviewer->second : "");
        row.chk_flag = fetch_column(stmt, 25);
        row.conf = fetch_column(stmt, 26);
        row.barcode_oper_state = std::atoi(fetch_column(stmt, 27).c_str());
        row.barcode_emergency = fetch_column(stmt, 28) == "1";
        row.report_emergency = fetch_column(stmt, 29) == "1";
        row.has_report = fetch_column(stmt, 30) == "1";
        row.report_reviewed = fetch_column(stmt, 31) == "1";
        row.report_sent = fetch_column(stmt, 32) == "1";
        raw_rows.push_back(std::move(row));
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return build_tat_statistics(query, raw_rows, summary, rows, error);
#endif
}

bool query_backup_blood_statistics(const BackupBloodStatQuery& query,
                                   BackupBloodStatSummary& summary,
                                   std::vector<BackupBloodStatDetailRow>& rows,
                                   std::string& error, LogFn log) {
    summary = BackupBloodStatSummary{};
    rows.clear();
#ifndef _WIN32
    (void)query;
    (void)log;
    error = "query_backup_blood_statistics is only available on Windows";
    return false;
#else
    constexpr const char* kBackupValue = "备血";
    const std::string start_date = trim(query.start_date);
    const std::string end_date = trim(query.end_date);
    if (start_date.empty() || end_date.empty()) {
        error = "start_date and end_date are required";
        return false;
    }
    if (start_date > end_date) {
        error = "start_date must not be later than end_date";
        return false;
    }

    DbContext db;
    if (!connect(query.connection_string, db, error, log)) {
        return false;
    }

    const std::string apply_status = trim(query.apply_status);
    const std::string requested_campus = trim(query.campus);
    const auto campus_for_dept = [](const std::string& apply_dept) {
        return contains_text(apply_dept, "滨水") ? std::string("新院") : std::string("老院");
    };
    const auto campus_matches = [&requested_campus](const std::string& campus) {
        return (requested_campus != "老院" && requested_campus != "新院") ||
               campus == requested_campus;
    };
    std::ostringstream sql;
    sql << "SELECT "
        << "isnull(LTRIM(RTRIM(a.ApplyFormNO)),''),"
        << "isnull(CONVERT(varchar(19),a.Apply_Time,120),''),"
        << "isnull(LTRIM(RTRIM(a.TranProperty)),''),"
        << "isnull(LTRIM(RTRIM(a.UseBloodNote)),''),"
        << "isnull(LTRIM(RTRIM(a.Apply_Purpose)),''),"
        << "isnull(LTRIM(RTRIM(a.ApplyForm_Statue)),''),"
        << "isnull(LTRIM(RTRIM(a.Patient_NO)),''),"
        << "isnull(LTRIM(RTRIM(a.Patient_Name)),''),"
        << "isnull(LTRIM(RTRIM(a.Apply_Dept)),''),"
        << "isnull(LTRIM(RTRIM(a.Apply_BedNo)),''),"
        << "CASE WHEN isnull(a.Delete_Bit,0)=1 THEN '1' ELSE '0' END"
        << " FROM LS_XK_BloodRequestApply a WITH (NOLOCK)"
        << " WHERE a.Apply_Time>='" << sql_escape(start_date) << "'"
        << " AND a.Apply_Time<DATEADD(day,1,'" << sql_escape(end_date) << "')"
        << " AND (LTRIM(RTRIM(isnull(a.TranProperty,'')))='" << sql_escape(kBackupValue) << "'"
        << " OR isnull(a.UseBloodNote,'') LIKE '%" << sql_escape(kBackupValue) << "%'"
        << " OR isnull(a.Apply_Purpose,'') LIKE '%" << sql_escape(kBackupValue) << "%')";
    if (!query.include_deleted) {
        sql << " AND isnull(a.Delete_Bit,0)=0"
            << " AND LTRIM(RTRIM(isnull(a.ApplyForm_Statue,'')))<>'已删除'";
    }
    if (!apply_status.empty() && apply_status != "全部") {
        sql << " AND LTRIM(RTRIM(isnull(a.ApplyForm_Statue,'')))='"
            << sql_escape(apply_status) << "'";
    }
    sql << " ORDER BY a.Apply_Time DESC,a.ApplyFormNO";

    if (log) log("exec sql: " + sql.str() + "\n");
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        return false;
    }

    std::map<std::string, size_t> apply_form_index;
    const auto fill_if_blank = [](std::string& target, const std::string& value) {
        if (trim(target).empty() && !trim(value).empty()) target = value;
    };
    while (SQLFetch(stmt) == SQL_SUCCESS) {
        const std::string apply_form_no = trim(fetch_column(stmt, 1));
        const std::string apply_time = fetch_column(stmt, 2);
        const std::string tran_property = trim(fetch_column(stmt, 3));
        const std::string use_blood_note = trim(fetch_column(stmt, 4));
        const std::string apply_purpose = trim(fetch_column(stmt, 5));
        const std::string status = fetch_column(stmt, 6);
        const std::string patient_no = fetch_column(stmt, 7);
        const std::string patient_name = fetch_column(stmt, 8);
        const std::string apply_dept = fetch_column(stmt, 9);
        const std::string bed_no = fetch_column(stmt, 10);
        const bool delete_bit = trim(fetch_column(stmt, 11)) == "1";
        const bool apply_type_match = tran_property == kBackupValue;
        const bool use_blood_note_match = use_blood_note.find(kBackupValue) != std::string::npos;
        const bool apply_purpose_match = apply_purpose.find(kBackupValue) != std::string::npos;

        if (apply_form_no.empty()) {
            if (campus_matches(campus_for_dept(apply_dept))) {
                ++summary.missing_apply_form_no_count;
            }
            continue;
        }

        auto found = apply_form_index.find(apply_form_no);
        if (found == apply_form_index.end()) {
            BackupBloodStatDetailRow row;
            row.apply_form_no = apply_form_no;
            row.apply_time = apply_time;
            row.tran_property = tran_property;
            row.use_blood_note = use_blood_note;
            row.apply_purpose = apply_purpose;
            row.apply_status = status;
            row.patient_no = patient_no;
            row.patient_name = patient_name;
            row.apply_dept = apply_dept;
            row.bed_no = bed_no;
            row.delete_bit = delete_bit;
            row.apply_type_match = apply_type_match;
            row.use_blood_note_match = use_blood_note_match;
            row.apply_purpose_match = apply_purpose_match;
            apply_form_index[apply_form_no] = rows.size();
            rows.push_back(std::move(row));
            continue;
        }

        auto& row = rows[found->second];
        row.apply_type_match = row.apply_type_match || apply_type_match;
        row.use_blood_note_match = row.use_blood_note_match || use_blood_note_match;
        row.apply_purpose_match = row.apply_purpose_match || apply_purpose_match;
        row.delete_bit = row.delete_bit || delete_bit;
        if (apply_type_match) row.tran_property = tran_property;
        if (use_blood_note_match) row.use_blood_note = use_blood_note;
        if (apply_purpose_match) row.apply_purpose = apply_purpose;
        if (delete_bit || status == "已删除") row.apply_status = status;
        fill_if_blank(row.apply_time, apply_time);
        fill_if_blank(row.apply_status, status);
        fill_if_blank(row.patient_no, patient_no);
        fill_if_blank(row.patient_name, patient_name);
        fill_if_blank(row.apply_dept, apply_dept);
        fill_if_blank(row.bed_no, bed_no);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);

    for (auto& row : rows) {
        row.campus = campus_for_dept(row.apply_dept);
    }
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const auto& row) {
        return !campus_matches(row.campus);
    }), rows.end());

    for (auto& row : rows) {
        row.match_source.clear();
        const auto append_source = [&row](const char* source) {
            if (!row.match_source.empty()) row.match_source += "/";
            row.match_source += source;
        };
        int match_count = 0;
        if (row.apply_type_match) { append_source("申请类型"); ++match_count; }
        if (row.use_blood_note_match) { append_source("用血备注"); ++match_count; }
        if (row.apply_purpose_match) { append_source("输血目的"); ++match_count; }
        if (match_count >= 2) ++summary.multiple_match_count;
        if (row.apply_type_match) ++summary.apply_type_count;
        if (row.use_blood_note_match) ++summary.use_blood_note_count;
        if (row.apply_purpose_match) ++summary.apply_purpose_count;
        const std::string status = trim(row.apply_status);
        const bool deleted = row.delete_bit || status == "已删除";
        if (deleted) ++summary.deleted_count;
        else if (status == "未审核") ++summary.unreviewed_count;
        else if (status == "已审核") ++summary.reviewed_count;
        else if (status == "已完结") ++summary.completed_count;
        else if (status == "已驳回") ++summary.rejected_count;
        else ++summary.other_status_count;
    }
    summary.total_count = static_cast<int>(rows.size());
    error.clear();
    return true;
#endif
}

namespace {

std::string transfusion_order_status(const std::string& status, bool deleted) {
    if (deleted || trim(status) == "已删除") return "已删除";
    const std::string value = trim(status);
    if (value == "已驳回" || value == "未审核" || value == "已审核" || value == "已完结") {
        return value;
    }
    return "其他状态";
}

int transfusion_order_status_priority(const std::string& status) {
    if (status == "已删除") return 6;
    if (status == "已驳回") return 5;
    if (status == "已完结") return 4;
    if (status == "已审核") return 3;
    if (status == "未审核") return 2;
    return 1;
}

}  // namespace

bool build_transfusion_order_statistics(
    const TransfusionOrderStatQuery& query,
    const std::vector<TransfusionOrderStatRawRow>& raw_rows,
    TransfusionOrderStatSummary& summary,
    std::vector<TransfusionOrderStatDetailRow>& rows,
    std::string& error) {
    summary = TransfusionOrderStatSummary{};
    rows.clear();

    const std::string requested_campus = trim(query.campus);
    const auto campus_for_dept = [](const std::string& apply_dept) {
        return contains_text(apply_dept, "滨水") ? std::string("新院") : std::string("老院");
    };
    const auto campus_matches = [&requested_campus](const std::string& campus) {
        return (requested_campus != "老院" && requested_campus != "新院") ||
               campus == requested_campus;
    };

    struct Aggregate {
        TransfusionOrderStatDetailRow row;
        std::set<std::string> statuses;
        std::set<std::string> patient_nos;
    };
    std::map<std::string, Aggregate> grouped;

    const auto fill_if_blank = [](std::string& target, const std::string& value) {
        if (trim(target).empty() && !trim(value).empty()) target = value;
    };

    for (const auto& raw : raw_rows) {
        const std::string apply_form_no = trim(raw.apply_form_no);
        if (apply_form_no.empty()) {
            if (campus_matches(campus_for_dept(raw.apply_dept))) {
                ++summary.missing_apply_form_no_count;
            }
            continue;
        }

        const std::string status = transfusion_order_status(raw.apply_status, raw.delete_bit);
        auto found = grouped.find(apply_form_no);
        if (found == grouped.end()) {
            Aggregate aggregate;
            aggregate.row.apply_form_no = apply_form_no;
            aggregate.row.apply_time = raw.apply_time;
            aggregate.row.apply_status = status;
            aggregate.row.patient_no = raw.patient_no;
            aggregate.row.patient_no_type = raw.patient_no_type;
            aggregate.row.patient_name = raw.patient_name;
            aggregate.row.apply_dept = raw.apply_dept;
            aggregate.row.apply_dept_id = raw.apply_dept_id;
            aggregate.row.bed_no = raw.bed_no;
            aggregate.row.apply_doctor = raw.apply_doctor;
            aggregate.row.tran_property = raw.tran_property;
            aggregate.row.delete_bit = raw.delete_bit || status == "已删除";
            aggregate.statuses.insert(status);
            if (!trim(raw.patient_no).empty()) aggregate.patient_nos.insert(trim(raw.patient_no));
            grouped.emplace(apply_form_no, std::move(aggregate));
            continue;
        }

        auto& aggregate = found->second;
        auto& row = aggregate.row;
        aggregate.statuses.insert(status);
        if (!trim(raw.patient_no).empty()) aggregate.patient_nos.insert(trim(raw.patient_no));
        if (transfusion_order_status_priority(status) >
            transfusion_order_status_priority(row.apply_status)) {
            row.apply_status = status;
        }
        row.delete_bit = row.delete_bit || raw.delete_bit || status == "已删除";
        if (raw.apply_time > row.apply_time) row.apply_time = raw.apply_time;
        fill_if_blank(row.patient_no, raw.patient_no);
        fill_if_blank(row.patient_no_type, raw.patient_no_type);
        fill_if_blank(row.patient_name, raw.patient_name);
        fill_if_blank(row.apply_dept, raw.apply_dept);
        fill_if_blank(row.apply_dept_id, raw.apply_dept_id);
        fill_if_blank(row.bed_no, raw.bed_no);
        fill_if_blank(row.apply_doctor, raw.apply_doctor);
        fill_if_blank(row.tran_property, raw.tran_property);
    }

    for (auto& entry : grouped) {
        auto& aggregate = entry.second;
        auto& row = aggregate.row;
        row.campus = campus_for_dept(row.apply_dept);
        if (!campus_matches(row.campus)) continue;

        const bool status_conflict = aggregate.statuses.size() > 1;
        const bool patient_conflict = aggregate.patient_nos.size() > 1;
        const bool missing_dept = trim(row.apply_dept).empty();
        const std::string status = transfusion_order_status(row.apply_status, row.delete_bit);

        if (status == "其他状态") {
            ++summary.other_status_count;
            continue;
        }
        if (status == "已驳回" && !query.include_rejected) continue;
        if (status == "已删除" && !query.include_deleted) continue;

        const auto append_data_status = [&row](const char* value) {
            if (!row.data_status.empty()) row.data_status += "/";
            row.data_status += value;
        };
        if (status_conflict) append_data_status("状态冲突");
        if (patient_conflict) append_data_status("病人号冲突");
        if (missing_dept) append_data_status("申请科室为空");
        if (row.data_status.empty()) row.data_status = "正常";
        if (status_conflict) ++summary.conflict_count;

        row.apply_status = status;
        ++summary.total_count;
        if (status == "未审核") ++summary.unreviewed_count;
        else if (status == "已审核") ++summary.reviewed_count;
        else if (status == "已完结") ++summary.completed_count;
        else if (status == "已驳回") ++summary.rejected_count;
        else if (status == "已删除") ++summary.deleted_count;
        rows.push_back(std::move(row));
    }

    std::stable_sort(rows.begin(), rows.end(), [](const auto& left, const auto& right) {
        if (left.apply_time != right.apply_time) return left.apply_time > right.apply_time;
        return left.apply_form_no < right.apply_form_no;
    });
    error.clear();
    return true;
}

bool query_transfusion_order_statistics(
    const TransfusionOrderStatQuery& query,
    TransfusionOrderStatSummary& summary,
    std::vector<TransfusionOrderStatDetailRow>& rows,
    std::string& error, LogFn log) {
    summary = TransfusionOrderStatSummary{};
    rows.clear();
#ifndef _WIN32
    (void)query;
    (void)log;
    error = "query_transfusion_order_statistics is only available on Windows";
    return false;
#else
    const std::string start_date = trim(query.start_date);
    const std::string end_date = trim(query.end_date);
    if (start_date.size() != 10 || end_date.size() != 10 || start_date > end_date) {
        error = "valid start_date and end_date are required";
        return false;
    }
    DbContext db;
    if (!connect(query.connection_string, db, error, log)) return false;

    std::ostringstream sql;
    sql << "SELECT "
        << "isnull(LTRIM(RTRIM(a.ApplyFormNO)),''),"
        << "isnull(CONVERT(varchar(19),a.Apply_Time,120),''),"
        << "isnull(LTRIM(RTRIM(a.ApplyForm_Statue)),''),"
        << "isnull(LTRIM(RTRIM(a.Patient_NO)),''),"
        << "isnull(LTRIM(RTRIM(a.Patient_NOType)),''),"
        << "isnull(LTRIM(RTRIM(a.Patient_Name)),''),"
        << "isnull(LTRIM(RTRIM(a.Apply_Dept)),''),"
        << "isnull(CONVERT(varchar(32),a.Apply_DeptID),''),"
        << "isnull(LTRIM(RTRIM(a.Apply_BedNo)),''),"
        << "isnull(LTRIM(RTRIM(a.Apply_Doctor)),''),"
        << "isnull(LTRIM(RTRIM(a.TranProperty)),''),"
        << "CASE WHEN isnull(a.Delete_Bit,0)=1 THEN '1' ELSE '0' END"
        << " FROM LS_XK_BloodRequestApply a WITH (NOLOCK)"
        << " WHERE a.Apply_Time>='" << sql_escape(start_date) << "'"
        << " AND a.Apply_Time<DATEADD(day,1,'" << sql_escape(end_date) << "')"
        << " ORDER BY a.Apply_Time DESC,a.ApplyFormNO";
    if (log) log("exec sql: " + sql.str() + "\n");

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) return false;
    std::vector<TransfusionOrderStatRawRow> raw_rows;
    while (SQLFetch(stmt) == SQL_SUCCESS) {
        TransfusionOrderStatRawRow row;
        row.apply_form_no = fetch_column(stmt, 1);
        row.apply_time = fetch_column(stmt, 2);
        row.apply_status = fetch_column(stmt, 3);
        row.patient_no = fetch_column(stmt, 4);
        row.patient_no_type = fetch_column(stmt, 5);
        row.patient_name = fetch_column(stmt, 6);
        row.apply_dept = fetch_column(stmt, 7);
        row.apply_dept_id = fetch_column(stmt, 8);
        row.bed_no = fetch_column(stmt, 9);
        row.apply_doctor = fetch_column(stmt, 10);
        row.tran_property = fetch_column(stmt, 11);
        row.delete_bit = trim(fetch_column(stmt, 12)) == "1";
        raw_rows.push_back(std::move(row));
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return build_transfusion_order_statistics(query, raw_rows, summary, rows, error);
#endif
}

bool build_massive_transfusion_statistics(
    const MassiveTransfusionStatQuery& query,
    const std::vector<MassiveTransfusionRawRow>& raw_rows,
    MassiveTransfusionStatSummary& summary,
    std::vector<MassiveTransfusionEventRow>& events,
    std::vector<MassiveTransfusionComponentDetailRow>& audit_rows,
    std::string& error) {
    summary = MassiveTransfusionStatSummary{};
    events.clear();
    audit_rows.clear();

    const std::string start_date = trim(query.start_date);
    const std::string end_date = trim(query.end_date);
    if (start_date.size() != 10 || end_date.size() != 10 || start_date > end_date) {
        error = "valid start_date and end_date are required";
        return false;
    }
    if (!std::isfinite(query.threshold_ml) || query.threshold_ml <= 0.0) {
        error = "threshold_ml must be a positive finite number";
        return false;
    }

    const auto campus_for_dept = [](const std::string& dept) {
        return contains_text(dept, "滨水") ? std::string("新院") : std::string("老院");
    };
    const std::string requested_campus = trim(query.campus);
    const auto campus_matches = [&requested_campus](const std::string& campus) {
        return (requested_campus != "老院" && requested_campus != "新院") ||
               campus == requested_campus;
    };
    const auto format_number = [](double value) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(2) << value;
        std::string text = out.str();
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
        return text.empty() ? std::string("0") : text;
    };
    const auto parse_number = [](const std::string& text, double& value) {
        const std::string input = trim(text);
        if (input.empty()) return false;
        char* end = nullptr;
        value = std::strtod(input.c_str(), &end);
        return end && *end == '\0' && value > 0.0;
    };
    const auto normalized_unit = [](std::string unit) {
        unit = trim(unit);
        for (char& ch : unit) {
            const unsigned char value = static_cast<unsigned char>(ch);
            if (value < 128) ch = static_cast<char>(std::toupper(value));
        }
        return unit;
    };
    const auto datetime_value = [](const std::string& text, std::time_t& value) {
        std::tm parsed{};
        if (!parse_sql_datetime(text, parsed)) return false;
        value = std::mktime(&parsed);
        return value != static_cast<std::time_t>(-1);
    };
    const auto format_datetime = [](std::time_t value) {
        std::tm* parsed = std::localtime(&value);
        if (!parsed) return std::string{};
        char buffer[20]{};
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", parsed);
        return std::string(buffer);
    };

    struct Component {
        std::string composition;
        std::string composition_big_id;
        std::string apply_num;
        std::string apply_unit;
    };
    struct Application {
        std::string main_id;
        std::string apply_form_no;
        std::string patient_no;
        std::string patient_no_type;
        std::string patient_name;
        std::string apply_time;
        std::time_t time_value = 0;
        bool valid_time = false;
        std::string apply_status;
        std::string apply_dept;
        std::string bed_no;
        std::string apply_doctor;
        std::vector<Component> components;
        std::set<std::string> component_keys;
    };

    std::map<std::string, Application> application_map;
    std::set<std::string> missing_patient_keys;
    std::set<std::string> missing_form_keys;
    for (size_t raw_index = 0; raw_index < raw_rows.size(); ++raw_index) {
        const auto& raw = raw_rows[raw_index];
        const std::string campus = campus_for_dept(raw.apply_dept);
        const std::string form_no = trim(raw.apply_form_no);
        const std::string patient_no = trim(raw.patient_no);
        const std::string physical_key = !trim(raw.main_id).empty()
            ? trim(raw.main_id) : std::to_string(raw_index);
        if (form_no.empty()) {
            if (campus_matches(campus)) missing_form_keys.insert(physical_key);
            continue;
        }
        if (patient_no.empty()) {
            if (campus_matches(campus)) missing_patient_keys.insert(form_no);
            continue;
        }

        auto found = application_map.find(form_no);
        if (found == application_map.end()) {
            Application app;
            app.main_id = trim(raw.main_id);
            app.apply_form_no = form_no;
            app.patient_no = patient_no;
            app.patient_no_type = trim(raw.patient_no_type);
            app.patient_name = trim(raw.patient_name);
            app.apply_time = trim(raw.apply_time);
            app.valid_time = datetime_value(app.apply_time, app.time_value);
            app.apply_status = trim(raw.apply_status);
            app.apply_dept = trim(raw.apply_dept);
            app.bed_no = trim(raw.bed_no);
            app.apply_doctor = trim(raw.apply_doctor);
            found = application_map.emplace(form_no, std::move(app)).first;
        }
        auto& app = found->second;
        const std::string son_id = trim(raw.son_id);
        if (!son_id.empty()) {
            std::string component_key = son_id;
            if (!app.component_keys.insert(component_key).second) continue;
            Component component;
            component.composition = trim(raw.composition);
            component.composition_big_id = trim(raw.composition_big_id);
            component.apply_num = trim(raw.apply_num);
            component.apply_unit = trim(raw.apply_unit);
            app.components.push_back(std::move(component));
        }
    }
    summary.missing_patient_no_count = static_cast<int>(missing_patient_keys.size());
    summary.missing_apply_form_no_count = static_cast<int>(missing_form_keys.size());

    std::map<std::string, std::vector<Application*>> effective_by_patient;
    std::map<std::string, std::vector<Application*>> rejected_by_patient;
    for (auto& pair : application_map) {
        auto& app = pair.second;
        if (!app.valid_time) continue;
        if (app.apply_status == "已驳回") {
            rejected_by_patient[app.patient_no].push_back(&app);
        } else if (app.apply_status == "未审核" || app.apply_status == "已审核" ||
                   app.apply_status == "已完结") {
            effective_by_patient[app.patient_no].push_back(&app);
        }
    }
    const auto app_order = [](const Application* left, const Application* right) {
        if (left->time_value != right->time_value) return left->time_value < right->time_value;
        if (left->apply_form_no != right->apply_form_no) return left->apply_form_no < right->apply_form_no;
        return left->main_id < right->main_id;
    };
    for (auto& pair : effective_by_patient) std::sort(pair.second.begin(), pair.second.end(), app_order);
    for (auto& pair : rejected_by_patient) std::sort(pair.second.begin(), pair.second.end(), app_order);

    const auto make_component_detail = [&](const Application& app, const Component* component,
                                           const std::string& event_id, bool counted) {
        MassiveTransfusionComponentDetailRow detail;
        detail.statistic_basis = "application";
        detail.time_source = "申请时间";
        detail.selected_time = app.apply_time;
        detail.event_id = event_id;
        detail.campus = campus_for_dept(app.apply_dept);
        detail.patient_no = app.patient_no;
        detail.patient_name = app.patient_name;
        detail.apply_form_no = app.apply_form_no;
        detail.apply_time = app.apply_time;
        detail.apply_status = app.apply_status;
        detail.apply_dept = app.apply_dept;
        detail.bed_no = app.bed_no;
        detail.apply_doctor = app.apply_doctor;
        detail.counted = counted;
        detail.rejected = app.apply_status == "已驳回";
        if (!component) {
            detail.counted = false;
            detail.data_status = detail.rejected ? "已驳回，不计量；申请成分子表缺失" : "申请成分子表缺失";
            return detail;
        }
        detail.composition = component->composition;
        detail.composition_category_id = component->composition_big_id;
        detail.apply_num = component->apply_num;
        detail.apply_unit = component->apply_unit;
        const bool known_category = is_known_composition_type_id(component->composition_big_id);
        const bool is_cryoprecipitate =
            component->composition_big_id == kCryoprecipitateCompositionTypeId;
        const bool is_platelet = component->composition_big_id == kPlateletCompositionTypeId;
        detail.excluded_by_component_filter =
            !query.include_platelet_and_cryoprecipitate &&
            (is_platelet || is_cryoprecipitate);
        if (detail.excluded_by_component_filter) detail.counted = false;
        const std::string unit = normalized_unit(component->apply_unit);
        double factor = 0.0;
        if (!known_category) factor = 0.0;
        else if (unit == "ML") factor = 1.0;
        else if (unit == "U" && is_cryoprecipitate) factor = 20.0;
        else if (unit == "U") factor = 200.0;
        else if (unit == "治疗量") factor = 250.0;
        double amount = 0.0;
        const bool valid_amount = parse_number(component->apply_num, amount);
        if (!known_category) {
            detail.counted = false;
            detail.data_status = "成分大类ID缺失或未识别，不计量";
        } else if (factor <= 0.0) {
            detail.counted = false;
            detail.data_status = "未识别申请单位";
        } else if (!valid_amount) {
            detail.counted = false;
            detail.data_status = "申请量为空或无效";
        } else {
            detail.conversion_factor = format_number(factor);
            detail.converted_ml = format_number(amount * factor);
            if (detail.rejected) detail.data_status = "已驳回，不计量";
            else if (detail.excluded_by_component_filter) detail.data_status = "未勾选，不计量";
            else detail.data_status = "完整";
        }
        if (detail.rejected && detail.data_status != "已驳回，不计量") {
            detail.data_status = "已驳回，不计量；" + detail.data_status;
        } else if (detail.excluded_by_component_filter &&
                   detail.data_status != "未勾选，不计量") {
            detail.data_status = "未勾选，不计量；" + detail.data_status;
        }
        return detail;
    };

    std::vector<MassiveTransfusionEventRow> candidate_events;
    for (auto& patient_pair : effective_by_patient) {
        auto& apps = patient_pair.second;
        size_t index = 0;
        while (index < apps.size()) {
            Application* anchor = apps[index];
            const std::string anchor_date = date_from_sql_datetime(anchor->apply_time);
            if (anchor_date < start_date) {
                ++index;
                continue;
            }
            if (anchor_date > end_date) break;
            const std::time_t window_end = anchor->time_value + 24 * 60 * 60;
            size_t next = index;
            while (next < apps.size() && apps[next]->time_value < window_end) ++next;

            MassiveTransfusionEventRow event;
            event.statistic_basis = "application";
            event.time_source = "申请时间";
            event.event_id = anchor->patient_no + "@" + anchor->apply_time;
            event.campus = campus_for_dept(anchor->apply_dept);
            event.patient_no = anchor->patient_no;
            event.patient_no_type = anchor->patient_no_type;
            event.first_apply_time = anchor->apply_time;
            event.window_end_time = format_datetime(window_end);
            event.last_apply_time = apps[next - 1]->apply_time;
            event.first_apply_form_no = anchor->apply_form_no;
            event.apply_dept = anchor->apply_dept;
            event.bed_no = anchor->bed_no;
            event.application_count = static_cast<int>(next - index);

            std::set<std::string> statuses;
            std::set<std::string> seen_patient_names;
            std::vector<std::string> ordered_patient_names;
            std::map<std::string, double> composition_totals;
            double total_ml = 0.0;
            for (size_t app_index = index; app_index < next; ++app_index) {
                const Application& app = *apps[app_index];
                collect_event_patient_name(
                    app.patient_name, ordered_patient_names, seen_patient_names);
                if (!event.apply_form_nos.empty()) event.apply_form_nos += ";";
                event.apply_form_nos += app.apply_form_no;
                statuses.insert(app.apply_status);
                if (app.components.empty()) {
                    event.complete = false;
                    ++event.issue_count;
                    event.components.push_back(make_component_detail(app, nullptr, event.event_id, true));
                    continue;
                }
                for (const auto& component : app.components) {
                    auto detail = make_component_detail(app, &component, event.event_id, true);
                    if (detail.excluded_by_component_filter) {
                        event.components.push_back(std::move(detail));
                        continue;
                    }
                    if (detail.converted_ml.empty()) {
                        event.complete = false;
                        ++event.issue_count;
                    } else {
                        ++event.component_count;
                        const double converted = std::strtod(detail.converted_ml.c_str(), nullptr);
                        total_ml += converted;
                        composition_totals[detail.composition.empty() ? "未命名制品" : detail.composition] += converted;
                    }
                    event.components.push_back(std::move(detail));
                }
            }
            finalize_event_patient_names(event, ordered_patient_names);
            event.total_ml = format_number(total_ml);
            event.qualifies = query.threshold_inclusive
                ? total_ml >= query.threshold_ml
                : total_ml > query.threshold_ml;
            for (const auto& status : statuses) {
                if (!event.status_summary.empty()) event.status_summary += "/";
                event.status_summary += status;
            }
            for (const auto& item : composition_totals) {
                if (!event.composition_summary.empty()) event.composition_summary += ";";
                event.composition_summary += item.first + " " + format_number(item.second) + "ml";
            }
            event.data_status = event.complete ? "完整" : "总量不完整";
            if (event.qualifies || !event.complete) candidate_events.push_back(std::move(event));
            index = next;
        }
    }

    std::set<std::string> attached_rejected_forms;
    for (auto& event : candidate_events) {
        auto rejected_it = rejected_by_patient.find(event.patient_no);
        if (rejected_it == rejected_by_patient.end()) continue;
        std::time_t start_value = 0;
        std::time_t end_value = 0;
        if (!datetime_value(event.first_apply_time, start_value) ||
            !datetime_value(event.window_end_time, end_value)) continue;
        for (const Application* rejected : rejected_it->second) {
            if (rejected->time_value < start_value || rejected->time_value >= end_value) continue;
            if (!attached_rejected_forms.insert(rejected->apply_form_no).second) continue;
            ++event.rejected_application_count;
            if (!event.apply_form_nos.empty()) event.apply_form_nos += ";";
            event.apply_form_nos += rejected->apply_form_no + "(已驳回)";
            if (event.status_summary.find("已驳回") == std::string::npos) {
                if (!event.status_summary.empty()) event.status_summary += "/";
                event.status_summary += "已驳回(不计量)";
            }
            if (rejected->components.empty()) {
                event.components.push_back(make_component_detail(*rejected, nullptr, event.event_id, false));
            } else {
                for (const auto& component : rejected->components) {
                    event.components.push_back(make_component_detail(*rejected, &component, event.event_id, false));
                }
            }
        }
    }

    for (const auto& rejected_pair : rejected_by_patient) {
        for (const Application* rejected : rejected_pair.second) {
            if (attached_rejected_forms.count(rejected->apply_form_no) != 0) continue;
            const std::string rejected_date = date_from_sql_datetime(rejected->apply_time);
            if (rejected_date < start_date || rejected_date > end_date) continue;
            const std::string campus = campus_for_dept(rejected->apply_dept);
            if (!campus_matches(campus)) continue;
            if (rejected->components.empty()) {
                audit_rows.push_back(make_component_detail(*rejected, nullptr, "", false));
            } else {
                for (const auto& component : rejected->components) {
                    audit_rows.push_back(make_component_detail(*rejected, &component, "", false));
                }
            }
        }
    }

    std::set<std::string> qualifying_patients;
    std::set<std::string> rejected_forms;
    for (auto& event : candidate_events) {
        if (!campus_matches(event.campus)) continue;
        if (event.qualifies) {
            ++summary.event_count;
            qualifying_patients.insert(event.patient_no);
            summary.application_count += event.application_count;
            summary.component_count += event.component_count;
            summary.total_ml += std::strtod(event.total_ml.c_str(), nullptr);
        }
        if (!event.complete) {
            ++summary.issue_event_count;
            summary.issue_component_count += event.issue_count;
        }
        for (const auto& component : event.components) {
            if (component.rejected) rejected_forms.insert(component.apply_form_no);
        }
        events.push_back(std::move(event));
    }
    for (const auto& component : audit_rows) rejected_forms.insert(component.apply_form_no);
    summary.patient_count = static_cast<int>(qualifying_patients.size());
    summary.rejected_application_count = static_cast<int>(rejected_forms.size());
    summary.audit_record_count = summary.rejected_application_count;

    std::sort(events.begin(), events.end(), [](const auto& left, const auto& right) {
        return left.first_apply_time > right.first_apply_time;
    });
    error.clear();
    return true;
}

bool build_actual_massive_transfusion_statistics(
    const MassiveTransfusionStatQuery& query,
    const std::vector<ActualTransfusionRawRow>& raw_rows,
    MassiveTransfusionStatSummary& summary,
    std::vector<MassiveTransfusionEventRow>& events,
    std::vector<MassiveTransfusionComponentDetailRow>& audit_rows,
    std::string& error) {
    summary = MassiveTransfusionStatSummary{};
    events.clear();
    audit_rows.clear();

    const std::string start_date = trim(query.start_date);
    const std::string end_date = trim(query.end_date);
    const std::string time_source = trim(query.event_time_source);
    if (start_date.size() != 10 || end_date.size() != 10 || start_date > end_date) {
        error = "valid start_date and end_date are required";
        return false;
    }
    if (!std::isfinite(query.threshold_ml) || query.threshold_ml <= 0.0) {
        error = "threshold_ml must be a positive finite number";
        return false;
    }
    if (time_source != "match" && time_source != "out" &&
        time_source != "apply" && time_source != "check") {
        error = "event_time_source must be match, out, apply, or check";
        return false;
    }

    const auto campus_for_dept = [](const std::string& dept) {
        return contains_text(dept, "滨水") ? std::string("新院") : std::string("老院");
    };
    const std::string requested_campus = trim(query.campus);
    const auto campus_matches = [&requested_campus](const std::string& campus) {
        return (requested_campus != "老院" && requested_campus != "新院") ||
               campus == requested_campus;
    };
    const auto selected_time = [&time_source](const ActualTransfusionRawRow& row) -> std::string {
        if (time_source == "out") return trim(row.blood_out_date);
        if (time_source == "apply") return trim(row.apply_time);
        if (time_source == "check") return trim(row.check_date);
        return trim(row.match_date);
    };
    const auto source_name = [&time_source]() {
        if (time_source == "out") return std::string("出库时间");
        if (time_source == "apply") return std::string("申请时间");
        if (time_source == "check") return std::string("血库审核时间");
        return std::string("配血时间");
    };
    const auto format_number = [](double value) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(2) << value;
        std::string text = out.str();
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
        return text.empty() ? std::string("0") : text;
    };
    const auto parse_number = [](const std::string& text, double& value) {
        const std::string input = trim(text);
        if (input.empty()) return false;
        char* end = nullptr;
        value = std::strtod(input.c_str(), &end);
        return end && *end == '\0' && value > 0.0;
    };
    const auto normalized_unit = [](std::string unit) {
        unit = trim(unit);
        for (char& ch : unit) {
            const unsigned char value = static_cast<unsigned char>(ch);
            if (value < 128) ch = static_cast<char>(std::toupper(value));
        }
        return unit;
    };
    const auto datetime_value = [](const std::string& text, std::time_t& value) {
        std::tm parsed{};
        if (!parse_sql_datetime(text, parsed)) return false;
        value = std::mktime(&parsed);
        return value != static_cast<std::time_t>(-1);
    };
    std::time_t load_start = 0;
    std::time_t end_day_start = 0;
    if (!datetime_value(start_date + " 00:00:00", load_start) ||
        !datetime_value(end_date + " 00:00:00", end_day_start)) {
        error = "valid start_date and end_date are required";
        return false;
    }
    const std::time_t primary_load_end = end_day_start + 2 * 24 * 60 * 60;
    const std::time_t audit_load_end = end_day_start + 24 * 60 * 60;
    const auto auxiliary_time = [](const ActualTransfusionRawRow& row) {
        if (!trim(row.match_date).empty()) return trim(row.match_date);
        if (!trim(row.blood_out_date).empty()) return trim(row.blood_out_date);
        if (!trim(row.apply_time).empty()) return trim(row.apply_time);
        return trim(row.check_date);
    };
    const auto in_load_scope = [&](const ActualTransfusionRawRow& row) {
        std::time_t value = 0;
        const std::string selected = selected_time(row);
        if (datetime_value(selected, value)) {
            return value >= load_start && value < primary_load_end;
        }
        if (!selected.empty()) return true;  // 保留格式异常，交给异常核查。
        return datetime_value(auxiliary_time(row), value) &&
               value >= load_start && value < audit_load_end;
    };
    const auto format_datetime = [](std::time_t value) {
        std::tm* parsed = std::localtime(&value);
        if (!parsed) return std::string{};
        char buffer[20]{};
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", parsed);
        return std::string(buffer);
    };
    const auto append_status = [](std::string& status, const std::string& item) {
        if (item.empty() || status.find(item) != std::string::npos) return;
        if (!status.empty()) status += "；";
        status += item;
    };

    const auto base_detail = [&](const ActualTransfusionRawRow& row) {
        MassiveTransfusionComponentDetailRow detail;
        detail.statistic_basis = "actual";
        detail.time_source = source_name();
        detail.selected_time = selected_time(row);
        detail.campus = campus_for_dept(row.apply_dept);
        detail.patient_no = trim(row.patient_no);
        detail.patient_name = trim(row.patient_name);
        detail.apply_form_no = trim(row.apply_form_no);
        detail.apply_time = trim(row.apply_time);
        detail.apply_status = trim(row.apply_status);
        detail.apply_dept = trim(row.apply_dept);
        detail.bed_no = trim(row.bed_no);
        detail.apply_doctor = trim(row.apply_doctor);
        detail.composition = trim(row.composition);
        detail.composition_category_id = trim(row.composition_type_id);
        detail.apply_num = trim(row.norm);
        detail.apply_unit = trim(row.unit);
        detail.cross_match_id = trim(row.cross_match_id);
        detail.blood_in_id = trim(row.blood_in_id);
        detail.blood_bag_no = trim(row.blood_bag_no);
        detail.product_code = trim(row.product_code);
        detail.verify_state = trim(row.verify_state);
        detail.match_date = trim(row.match_date);
        detail.blood_out_date = trim(row.blood_out_date);
        detail.check_date = trim(row.check_date);
        detail.blood_out_record_count = row.blood_out_record_count;
        detail.cross_match_deleted = row.cross_match_deleted;
        return detail;
    };
    const auto in_page = [&](const ActualTransfusionRawRow& row) {
        const std::string value = selected_time(row);
        if (value.empty()) return true;  // 候选范围已按辅助时间在 C++ 中精确复核。
        const std::string date = date_from_sql_datetime(value);
        return date >= start_date && date <= end_date;
    };
    const auto push_audit = [&](const ActualTransfusionRawRow& row, const std::string& reason) {
        if (!in_page(row)) return;
        auto detail = base_detail(row);
        detail.counted = false;
        detail.data_status = reason;
        if (campus_matches(detail.campus)) audit_rows.push_back(std::move(detail));
    };

    std::map<std::string, std::vector<const ActualTransfusionRawRow*>> reviewed_by_bag;
    std::vector<const ActualTransfusionRawRow*> candidates;
    for (const auto& row : raw_rows) {
        if (!in_load_scope(row)) continue;
        ++summary.raw_record_count;
        const std::string verify = trim(row.verify_state);
        if (row.cross_match_deleted) {
            push_audit(row, "交叉配血记录已删除，不计量");
            continue;
        }
        if (verify == "未审核") {
            push_audit(row, "未审核，未实际输血，不计量");
            continue;
        }
        if (verify != "已审核") {
            push_audit(row, verify.empty() ? "实际输血状态为空" : "未识别实际输血状态：" + verify);
            continue;
        }
        if (trim(row.blood_in_id).empty()) {
            push_audit(row, "已审核但血袋ID为空，不计量");
            continue;
        }
        if (trim(row.patient_no).empty()) {
            push_audit(row, "已审核但交叉配血病人号为空，不计量");
            ++summary.missing_patient_no_count;
            continue;
        }
        std::time_t ignored = 0;
        if (!datetime_value(selected_time(row), ignored)) {
            push_audit(row, source_name() + "缺失或无效，不建立事件");
            continue;
        }
        reviewed_by_bag[trim(row.blood_in_id)].push_back(&row);
    }
    for (const auto& bag : reviewed_by_bag) {
        if (bag.second.size() == 1) {
            candidates.push_back(bag.second.front());
            continue;
        }
        for (const auto* row : bag.second) {
            push_audit(*row, "同一血袋存在多条已审核记录，不重复计量");
        }
    }

    const auto row_order = [&](const ActualTransfusionRawRow* left,
                               const ActualTransfusionRawRow* right) {
        std::time_t left_time = 0;
        std::time_t right_time = 0;
        datetime_value(selected_time(*left), left_time);
        datetime_value(selected_time(*right), right_time);
        if (left_time != right_time) return left_time < right_time;
        if (trim(left->blood_in_id) != trim(right->blood_in_id)) {
            return trim(left->blood_in_id) < trim(right->blood_in_id);
        }
        return trim(left->cross_match_id) < trim(right->cross_match_id);
    };
    std::map<std::string, std::vector<const ActualTransfusionRawRow*>> by_patient;
    for (const auto* row : candidates) by_patient[trim(row->patient_no)].push_back(row);
    for (auto& pair : by_patient) std::sort(pair.second.begin(), pair.second.end(), row_order);

    std::vector<MassiveTransfusionEventRow> candidate_events;
    for (auto& patient : by_patient) {
        auto& bags = patient.second;
        size_t index = 0;
        while (index < bags.size()) {
            const auto* anchor = bags[index];
            const std::string anchor_time = selected_time(*anchor);
            const std::string anchor_date = date_from_sql_datetime(anchor_time);
            if (anchor_date < start_date) {
                ++index;
                continue;
            }
            if (anchor_date > end_date) break;
            std::time_t anchor_value = 0;
            datetime_value(anchor_time, anchor_value);
            const std::time_t window_end = anchor_value + 24 * 60 * 60;
            size_t next = index;
            while (next < bags.size()) {
                std::time_t value = 0;
                datetime_value(selected_time(*bags[next]), value);
                if (value >= window_end) break;
                ++next;
            }

            MassiveTransfusionEventRow event;
            event.statistic_basis = "actual";
            event.time_source = source_name();
            event.event_id = patient.first + "@" + anchor_time;
            event.campus = campus_for_dept(anchor->apply_dept);
            event.patient_no = patient.first;
            event.patient_no_type = trim(anchor->patient_no_type);
            event.first_apply_time = anchor_time;
            event.window_end_time = format_datetime(window_end);
            event.last_apply_time = selected_time(*bags[next - 1]);
            event.first_apply_form_no = trim(anchor->apply_form_no);
            event.apply_dept = trim(anchor->apply_dept);
            event.bed_no = trim(anchor->bed_no);

            std::set<std::string> form_nos;
            std::set<std::string> departments;
            std::set<std::string> seen_patient_names;
            std::vector<std::string> ordered_patient_names;
            std::map<std::string, double> composition_totals;
            double total_ml = 0.0;
            for (size_t bag_index = index; bag_index < next; ++bag_index) {
                const auto& row = *bags[bag_index];
                collect_event_patient_name(
                    row.patient_name, ordered_patient_names, seen_patient_names);
                auto detail = base_detail(row);
                detail.event_id = event.event_id;
                detail.counted = true;
                if (!detail.apply_form_no.empty()) form_nos.insert(detail.apply_form_no);
                else ++summary.missing_apply_form_no_count;
                if (!detail.apply_dept.empty()) departments.insert(detail.apply_dept);

                std::string anomaly;
                if (trim(row.apply_main_id).empty()) append_status(anomaly, "找不到申请单");
                if (row.apply_deleted || trim(row.apply_status) == "已删除") append_status(anomaly, "申请单已删除");
                if (trim(row.apply_status) == "已驳回") append_status(anomaly, "申请单已驳回");
                if (!trim(row.apply_main_id).empty() &&
                    trim(row.apply_patient_no) != trim(row.patient_no)) {
                    append_status(anomaly, "申请单病人号不一致");
                }
                if (row.blood_out_record_count > 1) append_status(anomaly, "同一血袋存在多条出库记录，采用最早出库时间");
                detail.application_anomaly = !anomaly.empty();
                if (detail.application_anomaly) {
                    ++event.audit_count;
                    auto audit = detail;
                    audit.counted = false;
                    audit.data_status = anomaly;
                    if (campus_matches(audit.campus)) audit_rows.push_back(std::move(audit));
                }

                const bool known_category =
                    is_known_composition_type_id(detail.composition_category_id);
                const bool is_cryo =
                    detail.composition_category_id == kCryoprecipitateCompositionTypeId;
                const bool is_platelet =
                    detail.composition_category_id == kPlateletCompositionTypeId;
                detail.excluded_by_component_filter =
                    !query.include_platelet_and_cryoprecipitate && (is_platelet || is_cryo);
                const std::string unit = normalized_unit(detail.apply_unit);
                double factor = 0.0;
                if (!known_category) factor = 0.0;
                else if (unit == "ML") factor = 1.0;
                else if (unit == "U" && is_cryo) factor = 20.0;
                else if (unit == "U") factor = 200.0;
                else if (unit == "治疗量") factor = 250.0;
                double amount = 0.0;
                const bool valid_amount = parse_number(detail.apply_num, amount);
                if (factor > 0.0) detail.conversion_factor = format_number(factor);
                if (factor > 0.0 && valid_amount) detail.converted_ml = format_number(amount * factor);

                if (detail.excluded_by_component_filter) {
                    detail.counted = false;
                    detail.data_status = "未勾选，不计量";
                } else if (!known_category) {
                    detail.counted = false;
                    detail.data_status = "成分类型ID缺失或未识别，不计量";
                } else if (factor <= 0.0) {
                    detail.counted = false;
                    detail.data_status = "血袋容量单位缺失或未识别";
                } else if (!valid_amount) {
                    detail.counted = false;
                    detail.data_status = "血袋规格为空或无效";
                } else {
                    detail.data_status = anomaly.empty() ? "完整" : "已计量；" + anomaly;
                    ++event.component_count;
                    const double converted = amount * factor;
                    total_ml += converted;
                    composition_totals[detail.composition.empty() ? "未命名制品" : detail.composition] += converted;
                }
                if (!detail.excluded_by_component_filter && !detail.counted) {
                    event.complete = false;
                    ++event.issue_count;
                    auto audit = detail;
                    audit.data_status = detail.data_status;
                    if (campus_matches(audit.campus)) audit_rows.push_back(std::move(audit));
                }
                event.components.push_back(std::move(detail));
            }
            finalize_event_patient_names(event, ordered_patient_names);
            event.application_count = static_cast<int>(form_nos.size());
            for (const auto& form : form_nos) {
                if (!event.apply_form_nos.empty()) event.apply_form_nos += ";";
                event.apply_form_nos += form;
            }
            event.cross_department = departments.size() > 1;
            event.status_summary = "已审核";
            if (event.cross_department) append_status(event.status_summary, "跨科室");
            if (event.audit_count > 0) append_status(event.status_summary, "存在核查项");
            for (const auto& item : composition_totals) {
                if (!event.composition_summary.empty()) event.composition_summary += ";";
                event.composition_summary += item.first + " " + format_number(item.second) + "ml";
            }
            event.total_ml = format_number(total_ml);
            event.qualifies = query.threshold_inclusive
                ? total_ml >= query.threshold_ml : total_ml > query.threshold_ml;
            event.data_status = event.complete
                ? (event.audit_count > 0 ? "完整；存在核查项" : "完整")
                : "总量不完整";
            if (event.qualifies || !event.complete) candidate_events.push_back(std::move(event));
            index = next;
        }
    }

    std::set<std::string> qualifying_patients;
    for (auto& event : candidate_events) {
        if (!campus_matches(event.campus)) continue;
        if (event.qualifies) {
            ++summary.event_count;
            qualifying_patients.insert(event.patient_no);
            summary.application_count += event.application_count;
            summary.component_count += event.component_count;
            summary.total_ml += std::strtod(event.total_ml.c_str(), nullptr);
        }
        if (!event.complete || event.audit_count > 0) {
            ++summary.issue_event_count;
            summary.issue_component_count += event.issue_count + event.audit_count;
        }
        events.push_back(std::move(event));
    }
    summary.patient_count = static_cast<int>(qualifying_patients.size());
    summary.audit_record_count = static_cast<int>(audit_rows.size());
    summary.rejected_application_count = summary.audit_record_count;
    std::sort(events.begin(), events.end(), [](const auto& left, const auto& right) {
        return left.first_apply_time > right.first_apply_time;
    });
    error.clear();
    return true;
}

bool query_massive_transfusion_statistics(
    const MassiveTransfusionStatQuery& query,
    MassiveTransfusionStatSummary& summary,
    std::vector<MassiveTransfusionEventRow>& events,
    std::vector<MassiveTransfusionComponentDetailRow>& audit_rows,
    std::string& error, LogFn log) {
#ifndef _WIN32
    (void)query;
    (void)summary;
    (void)events;
    (void)audit_rows;
    (void)log;
    error = "query_massive_transfusion_statistics is only available on Windows";
    return false;
#else
    const std::string start_date = trim(query.start_date);
    const std::string end_date = trim(query.end_date);
    if (start_date.size() != 10 || end_date.size() != 10 || start_date > end_date) {
        error = "valid start_date and end_date are required";
        return false;
    }
    if (!std::isfinite(query.threshold_ml) || query.threshold_ml <= 0.0) {
        error = "threshold_ml must be a positive finite number";
        return false;
    }
    DbContext db;
    if (!connect(query.connection_string, db, error, log)) return false;

    if (trim(query.statistic_basis) == "actual") {
        const std::string time_source = trim(query.event_time_source);
        if (time_source != "match" && time_source != "out" &&
            time_source != "apply" && time_source != "check") {
            error = "event_time_source must be match, out, apply, or check";
            return false;
        }
        const std::string selected_expr =
            time_source == "out" ? "bo.BloodOut_Date" :
            time_source == "apply" ? "a.Apply_Time" :
            time_source == "check" ? "a.Check_Date" : "cm.Match_Date";
        const auto range_end = [&](const std::string& source) {
            return "DATEADD(day," + std::string(source == time_source ? "2" : "1") +
                   ",'" + sql_escape(end_date) + "')";
        };
        std::ostringstream sql;
        sql << "WITH CandidateIds AS ("
            << "SELECT cm0.ID FROM LS_XK_BloodCrossMatch cm0 WITH (NOLOCK)"
            << " WHERE cm0.Match_Date>='" << sql_escape(start_date) << "'"
            << " AND cm0.Match_Date<" << range_end("match")
            << " UNION SELECT cm1.ID FROM LS_XK_BloodRequestApply a1 WITH (NOLOCK)"
            << " INNER JOIN LS_XK_BloodCrossMatch cm1 WITH (NOLOCK)"
            << " ON cm1.ApplyFormNO=a1.ApplyFormNO"
            << " WHERE a1.Apply_Time>='" << sql_escape(start_date) << "'"
            << " AND a1.Apply_Time<" << range_end("apply")
            << " UNION SELECT cm2.ID FROM LS_XK_BloodRequestApply a2 WITH (NOLOCK)"
            << " INNER JOIN LS_XK_BloodCrossMatch cm2 WITH (NOLOCK)"
            << " ON cm2.ApplyFormNO=a2.ApplyFormNO"
            << " WHERE a2.Check_Date>='" << sql_escape(start_date) << "'"
            << " AND a2.Check_Date<" << range_end("check")
            << " UNION SELECT cm3.ID FROM LS_XK_BloodOutInfo o3 WITH (NOLOCK)"
            << " INNER JOIN LS_XK_BloodCrossMatch cm3 WITH (NOLOCK)"
            << " ON cm3.BloodInID=o3.BloodInID"
            << " WHERE o3.BloodOut_Date>='" << sql_escape(start_date) << "'"
            << " AND o3.BloodOut_Date<" << range_end("out")
            << "),CandidateCrossMatch AS ("
            << "SELECT cm0.* FROM LS_XK_BloodCrossMatch cm0 WITH (NOLOCK)"
            << " INNER JOIN CandidateIds ids ON ids.ID=cm0.ID"
            << "),CandidateBloodIds AS ("
            << "SELECT DISTINCT cm0.BloodInID FROM CandidateCrossMatch cm0"
            << " WHERE cm0.BloodInID IS NOT NULL"
            << "),BloodOutSummary AS ("
            << "SELECT o.BloodInID,MIN(o.BloodOut_Date) BloodOut_Date,"
            << " COUNT_BIG(*) RecordCount FROM LS_XK_BloodOutInfo o WITH (NOLOCK)"
            << " INNER JOIN CandidateBloodIds ids ON ids.BloodInID=o.BloodInID"
            << " GROUP BY o.BloodInID) SELECT "
            << "isnull(CONVERT(varchar(32),cm.ID),''),isnull(LTRIM(RTRIM(cm.ApplyFormNO)),''),"
            << "isnull(LTRIM(RTRIM(cm.Patient_NO)),''),isnull(LTRIM(RTRIM(a.Patient_NOType)),''),"
            << "isnull(LTRIM(RTRIM(a.Patient_Name)),''),isnull(LTRIM(RTRIM(cm.VerifyState)),''),"
            << "isnull(CONVERT(varchar(10),cm.Delete_Bit),'0'),isnull(CONVERT(varchar(32),cm.BloodInID),''),"
            << "isnull(CONVERT(varchar(19),cm.Match_Date,120),''),isnull(CONVERT(varchar(19),bo.BloodOut_Date,120),''),"
            << "isnull(CONVERT(varchar(10),bo.RecordCount),'0'),isnull(CONVERT(varchar(32),a.ID),''),"
            << "isnull(LTRIM(RTRIM(a.Patient_NO)),''),isnull(CONVERT(varchar(19),a.Apply_Time,120),''),"
            << "isnull(CONVERT(varchar(19),a.Check_Date,120),''),isnull(LTRIM(RTRIM(a.ApplyForm_Statue)),''),"
            << "isnull(CONVERT(varchar(10),a.Delete_Bit),'0'),isnull(LTRIM(RTRIM(a.Apply_Dept)),''),"
            << "isnull(LTRIM(RTRIM(a.Apply_BedNo)),''),isnull(LTRIM(RTRIM(a.Apply_Doctor)),''),"
            << "isnull(CONVERT(varchar(32),bi.ID),''),isnull(LTRIM(RTRIM(bi.BloodBagNO)),''),"
            << "isnull(LTRIM(RTRIM(bi.CmpProductCode)),''),isnull(LTRIM(RTRIM(comp.Blood_Composition)),''),"
            << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),comp.Norm))),''),isnull(LTRIM(RTRIM(comp.Unit)),''),"
            << "isnull(CONVERT(varchar(32),comp.CompositionTypeID),'')"
            << " FROM CandidateCrossMatch cm"
            << " LEFT JOIN LS_XK_BloodRequestApply a WITH (NOLOCK)"
            << " ON a.ApplyFormNO=cm.ApplyFormNO"
            << " LEFT JOIN LS_XK_BloodInfo bi WITH (NOLOCK) ON bi.ID=cm.BloodInID"
            << " LEFT JOIN LS_XK_B_CompositionInfo comp WITH (NOLOCK) ON comp.ID=bi.CompositionID"
            << " LEFT JOIN BloodOutSummary bo ON bo.BloodInID=cm.BloodInID"
            << " ORDER BY cm.Patient_NO," << selected_expr << ",cm.BloodInID,cm.ID";
        if (log) log("exec sql: " + sql.str() + "\n");
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        if (!exec_query(db.dbc, sql.str(), stmt, error)) return false;
        std::vector<ActualTransfusionRawRow> raw_rows;
        while (SQLFetch(stmt) == SQL_SUCCESS) {
            ActualTransfusionRawRow row;
            row.cross_match_id = fetch_column(stmt, 1); row.apply_form_no = fetch_column(stmt, 2);
            row.patient_no = fetch_column(stmt, 3); row.patient_no_type = fetch_column(stmt, 4);
            row.patient_name = fetch_column(stmt, 5); row.verify_state = fetch_column(stmt, 6);
            row.cross_match_deleted = trim(fetch_column(stmt, 7)) == "1"; row.blood_in_id = fetch_column(stmt, 8);
            row.match_date = fetch_column(stmt, 9); row.blood_out_date = fetch_column(stmt, 10);
            row.blood_out_record_count = std::atoi(fetch_column(stmt, 11).c_str()); row.apply_main_id = fetch_column(stmt, 12);
            row.apply_patient_no = fetch_column(stmt, 13); row.apply_time = fetch_column(stmt, 14);
            row.check_date = fetch_column(stmt, 15); row.apply_status = fetch_column(stmt, 16);
            row.apply_deleted = trim(fetch_column(stmt, 17)) == "1"; row.apply_dept = fetch_column(stmt, 18);
            row.bed_no = fetch_column(stmt, 19); row.apply_doctor = fetch_column(stmt, 20);
            row.blood_info_id = fetch_column(stmt, 21); row.blood_bag_no = fetch_column(stmt, 22);
            row.product_code = fetch_column(stmt, 23); row.composition = fetch_column(stmt, 24);
            row.norm = fetch_column(stmt, 25); row.unit = fetch_column(stmt, 26);
            row.composition_type_id = fetch_column(stmt, 27);
            raw_rows.push_back(std::move(row));
        }
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        const bool built = build_actual_massive_transfusion_statistics(
            query, raw_rows, summary, events, audit_rows, error);
        return built;
    }

    std::ostringstream sql;
    sql << "SELECT "
        << "CONVERT(varchar(32),a.ID),"
        << "isnull(LTRIM(RTRIM(a.ApplyFormNO)),''),"
        << "isnull(LTRIM(RTRIM(a.Patient_NO)),''),"
        << "isnull(LTRIM(RTRIM(a.Patient_NOType)),''),"
        << "isnull(LTRIM(RTRIM(a.Patient_Name)),''),"
        << "isnull(LTRIM(RTRIM(a.Patient_Sex)),''),"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(20),a.Patient_Age))),'')+isnull(LTRIM(RTRIM(a.Patient_AgeUnit)),''),"
        << "isnull(CONVERT(varchar(19),a.Apply_Time,120),''),"
        << "isnull(LTRIM(RTRIM(a.ApplyForm_Statue)),''),"
        << "isnull(LTRIM(RTRIM(a.Apply_Dept)),''),"
        << "isnull(CONVERT(varchar(32),a.Apply_DeptID),''),"
        << "isnull(LTRIM(RTRIM(a.Apply_BedNo)),''),"
        << "isnull(LTRIM(RTRIM(a.Apply_Doctor)),''),"
        << "isnull(CONVERT(varchar(32),s.ID),''),"
        << "isnull(LTRIM(RTRIM(s.SonGuid)),''),"
        << "isnull(LTRIM(RTRIM(s.ApplyComposition)),''),"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(32),s.ApplyNum))),''),"
        << "isnull(LTRIM(RTRIM(s.ApplyUnit)),''),"
        << "isnull(CONVERT(varchar(32),s.CompositionBig_ID),'')"
        << " FROM LS_XK_BloodRequestApply a WITH (NOLOCK)"
        << " LEFT JOIN LS_XK_BloodRequestApplySon s WITH (NOLOCK)"
        << " ON s.ApplyFormNO=a.ApplyFormNO"
        << " WHERE a.Apply_Time>='" << sql_escape(start_date) << "'"
        << " AND a.Apply_Time<DATEADD(day,2,'" << sql_escape(end_date) << "')"
        << " AND isnull(a.Delete_Bit,0)=0"
        << " AND LTRIM(RTRIM(isnull(a.ApplyForm_Statue,'')))<>'已删除'"
        << " ORDER BY a.Patient_NO,a.Apply_Time,a.ApplyFormNO,s.ID";
    if (log) log("exec sql: " + sql.str() + "\n");

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) return false;
    std::vector<MassiveTransfusionRawRow> raw_rows;
    while (SQLFetch(stmt) == SQL_SUCCESS) {
        MassiveTransfusionRawRow row;
        row.main_id = fetch_column(stmt, 1);
        row.apply_form_no = fetch_column(stmt, 2);
        row.patient_no = fetch_column(stmt, 3);
        row.patient_no_type = fetch_column(stmt, 4);
        row.patient_name = fetch_column(stmt, 5);
        row.patient_sex = fetch_column(stmt, 6);
        row.patient_age = fetch_column(stmt, 7);
        row.apply_time = fetch_column(stmt, 8);
        row.apply_status = fetch_column(stmt, 9);
        row.apply_dept = fetch_column(stmt, 10);
        row.apply_dept_id = fetch_column(stmt, 11);
        row.bed_no = fetch_column(stmt, 12);
        row.apply_doctor = fetch_column(stmt, 13);
        row.son_id = fetch_column(stmt, 14);
        row.son_guid = fetch_column(stmt, 15);
        row.composition = fetch_column(stmt, 16);
        row.apply_num = fetch_column(stmt, 17);
        row.apply_unit = fetch_column(stmt, 18);
        row.composition_big_id = fetch_column(stmt, 19);
        raw_rows.push_back(std::move(row));
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    const bool built = build_massive_transfusion_statistics(
        query, raw_rows, summary, events, audit_rows, error);
    if (built) summary.raw_record_count = static_cast<int>(raw_rows.size());
    return built;
#endif
}

bool query_immune_duplicate_statistics(const ImmuneDuplicateStatQuery& query,
                                       ImmuneDuplicateStatSummary& summary,
                                       std::vector<ImmuneDuplicateStatDetailRow>& rows,
                                       std::string& error, LogFn log) {
    summary = ImmuneDuplicateStatSummary{};
    rows.clear();
#ifndef _WIN32
    (void)query;
    (void)log;
    error = "query_immune_duplicate_statistics is only available on Windows";
    return false;
#else
    constexpr const char* kBaseOrderKeyword = "输血前常规检查";
    constexpr const char* kHepatitisKeyword = "乙肝三对";
    constexpr const char* kSyphilisKeyword = "梅毒";

    const std::string start_time = trim(query.start_time);
    const std::string end_time = trim(query.end_time);
    if (start_time.empty() || end_time.empty()) {
        error = "start_time and end_time are required";
        return false;
    }
    if (start_time > end_time) {
        error = "start_time must not be later than end_time";
        return false;
    }

    DbContext db;
    if (!connect(query.connection_string, db, error, log)) {
        return false;
    }

    struct BaseGroup {
        std::set<std::string> barcodes;
        std::string patient_no;
        std::string name;
        std::string department;
        std::string order_text;
        std::string sign_time;
    };

    std::map<std::string, BaseGroup> base_groups;
    std::set<std::string> base_barcodes;
    std::set<std::string> missing_barcode_ids;

    std::ostringstream base_sql;
    base_sql << "SELECT "
             << "isnull(CONVERT(varchar(36),y.YJSQID),''),"
             << "isnull(CONVERT(varchar(36),y.INPATIENT_ID),''),"
             << "isnull(LTRIM(RTRIM(CONVERT(varchar(100),y.TXM))),''),"
             << "isnull(LTRIM(RTRIM(CONVERT(varchar(500),y.SQNR))),''),"
             << "isnull(CONVERT(varchar(19),y.JSSJ,120),''),"
             << "isnull(LTRIM(RTRIM(CONVERT(varchar(100),z.INPATIENT_NO))),''),"
             << "isnull(LTRIM(RTRIM(CONVERT(varchar(200),z.NAME))),''),"
             << "isnull(NULLIF(LTRIM(RTRIM(dept.NAME)),''),"
             << "isnull(LTRIM(RTRIM(CONVERT(varchar(100),y.SQKS))),''))"
             << " FROM YJ_ZYSQ y WITH (NOLOCK)"
             << " LEFT JOIN ZY_INPATIENT z WITH (NOLOCK) ON z.INPATIENT_ID=y.INPATIENT_ID"
             << " LEFT JOIN JC_DEPT_PROPERTY dept WITH (NOLOCK)"
             << " ON LTRIM(RTRIM(CONVERT(varchar(100),dept.DEPT_ID)))="
             << "LTRIM(RTRIM(CONVERT(varchar(100),y.SQKS)))"
             << " AND isnull(dept.DELETED,0)=0"
             << " WHERE y.JSSJ>='" << sql_escape(start_time) << "'"
             << " AND y.JSSJ<DATEADD(minute,1,'" << sql_escape(end_time) << "')"
             << " AND y.ZXKS IN (102,401)"
             << " AND (y.BSCBZ IS NULL OR y.BSCBZ=0)"
             << " AND y.SQNR LIKE '%" << sql_escape(kBaseOrderKeyword) << "%'"
             << " ORDER BY y.JSSJ,y.TXM,y.YJSQID";
    if (log) log("exec sql: " + base_sql.str() + "\n");

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, base_sql.str(), stmt, error)) {
        return false;
    }
    while (SQLFetch(stmt) == SQL_SUCCESS) {
        const std::string request_id = fetch_column(stmt, 1);
        const std::string inpatient_id = fetch_column(stmt, 2);
        const std::string barcode = fetch_column(stmt, 3);
        const std::string order_text = fetch_column(stmt, 4);
        const std::string sign_time = fetch_column(stmt, 5);
        const std::string patient_no = fetch_column(stmt, 6);
        const std::string name = fetch_column(stmt, 7);
        const std::string department = fetch_column(stmt, 8);
        if (inpatient_id.empty()) {
            continue;
        }
        if (barcode.empty()) {
            missing_barcode_ids.insert(request_id.empty() ? inpatient_id + "\n" + sign_time : request_id);
            continue;
        }
        auto& group = base_groups[inpatient_id];
        group.barcodes.insert(barcode);
        base_barcodes.insert(barcode);
        fill_if_empty(group.patient_no, patient_no);
        fill_if_empty(group.name, name);
        fill_if_empty(group.department, department);
        fill_if_empty(group.order_text, order_text);
        fill_if_empty(group.sign_time, sign_time);
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);

    summary.base_barcode_count = static_cast<int>(base_barcodes.size());
    summary.base_patient_count = static_cast<int>(base_groups.size());
    summary.missing_base_barcode_count = static_cast<int>(missing_barcode_ids.size());
    for (const auto& entry : base_groups) {
        if (trim(entry.second.patient_no).empty()) {
            ++summary.unmatched_inpatient_count;
        }
    }
    if (base_groups.empty()) {
        error.clear();
        return true;
    }

    std::ostringstream duplicate_sql;
    duplicate_sql << "SELECT "
                  << "isnull(CONVERT(varchar(36),d.YJSQID),''),"
                  << "isnull(CONVERT(varchar(36),d.INPATIENT_ID),''),"
                  << "isnull(LTRIM(RTRIM(CONVERT(varchar(100),d.TXM))),''),"
                  << "isnull(LTRIM(RTRIM(CONVERT(varchar(500),d.SQNR))),''),"
                  << "isnull(CONVERT(varchar(19),d.JSSJ,120),'')"
                  << " FROM YJ_ZYSQ d WITH (NOLOCK)"
                  << " WHERE d.JSSJ>='" << sql_escape(start_time) << "'"
                  << " AND d.JSSJ<DATEADD(minute,1,'" << sql_escape(end_time) << "')"
                  << " AND d.ZXKS IN (102,401)"
                  << " AND (d.BSCBZ IS NULL OR d.BSCBZ=0)"
                  << " AND (d.SQNR LIKE '%" << sql_escape(kHepatitisKeyword) << "%'"
                  << " OR d.SQNR LIKE '%" << sql_escape(kSyphilisKeyword) << "%')"
                  << " AND EXISTS (SELECT 1 FROM YJ_ZYSQ b WITH (NOLOCK)"
                  << " WHERE b.INPATIENT_ID=d.INPATIENT_ID"
                  << " AND b.JSSJ>='" << sql_escape(start_time) << "'"
                  << " AND b.JSSJ<DATEADD(minute,1,'" << sql_escape(end_time) << "')"
                  << " AND b.ZXKS IN (102,401)"
                  << " AND (b.BSCBZ IS NULL OR b.BSCBZ=0)"
                  << " AND NULLIF(LTRIM(RTRIM(isnull(b.TXM,''))),'') IS NOT NULL"
                  << " AND b.SQNR LIKE '%" << sql_escape(kBaseOrderKeyword) << "%')"
                  << " ORDER BY d.JSSJ,d.TXM,d.YJSQID";
    if (log) log("exec sql: " + duplicate_sql.str() + "\n");

    stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, duplicate_sql.str(), stmt, error)) {
        return false;
    }

    auto join_barcodes = [](const std::set<std::string>& values) {
        std::string joined;
        for (const auto& value : values) {
            if (!joined.empty()) joined += "/";
            joined += value;
        }
        return joined;
    };
    std::set<std::string> seen_request_ids;
    std::set<std::string> duplicate_patients;
    std::set<std::string> duplicate_barcodes;
    while (SQLFetch(stmt) == SQL_SUCCESS) {
        const std::string request_id = fetch_column(stmt, 1);
        const std::string inpatient_id = fetch_column(stmt, 2);
        const std::string duplicate_barcode = fetch_column(stmt, 3);
        const std::string duplicate_order_text = fetch_column(stmt, 4);
        const std::string duplicate_sign_time = fetch_column(stmt, 5);
        if (request_id.empty() || !seen_request_ids.insert(request_id).second) {
            continue;
        }
        const auto base_it = base_groups.find(inpatient_id);
        if (base_it == base_groups.end()) {
            continue;
        }
        const auto& base = base_it->second;
        const bool same_barcode = !duplicate_barcode.empty() && base.barcodes.count(duplicate_barcode) != 0;

        ImmuneDuplicateStatDetailRow row;
        row.patient_no = base.patient_no;
        row.name = base.name;
        row.type_name = "住院";
        row.department = base.department;
        row.base_barcode = join_barcodes(base.barcodes);
        row.base_order_text = base.order_text;
        row.base_sign_time = base.sign_time;
        row.duplicate_barcode = duplicate_barcode;
        row.duplicate_item_name = duplicate_order_text;
        const bool hepatitis = duplicate_order_text.find(kHepatitisKeyword) != std::string::npos;
        const bool syphilis = duplicate_order_text.find(kSyphilisKeyword) != std::string::npos;
        if (hepatitis) row.duplicate_category = kHepatitisKeyword;
        if (syphilis) {
            if (!row.duplicate_category.empty()) row.duplicate_category += "/";
            row.duplicate_category += kSyphilisKeyword;
        }
        row.duplicate_sign_time = duplicate_sign_time;
        row.relation = same_barcode ? "同条码" : "跨条码";
        rows.push_back(std::move(row));

        duplicate_patients.insert(inpatient_id);
        if (!duplicate_barcode.empty()) duplicate_barcodes.insert(duplicate_barcode);
        if (same_barcode) {
            ++summary.same_barcode_duplicate_count;
        } else {
            ++summary.cross_barcode_duplicate_count;
        }
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);

    summary.duplicate_patient_count = static_cast<int>(duplicate_patients.size());
    summary.duplicate_barcode_count = static_cast<int>(duplicate_barcodes.size());
    summary.duplicate_item_count = static_cast<int>(rows.size());
    error.clear();
    return true;
#endif
}

bool query_outpatient_charges(const OutpatientChargeQuery& query, std::vector<OutpatientChargeRow>& rows, std::string& error, LogFn log) {
    rows.clear();
#ifndef _WIN32
    (void)query;
    (void)log;
    error = "query_outpatient_charges is only available on Windows";
    return false;
#else
    const std::string start_time = trim(query.start_time);
    const std::string end_time = trim(query.end_time);
    if (start_time.empty() || end_time.empty()) {
        error = "start_time and end_time are required";
        return false;
    }

    DbContext db;
    if (!connect(query.connection_string, db, error, log)) {
        return false;
    }

    const std::string requested_campus = trim(query.lab_department);
    const std::string outpatient_no = trim(query.outpatient_no);
    const std::string outpatient_date = !outpatient_no.empty()
                                            ? date_from_yyyymmdd_prefix(outpatient_no)
                                            : "";
    const std::string fallback_outpatient_date = !outpatient_no.empty()
                                                     ? date_from_sql_datetime(end_time)
                                                     : "";
    std::ostringstream where;
    if (!outpatient_no.empty() && (!outpatient_date.empty() || !fallback_outpatient_date.empty())) {
        const std::string search_date = outpatient_date.empty() ? fallback_outpatient_date : outpatient_date;
        where << " WHERE y.SFRQ>='" << sql_escape(search_date) << "'"
              << " AND y.SFRQ<DATEADD(day,1,'" << sql_escape(search_date) << "')";
    } else {
        where << " WHERE y.SFRQ>='" << sql_escape(start_time) << "'"
              << " AND y.SFRQ<DATEADD(minute,1,'" << sql_escape(end_time) << "')";
    }
    where << " AND (y.BSCBZ IS NULL OR y.BSCBZ=0)";
    if (!query.include_non_lab && requested_campus == "全部") {
        where << " AND LTRIM(RTRIM(CONVERT(varchar(100),y.ZXKS))) IN ('102','401')";
    } else if (!query.include_non_lab && requested_campus == "老院") {
        where << " AND LTRIM(RTRIM(CONVERT(varchar(100),y.ZXKS)))='102'";
    } else if (!query.include_non_lab && requested_campus == "新院") {
        where << " AND LTRIM(RTRIM(CONVERT(varchar(100),y.ZXKS)))='401'";
    }
    if (!outpatient_no.empty()) {
        where << " AND LTRIM(RTRIM(CONVERT(varchar(100),y.BLH))) LIKE '%"
              << sql_escape(outpatient_no) << "'";
    }
    if (!trim(query.patient_name).empty()) {
        where << " AND p.BRXM LIKE '%" << sql_escape(trim(query.patient_name)) << "%'";
    }
    if (!trim(query.id_card).empty()) {
        where << " AND p.SFZH LIKE '%" << sql_escape(trim(query.id_card)) << "%'";
    }

    std::ostringstream sql;
    sql << "SELECT "
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(100),y.BLH))),'') AS BLH,"
        << "CASE WHEN NULLIF(LTRIM(RTRIM(CONVERT(varchar(100),isnull(y.FPH,'')))),'') IS NULL THEN '0'"
        << " ELSE LTRIM(RTRIM(CONVERT(varchar(100),y.FPH))) END AS FPH,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(100),p.SFZH))),'') AS SFZH,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(100),p.BRXM))),'') AS BRXM,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(20),p.XB))),'') AS XB,"
        << "isnull(CONVERT(varchar(23),p.CSRQ,121),'') AS CSRQ,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(200),y.SQNR))),'') AS SQNR,"
        << "isnull(NULLIF(LTRIM(RTRIM(dept.NAME)),''),"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(100),y.SQKS))),'')) AS SQKS,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(100),y.DJ))),'') AS DJ,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(100),y.SL))),'') AS SL,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(100),y.DW))),'') AS DW,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(100),y.JE))),'') AS JE,"
        << "isnull(CONVERT(varchar(19),y.SFRQ,120),'') AS SFRQ,"
        << "CASE WHEN NULLIF(LTRIM(RTRIM(CONVERT(varchar(100),isnull(y.TXM,'')))),'') IS NULL THEN '未生成'"
        << " ELSE LTRIM(RTRIM(CONVERT(varchar(100),y.TXM))) END AS TXM,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(100),y.BBMC))),'') AS BBMC,"
        << "isnull(CONVERT(varchar(19),y.TXMDYSJ,120),'') AS TXMDYSJ,"
        << "isnull(LTRIM(RTRIM(CONVERT(varchar(100),y.ZXKS))),'') AS ZXKS"
        << " FROM YJ_MZSQ y WITH (NOLOCK)"
        << " LEFT JOIN YY_BRXX p WITH (NOLOCK) ON p.BRXXID=y.BRXXID"
        << " LEFT JOIN JC_DEPT_PROPERTY dept WITH (NOLOCK)"
        << " ON LTRIM(RTRIM(CONVERT(varchar(100),dept.DEPT_ID)))=LTRIM(RTRIM(CONVERT(varchar(100),y.SQKS)))"
        << " AND isnull(dept.DELETED,0)=0"
        << where.str()
        << " ORDER BY y.SFRQ DESC";

    if (log) log("exec sql: " + sql.str() + "\n");

    SQLHSTMT stmt = SQL_NULL_HSTMT;
    if (!exec_query(db.dbc, sql.str(), stmt, error)) {
        return false;
    }

    const auto normalize_sex = [](const std::string& value) {
        const std::string text = trim(value);
        if (text == "1") return std::string("男");
        if (text == "2") return std::string("女");
        return text;
    };
    const auto normalize_campus = [](const std::string& value) {
        const std::string text = trim(value);
        if (text == "401") {
            return std::string("新院");
        }
        if (text == "102") {
            return std::string("老院");
        }
        return text;
    };
    const auto has_generated_barcode = [](const std::string& value) {
        const std::string text = trim(value);
        return !text.empty() && text != "未生成";
    };

    std::map<std::string, size_t> barcode_index;
    std::map<std::string, std::set<std::string>> barcode_item_names;

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        OutpatientChargeRow row;
        row.outpatient_no = fetch_column(stmt, 1);
        row.invoice_no = fetch_column(stmt, 2);
        row.card_no = fetch_column(stmt, 3);
        row.name = fetch_column(stmt, 4);
        row.sex = normalize_sex(fetch_column(stmt, 5));
        row.age = age_from_birthdate(fetch_column(stmt, 6));
        row.item_name = fetch_column(stmt, 7);
        row.application_department = fetch_column(stmt, 8);
        row.unit_price = fetch_column(stmt, 9);
        row.quantity = fetch_column(stmt, 10);
        row.unit = fetch_column(stmt, 11);
        row.amount = fetch_column(stmt, 12);
        row.charge_time = fetch_column(stmt, 13);
        row.barcode = fetch_column(stmt, 14);
        row.sample_name = fetch_column(stmt, 15);
        row.barcode_print_time = fetch_column(stmt, 16);
        row.lab_department = normalize_campus(fetch_column(stmt, 17));
        if (!query.include_non_lab && (requested_campus == "老院" || requested_campus == "新院") &&
            !row.lab_department.empty() && row.lab_department != requested_campus) {
            continue;
        }

        if (has_generated_barcode(row.barcode)) {
            const std::string barcode = trim(row.barcode);
            auto found = barcode_index.find(barcode);
            if (found == barcode_index.end()) {
                barcode_index[barcode] = rows.size();
                found = barcode_index.find(barcode);
                rows.push_back(std::move(row));
                const std::string item_name = trim(rows.back().item_name);
                if (!item_name.empty()) {
                    barcode_item_names[barcode].insert(item_name);
                }
                continue;
            }

            auto& target = rows[found->second];
            const std::string item_name = trim(row.item_name);
            if (!item_name.empty() && barcode_item_names[barcode].insert(item_name).second) {
                if (!target.item_name.empty()) target.item_name += "/";
                target.item_name += item_name;
            }
            fill_if_empty(target.invoice_no, row.invoice_no);
            fill_if_empty(target.card_no, row.card_no);
            fill_if_empty(target.name, row.name);
            fill_if_empty(target.sex, row.sex);
            fill_if_empty(target.age, row.age);
            fill_if_empty(target.application_department, row.application_department);
            fill_if_empty(target.sample_name, row.sample_name);
            fill_if_empty(target.barcode_print_time, row.barcode_print_time);
            fill_if_empty(target.lab_department, row.lab_department);
            continue;
        }

        rows.push_back(std::move(row));
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    error.clear();
    return true;
#endif
}

}  // namespace search
