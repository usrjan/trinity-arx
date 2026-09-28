// TrinityCore.cpp
#include "StdAfx.h"
#include "TrinityCore.h"

// ============================================
// КОНСТРУКТОР / ДЕСТРУКТОР
// ============================================
TrinityCore::~TrinityCore() { disconnect(); }

// ============================================
// ПОДКЛЮЧЕНИЕ
// ============================================
bool TrinityCore::connect(const char* host, const char* user,
                           const char* pass, const char* db) {
    if (m_connected) return true;

    m_mysql = mysql_init(nullptr);
    if (!mysql_real_connect(m_mysql, host, user, pass, db, 0, nullptr, 0)) {
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
    if (m_mysql && m_connected) {
        mysql_close(m_mysql);
        m_mysql = nullptr;
        m_connected = false;
    }
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
    if (!m_connected) return "'"; // соединение недоступно — вернём заведомо невалидный литерал

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
// ЗАГРУЗКА НЕЙРОНА ПО КОДУ
// ============================================
TrinityNeuron* TrinityCore::loadNeuronByCode(const std::string& code) {
    if (!m_connected) return nullptr;

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

    if (mysql_query(m_mysql, query.c_str()) != 0) return nullptr;

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
    if (!m_connected) return nullptr;

    const std::string query =
        "SELECT id, "
        "  JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')), "
        "  type, "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.category')), ''), "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.material')), 'PLYWOOD-FSF'), "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.status')), ''), "
        "  data "
        "FROM neuron WHERE id = " + std::to_string(id) + " AND is_deleted = 0";

    if (mysql_query(m_mysql, query.c_str()) != 0) return nullptr;

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
std::vector<TrinitySynapse> TrinityCore::loadChildren(int parentId) {
    std::vector<TrinitySynapse> children;
    if (!m_connected) return children;

    const std::string query =
        "SELECT s.id, s.parent, s.child, "
        "  JSON_UNQUOTE(JSON_EXTRACT(n.data, '$.code')), "
        "  s.data "
        "FROM synapse s "
        "JOIN neuron n ON s.child = n.id "
        "WHERE s.parent = " + std::to_string(parentId) + " AND n.is_deleted = 0 "
        "ORDER BY s.id";

    if (mysql_query(m_mysql, query.c_str()) != 0) return children;

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
std::vector<TrinityNeuron> TrinityCore::loadPendingProjects() {
    std::vector<TrinityNeuron> projects;
    if (!m_connected) return projects;

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

    if (mysql_query(m_mysql, query) != 0) return projects;

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
// ЗАГРУЗКА ВСЕХ ДЕТАЛЕЙ ПО CATEGORY (TRIB)
// ============================================
std::vector<TrinityNeuron> TrinityCore::loadDetailsByCategory(const std::string& category) {
    std::vector<TrinityNeuron> details;
    if (!m_connected) return details;

    const std::string query =
        "SELECT id, "
        "  JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')), "
        "  type, "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.category')), ''), "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.material')), 'PLYWOOD-FSF'), "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.status')), ''), "
        "  data "
        "FROM neuron "
        "WHERE type = 'detail' "
        "  AND JSON_UNQUOTE(JSON_EXTRACT(data, '$.category')) = " + escapeSqlLiteral(category) + " "
        "  AND is_deleted = 0 "
        "ORDER BY COALESCE(JSON_EXTRACT(data, '$.sort'), 999999), id";

    if (mysql_query(m_mysql, query.c_str()) != 0) return details;

    MYSQL_RES* result = mysql_store_result(m_mysql);
    if (!result) return details;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        details.push_back(parseNeuronRow(row));
    }

    mysql_free_result(result);
    return details;
}

// ============================================
// ОТМЕТКА "DONE"
// ============================================
bool TrinityCore::markNeuronDone(int id) {
    if (!m_connected) return false;

    const std::string query =
        "UPDATE neuron SET data = JSON_SET(data, '$.status', 'done') WHERE id = "
        + std::to_string(id);

    return mysql_query(m_mysql, query.c_str()) == 0;
}

// ============================================
// МИНИ-ПАРСЕР JSON (без внешних библиотек)
// ============================================
// Найти начало значения по ключу верхнего уровня: "key" : <value>.
// Реализация простая, но достаточная для плоских свойств нейрона.
static size_t findJsonValue(const std::string& json, const char* key) {
    std::string pat = std::string("\"") + key + "\"";
    size_t p = json.find(pat);
    while (p != std::string::npos) {
        size_t colon = json.find(':', p + pat.size());
        if (colon == std::string::npos) return std::string::npos;
        // между именем ключа и «:» может быть только пробельная строка
        bool ok = true;
        for (size_t i = p + pat.size(); i < colon; i++) {
            char c = json[i];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') { ok = false; break; }
        }
        if (ok) {
            size_t v = colon + 1;
            while (v < json.size() && (json[v] == ' ' || json[v] == '\t' ||
                                        json[v] == '\n' || json[v] == '\r')) v++;
            return v;
        }
        p = json.find(pat, p + pat.size());
    }
    return std::string::npos;
}

bool TrinityCore::jsonGetNumber(const std::string& json, const char* key, double& out) {
    size_t v = findJsonValue(json, key);
    if (v == std::string::npos) return false;
    char* end = nullptr;
    double d = strtod(json.c_str() + v, &end);
    if (end == json.c_str() + v) return false;   // число не распознано
    out = d;
    return true;
}

std::string TrinityCore::jsonString(const std::string& json, const char* key) {
    size_t v = findJsonValue(json, key);
    if (v == std::string::npos || json[v] != '"') return "";
    size_t end = json.find('"', v + 1);
    if (end == std::string::npos) return "";
    return json.substr(v + 1, end - v - 1);
}

// Прочитать числовое свойство из вложенного объекта: rib.slotDepth и т.п.
static bool jsonGetNestedNumber(const std::string& json, const char* obj,
                                const char* key, double& out) {
    size_t v = findJsonValue(json, obj);
    if (v == std::string::npos || json[v] != '{') return false;
    size_t objEnd = json.find('}', v);
    if (objEnd == std::string::npos) return false;
    std::string nested = json.substr(v, objEnd - v + 1);
    return TrinityCore::jsonGetNumber(nested, key, out);
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

    // 1. Значения по умолчанию — из кода детали: D.S.<proc>.<W>.<H>.<T>
    if (!n.code.empty()) {
        int w = 0, h = 0, t = 0;
        if (sscanf_s(n.code.c_str(), "D.S.%d.%d.%d.%d",
                     &n.processCode, &w, &h, &t) == 4) {
            n.width = w;
            n.height = h;
            n.thickness = t;
        }
    }

    // 2. Свойства нейрона переопределяют размеры из кода —
    //    деталь становится динамической (управление через свойства в БД).
    double v = 0;
    if (jsonGetNumber(n.jsonData, "width", v))     n.width = v;
    if (jsonGetNumber(n.jsonData, "height", v))    n.height = v;
    if (jsonGetNumber(n.jsonData, "thickness", v)) n.thickness = v;

    // 3. Параметры планки (rib): объект "rib": {height, slotHalf, slotDepth, holeOffset}
    //    или плоские ключи "rib_height" и т.п.
    if (jsonGetNestedNumber(n.jsonData, "rib", "height", v))      n.rib.height = v;
    if (jsonGetNestedNumber(n.jsonData, "rib", "slotHalf", v))    n.rib.slotHalf = v;
    if (jsonGetNestedNumber(n.jsonData, "rib", "slotDepth", v))   n.rib.slotDepth = v;
    if (jsonGetNestedNumber(n.jsonData, "rib", "holeOffset", v))  n.rib.holeOffset = v;

    if (jsonGetNumber(n.jsonData, "rib_height", v))      n.rib.height = v;
    if (jsonGetNumber(n.jsonData, "rib_slot_half", v))   n.rib.slotHalf = v;
    if (jsonGetNumber(n.jsonData, "rib_slot_depth", v))  n.rib.slotDepth = v;
    if (jsonGetNumber(n.jsonData, "rib_hole_offset", v)) n.rib.holeOffset = v;

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