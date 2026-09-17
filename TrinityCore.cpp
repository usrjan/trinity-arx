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
// ЗАГРУЗКА НЕЙРОНА ПО КОДУ
// ============================================
TrinityNeuron* TrinityCore::loadNeuronByCode(const std::string& code) {
    if (!m_connected) return nullptr;

    char escapedCode[256];
    mysql_real_escape_string(m_mysql, escapedCode, code.c_str(), (unsigned long)code.length());

    char query[512];
    snprintf(query, sizeof(query),
        "SELECT id, "
        "  JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')), "
        "  type, "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.category')), ''), "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.material')), 'PLYWOOD-FSF'), "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.status')), ''), "
        "  data "
        "FROM neuron "
        "WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = '%s' "
        "  AND is_deleted = 0 "
        "LIMIT 1",
        escapedCode);

    if (mysql_query(m_mysql, query) != 0) return nullptr;

    MYSQL_RES* result = mysql_store_result(m_mysql);
    if (!result || mysql_num_rows(result) == 0) {
        if (result) mysql_free_result(result);
        return nullptr;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    // Возвращаем сырой указатель для обратной совместимости
    // В новом коде используйте TrinityNeuronPtr
    TrinityNeuron* neuron = new TrinityNeuron(parseNeuronRow(row));
    mysql_free_result(result);
    return neuron;
}

// ============================================
// ЗАГРУЗКА НЕЙРОНА ПО ID
// ============================================
TrinityNeuron* TrinityCore::loadNeuronById(int id) {
    if (!m_connected) return nullptr;

    char query[256];
    snprintf(query, sizeof(query),
        "SELECT id, "
        "  JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')), "
        "  type, "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.category')), ''), "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.material')), 'PLYWOOD-FSF'), "
        "  COALESCE(JSON_UNQUOTE(JSON_EXTRACT(data, '$.status')), ''), "
        "  data "
        "FROM neuron WHERE id = %d AND is_deleted = 0",
        id);

    if (mysql_query(m_mysql, query) != 0) return nullptr;

    MYSQL_RES* result = mysql_store_result(m_mysql);
    if (!result || mysql_num_rows(result) == 0) {
        if (result) mysql_free_result(result);
        return nullptr;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    // Возвращаем сырой указатель для обратной совместимости
    // В новом коде используйте TrinityNeuronPtr
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

    char query[512];
    snprintf(query, sizeof(query),
        "SELECT s.id, s.parent, s.child, "
        "  JSON_UNQUOTE(JSON_EXTRACT(n.data, '$.code')), "
        "  s.data "
        "FROM synapse s "
        "JOIN neuron n ON s.child = n.id "
        "WHERE s.parent = %d AND n.is_deleted = 0 "
        "ORDER BY s.id",
        parentId);

    if (mysql_query(m_mysql, query) != 0) return children;

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
// ОТМЕТКА "DONE"
// ============================================
bool TrinityCore::markNeuronDone(int id) {
    if (!m_connected) return false;

    char query[256];
    snprintf(query, sizeof(query),
        "UPDATE neuron SET data = JSON_SET(data, '$.status', 'done') WHERE id = %d", id);

    return mysql_query(m_mysql, query) == 0;
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