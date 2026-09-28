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
// НЕЙРОН
// ============================================
struct TrinityNeuron {
    int id = 0;
    std::string code;          // D.S.0.425.850.10
    std::string type;          // detail, construction, project
    std::string category;      // shield, rib, sidewall, formwork
    std::string material;      // PLYWOOD-FSF
    std::string status;        // pending, done, error

    int width = 0;
    int height = 0;
    int thickness = 10;
    int processCode = 0;       // 0=None, 2=Hole, 3=Socket

    // --- Динамические свойства планки (rib). Габариты width/height/thickness
    //     по умолчанию парсятся из кода детали (D.S.3.425.125.10), но могут
    //     быть переопределены полями верхнего уровня в JSON нейрона (data):
    //     "width", "height", "thickness"
    //     Тонкая настройка гнёзд — секцией "rib":
    //     "rib": { "height": 125, "slotHalf": 4, "slotDepth": 54,
    //              "holeOffset": 53 }
    double ribHeight   = 0;            // 0 = использовать height нейрона
    double ribSlotHalf = 4.0;          // полуширина гнёзд (прорезей)
    // Глубина прямого участка паза по умолчанию = holeOffset + 1 мм:
    // дуга гнезда проходит через точку на расстоянии holeOffset от края,
    // поэтому прямой участок обязан быть чуть больше, иначе контур
    // самопересекается (см. sanity-проверку в buildRib).
    double ribSlotDepth = 54.0;        // глубина прямого участка паза
    double ribHoleOffset = 53.0;       // отступ дуги гнезда от края

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

    // Все детали с category = 'rib' (для команды TRIB — разработка)
    std::vector<TrinityNeuron> loadRibs();

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

    // Динамические свойства планки из JSON нейрона (секция "rib")
    static void applyRibProperties(TrinityNeuron& n);

    // Извлечение числового значения по ключу из JSON-объекта (упрощённый парсер)
    static bool jsonGetDouble(const std::string& json, const std::string& key, double& out);
};