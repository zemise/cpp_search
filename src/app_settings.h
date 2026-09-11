#pragma once

#include <string>

namespace search {

struct DbSettings {
    std::wstring server;
    std::wstring initial_database;
    std::wstring user;
    std::wstring password;
};

struct UiSettings {
    int font_size = 9;
    int splitter_x = 0;
};

struct LisSummaryCodeSettings {
    std::wstring abo_codes = L"91962;11101;91963;11102";
    std::wstring rhd_codes = L"91964;11103";
    std::wstring hgb_codes = L"91672;90891;1013;92563;90943;89786";
    std::wstring plt_codes = L"91678;90897;1019;92569;90949";
    std::wstring irregular_antibody_codes = L"11106;91966";
    std::wstring direct_antiglobulin_codes = L"11105;91965";
    std::wstring blood_type_machines = L"11:11101;64:626";
    std::wstring cbc_machines = L"1:1002,1011,1012;61:613,615";
    std::wstring blood_lis_exclude_machines = L"3:;71:;8:8004";
};

struct AppSettings {
    DbSettings db;
    UiSettings ui;
    LisSummaryCodeSettings lis;
};

std::wstring build_connection_string_w(const DbSettings& settings);

}  // namespace search
