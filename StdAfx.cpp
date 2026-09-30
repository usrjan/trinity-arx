// StdAfx.cpp
#include "StdAfx.h"

// ============================================
// Конвертация std::string → wchar_t* (в буфер)
// НЕ используется в коде проекта: небезопасна (при переполнении буфера
// MultiByteToWideChar не пишет ничего, и в буфере остаётся мусор).
// Оставлена только для совместимости со старым кодом.
// Применяйте utf8ToWide() ниже.
// ============================================
[[deprecated("use utf8ToWide(): fixed-size buffer conversion is unsafe")]]
void stringToWide(const std::string& str, wchar_t* out, size_t maxLen) {
    if (out && maxLen > 0) {
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, out, static_cast<int>(maxLen));
    }
}

// ============================================
// Конвертация UTF-8 → wchar_t* (возвращает новый указатель)
// Нужно освобождать через free()
// ============================================
wchar_t* utf2uni(const char* utf8_string) {
    if (!utf8_string) return nullptr;

    int res_len = MultiByteToWideChar(CP_UTF8, 0, utf8_string, -1, nullptr, 0);
    if (res_len == 0) return nullptr;

    wchar_t* res = (wchar_t*)calloc(sizeof(wchar_t), res_len);
    if (!res) return nullptr;

    int err = MultiByteToWideChar(CP_UTF8, 0, utf8_string, -1, res, res_len);
    if (err == 0) {
        free(res);
        return nullptr;
    }

    return res;
}

// ============================================
// Безопасная конвертация UTF-8 -> std::wstring
// Точный размер буфера всегда соответствует длине строки —
// переполнение и чтение неинициализированной памяти исключены.
// ============================================
std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();

    int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (need <= 0) return std::wstring();

    std::wstring w(static_cast<size_t>(need), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], need) == 0)
        return std::wstring();

    w.resize(static_cast<size_t>(need) - 1); // убрать терминальный ноль
    return w;
}