// TrinityConfig.h
#pragma once
#include <string>

struct TrinityDbConfig {
    std::string host;
    std::string user;
    std::string password;
    std::string database;
};

struct TrinityPathConfig {
    std::string basePath;
};

struct TrinitySettingsConfig {
    int timerIntervalMs;
    bool loggingEnabled;
};

class TrinityConfig {
private:
    static TrinityDbConfig s_dbConfig;
    static TrinityPathConfig s_pathConfig;
    static TrinitySettingsConfig s_settingsConfig;
    static bool s_loaded;

    static void loadDefaults();

public:
    // Загрузить конфигурацию из JSON-файла
    static bool loadFromFile(const std::string& filePath);

    // Геттеры
    static const TrinityDbConfig& getDbConfig() {
        if (!s_loaded) loadDefaults();
        return s_dbConfig;
    }

    static const TrinityPathConfig& getPathConfig() {
        if (!s_loaded) loadDefaults();
        return s_pathConfig;
    }

    static const TrinitySettingsConfig& getSettingsConfig() {
        if (!s_loaded) loadDefaults();
        return s_settingsConfig;
    }

    // Для сброса (тестирование)
    static void unload() { s_loaded = false; }
};
