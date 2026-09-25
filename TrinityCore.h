// TrinityCore.h
#pragma once
#include "StdAfx.h"
#include "TrinityJson.h"

// ============================================
// Позиция [x, y, z]
// ============================================
struct TrinityPosition {
    double x = 0, y = 0, z = 0;
    AcGePoint3d toAcGe() const { return AcGePoint3d(x, y, z); }
};

// ============================================
// Одиночный поворот: ось + угол в градусах
// ============================================
struct TrinityRotation {
    double x = 0, y = 0, z = 0;
    double angle = 0;
};

// ============================================
// Составной поворот (до 2 последовательных)
// ============================================
struct TrinityRotationCompound {
    TrinityRotation rotations[2];
    int count = 0;  // 0, 1 или 2
};

// ============================================
// Нейрон — запись о сущности в БД
// ============================================
struct TrinityNeuron {
    int id = 0;
    std::string code;          // D.S.0.425.850.10
    std::string type;          // detail, construction, project
    std::string category;      // shield, rib, sidewall
    std::string material;      // PLYWOOD-FSF
    std::string status;        // pending, done, error

    int width = 0;
    int height = 0;
    int thickness = 10;
    int processCode = 0;       // 0=None, 2=Hole, 3=Socket

    std::string jsonData;      // весь JSON нейрона
};

using TrinityNeuronPtr = std::unique_ptr<TrinityNeuron>;

// ============================================
// Синапс — связь родитель → ребёнок
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
// Ядро — доступ к базе данных MySQL.
// Все запросы параметризованы (MYSQL_BIND).
// Используется только из потока AutoCAD.
// ============================================
class TrinityCore {
public:
    TrinityCore() = default;
    ~TrinityCore();

    TrinityCore(const TrinityCore&) = delete;
    TrinityCore& operator=(const TrinityCore&) = delete;

    bool connect(const char* host, const char* user,
                 const char* pass, const char* db);
    void disconnect();
    bool isConnected() const { return m_connected; }

    // ----------------------------------------
    // Запросы (nullptr / пустой вектор при ошибке)
    // ----------------------------------------
    TrinityNeuronPtr loadNeuronByCode(const std::string& code);
    TrinityNeuronPtr loadNeuronById(int id);
    std::vector<TrinitySynapse> loadChildren(int parentId);
    std::vector<TrinityNeuron> loadPendingProjects();
    bool markNeuronDone(int id);

    // ----------------------------------------
    // Парсинг (публичны для тестирования)
    // ----------------------------------------
    static TrinityNeuron parseNeuronRow(int id, const std::string& code,
                                        const std::string& type,
                                        const std::string& category,
                                        const std::string& material,
                                        const std::string& status,
                                        const std::string& data);
    static TrinitySynapse parseSynapseRow(int id, int parentId, int childId,
                                          const std::string& childCode,
                                          const std::string& data);
    static TrinityPosition parsePosition(const TrinityJson::Value& data);
    static TrinityRotationCompound parseRotation(const TrinityJson::Value& data);
    // Размеры из JSON, либо из кода вида D.<T>.<proc>.<W>.<H>.<Th>
    static void applyGeometry(TrinityNeuron& n);

    // ----------------------------------------
    // RAII-обёртка над prepared statement.
    // Чтение результата — через fetch()/bindResult().
    // ----------------------------------------
    class Stmt {
    public:
        Stmt() = default;
        ~Stmt();

        Stmt(const Stmt&) = delete;
        Stmt& operator=(const Stmt&) = delete;

        bool prepare(MYSQL* mysql, const char* sql);
        bool bindParams(const std::vector<MYSQL_BIND>& params);
        bool executeStore();                      // execute + store_result
        my_ulonglong numRows() const;
        bool bindResult(std::vector<MYSQL_BIND> binds);
        bool fetch();                             // false = конец выборки
        void freeResult();

        MYSQL_STMT* handle() const { return m_stmt; }

    private:
        MYSQL*      m_mysql = nullptr;
        MYSQL_STMT* m_stmt  = nullptr;
    };

    // Подготовка + привязка входных параметров + execute + store_result.
    // true, если запрос выполнен (в т.ч. 0 строк).
    bool runPrepared(const char* stmtText,
                     const std::vector<MYSQL_BIND>& params,
                     Stmt& out);

    // Помощники для заполнения MYSQL_BIND (input)
    static MYSQL_BIND bindString(const char* data, unsigned long& lenInOut);
    static MYSQL_BIND bindLongLong(longlong& value);

private:
    // Общий SELECT нейронов по условию
    TrinityNeuronPtr loadNeuronWhere(const char* whereSql,
                                     const std::vector<std::string>& strParams,
                                     const std::vector<int>& intParams);

    MYSQL* m_mysql = nullptr;
    bool   m_connected = false;
};
