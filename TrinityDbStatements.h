// TrinityDbStatements.h
// ============================================================
// РЕЕСТР ПОДГОТОВЛЕННЫХ SQL-ЗАПРОСОВ (PREPARED STATEMENTS)
// ============================================================
// Мотивация (пункт «Дублирование SQL» плана улучшений):
//   раньше каждый метод TrinityCore склеивал текст запроса из фрагментов
//   строк и подставлял пользовательские данные через escapeSqlLiteral().
//   Проблемы такого подхода:
//     1. Дублирование: один и тот же список SELECT-столбцов нейрона
//        повторялся в четырёх методах — правка схемы требовала синхрон-
//        ной правки всех копий, рассинхронизация молча ломала парсинг;
//     2. Безопасность целиком держалась на дисциплине вызывающего кода:
//        любой пропущенный escapeSqlLiteral() = SQL-инъекция;
//     3. Производительность: сервер каждый раз заново парсил и состав-
//        лял план запроса (текстовый протокол), хотя форма постоянна.
//
// Prepared statements решают все три пункта:
//   - текст запроса пишется ОДИН РАЗ в реестре dbRegistry(), столбцы вы-
//     несены в общие константы kNeuronSelectCols / kProjectSelectCols;
//   - данные передаются БИНАРНО через MYSQL_BIND — экранирование не нужно
//     в принципе, инъекция структурно невозможна;
//   - сервер кэширует план запроса, повторные EXECUTE дешевле текстовых.
//
// Жизненный цикл: MySQL-стейтменты привязаны к КОНКРЕТНОМУ соединению.
// При переподключении (ensureConnected после обрыва создаёт новый MYSQL*)
// все старые хэндлы невалидны — поэтому реестр живёт внутри TrinityCore
// и полностью пересобирается при каждом успешном connect()
// (TrinityCore::prepareStatements). Деструктор закрывает всё, что ещё
// открыто (выгрузка ARX DLL / завершение процесса).
//
// Потоки: все операции со стейтментами выполняются исключительно с
// главного потока AutoCAD (таймер trinityProcess и команды ARX) — ровно
// как и прежде; отдельного блокирования реестр не требует.
// ============================================================
#pragma once

#include "StdAfx.h"
#include <string>
#include <vector>

// Ключи реестра — стабильные идентификаторы запросов. Используются как
// индексы массива m_stmts в TrinityCore; порядок перечисления обязан
// совпадать с порядком описаний в dbRegistry() (TrinityDbStatements.cpp),
// иначе методы ядра начнут исполнять чужие запросы. Контроль количества
// элементов — static_assert в .cpp; контроль порядка обеспечивается ком-
// ментированными заголовками секций (добавлять строго перед Count!).
enum class DbQuery : int {
    VerifyCodeIndex = 0,  // диагностика: наличие уникального индекса neuron.idx_code
    NeuronByCode,         // загрузка нейрона по бизнес-коду (через индекс idx_code)
    NeuronById,           // загрузка нейрона по числовому id (PRIMARY KEY)
    Children,             // дети конструкции через synapse (по parent id)
    PendingProjects,      // очередь сборки: статусы 'pending' + 'building'
    SetStatus,            // служебная смена $.status (pending->building)
    MarkDone,             // успех сборки: done + сброс счётчика попыток
    MarkError,            // провал сборки: attempts++ и pending/error по лимиту
    Count                 // всегда последний: число зарегистрированных запросов
};

// ------------------------------------------------------------
// Компилируемая склейка двух C-строковых литералов.
// Зачем: тексты NeuronByCode / NeuronById / PendingProjects собираются
// из общих констант списков столбцов — именно так устраняется прежнее
// четырёхкратное дублирование SELECT-полей, но реестр остаётся статиче-
// ским (без heap до первого использования, безопасно для ARX DLL).
// Только constexpr-операции над массивами фиксированного размера;
// std::string намеренно не используется.
// Tag — уникальный тип-маркер инстанцирования: без него разные склейки
// одинаковой длины слились бы в один объект и нарушили ODR.
// ------------------------------------------------------------
template <class Tag, size_t N, size_t M>
struct SqlConcat {
    char buf[N + M];                    // итоговая строка + '\0' (байт запаса)
    constexpr SqlConcat(const char (&a)[N], const char (&b)[M]) : buf{} {
        // a и b содержат собственные '\0'; склеиваем содержимое без них,
        // финальный нультерминатор уже обеспечен value-initialization'ом.
        for (size_t i = 0; i + 1 < N; ++i) buf[i] = a[i];
        for (size_t j = 0; j + 1 < M; ++j) buf[N - 1 + j] = b[j];
    }
};

// Общие куски SQL (определены в TrinityDbStatements.cpp).
extern const char kNeuronSelectCols[];      // SELECT карточки нейрона (7 колонок)
extern const char kProjectSelectCols[];     // SELECT очереди проектов (7 колонок)
extern const char kWhereByCode[];           // FROM/WHERE по виртуальному code
extern const char kWhereById[];             // FROM/WHERE по PRIMARY KEY id
extern const char kPendingProjectsTail[];   // FROM/WHERE очереди сборки

// Теги инстанцирования склеек — по одному на каждую сборку текста.
struct TagNeuronByCode {};
struct TagNeuronById {};
struct TagPendingProjects {};

// Собранные тексты запросов (constexpr-объекты в .cpp; буферы buf жи-
// вут в статической области модуля всё время жизни процесса).
extern const SqlConcat<TagNeuronByCode, sizeof(kNeuronSelectCols), sizeof(kWhereByCode)> sqlNeuronByCode;
extern const SqlConcat<TagNeuronById, sizeof(kNeuronSelectCols), sizeof(kWhereById)> sqlNeuronById;
extern const SqlConcat<TagPendingProjects, sizeof(kProjectSelectCols), sizeof(kPendingProjectsTail)> sqlPendingProjects;

// Описание одного подготовленного запроса в реестре.
struct DbQueryDef {
    const char* name;              // имя для логов (совпадает с методом-потребителем)
    const char* sql;               // текст с плейсхолдерами '?' (NULL-терминирован)
    std::vector<enum_field_types> paramTypes;  // типы параметров (по порядку '?')
    bool returnsResult = false;    // SELECT (биндится результат) или «пишущий» UPDATE?
};

// Единственная функция, знающая ВСЕ тексты запросов приложения.
// Возвращает указатель на статический массив из DbQuery::Count описы-
// ний; индекс массива == значение enum DbQuery. Инициализация ленивая
// (magic static): конструкторы vector<> откладываются до первого обра-
// щения из кода ядра, а не происходят до DllMain — это снимает риски
// порядка статической инициализации в ARX-модуле.
const DbQueryDef* dbRegistry();

// ------------------------------------------------------------
// Параметр prepared statement (одно значение для одного '?')
// ------------------------------------------------------------
// Значение хранится В КОПИИ (std::string / longlong), а не ссылкой на
// переменную вызывающего кода: биндинг выполняется во время execute,
// исходные строки к этому моменту вполне могли уйти из области види-
// мости. Тип задаёт способ кодирования в MYSQL_BIND:
//   MYSQL_TYPE_STRING   — текст (utf8mb4), длина берётся из строки;
//   MYSQL_TYPE_LONGLONG — int64 (id, лимиты); MySQL сам приведёт к
//                        колонке нужного числового типа.
struct DbParam {
    enum_field_types type = MYSQL_TYPE_STRING;
    std::string      text;        // заполнен, если type == MYSQL_TYPE_STRING
    long long        number = 0;  // заполнен, если type == MYSQL_TYPE_LONGLONG

    // Фабрики — читаемый синтаксис вызовов: DbParam::str("pending").
    static DbParam str(const std::string& s) {
        DbParam p; p.type = MYSQL_TYPE_STRING; p.text = s; return p;
    }
    static DbParam num(long long v) {
        DbParam p; p.type = MYSQL_TYPE_LONGLONG; p.number = v; return p;
    }
};

// ------------------------------------------------------------
// Строка результата SELECT в «нейтральном» виде
// ------------------------------------------------------------
// Колонки лежат в порядке SELECT-столбцов конкретного запроса реестра
// (для нейрона: id, code, type, category, material, status, data — см.
// parseNeuronRow). Числовые колонки приходят как longlong, текстовые —
// уже скопированы в std::string: буферы стейтмента переиспользуются на
// следующем fetch, держать в них ссылки нельзя. SQL NULL -> "" — ровно
// как в старом парсере текстового протокола (nullptr-ячейка -> "").
struct DbRow {
    longlong                  id = 0;       // колонка [0] — всегда числовая (PK)
    std::vector<std::string>  cols;         // остальные колонки по порядку

    // Доступ по индексу с защитой от выхода (пустая строка вместо UB).
    const std::string& col(size_t i) const {
        static const std::string s_empty;
        return i < cols.size() ? cols[i] : s_empty;
    }
};

// Набор строк, возвращаемый runSelect(). move-семантика: результат
// живёт ровно в одном владельце (локальная переменная метода-обёртки).
class DbResult {
public:
    DbResult() = default;
    DbResult(DbResult&&) = default;
    DbResult& operator=(DbResult&&) = default;
    DbResult(const DbResult&) = delete;             // копирование не нужно
    DbResult& operator=(const DbResult&) = delete;  // и дорого, и опасно

    size_t rowCount() const { return m_rows.size(); }
    bool   empty()    const { return m_rows.empty(); }
    const DbRow& row(size_t i) const { return m_rows[i]; }
    void   addRow(DbRow r) { m_rows.push_back(std::move(r)); }

private:
    std::vector<DbRow> m_rows;
};

// ------------------------------------------------------------
// RAII-обёртка над MYSQL_STMT*
// ------------------------------------------------------------
// Гарантирует mysql_stmt_close() на любом выходе из области видимости,
// включая ранние return при ошибках биндинга — утечек хэндлов нет.
// Некопируем: один хэндл = один владелец (std::move переносит владение).
class StmtGuard {
public:
    StmtGuard() = default;
    explicit StmtGuard(MYSQL_STMT* s) : m_s(s) {}
    ~StmtGuard() { reset(); }

    StmtGuard(const StmtGuard&) = delete;
    StmtGuard& operator=(const StmtGuard&) = delete;

    StmtGuard(StmtGuard&& o) noexcept : m_s(o.m_s) { o.m_s = nullptr; }
    StmtGuard& operator=(StmtGuard&& o) noexcept {
        if (this != &o) { reset(); m_s = o.m_s; o.m_s = nullptr; }
        return *this;
    }

    void reset(MYSQL_STMT* s = nullptr) {
        if (m_s) mysql_stmt_close(m_s);
        m_s = s;
    }

    MYSQL_STMT* get() const { return m_s; }
    explicit operator bool() const { return m_s != nullptr; }

private:
    MYSQL_STMT* m_s = nullptr;
};
