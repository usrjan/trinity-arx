// TrinityCore.cpp
#include "StdAfx.h"
#include "TrinityCore.h"

// ============================================
// Деструкторы / RAII
// ============================================
TrinityCore::~TrinityCore() { disconnect(); }

TrinityCore::QueryResult::~QueryResult() {
    if (result) mysql_free_result(result);
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
// Выполнение prepared-запроса c буферизованным результатом
// params — входные параметры (строки/числа); lifetime данных
// должен переживать вызов.
// ============================================
bool TrinityCore::runPrepared(const char* stmtText,
                              const std::vector<MYSQL_BIND>& params,
                              QueryResult& out) {
    out.mysql = m_mysql;
    if (!m_connected) return false;

    MYSQL_STMT* stmt = mysql_stmt_init(m_mysql);
    if (!stmt) return false;

    bool ok = false;
    do {
        if (mysql_stmt_prepare(stmt, stmtText,
                               static_cast<unsigned long>(strlen(stmtText))) != 0) {
            trinityLog(L"[TrinityCore] prepare failed: %hs", mysql_stmt_error(stmt));
            break;
        }

        // Копируем, т.к. mysql_stmt_bind_param требует non-const указатели
        std::vector<MYSQL_BIND> bindIn(params);
        if (!bindIn.empty()) {
            if (mysql_stmt_bind_param(stmt, bindIn.data()) != 0) {
                trinityLog(L"[TrinityCore] bind_param failed: %hs", mysql_stmt_error(stmt));
                break;
            }
        }

        if (mysql_stmt_execute(stmt) != 0) {
            trinityLog(L"[TrinityCore] execute failed: %hs", mysql_stmt_error(stmt));
            break;
        }

        if (mysql_stmt_store_result(stmt) != 0) {
            trinityLog(L"[TrinityCore] store_result failed: %hs", mysql_stmt_error(stmt));
            break;
        }

        out.result = mysql_store_result(stmt);   // дублирует буферы st_store
        mysql_stmt_close(stmt);
        ok = (out.result != nullptr);
        if (!ok) {
            trinityLog(L"[TrinityCore] store_result(NULL): %hs", mysql_error(m_mysql));
        }
        return ok;
    } while (false);

    mysql_stmt_close(stmt);
    return false;
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

TrinityNeuronPtr TrinityCore::loadNeuronWhere(const char* whereSql,
                                              const std::vector<std::string>& strParams,
                                              const std::vector<int>& intParams) {
    // упрощение: только один параметр (код или id)
    char sql[1024];
    snprintf(sql, sizeof(sql), kNeuronSelectColumns, whereSql);

    std::vector<MYSQL_BIND> params;
    unsigned long len = 0;
    enum TypeTag { kStr, kInt } tag = kStr;
    longlong intValue = 0;

    if (!strParams.empty()) {
        MYSQL_BIND b{};
        b.buffer_type = MYSQL_TYPE_STRING;
        b.buffer = const_cast<char*>(strParams.front().c_str());
        len = static_cast<unsigned long>(strParams.front().size());
        b.buffer_length = &len;
        b.is_null = nullptr;
        params.push_back(b);
        tag = kStr;
    } else if (!intParams.empty()) {
        MYSQL_BIND b{};
        b.buffer_type = MYSQL_TYPE_LONGLONG;
        intValue = intParams.front();
        b.buffer_value = &intValue;
        params.push_back(b);
        tag = kInt;
    }
    (void)tag;

    QueryResult res;
    if (!runPrepared(sql, params, res)) return nullptr;

    TrinityNeuronPtr neuron;
    MYSQL_ROW row = mysql_fetch_row(res.result);
    if (row) neuron = std::make_unique<TrinityNeuron>(
        parseNeuronRow(atoi(row[0] ? row[0] : "0"),
                       row[1] ? row[1] : "",
                       row[2] ? row[2] : "",
                       row[3] ? row[3] : "",
                       row[4] ? row[4] : "PLYWOOD-FSF",
                       row[5] ? row[5] : "",
                       row[6] ? row[6] : ""));
    return neuron;
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
    MYSQL_BIND b{};
    b.buffer_type = MYSQL_TYPE_LONGLONG;
    b.buffer_value = &pid;
    std::vector<MYSQL_BIND> params = { b };

    QueryResult res;
    if (!runPrepared(sql, params, res)) return children;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res.result))) {
        children.push_back(parseSynapseRow(
            row[0] ? atoi(row[0]) : 0,
            row[1] ? atoi(row[1]) : 0,
            row[2] ? atoi(row[2]) : 0,
            row[3] ? row[3] : "",
            row[4] ? row[4] : ""));
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

    QueryResult res;
    if (!runPrepared(sql, {}, res)) return projects;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res.result))) {
        projects.push_back(parseNeuronRow(
            row[0] ? atoi(row[0]) : 0,
            row[1] ? row[1] : "",
            row[2] ? row[2] : "",
            row[3] ? row[3] : "",
            row[4] ? row[4] : "",
            row[5] ? row[5] : "",
            row[6] ? row[6] : ""));
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
    MYSQL_BIND b{};
    b.buffer_type = MYSQL_TYPE_LONGLONG;
    b.buffer_value = &v;
    std::vector<MYSQL_BIND> params = { b };

    QueryResult res;
    return runPrepared(sql, params, res);
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
