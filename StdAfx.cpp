// StdAfx.cpp
#include "StdAfx.h"

void stringToWide(const std::string& str, wchar_t* out, size_t maxLen) {
    if (out && maxLen > 0)
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, out, static_cast<int>(maxLen));
}

wchar_t* utf2uni(const char* utf8_string) {
    if (!utf8_string) return nullptr;
    int res_len = MultiByteToWideChar(CP_UTF8, 0, utf8_string, -1, NULL, 0);
    if (res_len == 0) return nullptr;
    wchar_t* res = (wchar_t*)calloc(sizeof(wchar_t), res_len);
    if (!res) return nullptr;
    MultiByteToWideChar(CP_UTF8, 0, utf8_string, -1, res, res_len);
    return res;
}