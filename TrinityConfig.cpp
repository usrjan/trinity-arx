// TrinityConfig.cpp
#include "StdAfx.h"
#include "TrinityConfig.h"
#include <fstream>
#include <sstream>

// Инициализация статических членов
TrinityDbConfig TrinityConfig::s_dbConfig;
TrinityPathConfig TrinityConfig::s_pathConfig;
TrinitySettingsConfig TrinityConfig::s_settingsConfig;
bool TrinityConfig::s_loaded = false;

// ============================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ДЛЯ ПАРСИНГА JSON
// ============================================
static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r\"");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r\",");
    return str.substr(first, last - first + 1);
}

static std::string extractJsonValue(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) return "";

    size_t colonPos = json.find(':', keyPos + searchKey.length());
    if (colonPos == std::string::npos) return "";

    size_t start = json.find_first_not_of(" \t\n\r", colonPos + 1);
    if (start == std::string::npos) return "";

    if (json[start] == '"') {
        // Строковое значение
        size_t end = json.find('"', start + 1);
        if (end == std::string::npos) return "";
        return json.substr(start + 1, end - start - 1);
    } else if (json.substr(start, 4) == "true") {
        return "true";
    } else if (json.substr(start, 5) == "false") {
        return "false";
    } else {
        // Числовое значение
        size_t end = json.find_first_of(",}]", start);
        if (end == std::string::npos) end = json.length();
        return trim(json.substr(start, end - start));
    }
}

static std::string extractJsonSection(const std::string& json, const std::string& section) {
    std::string searchKey = "\"" + section + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) return "{}";

    size_t braceStart = json.find('{', keyPos);
    if (braceStart == std::string::npos) return "{}";

    int braceCount = 1;
    size_t pos = braceStart + 1;
    while (pos < json.length() && braceCount > 0) {
        if (json[pos] == '{') braceCount++;
        else if (json[pos] == '}') braceCount--;
        pos++;
    }

    return json.substr(braceStart, pos - braceStart);
}

// ============================================
// ЗАГРУЗКА КОНФИГУРАЦИИ ИЗ ФАЙЛА
// ============================================
bool TrinityConfig::loadFromFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        acutPrintf(_T("\n[TrinityConfig] Cannot open config file: %hs\n"), filePath.c_str());
        loadDefaults();
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string jsonContent = buffer.str();
    file.close();

    // Парсим секцию database
    std::string dbSection = extractJsonSection(jsonContent, "database");
    s_dbConfig.host = extractJsonValue(dbSection, "host");
    s_dbConfig.user = extractJsonValue(dbSection, "user");
    s_dbConfig.password = extractJsonValue(dbSection, "password");
    s_dbConfig.database = extractJsonValue(dbSection, "name");

    // Парсим секцию paths
    std::string pathsSection = extractJsonSection(jsonContent, "paths");
    s_pathConfig.basePath = extractJsonValue(pathsSection, "base");

    // Парсим секцию settings
    std::string settingsSection = extractJsonSection(jsonContent, "settings");
    std::string timerStr = extractJsonValue(settingsSection, "timer_interval_ms");
    std::string loggingStr = extractJsonValue(settingsSection, "logging_enabled");

    s_settingsConfig.timerIntervalMs = timerStr.empty() ? 5000 : std::stoi(timerStr);
    s_settingsConfig.loggingEnabled = (loggingStr == "true");

    s_loaded = true;

    acutPrintf(_T("\n[TrinityConfig] Configuration loaded from: %hs\n"), filePath.c_str());
    return true;
}

// ============================================
// ЗАГРУЗКА ЗНАЧЕНИЙ ПО УМОЛЧАНИЮ
// ============================================
void TrinityConfig::loadDefaults() {
    s_dbConfig.host = "localhost";
    s_dbConfig.user = "root";
    s_dbConfig.password = "";
    s_dbConfig.database = "trinity_core";

    s_pathConfig.basePath = "C:\\\\trinity";

    s_settingsConfig.timerIntervalMs = 5000;
    s_settingsConfig.loggingEnabled = false;

    s_loaded = true;
    acutPrintf(_T("\n[TrinityConfig] Loaded default configuration\n"));
}
