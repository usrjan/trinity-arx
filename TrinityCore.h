// TrinityCore.h
#pragma once
#include "StdAfx.h"

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

    // Сохранённые параметры подключения — нужны для автоматического
    // переподключения (ensureConnected) после обрыва соединения сервером.
    std::string m_host, m_user, m_pass, m_db;

    // Полное закрытие старого хэндла перед новым mysql_init
    void closeHandle();

public:
    TrinityCore() = default;
    ~TrinityCore();

    bool connect(const char* host, const char* user, const char* pass, const char* db);
    void disconnect();
    bool isConnected() const { return m_connected; }
    MYSQL* handle() { return m_mysql; }

    // Проверка живости соединения через mysql_ping(). При обрыве
    // (ошибки 2006/2013 и др.) автоматически переподключается с
    // сохранёнными параметрами. Возвращает true, если соединение живо.
    // Вызывается в начале каждой публичной операции с БД.
    bool ensureConnected();

private:
    // Логирование ошибки последнего неудачного mysql_query в консоль AutoCAD.
    // При фатальных кодах обрыва помечает соединение мёртвым для реконнекта.
    void logQueryError(const char* context, const std::string& query);

    // Проверка после подключения: существует ли уникальный индекс neuron.idx_code
    // (он покрывает виртуальный столбец code и обязателен, чтобы loadNeuronByCode()
    // не сканировал таблицу целиком). При отсутствии — предупреждение в консоль.
    void verifyCodeIndex();

public:

    // Нейроны
    // Поиск по бизнес-коду (D.S.0.425.850.10 / PROJ-TEST-001). Опирается на
    // виртуальный столбец neuron.code + UNIQUE KEY idx_code — см. комментарии
    // к реализации в TrinityCore.cpp. Вызывается рекурсивно при сборке DWG.
    TrinityNeuron* loadNeuronByCode(const std::string& code);
    TrinityNeuron* loadNeuronById(int id);

    // Связи
    std::vector<TrinitySynapse> loadChildren(int parentId);

    // Проекты
    // Загружает проекты со статусом 'pending' ИЛИ 'building'.
    //   'pending'  — новая заявка, собирается впервые;
    //   'building' — сборка была начата, но процесс завершился аварийно
    //                (крах плагина/AutoCAD, обрыв БД). Такой проект
    //                подхватывается следующим тиком и пересобирается.
    // Статусы 'done' и 'error' в выборку НЕ попадают:
    //   'done'  — успешно собранные файлы на диске;
    //   'error' — сборка провалилась N раз подряд (см. markNeuronError):
    //               бесконечные перезапуски каждые 5 секунд исключены,
    //               повтор возможен только после ручного сброса статуса
    //               в 'pending' (см. database.sql, блок СБРОС СТАТУСА).
    std::vector<TrinityNeuron> loadPendingProjects();

    // Статусы
    // Переход pending -> building перед началом сборки проекта. Нужен для
    // восстановления после аварийных завершений: проект, зависший в 'building'
    // (крах процесса до финального статуса), подхватывается следующим тиком.
    bool setBuildStatus(int id);

    // Успешное завершение сборки: $.status = 'done', счётчик попыток $.build_attempts
    // очищается (следующая заявка того же проекта начнёт с нуля).
    bool markNeuronDone(int id);

    // Неудачная сборка: инкремент $.build_attempts (JSON-поле нейрона, без ALTER TABLE)
    // и установка статуса:
    //   attempts < maxAttempts -> 'pending' (будет перебрано следующим тиком);
    //   attempts >= maxAttempts -> 'error'  (проекты больше НЕ загружаются
    //                                loadPendingProjects — защита от бесконечных
    //                                перезапусков каждые 5 секунд).
    // Причина отказа пишется в $.build_error для диагностики оператором.
    // Возвращает false при ошибке БД (тогда проект останется 'pending' и
    // будет перебран — допустимо, т.к. лимит попыток хранится на сервере).
    bool markNeuronError(int id, int maxAttempts, const std::string& reason);

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