// StdAfx.h
#pragma once

// ============================================
// Windows
// ============================================
#include <windows.h>
#include <tchar.h>

// Стандартные типы, необходимые уже в этом заголовке (stringToWide) и всем,
// кто его включает: на MSDN-сборке пробрасывались через цепочку mysql.h,
// теперь подключаются явно — независимая от MySQL корректная компиляция.
#include <string>
#include <vector>

// ============================================
// MySQL — C API (клиентская библиотека)
// ============================================
// ВАЖНО: этот плагин (ARX/.dll) сам по себе работает и БЕЗ MySQL-библиотек —
// они нужны только для компиляции модуля работы с базой (TrinityCore.cpp).
// Если заголовки mysql.h не найдены, код ниже НЕ будет выдавать ошибку:
// вместо этого определится TRINITY_HAS_MYSQL=0, и все обращения к БД
// безопасно отключатся (функции вернут «нет подключения»). Плагин соберётся
// и заработает; чтобы включить базу — укажите путь к include/lib клиентской
// библиотеки в Trinity.vcxproj (MysqlIncludeDir/MysqlLibDir) или установите
// MySQL Connector/C ZIP (сервер MySQL ставить на эту машину НЕ нужно —
// он работает на 192.168.30.5).
//
// Как найти mysql.h автоматически: если задана переменная окружения
// MYSQL_INCLUDE_DIR (MSVC раскрывает её как $(MYSQL_INCLUDE_DIR)), она уже
// добавлена в пути компилятора через vcxproj. Дополнительно пробуем
// стандартные расположения.
#ifdef TRINITY_HAS_MYSQL
#undef TRINITY_HAS_MYSQL
#endif
#if defined(__has_include)
#  if __has_include(<mysql.h>)
#    include <mysql.h>
#    define TRINITY_HAS_MYSQL 1
#  elif __has_include(<mysql/mysql.h>)
#    include <mysql/mysql.h>
#    define TRINITY_HAS_MYSQL 1
#  else
#    define TRINITY_HAS_MYSQL 0
#    pragma message("StdAfx.h: mysql.h not found - building WITHOUT MySQL support. \
Set MysqlIncludeDir in Trinity.vcxproj (or env MYSQL_INCLUDE_DIR) and rebuild to enable DB access.")
#  endif
#else
// Старый компилятор без __has_include: пробуем напрямую; если не найдётся —
// получите C1083, тогда добавьте путь к include в свойства проекта.
#  include <mysql.h>
#  define TRINITY_HAS_MYSQL 1
#endif

// ============================================
// AutoCAD ObjectARX 2026 — Основные
// ============================================
#include <aced.h>
#include <acestext.h>
#include <axlock.h>
#include <acutads.h>
#include <rxregsvc.h>
#include <acdocman.h>
#include <adscodes.h>
#include <acedads.h>

// ============================================
// AutoCAD — База данных
// ============================================
#include <acdb.h>
#include <dbents.h>
#include <dbsymtb.h>
#include <dbgroup.h>
#include <dbapserv.h>

// ============================================
// AutoCAD — 3D
// ============================================
#include <dbsol3d.h>
#include <dbregion.h>
#include <gepnt3d.h>
#include <gevec3d.h>
#include <gemat3d.h>

// ============================================
// AutoCAD — Транзакции
// ============================================
#include <actrans.h>

// ============================================
// Стандартная библиотека
// ============================================
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <cstdio>
#include <cmath>

// ============================================
// Файловые операции
// ============================================
#include <io.h>
#include <direct.h>

// ============================================
// Константы
// ============================================
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ============================================
// Объявления утилит (реализация в StdAfx.cpp)
// ============================================
void stringToWide(const std::string& str, wchar_t* out, size_t maxLen);
wchar_t* utf2uni(const char* utf8_string);