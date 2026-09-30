// TrinityConfig.h
// Чтение конфигурации подключения к БД из trinity.ini,
// лежащего в папке самой DLL (рядом с библиотекой).
#pragma once

#include <string>

// ============================================================
// Конфигурация плагина. Все методы — СТАТИЧЕСКИЕ УТИЛИТЫ: они
// не имеют состояния «объекта конфигурации» и раньше были сво-
// бодными функциями в глобальном пространстве имён (утечка аб-
// стракции: любой TU видел loadTrinityConfig()/moduleIniPath()
// как globals). Теперь API собран в классе — точка входа одна,
// служебные детали скрыты в private-секции.
// ============================================================
class TrinityConfig {
public:
    struct DbConfig {
        std::string host;
        std::string user;
        std::string pass;
        std::string db;
        std::string basePath;   // корневая папка для DWG-файлов проекта

        bool isValid() const {
            return !host.empty() && !user.empty() &&
                   !pass.empty() && !db.empty();
        }
    };

    // Загружает конфиг из trinity.ini в папке модуля.
    // Возвращает false, если файл не найден или обязательные поля пусты.
    static bool load(DbConfig& out);

    // Эквивалентный «бросковый» вариант: при ошибке возвращает конфиг
    // с пустыми полями (isValid() == false).
    static DbConfig load();

    // Путь к trinity.ini в папке самой DLL (был file-static free-func-
    // tion moduleIniPath() — инкапсулирован как приватная утилита).
    static std::wstring iniPath();

private:
    // Чтение строки из INI (ANSI-кодировка файла). Реализация — в .cpp,
    // наружу не торчит: это деталь формата trinity.ini, а не API.
    static std::string readIniStr(const std::wstring& ini,
                                  const wchar_t* section,
                                  const wchar_t* key);
};

// ---- Обратная совместимость ----
// Старые имена сохраняются как тонкие алиасы, чтобы внешний код не сло-
// мался; новый код обязан вызывать статические утилиты TrinityConfig::.
typedef TrinityConfig::DbConfig TrinityDbConfig;

inline bool loadTrinityConfig(TrinityDbConfig& out) {
    return TrinityConfig::load(out);
}
