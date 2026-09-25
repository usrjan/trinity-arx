// TrinityConfig.cpp
#include "StdAfx.h"
#include "TrinityConfig.h"

// ============================================
// Путь к trinity.ini в папке самой DLL
// ============================================
static std::wstring moduleIniPath() {
    wchar_t buf[MAX_PATH] = {0};
    HMODULE hm = nullptr;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      (LPCTSTR)&moduleIniPath, &hm);
    DWORD len = GetModuleFileName(hm, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L"";

    std::wstring path(buf, len);
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L"";
    return path.substr(0, slash + 1) + L"trinity.ini";
}

// ============================================
// Чтение строки из INI (ANSI-кодировка файла)
// ============================================
static std::string readIniStr(const std::wstring& ini,
                              const wchar_t* section,
                              const wchar_t* key) {
    wchar_t buf[512] = {0};
    GetPrivateProfileString(section, key, L"", buf, 512, ini.c_str());
    if (buf[0] == 0) return "";

    // wide -> utf8 (пароль может содержать не-ASCII)
    int need = WideCharToMultiByte(CP_UTF8, 0, buf, -1,
                                   nullptr, 0, nullptr, nullptr);
    if (need <= 0) return "";
    std::string out(need - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, -1,
                        &out[0], need, nullptr, nullptr);
    return out;
}

// ============================================
// Загрузка конфига
// ============================================
bool loadTrinityConfig(TrinityDbConfig& out) {
    std::wstring ini = moduleIniPath();
    if (ini.empty()) {
        acutPrintf(_T("\n[TrinityConfig] Cannot resolve module path\n"));
        return false;
    }

    if (GetFileAttributes(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        // ASCII-представление пути — безопасно для %hs
        int need = WideCharToMultiByte(CP_ACP, 0, ini.c_str(), -1,
                                       nullptr, 0, nullptr, nullptr);
        std::string ansi(need > 0 ? need - 1 : 0, '\0');
        if (need > 0)
            WideCharToMultiByte(CP_ACP, 0, ini.c_str(), -1,
                                &ansi[0], need, nullptr, nullptr);
        acutPrintf(_T("\n[TrinityConfig] Config file not found: %hs\n"), ansi.c_str());
        acutPrintf(_T("[TrinityConfig] Create it next to the ARX library. See trinity.ini.example\n"));
        return false;
    }

    out.host     = readIniStr(ini, L"database", L"host");
    out.user     = readIniStr(ini, L"database", L"user");
    out.pass     = readIniStr(ini, L"database", L"password");
    out.db       = readIniStr(ini, L"database", L"database");
    out.basePath = readIniStr(ini, L"paths",    L"base");

    if (out.host.empty() || out.user.empty() ||
        out.pass.empty() || out.db.empty()) {
        acutPrintf(_T("\n[TrinityConfig] Incomplete [database] section in trinity.ini\n"));
        return false;
    }
    if (out.basePath.empty()) {
        out.basePath = "D:\\trinity"; // резерв по умолчанию
    }
    return true;
}
