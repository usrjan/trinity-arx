// TrinityDbStatements.cpp
// ============================================================
// ЕДИНСТВЕННЫЙ ИСТОЧНИК ПРАВДЫ О ТЕКСТАХ SQL плагина Trinity.
//
// Здесь живут общие фрагменты запросов (списки столбцов, хвосты WHERE),
// компилируемые склейки текстов и реестр dbRegistry(). Методы TrinityCore
// более НЕ склеивают SQL из строк и НЕ подставляют пользовательские дан-
// ные через экранирование — они только биндят параметры и вызывают EXE-
// CUTE (см. runSelect/runUpdate в TrinityCore.cpp).
//
// Правила сопровождения:
//   - Новый запрос = новый элемент enum DbQuery (TrinityDbStatements.h)
//     + описание в dbRegistry() НИЖЕ, в ТОМ ЖЕ порядке (строго перед
//     Count). Контроль количества — static_assert в конце файла.
//   - Порядок колонок в SELECT обязан совпадать с ожиданиями парсеров
//     строк результата (parseNeuronRow / parseSynapseRow) и со списком
//     полей, которые биндит runSelect().
//   - Синтаксис WHERE по коду/статусу ДОЛЖЕН посимвольно совпадать с
//     определениями виртуальных столбцов neuron.code / neuron.job_status
//     (database.sql) — иначе оптимизатор не подставит генерируемый
//     столбец и уникальные индексы idx_code / idx_job_status_queue не
//     будут использованы. Поэтому выражения JSON_UNQUOTE(JSON_EXTRACT())
//     менять ни на ->>, ни на JSON_VALUE нельзя (проверка: EXPLAIN).
// ============================================================
#include "StdAfx.h"
#include "TrinityDbStatements.h"

// ------------------------------------------------------------
// Общие списки выбираемых столбцов (устранение дублирования).
// Колонки идут строго в порядке, который ожидают парсеры:
//   нейрон : id, code, type, category, material, status, data
//   синопс : s.id, s.parent, s.child, child_code, s.data
// Любое изменение порядка = правка парсера в том же коммите.
// Определения без static — объявлены extern в заголовке; единицы транс-
// ляции видят одни и те же массивы, sizeof() в объявлениях склеек корре-
// ктен при компиляции в этой же TU (см. .h).
// ------------------------------------------------------------

// Полная карточка нейрона. COALESCE для category/material/status даёт
// значения по умолчанию прямо на сервере — ровно как в прежних тексто-
// вых запросах (материал 'PLYWOOD-FSF' — стандарт до assign-этапа).
const char kNeuronSelectCols[] =
    "SELECT id, "
    "  JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')), "
    "  type, "
    "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.category')), ''), "
    "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.material')), 'PLYWOOD-FSF'), "
    "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.status')), ''), "
    "  data ";

// Список для очереди проектов. Отличается от kNeuronSelectCols одним
// полем: $.status читается БЕЗ COALESCE — проект без статуса в выборку
// IN ('pending','building') не попадает ни при каком значении по умол-
// чанию, маскировать NULL пустой строкой смысла нет.
const char kProjectSelectCols[] =
    "SELECT id, "
    "  JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')), "
    "  type, "
    "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.category')), ''), "
    "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.material')), ''), "
    "  JSON_UNQUOTE(JSON_EXTRACT(data, '$.status')), "
    "  data ";

// Хвосты FROM/WHERE для запросов по нейрону. Разделены на отдельные
// константы СПЕЦИАЛЬНО: условие по коду обязано быть посимвольной ко-
// пией определения виртуального столбца (ради подстановки индексом), а
// условие по id — обычным равенством первичному ключу. Смешивать их
// нельзя; каждая склейка фиксируется constexpr-объектом ниже, чтобы
// результат был готов на этапе компиляции (без heap и рантайм-копий).

// -- «по бизнес-коду»: форма = определение neuron.code (database.sql) --
const char kWhereByCode[] =
    "FROM neuron "
    "WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = ? "
    "  AND is_deleted = 0 "
    "LIMIT 1";

// -- «по числовому id»: обслуживается PRIMARY KEY --
const char kWhereById[] =
    "FROM neuron WHERE id = ? AND is_deleted = 0";

// -- очередь сборки: фильтр по type + статусам pending/building --
const char kPendingProjectsTail[] =
    "FROM neuron "
    "WHERE type = 'project' "
    "  AND JSON_UNQUOTE(JSON_EXTRACT(data, '$.status')) IN ('pending', 'building') "
    "  AND is_deleted = 0 "
    "ORDER BY id";

// ------------------------------------------------------------
// Инстансы компилируемых склеек. constinit запрещает динамическую ini-
// тialization: объекты ложатся в статическую область модуля целиком, а
// constexpr-конструктор выполняется компилятором. buf живёт всё время
// жизни процесса — указатели на него свободно хранятся в реестре.
// ------------------------------------------------------------
constinit const SqlConcat<TagNeuronByCode, sizeof(kNeuronSelectCols), sizeof(kWhereByCode)>
    sqlNeuronByCode(kNeuronSelectCols, kWhereByCode);

constinit const SqlConcat<TagNeuronById, sizeof(kNeuronSelectCols), sizeof(kWhereById)>
    sqlNeuronById(kNeuronSelectCols, kWhereById);

constinit const SqlConcat<TagPendingProjects, sizeof(kProjectSelectCols), sizeof(kPendingProjectsTail)>
    sqlPendingProjects(kProjectSelectCols, kPendingProjectsTail);

// ------------------------------------------------------------
// Реестр запросов. Индекс массива = значение enum DbQuery
// (порядок элементов ОБЯЗАТЕЛЬНО соответствует перечислению!).
// Типы параметров перечислены в том же порядке, что '?' в SQL.
// ------------------------------------------------------------
const DbQueryDef* dbRegistry() {
    // Magic static: инициализация выполнится ровно один раз, при первом
    // обращении из кода ядра (после DllMain) — порядок статической ини-
    // циализации модуля ARX больше ни на что не влияет.
    static const DbQueryDef defs[static_cast<size_t>(DbQuery::Count)] = {

        // ---- VerifyCodeIndex ------------------------------------------------
        // Диагностика схемы сразу после connect(): существует ли уникальный
        // индекс neuron.idx_code (покрывает виртуальный столбец code). Без
        // него NeuronByCode работает полным сканом таблицы, и деградация
        // времени сборки была бы незаметна. Параметров нет: DATABASE() бе-
        // рёт текущую схему соединения, пользовательских данных нет.
        {
            "verifyCodeIndex",
            "SELECT COUNT(*) FROM information_schema.STATISTICS "
            "WHERE TABLE_SCHEMA = DATABASE() "
            "  AND TABLE_NAME = 'neuron' "
            "  AND INDEX_NAME = 'idx_code'",
            { /* нет параметров */ },
            /*returnsResult=*/true
        },

        // ---- NeuronByCode ----------------------------------------------------
        // Горячий путь сборки: вызывается рекурсивно для каждого узла графа
        // (после кэша m_builtCache — не чаще одного раза на код за проход).
        // WHERE оставлен ПОСИМВОЛЬНО идентичным определению виртуального
        // столбца neuron.code — только так MySQL подставляет генерируемый
        // столбец и использует UNIQUE KEY idx_code (EXPLAIN: type=ref).
        // Код передаётся параметром '?': кавычки/слэши/Юникод в коде абсо-
        // лютно безопасны (бинарный протокол, никакого экранирования).
        {
            "loadNeuronByCode",
            sqlNeuronByCode.buf,
            { MYSQL_TYPE_STRING },
            /*returnsResult=*/true
        },

        // ---- NeuronById --------------------------------------------------------
        // Точечная загрузка нейрона по числовому id (PRIMARY KEY).
        // Тот же список столбцов, что у NeuronByCode, — тот же парсер строки.
        {
            "loadNeuronById",
            sqlNeuronById.buf,
            { MYSQL_TYPE_LONGLONG },
            /*returnsResult=*/true
        },

        // ---- Children ----------------------------------------------------------
        // Дети конструкции через synapse. Родитель фильтруется по индексу
        // idx_parent, join к neuron идёт по PRIMARY KEY, n.is_deleted = 0
        // отсекает удалённые компоненты. ORDER BY s.id даёт детерминирован-
        // ный порядок присоединения XREF (воспроизводимость DWG-сборок).
        {
            "loadChildren",
            "SELECT s.id, s.parent, s.child, "
            "  JSON_UNQUOTE(JSON_EXTRACT(n.data, '$.code')), "
            "  s.data "
            "FROM synapse s "
            "JOIN neuron n ON s.child = n.id "
            "WHERE s.parent = ? AND n.is_deleted = 0 "
            "ORDER BY s.id",
            { MYSQL_TYPE_LONGLONG },
            /*returnsResult=*/true
        },

        // ---- PendingProjects ----------------------------------------------------
        // Очередь таймера: статусы 'pending' (новая заявка / retry после
        // провала в пределах лимита) и 'building' (подхват сборки, прерван-
        // ной аварийно до финального статуса). 'done'/'error' сюда НЕ попа-
        // дают — ключевой механизм защиты от бесконечных перезапусков.
        // Выражение статуса совпадает с виртуальным столбцом job_status,
        // поэтому фильтр может использовать idx_job_status_queue; ORDER BY
        // id идёт по PRIMARY KEY (FIFO-порядок обработки).
        {
            "loadPendingProjects",
            sqlPendingProjects.buf,
            { /* нет параметров */ },
            /*returnsResult=*/true
        },

        // ---- SetStatus ------------------------------------------------------------
        // Единая точка записи $.status (реализована методом setJsonStatus):
        //   pending -> building (setBuildStatus перед началом сборки).
        // Статус — закрытый набор констант плагина, но уходит параметром:
        // форма запроса постоянна, сервер переиспользует план; случайная
        // опечатка в новом статусе не сможет «склеить» произвольный SQL.
        {
            "setBuildStatus",
            "UPDATE neuron SET data = JSON_SET(data, '$.status', ?) "
            "WHERE id = ?",
            { MYSQL_TYPE_STRING, MYSQL_TYPE_LONGLONG },
            /*returnsResult=*/false
        },

        // ---- MarkDone --------------------------------------------------------------
        // Успех сборки: status='done' + обнуление счётчика попыток
        // build_attempts + удаление устаревшей причины build_error.
        // Один запрос вместо прежней ручной склейки трёх JSON-функций.
        {
            "markNeuronDone",
            "UPDATE neuron SET data = JSON_REMOVE("
            "  JSON_SET(JSON_SET(data, '$.status', 'done'), "
            "           '$.build_attempts', 0), "
            "  '$.build_error') "
            "WHERE id = ?",
            { MYSQL_TYPE_LONGLONG },
            /*returnsResult=*/false
        },

        // ---- MarkError ----------------------------------------------------------------
        // Провал сборки — одной АТОМАРНОЙ операцией (как и раньше):
        //   1) build_attempts := COALESCE(attempts,0)+1 — поля может не быть
        //      у старых записей;
        //   2) status := 'error', если новый счётчик >= лимита (?maxAttempts),
        //      иначе 'pending' — проект переберут следующим тиком;
        //   3) build_error := текст причины (?reason) — любые кавычки/слэши
        //      в тексте ошибок ARX теперь безопасны без экранирования.
        // Лимит раньше вклеивался в текст через to_string — теперь и он па-
        // раметр: форма запроса не зависит ни от каких входных значений.
        // ПОРЯДОК '?': сначала лимит (в CASE), затем причина, затем id.
        {
            "markNeuronError",
            "UPDATE neuron SET data = JSON_SET("
            "  JSON_SET(data, '$.build_attempts', "
            "    (COALESCE(CAST(JSON_EXTRACT(data, '$.build_attempts') AS SIGNED), 0) + 1)), "
            "  '$.status', CASE WHEN "
            "    (COALESCE(CAST(JSON_EXTRACT(data, '$.build_attempts') AS SIGNED), 0) + 1) >= ? "
            "    THEN 'error' ELSE 'pending' END, "
            "  '$.build_error', ?) "
            "WHERE id = ?",
            { MYSQL_TYPE_LONGLONG, MYSQL_TYPE_STRING, MYSQL_TYPE_LONGLONG },
            /*returnsResult=*/false
        },
    };

    // Контроль согласованности реестра и enum на этапе компиляции: если
    // в enum добавили значение, но забыли описание (или наоборот) — сбор-
    // ка упадёт ЗДЕСЬ, а не в рантайме на выходе за границу массива (UB).
    static_assert(sizeof(defs) / sizeof(defs[0]) ==
                  static_cast<size_t>(DbQuery::Count),
                  "dbRegistry size mismatch: keep the registry in sync with enum DbQuery");

    return defs;
}
