// TrinityCore.h
#pragma once
#include "StdAfx.h"

// ============================================
// MySQL C API — используется ТОЛЬКО стабильный публичный C API libmysql
// (идентичен в Connector/C 5.7 и 8.x): mysql_init / mysql_options /
// mysql_real_connect / mysql_close / mysql_ping / mysql_query /
// mysql_store_result / mysql_fetch_row / mysql_free_result / mysql_num_rows /
// mysql_errno / mysql_error / mysql_real_escape_string / mysql_set_character_set.
// my_bool и MYSQL_OPT_RECONNECT НЕ используются (в Connector/C 8.0 удалены).
// Заголовок mysql.h подключается в StdAfx.h; если он не найден — компиляция
// прервётся с понятным сообщением (#error) и инструкцией по установке.
// ============================================
#ifndef TRINITY_HAS_MYSQL
#define TRINITY_HAS_MYSQL 1
#endif

// ============================================
// ПОЗИЦИЯ
// ============================================
struct TrinityPosition {
    double x = 0, y = 0, z = 0;
    AcGePoint3d toAcGe() const { return AcGePoint3d(x, y, z); }
};

// ============================================
// ПОВОРОТ (одиночный)
// ============================================
struct TrinityRotation {
    double x = 0, y = 0, z = 0;   // ось
    double angle = 0;              // угол в градусах
};

// ============================================
// СОСТАВНОЙ ПОВОРОТ (до 2 последовательных)
// ============================================
struct TrinityRotationCompound {
    TrinityRotation rotations[2];
    int count = 0;  // 0, 1 или 2
};

// ============================================
// НЕЙРОН
// ============================================
struct TrinityNeuron {
    int id = 0;
    std::string code;          // D.S.0.425.850.10
    std::string type;          // detail, construction, project
    std::string category;      // shield, rib, sidewall, formwork
    std::string material;      // PLYWOOD-FSF
    std::string status;        // pending, done, error

    int width = 0;
    int height = 0;
    int thickness = 10;
    int processCode = 0;       // 0=None, 2=Hole, 3=Socket

    std::string jsonData;      // весь JSON нейрона
};

// ============================================
// СИНАПС
// ============================================
struct TrinitySynapse {
    int id = 0;
    int parentId = 0;
    int childId = 0;
    std::string childCode;
    TrinityPosition position;
    TrinityRotationCompound rotation;
    std::string status;
};

// ============================================
// ЯДРО — доступ к базе данных
// ============================================
class TrinityCore {
    MYSQL* m_mysql = nullptr;
    bool m_connected = false;

    // Повторное подключение с повторами (вызывается при обрыве соединения)
    bool reconnect(int maxAttempts = 3, unsigned delayMs = 500);

    // Проверка живости соединения (mysql_ping, не чаще раза в MIN_PING_INTERVAL_S)
    // с автоматическим переподключением. Вызывается перед каждым запросом.
    bool ensureConnected();

    // Параметры последнего успешного подключения — используются для переподключения
    std::string m_host, m_user, m_pass, m_db;

    // Восстановление соединения после ошибки обрыва в середине запроса:
    // переподключение + повторная отправка того же SQL. true — запрос выполнен успешно.
    bool recoverQuery(const char* query);

public:
    TrinityCore() = default;
    ~TrinityCore();

    bool connect(const char* host, const char* user, const char* pass, const char* db);
    void disconnect();
    bool isConnected() const { return m_connected; }
    MYSQL* handle() { return m_mysql; }

    // Нейроны
    TrinityNeuron* loadNeuronByCode(const std::string& code);
    TrinityNeuron* loadNeuronById(int id);

    // Связи
    std::vector<TrinitySynapse> loadChildren(int parentId);

    // Проекты
    std::vector<TrinityNeuron> loadPendingProjects();

    // Статусы
    bool markNeuronDone(int id);

    // Экранирование строки для безопасной подстановки в SQL (учитывает кодировку соединения).
    // Возвращает готовый литерал с кавычками: 'escaped\'text' — подставляется в запрос как есть.
    // Пустая строка -> NULL (для необязательных параметров).
    std::string escapeSqlLiteral(const std::string& value, bool emptyMeansNull = false) const;

    // Парсинг
    static TrinityNeuron parseNeuronRow(MYSQL_ROW row);
    static TrinitySynapse parseSynapseRow(MYSQL_ROW row);
    static TrinityPosition parsePosition(const std::string& json);
    static TrinityRotationCompound parseRotation(const std::string& json);
};