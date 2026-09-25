// StdAfx.cpp
#include "StdAfx.h"

// ============================================
// Конвертация std::string → wchar_t* (в буфер вызывающего)
// Гарантирует null-терминатор даже при обрезке.
// ============================================
void stringToWide(const std::string& str, wchar_t* out, size_t maxLen) {
    if (!out || maxLen == 0) return;
    out[0] = L'\0';
    int need = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (need <= 0) return;
    if (static_cast<size_t>(need) > maxLen) need = static_cast<int>(maxLen);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, out, need);
}

// ============================================
// Конвертация UTF-8 → std::wstring (RAII, без ручных free())
// ============================================
std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring w(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], len);
    return w;
}

// ============================================
// Логирование в командную строку AutoCAD
// ============================================
void trinityLog(const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    acutPrintf(L"\n%s", buf);
}