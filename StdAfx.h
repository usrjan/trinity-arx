// StdAfx.h
#pragma once

// ============================================
// Windows
// ============================================
#include <windows.h>
#include <tchar.h>

// ============================================
// MySQL
// ============================================
#include <mysql.h>

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
#include <dbdict.h>
#include <dbxrecord.h>
#include <dbdynblk.h>
#include <DbBlockTableRecord.h>
#include <DbDynBlockReferenceProperty.h>
#include <DbDynBlockReference.h>

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