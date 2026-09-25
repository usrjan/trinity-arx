// TrinityCore.cpp
#include "StdAfx.h"
#include "TrinityCore.h"
#include <ctime>   // time_t / time() — троттлинг mysql_ping в ensureConnected()

// ============================================
// БЕЗОПАСНЫЕ УТИЛИТЫ (общие для обоих режимов сборки)
// ============================================
namespace {
    // NULL-безопасное преобразование MYSQL_ROW[i] -> std::string.
    // В MySQL_ROW колонки могут быть NULL (например, JSON_UNQUOTE от
    // отсутствующего поля JSON или NULL из COALESCE-обходных путей).
    // Конструкция вида std::string s = row[3]; без проверки — гарантированное
    // обращение к nullptr и AV (падение AutoCAD).
    std::string colStr(const char* cell, const char* fallback = "") {
        return cell ? std::string(cell) : std::string(fallback);
    }

    // wide -> UTF-8 (для вывода путей в консоль AutoCAD через %hs)
    std::string wideToUtf8(const wchar_t* w) {
        if (!w || !*w) return "";
        int need = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (need <= 0) return "";
        std::string out(need - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, -1, &out[0], need, nullptr, nullptr);
        return out;
    }
}

// ============================================
// СБОРКА БЕЗ MYSQL CLIENT (mysql.h не найден)
// ============================================
// Если на машине разработки нет клиентской библиотеки MySQL, StdAfx.h
// определяет TRINITY_HAS_MYSQL = 0. Чтобы проект при этом всё равно
// собирался (как раньше), ниже — заглушки модуля БД: все функции
// возвращают «нет подключения» и печатают одно понятное сообщение.
// Установите Connector/C ZIP и укажите путь MysqlIncludeDir в vcxproj,
// чтобы включить реальный доступ к базе.
#if !defined(TRINITY_HAS_MYSQL) || (TRINITY_HAS_MYSQL == 0)

// libmysql.dll инициализируется ЛЕНИВО, при первом обращении к БД (см.
// TrinityCore::connect в полной реализации ниже), а НЕ при загрузке .arx:
//  - если библиотека не найдена — плагин всё равно загружается в AutoCAD,
//    а пользователь получает понятное сообщение вместо падения;
//  - поиск идёт сначала в папке самого модуля (.arx), затем по PATH.
static HMODULE loadLibMysql() {
    wchar_t modPath[MAX_PATH] = {0};
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&loadLibMysql), &self) &&
        GetModuleFileNameW(self, modPath, MAX_PATH)) {
        std::wstring dir(modPath);
        size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            dir = dir.substr(0, slash + 1) + L"libmysql.dll";
            SetDllDirectoryW(L""); // сбросить влияние предыдущих SetDllDirectory
            HMODULE h = LoadLibraryExW(dir.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (h) return h;
        }
    }
    return LoadLibraryW(L"libmysql.dll"); // стандартный поиск: PATH и т.д.
}

namespace {
    void warnNoMysql() {
        static bool warned = false;
        if (!warned) {
            warned = true;
            acutPrintf(_T("\n[TrinityCore] MySQL client library is not installed on this machine.\n"));
            acutPrintf(_T("[TrinityCore] Database features are disabled. Install MySQL Connector/C (ZIP)\n"));
            acutPrintf(_T("[TrinityCore] and set MysqlIncludeDir/MysqlLibDir in Trinity.vcxproj, then rebuild.\n"));
        }
    }
}

TrinityCore::~TrinityCore() {}

bool TrinityCore::connect(const char*, const char*, const char*, const char*) {
    warnNoMysql();
    return false;
}
void TrinityCore::disconnect() {}
bool TrinityCore::reconnect(int, unsigned) { return false; }
bool TrinityCore::ensureConnected() { return false; }
bool TrinityCore::recoverQuery(const char*) { return false; }

std::string TrinityCore::escapeSqlLiteral(const std::string& value, bool emptyMeansNull) const {
    (void)emptyMeansNull;
    // Без mysql_real_escape_string экранируем минимально: одинарные кавычки
    // удваиваем, обратные слеши удваиваем — литерал в SQL не «сломается».
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        if (c == '\'')      out += "''";
        else if (c == '\\') out += "\\\\";
        else                out += c;
    }
    return out;
}

TrinityNeuron* TrinityCore::loadNeuronByCode(const std::string&) { warnNoMysql(); return nullptr; }
TrinityNeuron* TrinityCore::loadNeuronById(int)                  { warnNoMysql(); return nullptr; }
std::vector<TrinitySynapse> TrinityCore::loadChildren(int)       { warnNoMysql(); return {}; }
std::vector<TrinityNeuron>  TrinityCore::loadPendingProjects()   { warnNoMysql(); return {}; }
bool TrinityCore::markNeuronDone(int)                            { warnNoMysql(); return false; }

TrinityNeuron TrinityCore::parseNeuronRow(MYSQL_ROW)   { return {}; }
TrinitySynapse TrinityCore::parseSynapseRow(MYSQL_ROW) { return {}; }

#else // ==================== ПОЛНАЯ РЕАЛИЗАЦИЯ С MYSQL ====================

// ============================================
// КОНСТРУКТОР / ДЕСТРУКТОР
// ============================================
TrinityCore::~TrinityCore() { disconnect(); }

// ============================================
// ЛЕНИВАЯ ИНИЦИАЛИЗАЦИЯ libmysql.dll (режим с mysql.h)
// ============================================
// Если заголовки найдены, но сам DLL отсутствует/битый/не той разрядности,
// отложенная загрузка (delay-load) превращает первый вызов mysql_* в
// структурное исключение SEH 0xC000027D — AutoCAD падает СРАЗУ после
// запроса к базе без какого-либо сообщения. Поэтому при первом connect()
// проверяем наличие библиотеки явно; поиск: сначала папка самого .arx,
// затем стандартный (PATH и т.д.).
static HMODULE loadLibMysql() {
    static HMODULE s_h = nullptr;
    static bool    s_tried = false;
    if (s_tried) return s_h;
    s_tried = true;

    wchar_t modPath[MAX_PATH] = {0};
    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&loadLibMysql), &self) &&
        GetModuleFileNameW(self, modPath, MAX_PATH)) {
        std::wstring dir(modPath);
        size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            dir = dir.substr(0, slash + 1) + L"libmysql.dll";
            s_h = LoadLibraryExW(dir.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (s_h) return s_h;
        }
    }
    s_h = LoadLibraryW(L"libmysql.dll");
    return s_h;
}

// ============================================
// КОНСТАНТЫ ПЕРЕПОДКЛЮЧЕНИЯ
// ============================================
namespace {
    constexpr int      RECONNECT_MAX_ATTEMPTS = 3;      // попыток переподключения
    constexpr unsigned RECONNECT_DELAY_MS     = 500;    // пауза между попытками, мс
    constexpr unsigned MYSQL_READ_TIMEOUT_S   = 10;     // таймауты сокета, чтобы «мёртвое»
    constexpr unsigned MYSQL_WRITE_TIMEOUT_S  = 10;     // соединение определялось быстрее
    constexpr unsigned MYSQL_CONNECT_TIMEOUT_S = 5;
    constexpr double   MIN_PING_INTERVAL_S     = 30;    // мин. интервал между mysql_ping, сек

// Примечание по версии клиента: в MySQL Connector/C 8.0 typedef my_bool удалён из
// mysql.h, а опция MYSQL_OPT_RECONNECT — из enum_mysql_sock_option (не поддерживается).
// Поэтому переподключение реализовано на уровне приложения (ensureConnected/reconnect),
// и тип my_bool в этом файле не используется нигде.

    // Ошибки MySQL, означающие обрыв / потерю соединения (нужно переподключаться).
    // Сравниваем по числовым кодам (errmsg.h), т.к. в разных версиях коннектора
    // имена/значения части констант различаются.
    bool isConnectionError(unsigned errNo) {
        switch (errNo) {
        case 2002:                      // CR_CONNECTION_ERROR / CR_CONN_HOST_ERROR
        case 2003:                      // CR_UNKNOWN_HOST — сервер недоступен
        case 2006:                      // CR_SERVER_GONE_ERROR: MySQL server has gone away
        case 2013:                      // CR_SERVER_LOST: Lost connection to MySQL server
        case 2055:                      // CR_SERVER_LOST_EXTENDED: Lost connection (extended info)
            return true;
        default:
            return false;
        }
    }
}

// ============================================
// ПОДКЛЮЧЕНИЕ
// ============================================
bool TrinityCore::connect(const char* host, const char* user,
                           const char* pass, const char* db) {
    if (m_connected) return true;

    // Сохраняем параметры ДО любых проверок: даже если сейчас libmysql
    // недоступна, reconnect()/ensureConnected() смогут корректно работать
    // по этим данным после восстановления.
    m_host = host ? host : "";
    m_user = user ? user : "";
    m_pass = pass ? pass : "";
    m_db   = db   ? db   : "";

    // Явная проверка наличия libmysql.dll ДО первого вызова mysql_* —
    // иначе отсутствующая библиотека даёт молчаливое падение AutoCAD.
    if (!loadLibMysql()) {
        acutPrintf(_T("\n[TrinityCore] ERROR: libmysql.dll not found.\n"));
        acutPrintf(_T("[TrinityCore] Copy libmysql.dll (x64, from MySQL Connector/C 8.0)\n"));
        acutPrintf(_T("[TrinityCore] next to the .arx file or into a PATH folder, then retry.\n"));
        return false;
    }

    m_mysql = mysql_init(nullptr);
    if (!m_mysql) {
        acutPrintf(_T("\n[TrinityCore] mysql_init failed\n"));
        return false;
    }

    // Таймауты сокета: без них запрос к оборванному соединению может «висеть» минутами
    // (по умолчанию wait_timeout сервера + TCP-ретраи).
    unsigned timeout = MYSQL_CONNECT_TIMEOUT_S;
    mysql_options(m_mysql, MYSQL_OPT_CONNECT_TIMEOUT, static_cast<const void*>(&timeout));
    timeout = MYSQL_READ_TIMEOUT_S;
    mysql_options(m_mysql, MYSQL_OPT_READ_TIMEOUT, static_cast<const void*>(&timeout));
    timeout = MYSQL_WRITE_TIMEOUT_S;
    mysql_options(m_mysql, MYSQL_OPT_WRITE_TIMEOUT, static_cast<const void*>(&timeout));

    // Примечание: MYSQL_OPT_RECONNECT в MySQL Connector/C 8.0 НЕ поддерживается
    // (константа удалена из enum_mysql_sock_option). Автоматическое восстановление
    // после обрыва полностью обеспечивают наши ensureConnected() / reconnect().

    if (!mysql_real_connect(m_mysql, m_host.c_str(), m_user.c_str(),
                            m_pass.c_str(), m_db.c_str(), 0, nullptr, 0)) {
        acutPrintf(_T("\n[TrinityCore] Connection error: %hs\n"), mysql_error(m_mysql));
        mysql_close(m_mysql);
        m_mysql = nullptr;
        return false;
    }

    mysql_set_character_set(m_mysql, "utf8mb4");
    mysql_query(m_mysql, "SET NAMES utf8mb4");
    m_connected = true;
    return true;
}

void TrinityCore::disconnect() {
    if (m_mysql) {
        mysql_close(m_mysql);
        m_mysql = nullptr;
    }
    m_connected = false;
}

// ============================================
// ПЕРЕПОДКЛЮЧЕНИЕ С ПОВТОРАМИ
// Закрывает старое соединение и заново открывает его с сохранёнными
// параметрами. Между попытками — пауза (сервер мог перезапускаться).
// ============================================
bool TrinityCore::reconnect(int maxAttempts, unsigned delayMs) {
    // Соединение ещё не устанавливалось — переподключаться не к чему
    // (и libmysql может быть недоступна — не трогаем её вовсе).
    if (m_host.empty()) {
        return false;
    }
    if (!loadLibMysql()) {
        acutPrintf(_T("\n[TrinityCore] ERROR: libmysql.dll not found - cannot reconnect.\n"));
        return false;
    }

    // Отбрасываем прежнее соединение полностью
    if (m_mysql) {
        mysql_close(m_mysql);
        m_mysql = nullptr;
    }
    m_connected = false;

    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        acutPrintf(_T("\n[TrinityCore] Reconnecting to MySQL (attempt %d/%d)...\n"),
                   attempt, maxAttempts);

        m_mysql = mysql_init(nullptr);
        if (!m_mysql) return false;

        unsigned timeout = MYSQL_CONNECT_TIMEOUT_S;
        mysql_options(m_mysql, MYSQL_OPT_CONNECT_TIMEOUT, static_cast<const void*>(&timeout));
        timeout = MYSQL_READ_TIMEOUT_S;
        mysql_options(m_mysql, MYSQL_OPT_READ_TIMEOUT, static_cast<const void*>(&timeout));
        timeout = MYSQL_WRITE_TIMEOUT_S;
        mysql_options(m_mysql, MYSQL_OPT_WRITE_TIMEOUT, static_cast<const void*>(&timeout));
        // MYSQL_OPT_RECONNECT в Connector/C 8.0 не поддерживается —
        // повторное соединение выполняется этим reconnect() (цикл попыток выше).

        if (mysql_real_connect(m_mysql, m_host.c_str(), m_user.c_str(),
                               m_pass.c_str(), m_db.c_str(), 0, nullptr, 0)) {
            mysql_set_character_set(m_mysql, "utf8mb4");
            mysql_query(m_mysql, "SET NAMES utf8mb4");
            m_connected = true;
            acutPrintf(_T("[TrinityCore] Reconnected successfully.\n"));
            return true;
        }

        acutPrintf(_T("[TrinityCore] Reconnect failed: %hs\n"), mysql_error(m_mysql));
        mysql_close(m_mysql);
        m_mysql = nullptr;

        if (attempt < maxAttempts) {
            Sleep(delayMs);
            delayMs *= 2; // экспоненциальная задержка: 500 -> 1000 -> 2000 мс
        }
    }

    acutPrintf(_T("[TrinityCore] All reconnect attempts failed.\n"));
    return false;
}

// ============================================
// ПИНГ СОЕДИНЕНИЯ + АВТОМАТИЧЕСКОЕ ВОССТАНОВЛЕНИЕ
// Вызывается перед каждым обращением к БД. Если соединение оборвалось
// (сервер перезапустился, обрыв сети, wait_timeout), выполняет ping,
// при неудаче — переподключение с повторами.
//
// Частота пинга ограничена MIN_PING_INTERVAL_S: если с последней
// успешной проверки прошло меньше интервала, запрос считается безопасным
// (это исключает лишний round-trip на каждый запрос в цикле сборки).
// Потерянное соединение всё равно будет обработано: mysql_query вернёт
// ошибку обрыва и метод запроса восстановит его через ensureConnected().
// ============================================
bool TrinityCore::ensureConnected() {
    static time_t s_lastPing = 0;

    if (m_mysql && m_connected) {
        const time_t now = time(nullptr);
        if (s_lastPing != 0 && difftime(now, s_lastPing) < MIN_PING_INTERVAL_S) {
            return true; // недавно проверяли — считаем соединение живым
        }

        if (mysql_ping(m_mysql) == 0) {
            s_lastPing = now;
            m_connected = true;
            return true;
        }

        const unsigned errNo = mysql_errno(m_mysql);
        if (isConnectionError(errNo)) {
            acutPrintf(_T("\n[TrinityCore] Connection lost (%hs), trying to restore...\n"),
                       mysql_error(m_mysql));
            if (reconnect(RECONNECT_MAX_ATTEMPTS, RECONNECT_DELAY_MS)) {
                s_lastPing = time(nullptr);
                return true;
            }
            return false;
        }

        // Прочие ошибки пинга (например, проблемы с правами) — соединение нерабочее
        acutPrintf(_T("\n[TrinityCore] mysql_ping error %u: %hs\n"), errNo, mysql_error(m_mysql));
        m_connected = false;
        return false;
    }

    // Соединения нет (не подключались или уже закрыто) — пробуем восстановить
    if (reconnect(RECONNECT_MAX_ATTEMPTS, RECONNECT_DELAY_MS)) {
        s_lastPing = time(nullptr);
        return true;
    }
    s_lastPing = 0;
    return false;
}

// ============================================
// ВОССТАНОВЛЕНИЕ ПОСЛЕ ОШИБКИ ОБРЫВА В СЕРЕДИНЕ ЗАПРОСА
// ping перед запросом не гарантирует, что соединение не оборвётся во время
// его выполнения (например, сервер ушёл в рестарт). Если mysql_query вернул
// ошибку обрыва — переподключаемся и повторяем запрос один раз.
// Все запросы класса — SELECT либо идемпотентный UPDATE c JSON_SET, поэтому
// повтор безопасен. true — повторный запрос выполнен успешно.
// ============================================
bool TrinityCore::recoverQuery(const char* query) {
    const unsigned errNo = mysql_errno(m_mysql);
    if (!isConnectionError(errNo)) return false;

    acutPrintf(_T("\n[TrinityCore] Query failed: %hs, reconnecting...\n"),
               mysql_error(m_mysql));
    if (!reconnect(RECONNECT_MAX_ATTEMPTS, RECONNECT_DELAY_MS)) return false;

    return mysql_query(m_mysql, query) == 0;
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
    // Экранирование требует живого соединения (кодировка берётся из него) —
    // при обрыве пробуем восстановить его через пинг/переподключение.
    if (!const_cast<TrinityCore*>(this)->ensureConnected()) return "'"; // заведомо невалидный литерал

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

// ============================================================
// ПРОВЕРКА ЦЕЛОСТНОСТИ СТРОКИ НЕЙРОНА (защита от падения AutoCAD)
// ============================================================
namespace {
    // Пустой code критичен: ensureExists/deleteProjectFiles с пустым кодом
    // зацикливаются рекурсивно по детям -> стек переполняется и AutoCAD
    // падает без сообщения. Строку с пустым кодом выбрасываем с логом.
    bool isUsableNeuron(const TrinityNeuron& n, const wchar_t* ctx) {
        if (n.code.empty()) {
            acutPrintf(_T("\n[TrinityCore] SKIP neuron id=%d in %ls: empty code (check data->'$.code' in DB)\n"),
                       n.id, ctx);
            return false;
        }
        return true;
    }

    // Колонка type не должна быть NULL/пустой — иначе ветвление detail/assembly
    // уходит в «projects» молча; логируем, но не выбрасываем строку.
}

// ============================================
// ЗАГРУЗКА НЕЙРОНА ПО КОДУ
// ============================================
TrinityNeuron* TrinityCore::loadNeuronByCode(const std::string& code) {
    if (!ensureConnected()) return nullptr;

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

    if (mysql_query(m_mysql, query.c_str()) != 0 && !recoverQuery(query.c_str()))
        return nullptr;

    MYSQL_RES* result = mysql_store_result(m_mysql);
    if (!result || mysql_num_rows(result) == 0) {
        if (result) mysql_free_result(result);
        return nullptr;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    TrinityNeuron* neuron = new TrinityNeuron(parseNeuronRow(row));
    mysql_free_result(result);
    if (!isUsableNeuron(*neuron, L"loadNeuronByCode")) {
        delete neuron;
        return nullptr;
    }
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

    if (mysql_query(m_mysql, query.c_str()) != 0 && !recoverQuery(query.c_str()))
        return nullptr;

    MYSQL_RES* result = mysql_store_result(m_mysql);
    if (!result || mysql_num_rows(result) == 0) {
        if (result) mysql_free_result(result);
        return nullptr;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    TrinityNeuron* neuron = new TrinityNeuron(parseNeuronRow(row));
    mysql_free_result(result);
    if (!isUsableNeuron(*neuron, L"loadNeuronById")) {
        delete neuron;
        return nullptr;
    }
    return neuron;
}

// ============================================
// ЗАГРУЗКА ДЕТЕЙ ЧЕРЕЗ SYNAPSE
// ============================================
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

    if (mysql_query(m_mysql, query.c_str()) != 0 && !recoverQuery(query.c_str()))
        return children;

    MYSQL_RES* result = mysql_store_result(m_mysql);
    if (!result) return children;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        TrinitySynapse syn = parseSynapseRow(row);
        // Пустой childCode -> рекурсия deleteProjectFiles/ensureFileExists
        // уходит по «пустым» детям и зацикливается (стек переполняется,
        // AutoCAD падает). Такой синапс пропускаем с логом.
        if (syn.childCode.empty()) {
            acutPrintf(_T("\n[TrinityCore] SKIP synapse id=%d (parent=%d): empty child code\n"),
                       syn.id, syn.parentId);
            continue;
        }
        children.push_back(syn);
    }

    mysql_free_result(result);
    return children;
}

// ============================================
// ЗАГРУЗКА PENDING-ПРОЕКТОВ
// ============================================
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
        "  AND JSON_UNQUOTE(JSON_EXTRACT(data, '$.status')) = 'pending' "
        "  AND is_deleted = 0 "
        "ORDER BY id";

    if (mysql_query(m_mysql, query) != 0 && !recoverQuery(query))
        return projects;

    MYSQL_RES* result = mysql_store_result(m_mysql);
    if (!result) return projects;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        TrinityNeuron proj = parseNeuronRow(row);
        if (!isUsableNeuron(proj, L"loadPendingProjects"))
            continue;
        projects.push_back(proj);
    }

    mysql_free_result(result);
    return projects;
}

// ============================================
// ОТМЕТКА "DONE"
// ============================================
bool TrinityCore::markNeuronDone(int id) {
    if (!ensureConnected()) return false;

    const std::string query =
        "UPDATE neuron SET data = JSON_SET(data, '$.status', 'done') WHERE id = "
        + std::to_string(id);

    if (mysql_query(m_mysql, query.c_str()) == 0) return true;
    return recoverQuery(query.c_str());
}

// ============================================
// ПАРСИНГ СТРОКИ НЕЙРОНА
// ============================================
TrinityNeuron TrinityCore::parseNeuronRow(MYSQL_ROW row) {
    TrinityNeuron n;
    if (!row) return n;
    // ВАЖНО: любая колонка может быть NULL (например, JSON_UNQUOTE возвращает
    // NULL, если в data нет поля $.code / $.status). Прямое row[i] -> std::string
    // на NULL = обращение к nullptr = Access Violation и падение AutoCAD.
    n.id       = row[0] ? atoi(row[0]) : 0;
    n.code     = colStr(row[1]);
    n.type     = colStr(row[2]);
    n.category = colStr(row[3]);
    n.material = row[4] ? std::string(row[4]) : "PLYWOOD-FSF";
    n.status   = colStr(row[5]);
    n.jsonData = colStr(row[6]);

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
    if (!row) return s;
    s.id       = row[0] ? atoi(row[0]) : 0;
    s.parentId = row[1] ? atoi(row[1]) : 0;
    s.childId  = row[2] ? atoi(row[2]) : 0;
    s.childCode = colStr(row[3]);

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
#endif // TRINITY_HAS_MYSQL
