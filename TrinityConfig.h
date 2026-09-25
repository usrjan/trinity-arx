// TrinityConfig.h
// Чтение конфигурации подключения к БД из trinity.ini,
// лежащего в папке самой DLL (рядом с библиотекой).
#pragma once

#include <string>

struct TrinityDbConfig {
    std::string host;
    std::string user;
    std::string pass;
    std::string db;
    std::string basePath;   // корневая папка для DWG-файлов проекта
};

// Загружает конфиг из trinity.ini в папке модуля.
// Возвращает false, если файл не найден или обязательные поля пусты.
bool loadTrinityConfig(TrinityDbConfig& out);
