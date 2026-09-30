// StdAfx.cpp
#include "StdAfx.h"

// stringToWide()/utf2uni() — устаревшие небезопасные конвертеры (неза-
// писанный мусор в буфере при переполнении / сырой malloc-буфер, требу-
// ющий free()). Объявлений в StdAfx.h больше нет, вызовов в TU проекта
// тоже не осталось (все места переведены на utf8ToWide()) — реализации
// удалены как мёртвый код.

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