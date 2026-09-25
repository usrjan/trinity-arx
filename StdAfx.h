// StdAfx.h
#pragma once

// ============================================
// Windows
// ============================================
#include <windows.h>
#include <tchar.h>

// ============================================
// MySQL — C API (libmysql / mysqlclient)
// ============================================
// Требуется ТОЛЬКО клиентская библиотека (Connector/C 8.x или 5.7): заголовки + libmysql.lib.
// Сервер MySQL отдельно ставить НЕ нужно — он работает на хосте 192.168.30.5.
// Путь к include задаётся в Trinity.vcxproj: D:\devel\MySQL\include
// (или переопределите через переменную окружения MYSQL_INCLUDE_DIR).
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
#    error "MySQL client headers not found. Install MySQL Connector/C and add its \\include folder to Additional Include Directories (Trinity.vcxproj -> C/C++ -> General), e.g. D:\\devel\\MySQL\\include"
#  endif
#else
// MSVC < 19.20 без __has_include: пробуем напрямую
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