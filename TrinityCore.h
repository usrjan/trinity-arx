// TrinityCore.h
#pragma once
#include "StdAfx.h"

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
// ПАРАМЕТРЫ ПЛАНКИ (rib) — см. buildRib()
// ============================================
// Значения по умолчанию исторически зашиты в геометрии планки
// (D.S.3.*). Теперь они вынесены в структуру и могут быть
// переопределены свойствами нейрона в БД:
//   "rib": { "height": 125, "slotHalf": 4, "slotDepth": 23.54316771,
//            "holeOffset": 53 }
// либо отдельными полями верхнего уровня: "rib_height", "rib_slot_half",
// "rib_slot_depth", "rib_hole_offset".
struct RibParams {
    double height     = 125.0;          // высота планки (постоянная Y контура)
    double slotHalf   = 4.0;            // половина ширины прорези под гнёздом
    double slotDepth  = 23.54316771;    // глубина прямой части паза от кромки
    double holeOffset = 53.0;           // расстояние до крайней точки дуги гнезда

    bool isValid() const {
        return height > 0 && slotHalf > 0 &&
               slotDepth > slotHalf && holeOffset > slotDepth;
    }
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
    std::string status;        // pending, done, error

    double width = 0;          // из кода детали или свойства "width"
    double height = 0;         // из кода детали или свойства "height"
    double thickness = 10;     // из кода детали или свойства "thickness"
    int processCode = 0;       // 0=None, 2=Hole, 3=Socket

    RibParams rib;             // параметры планки (для category == "rib")

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

// ============================================
// ЯДРО — доступ к базе данных
// ============================================
class TrinityCore {
    MYSQL* m_mysql = nullptr;
    bool m_connected = false;

public:
    TrinityCore() = default;
    ~TrinityCore();

    bool connect(const char* host, const char* user, const char* pass, const char* db);
    void disconnect();
    bool isConnected() const { return m_connected; }
    MYSQL* handle() { return m_mysql; }

    // Нейроны
    TrinityNeuron* loadNeuronByCode(const std::string& code);
    TrinityNeuron* loadNeuronById(int id);

    // Связи
    std::vector<TrinitySynapse> loadChildren(int parentId);

    // Проекты
    std::vector<TrinityNeuron> loadPendingProjects();

    // Все детали указанного category (например "rib") — для команды TRIB.
    // Свойства width/height/thickness при этом приходят из JSON нейрона,
    // а не из кода (см. parseNeuronRow).
    std::vector<TrinityNeuron> loadDetailsByCategory(const std::string& category);

    // Статусы
    bool markNeuronDone(int id);

    // Экранирование строки для безопасной подстановки в SQL (учитывает кодировку соединения).
    // Возвращает готовый литерал с кавычками: 'escaped\'text' — подставляется в запрос как есть.
    // Пустая строка -> NULL (для необязательных параметров).
    std::string escapeSqlLiteral(const std::string& value, bool emptyMeansNull = false) const;

    // Парсинг
    static TrinityNeuron parseNeuronRow(MYSQL_ROW row);
    static TrinitySynapse parseSynapseRow(MYSQL_ROW row);
    static TrinityPosition parsePosition(const std::string& json);
    static TrinityRotationCompound parseRotation(const std::string& json);

    // Извлечение числового свойства из JSON-строки (без внешних JSON-библиотек).
    // Ищет "key" и читает следующее за ним число. Возвращает false, если не найдено.
    static bool jsonGetNumber(const std::string& json, const char* key, double& out);

    // Извлечение строкового свойства из JSON-строки. Пустая строка = не найдено.
    static std::string jsonString(const std::string& json, const char* key);
};