// TrinityCore.cpp
#include "StdAfx.h"
#include "TrinityCore.h"

// ============================================
// Деструкторы / RAII
// ============================================
TrinityCore::~TrinityCore() { disconnect(); }

// ============================================
// RAII-обёртка над prepared statement
// ============================================
TrinityCore::Stmt::~Stmt() {
    if (m_stmt) {
        mysql_stmt_close(m_stmt);   // закрывает и буферизованный результат
        m_stmt = nullptr;
    }
}

bool TrinityCore::Stmt::prepare(MYSQL* mysql, const char* sql) {
    m_mysql = mysql;
    m_stmt  = mysql_stmt_init(mysql);
    if (!m_stmt) {
        trinityLog(L"[TrinityCore] mysql_stmt_init failed");
        return false;
    }
    if (mysql_stmt_prepare(m_stmt, sql,
                           static_cast<unsigned long>(strlen(sql))) != 0) {
        trinityLog(L"[TrinityCore] prepare failed: %hs | sql=%hs",
                   mysql_stmt_error(m_stmt), sql);
        mysql_stmt_close(m_stmt);
        m_stmt = nullptr;
        return false;
    }
    return true;
}

bool TrinityCore::Stmt::bindParams(const std::vector<MYSQL_BIND>& params) {
    if (params.empty()) return true;
    // Копируем, т.к. mysql_stmt_bind_param требует non-const указатели
    std::vector<MYSQL_BIND> bindIn(params);
    if (mysql_stmt_bind_param(m_stmt, bindIn.data()) != 0) {
        trinityLog(L"[TrinityCore] bind_param failed: %hs", mysql_stmt_error(m_stmt));
        return false;
    }
    return true;
}

bool TrinityCore::Stmt::executeStore() {
    if (mysql_stmt_execute(m_stmt) != 0) {
        trinityLog(L"[TrinityCore] execute failed: %hs", mysql_stmt_error(m_stmt));
        return false;
    }
    if (mysql_stmt_store_result(m_stmt) != 0) {
        trinityLog(L"[TrinityCore] store_result failed: %hs", mysql_stmt_error(m_stmt));
        return false;
    }
    return true;
}

my_ulonglong TrinityCore::Stmt::numRows() const {
    return mysql_stmt_num_rows(m_stmt);
}

bool TrinityCore::Stmt::bindResult(std::vector<MYSQL_BIND> binds) {
    if (mysql_stmt_bind_result(m_stmt, binds.data()) != 0) {
        trinityLog(L"[TrinityCore] bind_result failed: %hs", mysql_stmt_error(m_stmt));
        return false;
    }
    return true;
}

bool TrinityCore::Stmt::fetch() {
    const int rc = mysql_stmt_fetch(m_stmt);
    if (rc == 0 || rc == MYSQL_DATA_TRUNCATED) return true;
    if (rc == MYSQL_NO_DATA) return false;           // конец выборки
    trinityLog(L"[TrinityCore] fetch failed: %hs", mysql_stmt_error(m_stmt));
    return false;
}

void TrinityCore::Stmt::freeResult() {
    mysql_stmt_free_result(m_stmt);
}

// --------------------------------------------
// Помощники заполнения MYSQL_BIND (input)
// buffer_length для input-строк — УКАЗАТЕЛЬ на
// переменную с длиной данных (lifetime должен
// переживать вызов mysql_stmt_execute).
// --------------------------------------------
MYSQL_BIND TrinityCore::bindString(const char* data, unsigned long& lenInOut) {
    MYSQL_BIND b{};
    b.buffer_type   = MYSQL_TYPE_STRING;
    b.buffer        = const_cast<char*>(data);
    b.buffer_length = &lenInOut;
    b.is_null       = nullptr;
    return b;
}

MYSQL_BIND TrinityCore::bindLongLong(longlong& value) {
    MYSQL_BIND b{};
    b.buffer_type = MYSQL_TYPE_LONGLONG;
    b.buffer      = &value;
    b.is_null     = nullptr;
    return b;
}

// ============================================
// Выполнение prepared-запроса c буферизованным результатом
// params — входные параметры (строки/числа); lifetime данных
// должен переживать вызов.
// ============================================
bool TrinityCore::runPrepared(const char* stmtText,
                              const std::vector<MYSQL_BIND>& params,
                              Stmt& out) {
    if (!m_connected || !m_mysql) return false;
    if (!out.prepare(m_mysql, stmtText)) return false;
    if (!out.bindParams(params))         return false;
    return out.executeStore();
}

// ============================================
// Подключение
// ============================================
bool TrinityCore::connect(const char* host, const char* user,
                          const char* pass, const char* db) {
    if (m_connected) return true;
    if (!host || !user || !db) return false;

    m_mysql = mysql_init(nullptr);
    if (!m_mysql) {
        trinityLog(L"[TrinityCore] mysql_init failed");
        return false;
    }

    unsigned int timeoutSec = 5;
    mysql_options(m_mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeoutSec);

    if (!mysql_real_connect(m_mysql, host, user, pass ? pass : "",
                            db, 0, nullptr, 0)) {
        trinityLog(L"[TrinityCore] Connection error: %hs", mysql_error(m_mysql));
        mysql_close(m_mysql);
        m_mysql = nullptr;
        return false;
    }

    mysql_set_character_set(m_mysql, "utf8mb4");
    m_connected = true;
    trinityLog(L"[TrinityCore] Connected to %hs/%hs", host, db);
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
// Общий SELECT одного нейрона
// ============================================
static const char* kNeuronSelectColumns =
    "SELECT id, "
    "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')), ''), "
    "  type, "
    "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.category')), ''), "
    "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.material')), 'PLYWOOD-FSF'), "
    "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.status')), ''), "
    "  COALESCE(CAST(data AS CHAR), '') "
    "FROM neuron WHERE %s AND is_deleted = 0 LIMIT 1";

// Буферы для чтения строки нейрона (7 колонок)
struct TrinityCoreNeuronBuffers {
    longlong       id      = 0;
    my_bool        idNull  = 0;
    std::vector<char> code, type, category, material, status, data;
    std::vector<unsigned long> lengths;
    std::vector<my_bool> errors, nulls;

    TrinityCoreNeuronBuffers()
        : lengths(6), errors(6, 0), nulls(6, 0)
    {
        // id — не NULL (PK); строковые колонки защищены COALESCE,
        // но is_null буферы обязательны для корректной работы fetch
        code.resize(256); type.resize(64); category.resize(128);
        material.resize(128); status.resize(32); data.resize(16384);
    }

    std::vector<MYSQL_BIND> makeBinds() {
        std::vector<MYSQL_BIND> b(7);
        memset(b.data(), 0, sizeof(MYSQL_BIND) * b.size());

        b[0].buffer_type = MYSQL_TYPE_LONGLONG;
        b[0].buffer      = &id;
        b[0].is_null     = &idNull;

        auto strBind = [&](size_t i, std::vector<char>& buf) {
            b[i].buffer_type   = MYSQL_TYPE_STRING;
            b[i].buffer        = buf.data();
            b[i].u_length      = static_cast<unsigned long>(buf.size() - 1);
            b[i].length        = &lengths[i - 1];
            b[i].error         = &errors[i - 1];
            b[i].is_null       = &nulls[i - 1];
        };
        strBind(1, code); strBind(2, type); strBind(3, category);
        strBind(4, material); strBind(5, status); strBind(6, data);
        return b;
    }

    // Колонка i (1..6) как std::string ('' если NULL/обрезана до 0)
    std::string col(size_t i) const {
        const unsigned long* map[6] = { &lengths[0], &lengths[1], &lengths[2],
                                        &lengths[3], &lengths[4], &lengths[5] };
        const my_bool* nmap[6] = { &nulls[0], &nulls[1], &nulls[2],
                                   &nulls[3], &nulls[4], &nulls[5] };
        const std::vector<char>* bmap[6] = { &code, &type, &category,
                                             &material, &status, &data };
        const size_t k = i - 1;
        if (*nmap[k]) return "";
        const unsigned long len = *map[k];
        if (len == 0) return "";
        return std::string(bmap[k]->data(), len);
    }
};

TrinityNeuronPtr TrinityCore::loadNeuronWhere(const char* whereSql,
                                              const std::vector<std::string>& strParams,
                                              const std::vector<int>& intParams) {
    // упрощение: только один параметр (код или id)
    char sql[1024];
    snprintf(sql, sizeof(sql), kNeuronSelectColumns, whereSql);

    std::vector<MYSQL_BIND> params;
    unsigned long len = 0;
    longlong intValue = 0;

    if (!strParams.empty()) {
        len = static_cast<unsigned long>(strParams.front().size());
        params.push_back(bindString(strParams.front().c_str(), len));
    } else if (!intParams.empty()) {
        intValue = intParams.front();
        params.push_back(bindLongLong(intValue));
    }

    Stmt stmt;
    if (!runPrepared(sql, params, stmt)) return nullptr;

    TrinityCoreNeuronBuffers bufs;
    if (!stmt.bindResult(bufs.makeBinds())) return nullptr;
    if (!stmt.fetch()) return nullptr;               // нет строк / ошибка

    return std::make_unique<TrinityNeuron>(
        parseNeuronRow(static_cast<int>(bufs.id),
                       bufs.col(1), bufs.col(2), bufs.col(3),
                       bufs.col(4).empty() ? "PLYWOOD-FSF" : bufs.col(4),
                       bufs.col(5), bufs.col(6)));
}

// ============================================
// Загрузка нейрона по коду / id
// ============================================
TrinityNeuronPtr TrinityCore::loadNeuronByCode(const std::string& code) {
    return loadNeuronWhere("JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = ?",
                           { code }, {});
}

TrinityNeuronPtr TrinityCore::loadNeuronById(int id) {
    return loadNeuronWhere("id = ?", {}, { id });
}

// ============================================
// Загрузка детей через synapse
// ============================================
std::vector<TrinitySynapse> TrinityCore::loadChildren(int parentId) {
    std::vector<TrinitySynapse> children;
    if (!m_connected) return children;

    const char* sql =
        "SELECT s.id, s.parent, s.child, "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(n.data, '$.code')), ''), "
        "  COALESCE(CAST(s.data AS CHAR), '') "
        "FROM synapse s "
        "JOIN neuron n ON s.child = n.id "
        "WHERE s.parent = ? AND n.is_deleted = 0 "
        "ORDER BY s.id";

    longlong pid = parentId;
    std::vector<MYSQL_BIND> params = { bindLongLong(pid) };

    Stmt stmt;
    if (!runPrepared(sql, params, stmt)) return children;

    // Буферы результата: id/parent/child (LONGLONG), code, data (STRING)
    struct SynapseBuffers {
        longlong rowId = 0, rowParent = 0, rowChild = 0;
        my_bool nulls[5] = { 0, 0, 0, 0, 0 };
        std::vector<char> codeBuf(256), dataBuf(16384);
        unsigned long lengths[5] = { 0 };
        my_bool       errors[5]  = { 0 };

        std::vector<MYSQL_BIND> makeBinds() {
            std::vector<MYSQL_BIND> b(5);
            memset(b.data(), 0, sizeof(MYSQL_BIND) * b.size());
            b[0].buffer_type = MYSQL_TYPE_LONGLONG; b[0].buffer = &rowId;
            b[1].buffer_type = MYSQL_TYPE_LONGLONG; b[1].buffer = &rowParent;
            b[2].buffer_type = MYSQL_TYPE_LONGLONG; b[2].buffer = &rowChild;
            for (int i = 0; i < 5; ++i) b[i].is_null = &nulls[i];

            b[3].buffer_type = MYSQL_TYPE_STRING;
            b[3].buffer      = codeBuf.data();
            b[3].u_length    = static_cast<unsigned long>(codeBuf.size() - 1);
            b[3].length      = &lengths[3];
            b[3].error       = &errors[3];

            b[4].buffer_type = MYSQL_TYPE_STRING;
            b[4].buffer      = dataBuf.data();
            b[4].u_length    = static_cast<unsigned long>(dataBuf.size() - 1);
            b[4].length      = &lengths[4];
            b[4].error       = &errors[4];
            return b;
        }
    };

    SynapseBuffers bufs;
    if (!stmt.bindResult(bufs.makeBinds())) return children;

    while (stmt.fetch()) {
        const std::string code =
            (!bufs.nulls[3] && bufs.lengths[3])
                ? std::string(bufs.codeBuf.data(), bufs.lengths[3]) : "";
        const std::string data =
            (!bufs.nulls[4] && bufs.lengths[4])
                ? std::string(bufs.dataBuf.data(), bufs.lengths[4]) : "";
        children.push_back(parseSynapseRow(
            static_cast<int>(bufs.rowId), static_cast<int>(bufs.rowParent),
            static_cast<int>(bufs.rowChild), code, data));
    }
    return children;
}

// ============================================
// Загрузка pending-проектов
// ============================================
std::vector<TrinityNeuron> TrinityCore::loadPendingProjects() {
    std::vector<TrinityNeuron> projects;
    if (!m_connected) return projects;

    const char* sql =
        "SELECT id, "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')), ''), "
        "  type, "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.category')), ''), "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.material')), ''), "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.status')), ''), "
        "  COALESCE(CAST(data AS CHAR), '') "
        "FROM neuron "
        "WHERE type = 'project' "
        "  AND JSON_UNQUOTE(JSON_EXTRACT(data, '$.status')) = 'pending' "
        "  AND is_deleted = 0 "
        "ORDER BY id";

    Stmt stmt;
    if (!runPrepared(sql, {}, stmt)) return projects;

    TrinityCoreNeuronBuffers bufs;
    if (!stmt.bindResult(bufs.makeBinds())) return projects;

    while (stmt.fetch()) {
        projects.push_back(parseNeuronRow(
            static_cast<int>(bufs.id),
            bufs.col(1), bufs.col(2), bufs.col(3),
            bufs.col(4), bufs.col(5), bufs.col(6)));
    }
    return projects;
}

// ============================================
// Отметка «done»
// ============================================
bool TrinityCore::markNeuronDone(int id) {
    if (!m_connected) return false;

    const char* sql =
        "UPDATE neuron SET data = JSON_SET(data, '$.status', 'done') WHERE id = ?";

    longlong v = id;
    std::vector<MYSQL_BIND> params = { bindLongLong(v) };

    Stmt stmt;
    return runPrepared(sql, params, stmt);
}

// ============================================
// Парсинг строки нейрона + геометрия из JSON/кода
// ============================================
TrinityNeuron TrinityCore::parseNeuronRow(int id, const std::string& code,
                                          const std::string& type,
                                          const std::string& category,
                                          const std::string& material,
                                          const std::string& status,
                                          const std::string& data) {
    TrinityNeuron n;
    n.id       = id;
    n.code     = code;
    n.type     = type;
    n.category = category;
    n.material = material.empty() ? "PLYWOOD-FSF" : material;
    n.status   = status;
    n.jsonData = data;
    applyGeometry(n);
    return n;
}

void TrinityCore::applyGeometry(TrinityNeuron& n) {
    // 1. Из JSON, если поля есть
    TrinityJson::Value root = TrinityJson::parse(n.jsonData);
    if (root.isObject()) {
        const auto& w = root["width"];
        const auto& h = root["height"];
        const auto& t = root["thickness"];
        const auto& p = root["process"];
        if (!w.isNull()) n.width     = w.asInt(n.width);
        if (!h.isNull()) n.height    = h.asInt(n.height);
        if (!t.isNull()) n.thickness = t.asInt(n.thickness);
        if (!p.isNull()) n.processCode = p.asInt(n.processCode);
    }

    // 2. Fallback: разбор кода D.<T>.<proc>.<W>.<H>.<Th> (например D.S.0.425.850.10)
    if (n.width == 0 || n.height == 0) {
        int proc = 0, w = 0, h = 0, th = 0;
        char first = 0, second = 0;
        if (sscanf(n.code.c_str(), "%c.%c.%d.%d.%d.%d",
                   &first, &second, &proc, &w, &h, &th) == 6 && first == 'D') {
            if (n.width == 0)     n.width = w;
            if (n.height == 0)    n.height = h;
            if (th > 0)           n.thickness = th;
            if (n.processCode == 0) n.processCode = proc;
        }
    }

    if (n.thickness <= 0) n.thickness = 10;
}

// ============================================
// Парсинг строки синапса
// ============================================
TrinitySynapse TrinityCore::parseSynapseRow(int id, int parentId, int childId,
                                            const std::string& childCode,
                                            const std::string& data) {
    TrinitySynapse s;
    s.id        = id;
    s.parentId  = parentId;
    s.childId   = childId;
    s.childCode = childCode;

    TrinityJson::Value root = TrinityJson::parse(data);
    if (root.isObject()) {
        s.position = parsePosition(root);
        s.rotation = parseRotation(root);
        s.status   = root["status"].asString();
    }
    return s;
}

// ============================================
// Позиция: {"pos": [x, y, z]}
// ============================================
TrinityPosition TrinityCore::parsePosition(const TrinityJson::Value& data) {
    TrinityPosition pos;
    const auto& arr = data["pos"];
    if (arr.isArray() && arr.size() >= 3) {
        pos.x = arr[0].asDouble();
        pos.y = arr[1].asDouble();
        pos.z = arr[2].asDouble();
    }
    return pos;
}

// ============================================
// Поворот: {"rot": [x,y,z,a]} или {"rot": [[x,y,z,a],[x,y,z,a]]}
// ============================================
static void fillOneRotation(const TrinityJson::Value& axisArr,
                            TrinityRotation& r) {
    if (axisArr.isArray() && axisArr.size() >= 4) {
        r.x = axisArr[0].asDouble();
        r.y = axisArr[1].asDouble();
        r.z = axisArr[2].asDouble();
        r.angle = axisArr[3].asDouble();
    }
}

TrinityRotationCompound TrinityCore::parseRotation(const TrinityJson::Value& data) {
    TrinityRotationCompound compound;
    const auto& rot = data["rot"];
    if (!rot.isArray()) return compound;

    if (rot.size() >= 1) {
        if (rot[0].isArray()) {
            // Compound: [[x,y,z,a], [x,y,z,a]]
            for (size_t i = 0; i < rot.size() && compound.count < 2; ++i) {
                TrinityRotation r;
                fillOneRotation(rot[i], r);
                compound.rotations[compound.count++] = r;
            }
        } else {
            // Одиночный: [x,y,z,a]
            fillOneRotation(rot, compound.rotations[0]);
            compound.count = 1;
        }
    }
    return compound;
}
