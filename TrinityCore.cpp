// TrinityCore.cpp
#include "StdAfx.h"
#include "TrinityCore.h"

// Коды ошибок MySQL, означающие, что соединение разорвано и его нужно
// пересоздать (mysql_ping их тоже возвращает):
//   2006 CR_SERVER_GONE_ERROR     — сервер закрыл соединение (wait_timeout)
//   2013 CR_SERVER_LOST           — потеря связи во время запроса
//   2003 CR_CONNECTION_ERROR      — сервер недоступен
static bool isFatalConnectionError(unsigned int errNo) {
    return errNo == 2006 || errNo == 2013 || errNo == 2003;
}

// ============================================
// КОНСТРУКТОР / ДЕСТРУКТОР
// ============================================
TrinityCore::~TrinityCore() { disconnect(); }

// ============================================
// ЗАКРЫТИЕ ХЭНДЛА
// ============================================
void TrinityCore::closeHandle() {
    if (m_mysql) {
        mysql_close(m_mysql);   // mysql_close безопасен и для несоединённого хэндла
        m_mysql = nullptr;
    }
    m_connected = false;
}

// ============================================
// ПОДКЛЮЧЕНИЕ
// ============================================
bool TrinityCore::connect(const char* host, const char* user,
                           const char* pass, const char* db) {
    if (m_connected) return true;

    // Запоминаем параметры — они пригодятся ensureConnected() для
    // автоматического переподключения после обрыва.
    m_host = host ? host : "";
    m_user = user ? user : "";
    m_pass = pass ? pass : "";
    m_db   = db   ? db   : "";

    closeHandle();  // на случай повторного connect() с другим набором параметров

    m_mysql = mysql_init(nullptr);
    if (!m_mysql) {
        acutPrintf(_T("\n[TrinityCore] mysql_init failed\n"));
        return false;
    }

    // Таймауты: без них блокирующий TCP-запрос при «зависшем» сервере
    // может подвесить поток таймера AutoCAD на минуты.
    unsigned int connectTimeoutSec = 5;
    unsigned int readTimeoutSec    = 15;
    mysql_options(m_mysql, MYSQL_OPT_CONNECT_TIMEOUT, &connectTimeoutSec);
    mysql_options(m_mysql, MYSQL_OPT_READ_TIMEOUT, &readTimeoutSec);

    if (!mysql_real_connect(m_mysql, m_host.c_str(), m_user.c_str(),
                            m_pass.c_str(), m_db.c_str(), 0, nullptr, 0)) {
        acutPrintf(_T("\n[TrinityCore] Connection error (%u): %hs\n"),
                   mysql_errno(m_mysql), mysql_error(m_mysql));
        closeHandle();
        return false;
    }

    mysql_set_character_set(m_mysql, "utf8mb4");
    mysql_query(m_mysql, "SET NAMES utf8mb4");
    m_connected = true;

    // Диагностика схемы: без уникального индекса neuron.idx_code поиск по
    // коду (loadNeuronByCode) работает полным сканом таблицы. Проверяем это
    // один раз за подключение и предупреждаем оператора в консоли AutoCAD.
    verifyCodeIndex();
    return true;
}

void TrinityCore::disconnect() {
    closeHandle();
}

// ============================================
// ДИАГНОСТИКА: НАЛИЧИЕ ИНДЕКСА neuron.idx_code
// ============================================
// Сборка проекта рекурсивно вызывает loadNeuronByCode() для каждого узла
// графа. Без виртуального столбца neuron.code и уникального индекса idx_code
// (см. database.sql, раздел 2b) такой запрос выполняется полным сканом
// таблицы, и время сборки растёт линейно с размером базы. Если плагин
// подключился к базе, созданной старой версией дампа, деградация была бы
// совершенно незаметна — поэтому проверяем метаданные один раз за сеанс
// и явно предупреждаем в консоли AutoCAD, как запустить миграцию.
//
// Стоимость проверки: один лёгкий SELECT по information_schema сразу после
// mysql_real_connect(), т.е. не чаще одного раза на переподключение.
// Результат НЕ кэшируется между реконнектами специально: миграцию могут
// выполнить, пока AutoCAD открыт, и при следующем обрыве связи предупреждение
// должно исчезнуть само.
void TrinityCore::verifyCodeIndex() {
    if (!m_mysql) return;

    const char* query =
        "SELECT COUNT(*) FROM information_schema.STATISTICS "
        "WHERE TABLE_SCHEMA = DATABASE() "
        "  AND TABLE_NAME = 'neuron' "
        "  AND INDEX_NAME = 'idx_code'";

    if (mysql_query(m_mysql, query) != 0) {
        // Нет прав на information_schema или сбой соединения — не считаем
        // это фатальным: основная работа продолжится, просто без диагностики.
        acutPrintf(_T("\n[TrinityCore] WARNING: cannot verify neuron.idx_code (%u): %hs\n"),
                   mysql_errno(m_mysql), mysql_error(m_mysql));
        return;
    }

    MYSQL_RES* result = mysql_store_result(m_mysql);
    if (!result) return;

    bool hasIndex = false;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row && row[0]) hasIndex = (atoi(row[0]) > 0);
    mysql_free_result(result);

    if (!hasIndex) {
        acutPrintf(_T("\n[TrinityCore] WARNING: index neuron.idx_code is MISSING.\n"));
        acutPrintf(_T("[TrinityCore]   Searches by code do a FULL TABLE SCAN -> slow builds.\n"));
        acutPrintf(_T("[TrinityCore]   Run the migration block \"2b\" in database.sql:\n"));
        acutPrintf(_T("[TrinityCore]   ALTER TABLE neuron ADD COLUMN code ... VIRTUAL,\n"));
        acutPrintf(_T("[TrinityCore]                  ADD UNIQUE KEY idx_code (code);\n"));
    }
}

// ============================================
// ПРОВЕРКА ЖИВОСТИ / АВТОПЕРЕПОДКЛЮЧЕНИЕ
// Раньше соединение создавалось один раз в init(), а все операции
// проверяли только флаг m_connected. Если MySQL закрывал idle-соединение
// (wait_timeout) или пропадала сеть, каждый следующий mysql_query
// молча возвращал ошибку, loadPendingProjects() давал пустой список,
// и таймер «тихо» тикал впустую. Теперь каждая операция начинается с
// ensureConnected(): ping подтверждает живость, при обрыве выполняется
// переподключение с сохранёнными параметрами и логирование в консоль.
// ============================================
bool TrinityCore::ensureConnected() {
    if (!m_mysql) {
        // Хэндла нет: если параметры подключения известны (после первой
        // неудачной попытки TSTART) — пробуем подключиться заново.
        if (!m_host.empty() && !m_db.empty())
            return connect(m_host.c_str(), m_user.c_str(), m_pass.c_str(), m_db.c_str());
        return false;
    }

    if (m_connected && mysql_ping(m_mysql) == 0)
        return true;   // соединение живо — быстрый путь

    unsigned int errNo = mysql_errno(m_mysql);
    if (m_connected && !isFatalConnectionError(errNo)) {
        // mysql_ping упал не из-за обрыва (например, читается незавершённый
        // результат). Сбрасываем состояние и пробуем переподключиться ниже.
        acutPrintf(_T("\n[TrinityCore] mysql_ping failed (%u): %hs\n"),
                   errNo, mysql_error(m_mysql));
    }

    acutPrintf(_T("\n[TrinityCore] Connection lost (errno=%u). Reconnecting to '%hs'...\n"),
               errNo, m_host.c_str());

    // Полностью закрываем мёртвый хэндл: connect() при m_mysql == nullptr
    // просто создаст новый. Без этого шага mysql_close() вызывался бы для
    // уже разорванного соединения, а mysql_real_connect() — для хэндла,
    // на котором он уже выполнялся (недокументированное поведение libmysql).
    closeHandle();

    // connect() запомнит/переиспользует параметры и выставит таймауты.
    if (connect(m_host.c_str(), m_user.c_str(), m_pass.c_str(), m_db.c_str())) {
        acutPrintf(_T("[TrinityCore] Reconnected successfully.\n"));
        return true;
    }

    acutPrintf(_T("[TrinityCore] Reconnect failed. DB operations disabled until next retry.\n"));
    return false;
}

// ============================================
// ЭКРАНИРОВАНИЕ SQL
// Динамический буфер: mysql_real_escape_string гарантирует, что
// экранированная строка не длиннее 2*len+1 байт, переполнение невозможно.
// Экранирование учитывает кодировку соединения (utf8mb4), что защищает
// от атак типа GBK-multi-byte.
// ============================================
std::string TrinityCore::escapeSqlLiteral(const std::string& value, bool emptyMeansNull) const {
    if (value.empty() && emptyMeansNull) return "NULL";
    // mysql_real_escape_string требует живого соединения. ensureConnected()
    // не может быть вызван из-за const — при отсутствии соединения возвращаем
    // заведомо невалидный литерал; все публичные методы уже вызывают
    // ensureConnected() до формирования запроса.
    if (!m_connected) return "'";

    std::vector<char> buf(value.size() * 2 + 1);
    unsigned long outLen = mysql_real_escape_string(
        m_mysql, buf.data(), value.c_str(), (unsigned long)value.size());
    buf.resize(outLen);

    std::string result;
    result.reserve(outLen + 2);
    result += '\'';
    result.append(buf.data(), buf.size());
    result += '\'';
    return result;
}

// ============================================
// ЛОГИРОВАНИЕ ОШИБОК SQL
// Раньше ошибки mysql_query проглатывались молча (метод просто возвращал
// nullptr/пустой список). Теперь код и текст ошибки выводятся в консоль
// AutoCAD, а при фатальных кодах обрыва сбрасывается m_connected —
// следующий ensureConnected() переподключится.
// ============================================
void TrinityCore::logQueryError(const char* context, const std::string& query) {
    unsigned int errNo = m_mysql ? mysql_errno(m_mysql) : 0;
    acutPrintf(_T("\n[TrinityCore] SQL error in %hs (%u): %hs\n"),
               context, errNo, m_mysql ? mysql_error(m_mysql) : "no handle");
    acutPrintf(_T("[TrinityCore] Query: %hs\n"),
               query.substr(0, 300).c_str());   // не затапливаем консоль длинным JSON
    if (isFatalConnectionError(errNo))
        m_connected = false;   // ping ещё не падал на этом хэндле — помечаем для реконнекта
}

// ============================================
// ЗАГРУЗКА НЕЙРОНА ПО КОДУ
// ============================================
// ВАЖНО (индекс): условие WHERE намеренно оставлено в форме
//   JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = '<код>'
// — посимвольно идентичной определению виртуального столбца
// neuron.code (см. database.sql). Только при точном совпадении
// выражения оптимизатор MySQL подставляет генерируемый столбец
// и использует уникальный индекс idx_code (type=ref, 1 строка).
// Любая «оптимизация» вида data->>'$.code', JSON_VALUE(...) или
// UPPER(...) сломает match и вернёт полный скан таблицы.
// Проверка плана: EXPLAIN SELECT ... (ожидается key=idx_code).
TrinityNeuron* TrinityCore::loadNeuronByCode(const std::string& code) {
    if (!ensureConnected()) return nullptr;

    // Пустой код — заведомо отсутствующая сущность: нейроны без $.code
    // имеют NULL в виртуальном столбце, а сравнение '= '\'\'' в индексе
    // ничего не найдёт. Раньше такой запрос всё равно уходил на сервер
    // (сетевой RTT + парсинг) при каждом рекурсивном вызове сборки.
    if (code.empty()) return nullptr;

    const std::string query =
        "SELECT id, "
        "  JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')), "
        "  type, "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.category')), ''), "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.material')), 'PLYWOOD-FSF'), "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.status')), ''), "
        "  data "
        "FROM neuron "
        "WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = " + escapeSqlLiteral(code) + " "
        "  AND is_deleted = 0 "
        "LIMIT 1";

    if (mysql_query(m_mysql, query.c_str()) != 0) {
        logQueryError("loadNeuron", query);
        return nullptr;
    }

    MYSQL_RES* result = mysql_store_result(m_mysql);
    if (!result || mysql_num_rows(result) == 0) {
        if (result) mysql_free_result(result);
        return nullptr;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    TrinityNeuron* neuron = new TrinityNeuron(parseNeuronRow(row));
    mysql_free_result(result);
    return neuron;
}

// ============================================
// ЗАГРУЗКА НЕЙРОНА ПО ID
// ============================================
TrinityNeuron* TrinityCore::loadNeuronById(int id) {
    if (!ensureConnected()) return nullptr;

    const std::string query =
        "SELECT id, "
        "  JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')), "
        "  type, "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.category')), ''), "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.material')), 'PLYWOOD-FSF'), "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.status')), ''), "
        "  data "
        "FROM neuron WHERE id = " + std::to_string(id) + " AND is_deleted = 0";

    if (mysql_query(m_mysql, query.c_str()) != 0) {
        logQueryError("loadNeuron", query);
        return nullptr;
    }

    MYSQL_RES* result = mysql_store_result(m_mysql);
    if (!result || mysql_num_rows(result) == 0) {
        if (result) mysql_free_result(result);
        return nullptr;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    TrinityNeuron* neuron = new TrinityNeuron(parseNeuronRow(row));
    mysql_free_result(result);
    return neuron;
}

// ============================================
// ЗАГРУЗКА ДЕТЕЙ ЧЕРЕЗ SYNAPSE
// ============================================
// Производительность: фильтр по коду ребёнка (n.is_deleted = 0 и выборка
// $.code) идёт уже после обращения к synapse по индексу idx_parent, поэтому
// полного скана neuron здесь нет. Выражение JSON_UNQUOTE(JSON_EXTRACT(
// n.data, '$.code')) оставлено без изменений намеренно — оно совпадает с
// определением виртуального столбца neuron.code и при необходимости может
// быть обслужено уникальным индексом idx_code (например, если оптимизатор
// выберет join-порядок child -> parent). См. комментарий в loadNeuronByCode.
std::vector<TrinitySynapse> TrinityCore::loadChildren(int parentId) {
    std::vector<TrinitySynapse> children;
    if (!ensureConnected()) return children;

    const std::string query =
        "SELECT s.id, s.parent, s.child, "
        "  JSON_UNQUOTE(JSON_EXTRACT(n.data, '$.code')), "
        "  s.data "
        "FROM synapse s "
        "JOIN neuron n ON s.child = n.id "
        "WHERE s.parent = " + std::to_string(parentId) + " AND n.is_deleted = 0 "
        "ORDER BY s.id";

    if (mysql_query(m_mysql, query.c_str()) != 0) {
        logQueryError("loadChildren", query);
        return children;
    }

    MYSQL_RES* result = mysql_store_result(m_mysql);
    if (!result) return children;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        children.push_back(parseSynapseRow(row));
    }

    mysql_free_result(result);
    return children;
}

// ============================================
// ЗАГРУЗКА PENDING-ПРОЕКТОВ
// ============================================
// Выбираются проекты в двух статусах:
//   'pending'  — обычная очередь: новые заявки и заявки, чья предыдущая
//                попытка сборки не увенчалась, но лимит попыток ещё не
//                исчерпан (см. markNeuronError);
//   'building' — «зависшие» сборки: проект был помечен как собираемый
//                (setBuildStatus), но процесс оборвался аварийно до
//                финального done/error (крах плагина или AutoCAD, обрыв
//                соединения в момент сборки). Без подхвата таких проектов
//                они навсегда остались бы в промежуточном статусе.
//
// Статусы 'done' и 'error' в выборку НЕ попадают — это ключевой механизм
// защиты от бесконечных перезапусков: провалившийся после лимита попыток
// проект больше не перебирается каждые 5 секунд таймером, пока оператор
// вручную не вернёт ему статус 'pending'.
//
// Примечание по индексу: условие по status выражено через полное совпадение
// с определением генерируемого столбца job_status (COALESCE(...,'pending')
// здесь сознательно заменён на прямое сравнение — оптимизатор MySQL может
// использовать idx_job_status_queue для фильтрации по статусу; ORDER BY id
// идёт по PRIMARY KEY. Для выборок малого размера это не критично, но при
// разрастании таблицы важно не ломать синтаксис выражения в WHERE.
std::vector<TrinityNeuron> TrinityCore::loadPendingProjects() {
    std::vector<TrinityNeuron> projects;
    if (!ensureConnected()) return projects;

    const char* query =
        "SELECT id, "
        "  JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')), "
        "  type, "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.category')), ''), "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.material')), ''), "
        "  JSON_UNQUOTE(JSON_EXTRACT(data, '$.status')), "
        "  data "
        "FROM neuron "
        "WHERE type = 'project' "
        "  AND JSON_UNQUOTE(JSON_EXTRACT(data, '$.status')) IN ('pending', 'building') "
        "  AND is_deleted = 0 "
        "ORDER BY id";

    if (mysql_query(m_mysql, query) != 0) {
        logQueryError("loadPendingProjects", query);
        return projects;
    }

    MYSQL_RES* result = mysql_store_result(m_mysql);
    if (!result) return projects;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        projects.push_back(parseNeuronRow(row));
    }

    mysql_free_result(result);
    return projects;
}

// ============================================
// СМЕНА СТАТУСА ПРОЕКТА (служебный хелпер)
// ============================================
// Единая точка записи $.status. Используется для перехода
// pending -> building (перед началом сборки) и building -> done/error
// (по её итогам). id приходит из строк БД (atoi), дополнительных
// пользовательских данных в запросе нет — инъекция невозможна.
static bool setJsonStatus(MYSQL* mysql, int id, const char* status,
                          const char* context) {
    const std::string query =
        std::string("UPDATE neuron SET data = JSON_SET(data, '$.status', '") +
        status + "') WHERE id = " + std::to_string(id);

    if (mysql_query(mysql, query.c_str()) != 0) {
        // Контекст передаётся вызывающей функцией — так сообщение об ошибке
        // в логе соответствует реальному методу-потребителю.
        acutPrintf(_T("\n[TrinityCore] %hs failed: %s\n"),
                   context, mysql_error(mysql));
        return false;
    }
    return true;
}

// ============================================
// ПОМЕТИТЬ ПРОЕКТ КАК СОБИРАЕМЫЙ (pending -> building)
// ============================================
// Вызывается НЕМЕДЛЕННО перед началом сборки проекта. Если процесс умрёт
// до выставления финального статуса (done/error), проект останется в
// 'building' и будет подобран следующим тиком loadPendingProjects() —
// без зависания очереди. Возвращает false при ошибке БД; вызывающий код
// трактует это как «прервать обработку пачки» (БД недоступна — смысла
// продолжать сборку нет, файлы всё равно некуда сохранить по статусу).
bool TrinityCore::setBuildStatus(int id) {
    if (!ensureConnected()) return false;
    return setJsonStatus(m_mysql, id, "building", "setBuildStatus");
}

// ============================================
// ОТМЕТКА "DONE"
// ============================================
// Успешная сборка: статус 'done' + сброс счётчика попыток build_attempts
// (если проект позже пересоздадут в 'pending', он начнёт с чистого
// лимита). JSON_REMOVE убирает и служебную причину последней ошибки —
// она больше неактуальна.
bool TrinityCore::markNeuronDone(int id) {
    if (!ensureConnected()) return false;

    const std::string query =
        "UPDATE neuron SET data = JSON_REMOVE("
        "  JSON_SET(JSON_SET(data, '$.status', 'done'), "
        "           '$.build_attempts', 0), "
        "  '$.build_error') "
        "WHERE id = " + std::to_string(id);

    if (mysql_query(m_mysql, query.c_str()) != 0) {
        logQueryError("markNeuronDone", query);
        return false;
    }
    return true;
}

// ============================================
// ОТМЕТКА О НЕУДАЧНОЙ СБОРКЕ (retry / error)
// ============================================
// Реализует экспоненциально затухающую политику перезапусков:
//   1. build_attempts увеличивается на единицу прямо в JSON
//      (COALESCE(...,0)+1 — поля может не быть у старых записей);
//   2. если после инкремента attempts < maxAttempts — проект возвращается
//      в 'pending' и будет перебран следующим тиком (5 секунд);
//   3. если attempts >= maxAttempts — статус 'error': loadPendingProjects
//      его больше не видит, БЕСКОНЕЧНЫЕ ПЕРЕЗАПУСКИ ПРЕКРАЩАЮТСЯ.
// Причина отказа пишется в $.build_error — оператор увидит её при
// разборе зависшего проекта.
//
// Всё делается одним UPDATE: значение статуса вычисляется тем же
// выражением CASE, что и только что записанный счётчик, — состояние
// атомарно и не может «застрять» между двумя запросами.
//
// id — числовой из БД; reason экранируется escapeSqlLiteral (может
// содержать текст ошибок ARX с кавычками/слэшами).
bool TrinityCore::markNeuronError(int id, int maxAttempts, const std::string& reason) {
    if (!ensureConnected()) return false;
    if (maxAttempts < 1) maxAttempts = 1;   // страховка от нуля/отрицательного лимита

    const std::string newAttempts =
        "(COALESCE(CAST(JSON_EXTRACT(data, '$.build_attempts') AS SIGNED), 0) + 1)";

    const std::string query =
        "UPDATE neuron SET data = JSON_SET("
        "  JSON_SET(data, '$.build_attempts', " + newAttempts + "), "
        "  '$.status', CASE WHEN " + newAttempts + " >= " + std::to_string(maxAttempts) +
                        " THEN 'error' ELSE 'pending' END, "
        "  '$.build_error', " + escapeSqlLiteral(reason, /*emptyMeansNull=*/false) + ") "
        "WHERE id = " + std::to_string(id);

    if (mysql_query(m_mysql, query.c_str()) != 0) {
        logQueryError("markNeuronError", query);
        return false;
    }
    return true;
}

// ============================================
// ПАРСИНГ СТРОКИ НЕЙРОНА
// ============================================
TrinityNeuron TrinityCore::parseNeuronRow(MYSQL_ROW row) {
    TrinityNeuron n;
    n.id = row[0] ? atoi(row[0]) : 0;
    n.code = row[1] ? row[1] : "";
    n.type = row[2] ? row[2] : "";
    n.category = row[3] ? row[3] : "";
    n.material = row[4] ? row[4] : "PLYWOOD-FSF";
    n.status = row[5] ? row[5] : "";
    n.jsonData = row[6] ? row[6] : "";

    // Парсим код: D.S.0.425.850.10
    if (!n.code.empty()) {
        sscanf_s(n.code.c_str(), "D.S.%d.%d.%d.%d",
                 &n.processCode, &n.width, &n.height, &n.thickness);
    }

    return n;
}

// ============================================
// ПАРСИНГ СТРОКИ СИНАПСА
// ============================================
TrinitySynapse TrinityCore::parseSynapseRow(MYSQL_ROW row) {
    TrinitySynapse s;
    s.id = row[0] ? atoi(row[0]) : 0;
    s.parentId = row[1] ? atoi(row[1]) : 0;
    s.childId = row[2] ? atoi(row[2]) : 0;
    s.childCode = row[3] ? row[3] : "";

    if (row[4]) {
        std::string json(row[4]);
        s.position = parsePosition(json);
        s.rotation = parseRotation(json);

        // Статус
        size_t statusPos = json.find("\"status\"");
        if (statusPos != std::string::npos) {
            size_t valStart = json.find('"', statusPos + 8);
            size_t valEnd = json.find('"', valStart + 1);
            if (valStart != std::string::npos && valEnd != std::string::npos) {
                s.status = json.substr(valStart + 1, valEnd - valStart - 1);
            }
        }
    }

    return s;
}

// ============================================
// ПАРСИНГ ПОЗИЦИИ [x, y, z]
// ============================================
TrinityPosition TrinityCore::parsePosition(const std::string& json) {
    TrinityPosition pos;
    size_t posKey = json.find("\"pos\"");
    if (posKey != std::string::npos) {
        size_t arrStart = json.find('[', posKey);
        if (arrStart != std::string::npos) {
            sscanf_s(json.c_str() + arrStart, "[%lf, %lf, %lf]", &pos.x, &pos.y, &pos.z);
        }
    }
    return pos;
}

// ============================================
// ПАРСИНГ ПОВОРОТА (одиночный или compound)
// ============================================
TrinityRotationCompound TrinityCore::parseRotation(const std::string& json) {
    TrinityRotationCompound compound;

    size_t rotKey = json.find("\"rot\"");
    if (rotKey == std::string::npos) return compound;

    size_t arrStart = json.find('[', rotKey);
    if (arrStart == std::string::npos) return compound;

    // Проверяем — массив массивов или один массив?
    size_t nextChar = arrStart + 1;
    while (nextChar < json.length() && json[nextChar] == ' ') nextChar++;

    if (json[nextChar] == '[') {
        // Compound: [[x,y,z,a],[x,y,z,a]]
        size_t rot1Start = nextChar;
        size_t rot1End = json.find(']', rot1Start);

        if (rot1End != std::string::npos && compound.count < 2) {
            sscanf_s(json.c_str() + rot1Start, "[%lf, %lf, %lf, %lf]",
                     &compound.rotations[0].x, &compound.rotations[0].y,
                     &compound.rotations[0].z, &compound.rotations[0].angle);
            compound.count++;
        }

        size_t rot2Start = json.find('[', rot1End + 1);
        if (rot2Start != std::string::npos && compound.count < 2) {
            sscanf_s(json.c_str() + rot2Start, "[%lf, %lf, %lf, %lf]",
                     &compound.rotations[1].x, &compound.rotations[1].y,
                     &compound.rotations[1].z, &compound.rotations[1].angle);
            compound.count++;
        }
    } else {
        // Одиночный: [x,y,z,a]
        sscanf_s(json.c_str() + arrStart, "[%lf, %lf, %lf, %lf]",
                 &compound.rotations[0].x, &compound.rotations[0].y,
                 &compound.rotations[0].z, &compound.rotations[0].angle);
        compound.count = 1;
    }

    return compound;
}