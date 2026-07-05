#include "search_core.h"
#include "search_text.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <sqlext.h>
#endif

namespace search {
namespace {

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

int seconds_between_sql_datetimes(const std::string& start, const std::string& end) {
    std::tm start_tm{};
    std::tm end_tm{};
    if (!parse_sql_datetime(start, start_tm) || !parse_sql_datetime(end, end_tm)) {
        return -1;
    }
    const std::time_t start_time = std::mktime(&start_tm);
    const std::time_t end_time = std::mktime(&end_tm);
    if (start_time == static_cast<std::time_t>(-1) || end_time == static_cast<std::time_t>(-1)) {
        return -1;
    }
    const double seconds = std::difftime(end_time, start_time);
    if (seconds < 0) return -1;
    return static_cast<int>(seconds);
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

void add_barcode_machine_status(std::ostringstream& sql, const std::string& value) {
    const auto status = trim(value);
    if (status.empty() || status == "全部") {
        return;
    }
    if (status == "已签收未上机") {
        sql << " AND isnull(rs.REPORT_SENT,0)=0"
            << " AND isnull(rs.REPORT_REVIEWED,0)=0"
            << " AND isnull(rs.HAS_REPORT,0)=0"
            << " AND isnull(b.OPER_STATE,0)=0";
    } else if (status == "已上机未审核") {
        sql << " AND isnull(rs.REPORT_SENT,0)=0"
            << " AND isnull(rs.REPORT_REVIEWED,0)=0"
            << " AND (isnull(rs.HAS_REPORT,0)=1 OR isnull(b.OPER_STATE,0)>=1)";
    } else if (status == "已审核未发送") {
        sql << " AND isnull(rs.REPORT_SENT,0)=0"
            << " AND isnull(rs.REPORT_REVIEWED,0)=1";
    } else if (status == "发送完成") {
        sql << " AND isnull(rs.REPORT_SENT,0)=1";
    }
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
    DbContext db;
    if (!connect(filters.connection_string, db, error, log)) return false;

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
        where << " AND " << date_col << " < DATEADD(day,1,'" << sql_escape(trim(filters.end_date)) << "')";
    }
    add_like(where, "b.BARCODE", filters.barcode);
    add_like(where, "b.NAME", filters.patient_name);
    add_like(where, "b.REG_NO", filters.reg_no);
    add_eq(where, "CONVERT(varchar(20),b.ROOM_CODE)", filters.room_code);

    add_barcode_machine_status(where, filters.machine_status);

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
        << " r.REP_NO,r.OPER_NO,r.CHK_DATE,r.MACH_CODE,r.ROOM_CODE,mach.MACH_NAME"
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
        row.fee            = fetch_column(stmt, 14);
        row.request_doctor = fetch_column(stmt, 15);
        row.status         = fetch_column(stmt, 16);
        row.note           = fetch_column(stmt, 17);
        row.reason         = fetch_column(stmt, 18);
        row.submitter      = fetch_column(stmt, 19);
        row.submit_time    = fetch_column(stmt, 20);
        row.request_time   = fetch_column(stmt, 21);
        row.cancel_time    = fetch_column(stmt, 22);
        row.cancel_operator = fetch_column(stmt, 23);
        row.hzid           = fetch_column(stmt, 24);
        row.machine_status = fetch_column(stmt, 25);
        row.report_no      = fetch_column(stmt, 26);
        row.machine_code   = fetch_column(stmt, 27);
        row.machine_name   = fetch_column(stmt, 28);
        row.room_code      = fetch_column(stmt, 29);
        row.inspect_date   = fetch_column(stmt, 30);
        rows.push_back(row);
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
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
