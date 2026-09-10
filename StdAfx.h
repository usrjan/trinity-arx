// StdAfx.h
#pragma once

#include <windows.h>
#include <tchar.h>
#include <mysql.h>

#include <aced.h>
#include <acestext.h>
#include <axlock.h>
#include <acutads.h>
#include <rxregsvc.h>
#include <acdocman.h>
#include <adscodes.h>
#include <acedads.h>
#include <acdb.h>
#include <dbents.h>
#include <dbsymtb.h>
#include <dbgroup.h>
#include <dbapserv.h>
#include <dbsol3d.h>
#include <dbregion.h>
#include <gepnt3d.h>
#include <gevec3d.h>
#include <gemat3d.h>
#include <actrans.h>

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <io.h>
#include <direct.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void stringToWide(const std::string& str, wchar_t* out, size_t maxLen);
wchar_t* utf2uni(const char* utf8_string);