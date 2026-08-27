#include "search_text.h"

#include <algorithm>
#include <cctype>
#ifndef _WIN32
#include <codecvt>
#include <locale>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace search {

std::string trim(std::string text) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
    text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
    return text;
}

// Qt migration: replace with QString::fromStdWString(...).toUtf8().toStdString()
std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }

#ifdef _WIN32
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), size, nullptr, nullptr);
    if (!out.empty() && out.back() == '\0') {
        out.pop_back();
    }
    return out;
#else
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.to_bytes(text);
#endif
}

// Qt migration: replace with QString::fromUtf8(text).toStdWString()
std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }

#ifdef _WIN32
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), size);
    if (!out.empty() && out.back() == L'\0') {
        out.pop_back();
    }
    return out;
#else
    return std::wstring_convert<std::codecvt_utf8<wchar_t>>{}.from_bytes(text);
#endif
}

}  // namespace search
