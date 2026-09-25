// TrinityGeometryBuilder.h
#pragma once
#include "StdAfx.h"
#include "TrinityCore.h"

// ============================================
// Построение 3D-геометрии деталей опалубки.
// Все методы статические; возвращённые солиды
// не прикреплены к базе (владеет вызывающий).
// ============================================
class TrinityGeometryBuilder {
public:
    // Диспетчер по processCode: 0=щит, 2=боковая стенка, 3=планка
    static AcDb3dSolid* build(const TrinityNeuron& detail);

    // Позиции болтов щита (±5 мм за краями, середина высоты)
    static std::vector<AcGePoint3d> getBoltPositions(const TrinityNeuron& d);

    // Маркеры болтов — окружности Ø6 в плоскости YZ на слое _bolt
    static void drawBoltMarkers(const TrinityNeuron& d,
                                AcDbBlockTableRecord* pMs,
                                AcDbObjectIdArray& ids);

    // Трансформация солида: compound rotation + translation
    static void transform(AcDb3dSolid* solid,
                          const TrinityPosition& pos,
                          const TrinityRotationCompound& rot);

    // Матрица составного поворота (градусы → радианы)
    static AcGeMatrix3d rotationMatrix(const TrinityRotationCompound& rot,
                                       const AcGePoint3d& center);

    // Отверстия боковой стенки из JSON нейрона: {"holes":[{"x":..,"y":..},...]}
    static std::vector<AcGePoint3d> getHolePositions(const TrinityNeuron& d);

private:
    static AcDb3dSolid* buildBox(const TrinityNeuron& d);   // щит / стенка
    static AcDb3dSolid* buildRib(const TrinityNeuron& d);   // планка с гнёздами
};
