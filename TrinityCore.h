// TrinityCore.h
#pragma once
#include "StdAfx.h"
#include <atomic>   // std::atomic — состояние купола доступности БД (TrinityDbBreaker)

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
    std::string status;        // pending, building, done, error

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

// ============================================================
// КУПОЛ ДОСТУПНОСТИ БД (CIRCUIT BREAKER)
// ============================================================
// Проблема, которую решает купол: ensureConnected() при недоступном
// сервере MySQL выполнял ПОЛНОЦЕННУЮ попытку TCP-подключения с таймау-
// тами MYSQL_OPT_CONNECT_TIMEOUT = 5 c (и до 15 c на чтение ответа).
// Таймер плагина бьёт каждые 5 секунд на ГЛАВНОМ потоке AutoCAD (из об-
// работчика WM_TRINITY_TICK под write-lock активного документа), поэто-
// му «упавшая» БД превращала каждый тик в 5-секундную блокировку mes-
// sage pump: интерфейс AutoCAD подвисал на всё время нетворк-таймаута,
// а файрвол, молча дропающий SYN-пакеты, мог подвешивать UI практически
// неопределённо долго (до истечения TCP-ретрансляций ядра Windows).
//
// Как работает (классический трёхсостоящий circuit breaker):
//   CLOSED    — штатный режим: каждая операция может пытаться подклю-
//               читься; первые kFailureThreshold неудач подряд ещё
//               проходят через реальный сетевой стек (обычные крат-
//               ковременные сбои сети купол глотает молча);
//   OPEN      — счётчик неудач достиг порога: ВСЕ попытки подключения
//               ОТКАЗЫВАЮТСЯ мгновенно (несколько инструкций, без се-
//               тевого I/O, без ожидания) в течение kOpenIntervalMs.
//               Именно здесь рождается главный эффект для UI: тики бо-
//               лее никогда не зависают;
//   HALF-OPEN — интервал истёк: пропускаем РОВНО ОДНУ пробную попытку
//               подключения. Успех -> CLOSED (счётчик обнулён, очередь
//               проектов возобновляется сам собой); отказ -> снова OPEN
//               на тот же интервал.
//
// Итог для пользователя: вместо бесконечных 5-секундных заморозок каж-
// дого тика — максимум kFailureThreshold медленных попыток суммарно,
// затем лёгкие мгновенные отказы (~0 мкс) и ровно одна реальная провер-
// ка живости сервера раз в kOpenIntervalMs.
//
// ВАЖНО ПРО ПОТОКИ: механизм рассчитан на однопоточное использование из
// главного потока AutoCAD (единственный писатель — таймер trinityPro-
// cess). Поля сделаны std::atomic «на вырост» и для безопасного ЧТЕ-
// НЯ статистики из команд TRINITY_STATUS: полноценной многопоточной ma-
// шины состояний здесь нет и не нужно (запись всегда идёт с одного по-
// тока, гонки между writer'ами невозможны в принципе).
struct TrinityDbBreaker {
    // ---- Константы политики ----

    // Сколько неудачных попыток подключения ПОДРЯД накапливаем, прежде
    // чем «замкнуть цепь». 3 — компромисс: достаточно, чтобы отделить
    // транзитный джиттер/переподключение Wi-Fi от реальной недоступно-
    // сти сервера, и недостаточно, чтобы пользователь успел заметить
    // «зависание» (3 медленные попытки ~15 c суммарно — один раз, по-
    // сле чего купол открывается и UI больше не тормозится никогда).
    static const int kFailureThreshold = 3;

    // Длительность состояния OPEN, мс. 30 секунд: типичное время руч-
    // ного перезапуска службы MySQL / восстановления сетевого пути.
    // Занижать — лишние долбления в мёртвый сервер; завышать — долгий
    // простой очереди pending-проектов после восстановления связи.
    static const DWORD kOpenIntervalMs = 30000;

    // ---- Состояние ----

    // Подряд идущие НЕУДАЧИ попыток подключения (mysql_real_connect /
    // mysql_ping). Сбрасывается в 0 при любом успехе.
    std::atomic<int> m_consecutiveFailures{ 0 };

    // Тик-момент (GetTickCount64), когда купол открылся; 0 = закрыт.
    std::atomic<unsigned long long> m_openedAtTick{ 0 };

    // Общее число срабатываний купола за сеанс — только диагностика
    // (TRINITY_STATUS показывает оператору, насколько «болен» канал).
    std::atomic<int> m_tripCount{ 0 };

    // ---- Логика ----

    // Истина = купол ОТКРЫТ и попытка подключения сейчас ЗАПРЕЩЕНА:
    // вызывающий код обязан мгновенно вернуть «БД недоступна», ничего
    // не читая и не подключая. В состоянии HALF-OPEN (интервал истёк)
    // возвращает false — одну проверку живости пропускаем наверх.
    bool isTripped() const {
        const unsigned long long opened = m_openedAtTick.load();
        if (opened == 0)
            return false;   // купол закрыт — работать штатно
        const unsigned long long now = GetTickCount64();
        // Целочисленная арифметика монотонных тиков переносит переход
        // 64-битного счётчика через ноль корректно (без проблем со знаком).
        return (now - opened) < kOpenIntervalMs;
    }

    // Отметить успешное подключение/пинг: счётчик неудач в ноль, ку-
    // пол закрывается — следующая операция пойдёт через реальный стек.
    void recordSuccess() {
        m_consecutiveFailures.store(0);
        m_openedAtTick.store(0);
    }

    // Отметить неудачную попытку подключения. При достижении порога —
    // открываем купол (старт kOpenIntervalMs) и увеличиваем счётчик
    // срабатываний. Возвращает true, если купол ТОЛЬКО ЧТО открылся,
    // чтобы вызывающий код напечатал предупреждение ровно ОДИН раз на
    // срабатывание (а не в каждый заблокированный тик).
    bool recordFailure() {
        const int fails = m_consecutiveFailures.fetch_add(1) + 1;
        if (fails >= kFailureThreshold &&
            m_openedAtTick.exchange(GetTickCount64()) == 0) {
            // exchange вернул 0 => купол был закрыт => мы его открыли.
            m_tripCount.fetch_add(1);
            return true;   // свежее срабатывание — можно предупредить
        }
        return false;
    }

    // Полный сброс (TRINITY_START — осознанная команда оператора):
    // забыть историю неудач и немедленно разрешить подключение, не
    // дожидаясь истечения интервала HALF-OPEN.
    void reset() {
        m_consecutiveFailures.store(0);
        m_openedAtTick.store(0);
    }

    // ---- Диагностика (для TRINITY_STATUS) ----

    int  failureCount() const { return m_consecutiveFailures.load(); }
    int  trips()        const { return m_tripCount.load(); }
    bool open()         const { return m_openedAtTick.load() != 0 && isTripped(); }
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

    // Купол доступности БД (см. TrinityDbBreaker выше). Живёт столько
    // же, сколько сам TrinityCore: история неудач принадлежит конкретному про-
    // цессу плагина и обнуляется вместе с ним.
    TrinityDbBreaker m_breaker;

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
    // !!! Все сетевые попытки проходят через купол m_breaker: при не-
    // доступном сервере метод отказывает мгновенно, не блокируя глав-
    // ный поток AutoCAD (см. комментарий к TrinityDbBreaker).
    bool ensureConnected();

    // Принудительное открытие купола (команда TRINITY_STOP): гаранти-
    // рует, что ни один «последний» тик не уйдёт в сетевой таймаут.
    void tripBreakerNow() { m_breaker.m_openedAtTick.store(GetTickCount64()); }

    // Сброс купола (TRINITY_START): оператор явно хочет работать — даём
    // подключению шанс немедленно, без ожидания HALF-OPEN-интервала.
    void resetBreaker() { m_breaker.reset(); }

    // Доступ к состоянию купола для диагностических команд.
    const TrinityDbBreaker& breaker() const { return m_breaker; }

    // Человекочитаемое состояние купола ("CLOSED" / "OPEN (Xs until
    // probe)" / "HALF-OPEN") + число подряд идущих неудач и срабатыва-
    // ний — для команды TRINITY_STATUS (реализация в TrinityCore.cpp).
    std::string breakerState() const;

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
