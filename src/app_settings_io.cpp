#include "app_settings_io.h"
#include "search_text.h"

#ifdef _WIN32

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <vector>

namespace search {

namespace {

constexpr const wchar_t* CONFIG_FILE_NAME = L"ClientConfig.ini";
constexpr const wchar_t* LEGACY_CONFIG_FILE_NAME = L"result_search.ini";
constexpr const wchar_t* ENCODED_VALUE_PREFIX = L"@utf8hex:";

int clamp_font_size(int value) {
    return std::max(8, std::min(24, value));
}

std::wstring join_path(const std::wstring& dir, const wchar_t* file) {
    if (dir.empty()) return file;
    const wchar_t last = dir.back();
    if (last == L'\\' || last == L'/') {
        return dir + file;
    }
    return dir + L"\\" + file;
}

bool file_exists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool read_file_bytes(const std::wstring& path, std::vector<unsigned char>& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(file);
        return false;
    }

    bytes.assign(static_cast<size_t>(size.QuadPart), 0);
    DWORD read = 0;
    const BOOL ok = bytes.empty() ||
        ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) {
        bytes.clear();
        return false;
    }
    bytes.resize(read);
    return true;
}

bool has_utf16le_bom(const std::vector<unsigned char>& bytes) {
    return bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE;
}

bool looks_like_utf16le_ini(const std::vector<unsigned char>& bytes) {
    if (bytes.size() < 8) {
        return false;
    }
    const size_t sample = std::min<size_t>(bytes.size(), 256);
    size_t nul_odd = 0;
    size_t ascii_even = 0;
    size_t pairs = 0;
    for (size_t i = 0; i + 1 < sample; i += 2) {
        ++pairs;
        if (bytes[i + 1] == 0) {
            ++nul_odd;
        }
        if (bytes[i] == '\r' || bytes[i] == '\n' || bytes[i] == '\t' ||
            (bytes[i] >= 0x20 && bytes[i] <= 0x7E)) {
            ++ascii_even;
        }
    }
    return pairs > 0 && nul_odd * 4 >= pairs * 3 && ascii_even * 2 >= pairs;
}

std::wstring utf16le_bytes_to_wide(const std::vector<unsigned char>& bytes, size_t offset) {
    if (bytes.size() <= offset) {
        return {};
    }
    std::wstring text;
    text.reserve((bytes.size() - offset) / 2);
    for (size_t i = offset; i + 1 < bytes.size(); i += 2) {
        const wchar_t ch = static_cast<wchar_t>(bytes[i] | (bytes[i + 1] << 8));
        text.push_back(ch);
    }
    return text;
}

std::string wide_to_acp_bytes(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string bytes(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()),
                        bytes.data(), needed, nullptr, nullptr);
    return bytes;
}

bool write_acp_file(const std::wstring& path, const std::wstring& text) {
    const std::string bytes = wide_to_acp_bytes(text);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = bytes.empty() ||
        WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(file);
    return ok != FALSE;
}

void normalize_legacy_unicode_ini_file(const std::wstring& path) {
    std::vector<unsigned char> bytes;
    if (!read_file_bytes(path, bytes)) {
        return;
    }
    if (has_utf16le_bom(bytes)) {
        write_acp_file(path, utf16le_bytes_to_wide(bytes, 2));
    } else if (looks_like_utf16le_ini(bytes)) {
        write_acp_file(path, utf16le_bytes_to_wide(bytes, 0));
    }
}

bool should_encode_module_value(const std::wstring& value) {
    if (value.rfind(ENCODED_VALUE_PREFIX, 0) == 0) {
        return true;
    }
    for (wchar_t ch : value) {
        if (ch < 0x20 || ch > 0x7E) {
            return true;
        }
    }
    return false;
}

std::wstring encode_module_value(const std::wstring& value) {
    if (!should_encode_module_value(value)) {
        return value;
    }
    const std::string utf8 = search::wide_to_utf8(value);
    static constexpr wchar_t hex[] = L"0123456789ABCDEF";
    std::wstring encoded(ENCODED_VALUE_PREFIX);
    encoded.reserve(wcslen(ENCODED_VALUE_PREFIX) + utf8.size() * 2);
    for (unsigned char ch : utf8) {
        encoded.push_back(hex[(ch >> 4) & 0x0F]);
        encoded.push_back(hex[ch & 0x0F]);
    }
    return encoded;
}

int hex_value(wchar_t ch) {
    if (ch >= L'0' && ch <= L'9') return ch - L'0';
    if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
    if (ch >= L'A' && ch <= L'F') return ch - L'A' + 10;
    return -1;
}

std::wstring decode_module_value(const std::wstring& value) {
    if (value.rfind(ENCODED_VALUE_PREFIX, 0) != 0) {
        return value;
    }
    const size_t prefix_len = wcslen(ENCODED_VALUE_PREFIX);
    const size_t hex_len = value.size() - prefix_len;
    if (hex_len % 2 != 0) {
        return value;
    }
    std::string utf8;
    utf8.reserve(hex_len / 2);
    for (size_t i = prefix_len; i < value.size(); i += 2) {
        const int hi = hex_value(value[i]);
        const int lo = hex_value(value[i + 1]);
        if (hi < 0 || lo < 0) {
            return value;
        }
        utf8.push_back(static_cast<char>((hi << 4) | lo));
    }
    return search::utf8_to_wide(utf8);
}

void migrate_legacy_config_if_needed(const std::wstring& dir) {
    static bool checked = false;
    if (checked) return;
    checked = true;

    const auto current = join_path(dir, CONFIG_FILE_NAME);
    const auto legacy = join_path(dir, LEGACY_CONFIG_FILE_NAME);
    if (!file_exists(current) && file_exists(legacy)) {
        CopyFileW(legacy.c_str(), current.c_str(), TRUE);
    }
}

}  // namespace

std::wstring module_dir() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(len);
    const auto pos = buffer.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return buffer.substr(0, pos);
}

std::wstring default_ini_path() {
    const auto dir = module_dir();
    migrate_legacy_config_if_needed(dir);
    return join_path(dir, CONFIG_FILE_NAME);
}

AppSettings load_settings(const std::wstring& ini_path) {
    normalize_legacy_unicode_ini_file(ini_path);
    AppSettings s;
    auto read_str = [&](const wchar_t* key, const wchar_t* fallback = L"") {
        wchar_t buf[1024] = {};
        GetPrivateProfileStringW(L"Database", key, fallback, buf, 1024, ini_path.c_str());
        return std::wstring(buf);
    };
    auto read_int = [&](const wchar_t* section, const wchar_t* key, int fallback) {
        return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, ini_path.c_str()));
    };
    s.db.server = read_str(L"Server");
    s.db.initial_database = read_str(L"InitialDatabase");
    s.db.user = read_str(L"User");
    s.db.password = read_str(L"Password");
    s.ui.font_size = clamp_font_size(read_int(L"UI", L"FontSize", s.ui.font_size));
    s.ui.splitter_x = read_int(L"UI", L"SplitterX", s.ui.splitter_x);

    auto read_lis_str = [&](const wchar_t* key, const std::wstring& fallback) {
        wchar_t buf[1024] = {};
        GetPrivateProfileStringW(L"LisSummary", key, fallback.c_str(), buf, 1024, ini_path.c_str());
        if (buf[0] == L'\0') {
            return fallback;
        }
        return std::wstring(buf);
    };
    auto read_lis_optional_str = [&](const wchar_t* key, const std::wstring& fallback) {
        wchar_t buf[1024] = {};
        GetPrivateProfileStringW(L"LisSummary", key, fallback.c_str(), buf, 1024, ini_path.c_str());
        return std::wstring(buf);
    };
    s.lis.abo_codes = read_lis_str(L"AboCodes", s.lis.abo_codes);
    s.lis.rhd_codes = read_lis_str(L"RhdCodes", s.lis.rhd_codes);
    s.lis.hgb_codes = read_lis_str(L"HgbCodes", s.lis.hgb_codes);
    s.lis.plt_codes = read_lis_str(L"PltCodes", s.lis.plt_codes);
    s.lis.irregular_antibody_codes = read_lis_str(L"IrregularAntibodyCodes", s.lis.irregular_antibody_codes);
    s.lis.direct_antiglobulin_codes = read_lis_str(L"DirectAntiglobulinCodes", s.lis.direct_antiglobulin_codes);
    s.lis.blood_type_machines = read_lis_optional_str(L"BloodTypeMachines", s.lis.blood_type_machines);
    s.lis.cbc_machines = read_lis_optional_str(L"CbcMachines", s.lis.cbc_machines);
    s.lis.blood_lis_exclude_machines = read_lis_optional_str(L"BloodLisExcludeMachines", s.lis.blood_lis_exclude_machines);
    return s;
}

bool save_settings(const std::wstring& ini_path, const AppSettings& s) {
    normalize_legacy_unicode_ini_file(ini_path);
    auto write_str = [&](const wchar_t* key, const std::wstring& value) {
        return WritePrivateProfileStringW(L"Database", key, value.c_str(), ini_path.c_str()) != FALSE;
    };
    bool ok = true;
    ok &= write_str(L"Server", s.db.server);
    ok &= write_str(L"InitialDatabase", s.db.initial_database);
    ok &= write_str(L"User", s.db.user);
    ok &= write_str(L"Password", s.db.password);
    ok &= WritePrivateProfileStringW(L"Database", L"Database", nullptr, ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"Database", L"Driver", nullptr, ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"UI", L"FontSize", std::to_wstring(clamp_font_size(s.ui.font_size)).c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"UI", L"SplitterX", std::to_wstring(s.ui.splitter_x).c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"LisSummary", L"AboCodes", s.lis.abo_codes.c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"LisSummary", L"RhdCodes", s.lis.rhd_codes.c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"LisSummary", L"HgbCodes", s.lis.hgb_codes.c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"LisSummary", L"PltCodes", s.lis.plt_codes.c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"LisSummary", L"IrregularAntibodyCodes", s.lis.irregular_antibody_codes.c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"LisSummary", L"DirectAntiglobulinCodes", s.lis.direct_antiglobulin_codes.c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"LisSummary", L"BloodTypeMachines", s.lis.blood_type_machines.c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"LisSummary", L"CbcMachines", s.lis.cbc_machines.c_str(), ini_path.c_str()) != FALSE;
    ok &= WritePrivateProfileStringW(L"LisSummary", L"BloodLisExcludeMachines", s.lis.blood_lis_exclude_machines.c_str(), ini_path.c_str()) != FALSE;
    return ok;
}

void save_module_int(const wchar_t* module, const wchar_t* key, int value) {
    const std::wstring path = default_ini_path();
    normalize_legacy_unicode_ini_file(path);
    WritePrivateProfileStringW(module, key, std::to_wstring(value).c_str(), path.c_str());
}

void save_module_str(const wchar_t* module, const wchar_t* key, const std::wstring& value) {
    const std::wstring path = default_ini_path();
    normalize_legacy_unicode_ini_file(path);
    const std::wstring stored = encode_module_value(value);
    WritePrivateProfileStringW(module, key, stored.c_str(), path.c_str());
}

int load_module_int(const wchar_t* module, const wchar_t* key, int fallback) {
    const std::wstring path = default_ini_path();
    normalize_legacy_unicode_ini_file(path);
    return static_cast<int>(GetPrivateProfileIntW(module, key, fallback, path.c_str()));
}

std::wstring load_module_str(const wchar_t* module, const wchar_t* key, const wchar_t* fallback) {
    const std::wstring path = default_ini_path();
    normalize_legacy_unicode_ini_file(path);
    wchar_t buf[1024] = {};
    GetPrivateProfileStringW(module, key, fallback, buf, 1024, path.c_str());
    return decode_module_value(std::wstring(buf));
}

}  // namespace search

#endif
