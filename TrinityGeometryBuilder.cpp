// TrinityGeometryBuilder.cpp
#include "StdAfx.h"
#include "TrinityGeometryBuilder.h"
#include "TrinityLayerManager.h"
#include "TrinityJson.h"

// ============================================
// Геометрические константы планки (мм)
// ============================================
namespace {
constexpr double RIB_HEIGHT      = 125.0;        // фиксированная высота планки
constexpr double SLOT_HALF       = 4.0;          // полуширина паза
constexpr double SLOT_DEPTH      = 23.54316771;  // глубина гнёзда
constexpr double HOLE_OFFSET     = 53.0;         // отступ дуги гнезда от края
constexpr double BOLT_RADIUS     = 3.0;          // маркер болта Ø6
constexpr double BOLT_EDGE_SHIFT = 5.0;          // вынос болтов за край щита
} // namespace

// ============================================
// Диспетчер по processCode
// ============================================
AcDb3dSolid* TrinityGeometryBuilder::build(const TrinityNeuron& detail) {
    switch (detail.processCode) {
        case 3:  return buildRib(detail);        // планка с гнёздами
        case 2:  // fallthrough — боковая стенка = щит, отверстия сверляет BuildEngine
        case 0:
        default: return buildBox(detail);
    }
}

// ============================================
// Щит (прямоугольный параллелепипед центрованный в 0)
// ============================================
AcDb3dSolid* TrinityGeometryBuilder::buildBox(const TrinityNeuron& d) {
    AcDb3dSolid* solid = new AcDb3dSolid();
    if (solid->createBox(d.width, d.height, d.thickness) != Acad::eOk) {
        delete solid;
        return nullptr;
    }

    AcGeMatrix3d mat;
    mat.setToTranslation(AcGeVector3d(d.width / 2.0, d.height / 2.0, d.thickness / 2.0));
    solid->transformBy(mat);

    solid->setLayer(toWide(TrinityLayerManager::layerName(d.material)).c_str());
    return solid;
}

// ============================================
// Планка: контур с двумя гнёздами (дугами) на торцах
// ============================================
static void addArcVertex(AcDbPolyline* pPoly, int index,
                         const AcGePoint2d& start,
                         double startAngle, double endAngle) {
    // Дуга через bulge: сегмент от start на угол (endAngle - startAngle)
    const double sweep = endAngle - startAngle;
    const double bulge = tan(sweep / 4.0);
    pPoly->addVertexAt(index, start, bulge, 0, 0);
}

AcDb3dSolid* TrinityGeometryBuilder::buildRib(const TrinityNeuron& d) {
    const double W = d.width;
    const double H = RIB_HEIGHT;
    const double T = d.thickness;
    const double centerY = H / 2.0;

    // ---- Левое гнездо (у X=W): дуга через 3 точки --------------------------
    AcGePoint2d ptLStart(centerY - SLOT_HALF, W - SLOT_DEPTH);
    AcGePoint2d ptLArc  (centerY,              W - HOLE_OFFSET);
    AcGePoint2d ptLEnd  (centerY + SLOT_HALF,  W - SLOT_DEPTH);

    AcGeCircArc2d arcL(ptLStart, ptLArc, ptLEnd);
    AcGePoint2d cL = arcL.center();
    double angLStart = atan2(ptLStart.y - cL.y, ptLStart.x - cL.x);
    double angLEnd   = atan2(ptLEnd.y  - cL.y, ptLEnd.x  - cL.x);
    if (angLEnd < angLStart) angLEnd += 2.0 * M_PI;   // против часовой

    // ---- Правое гнездо (у X=0): зеркально ----------------------------------
    AcGePoint2d ptRStart(centerY + SLOT_HALF, SLOT_DEPTH);
    AcGePoint2d ptRArc  (centerY,             HOLE_OFFSET);
    AcGePoint2d ptREnd  (centerY - SLOT_HALF, SLOT_DEPTH);

    AcGeCircArc2d arcR(ptRStart, ptRArc, ptREnd);
    AcGePoint2d cR = arcR.center();
    double angRStart = atan2(ptRStart.y - cR.y, ptRStart.x - cR.x);
    double angREnd   = atan2(ptREnd.y  - cR.y, ptREnd.x  - cR.x);
    if (angREnd < angRStart) angREnd += 2.0 * M_PI;

    // ---- Замкнутый контур (12 вершин) --------------------------------------
    std::unique_ptr<AcDbPolyline> pPoly(new AcDbPolyline(12));
    pPoly->addVertexAt(0,  AcGePoint2d(0.0,            W), 0, 0, 0);
    pPoly->addVertexAt(1,  AcGePoint2d(centerY - 4.0,  W), 0, 0, 0);
    addArcVertex(pPoly.get(), 2, ptLStart, angLStart, angLEnd);
    pPoly->addVertexAt(3,  ptLEnd,                     0, 0, 0);
    pPoly->addVertexAt(4,  AcGePoint2d(centerY + 4.0,  W), 0, 0, 0);
    pPoly->addVertexAt(5,  AcGePoint2d(H,              W), 0, 0, 0);
    pPoly->addVertexAt(6,  AcGePoint2d(H,              0.0), 0, 0, 0);
    pPoly->addVertexAt(7,  AcGePoint2d(centerY + 4.0,  0.0), 0, 0, 0);
    addArcVertex(pPoly.get(), 8, ptRStart, angRStart, angREnd);
    pPoly->addVertexAt(9,  ptREnd,                     0, 0, 0);
    pPoly->addVertexAt(10, AcGePoint2d(centerY - 4.0,  0.0), 0, 0, 0);
    pPoly->addVertexAt(11, AcGePoint2d(0.0,            0.0), 0, 0, 0);
    pPoly->setClosed(Adesk::kTrue);

    const std::wstring layerW = toWide(TrinityLayerManager::layerName(d.material));
    pPoly->setLayer(layerW.c_str());

    // Контур лежит в XY-плоскости плана: разворачиваем на -90° вокруг X
    // и сдвигаем так, чтобы деталь занимала Y ∈ [0, H].
    AcGeMatrix3d rot;
    rot.setToRotation(-M_PI / 2.0, AcGeVector3d(1, 0, 0), AcGePoint3d::kOrigin);
    AcGeMatrix3d shift;
    shift.setToTranslation(AcGeVector3d(0, H, 0));
    pPoly->transformBy(shift * rot);

    // ---- Region → extrude ---------------------------------------------------
    AcDbVoidPtrArray curves, regions;
    pPoly->explode(curves);
    if (AcDbRegion::createFromCurves(curves, regions) != Acad::eOk ||
        regions.length() != 1) {
        for (int i = 0; i < curves.length(); ++i) delete static_cast<AcRxObject*>(curves[i]);
        for (int i = 0; i < regions.length(); ++i) delete static_cast<AcRxObject*>(regions[i]);
        return nullptr;
    }

    AcDbRegion* pRegion = static_cast<AcDbRegion*>(regions[0]);
    AcDb3dSolid* solid = new AcDb3dSolid();
    const Acad::ErrorStatus es = solid->extrude(pRegion, T, 0.0);

    pRegion->erase();                                 // регион из массива — в базу не пошёл
    for (int i = 0; i < regions.length(); ++i)
        if (regions[i] && regions[i] != pRegion) delete static_cast<AcRxObject*>(regions[i]);
    for (int i = 0; i < curves.length(); ++i) delete static_cast<AcRxObject*>(curves[i]);

    if (es != Acad::eOk) { delete solid; return nullptr; }

    solid->setLayer(layerW.c_str());
    return solid;
}

// ============================================
// Позиции болтов щита
// ============================================
std::vector<AcGePoint3d> TrinityGeometryBuilder::getBoltPositions(const TrinityNeuron& d) {
    const double y = d.height / 2.0;
    const double z = d.thickness / 2.0;
    return {
        AcGePoint3d(-BOLT_EDGE_SHIFT,        y, z),
        AcGePoint3d(d.width + BOLT_EDGE_SHIFT, y, z),
    };
}

// ============================================
// Маркеры болтов (окружности Ø6, плоскость YZ)
// ============================================
void TrinityGeometryBuilder::drawBoltMarkers(const TrinityNeuron& d,
                                             AcDbBlockTableRecord* pMs,
                                             AcDbObjectIdArray& ids) {
    if (!pMs) return;
    for (const auto& pos : getBoltPositions(d)) {
        AcDbCircle* pCircle = new AcDbCircle();
        pCircle->setCenter(pos);
        pCircle->setRadius(BOLT_RADIUS);
        pCircle->setNormal(AcGeVector3d(1, 0, 0));
        pCircle->setLayer(TrinityLayerManager::LAYER_BOLT);

        // appendAcDbEntity закрывает record сам при успехе;
        // при неудаче объект нужно удалить вручную.
        AcDbObjectId circleId;
        if (pMs->appendAcDbEntity(circleId, pCircle) == Acad::eOk) {
            ids.append(circleId);
        } else {
            delete pCircle;
        }
    }
}

// ============================================
// Матрица составного поворота вокруг center
// ============================================
AcGeMatrix3d TrinityGeometryBuilder::rotationMatrix(const TrinityRotationCompound& rot,
                                                    const AcGePoint3d& center) {
    AcGeMatrix3d mat;
    mat.setToIdentity();
    for (int i = 0; i < rot.count; ++i) {
        const auto& r = rot.rotations[i];
        if (r.angle == 0) continue;
        AcGeVector3d axis(r.x, r.y, r.z);
        if (axis.isZeroLength()) continue;
        axis.normalize();
        AcGeMatrix3d rotMat;
        rotMat.setToRotation(r.angle * M_PI / 180.0, axis, center);
        mat = rotMat * mat;   // первый поворот применяется первым
    }
    return mat;
}

// ============================================
// Трансформация солида: поворот + перенос
// ============================================
void TrinityGeometryBuilder::transform(AcDb3dSolid* solid,
                                       const TrinityPosition& pos,
                                       const TrinityRotationCompound& rot) {
    if (!solid) return;
    AcGeMatrix3d mat = rotationMatrix(rot, AcGePoint3d::kOrigin);
    mat.setTranslation(AcGeVector3d(pos.x, pos.y, pos.z));
    solid->transformBy(mat);
}

// ============================================
// Отверстия боковой стенки из JSON нейрона
// ============================================
std::vector<AcGePoint3d> TrinityGeometryBuilder::getHolePositions(const TrinityNeuron& d) {
    std::vector<AcGePoint3d> holes;
    TrinityJson::Value root = TrinityJson::parse(d.jsonData);
    const auto& arr = root["holes"];
    if (!arr.isArray()) return holes;

    for (size_t i = 0; i < arr.size(); ++i) {
        const auto& h = arr[i];
        holes.emplace_back(h["x"].asDouble(), h["y"].asDouble(), 0.0);
    }
    return holes;
}
