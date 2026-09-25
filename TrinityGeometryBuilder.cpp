// TrinityGeometryBuilder.cpp
#include "StdAfx.h"
#include "TrinityGeometryBuilder.h"
#include "TrinityLayerManager.h"

// ============================================
// ДИСПЕТЧЕР
// ============================================
AcDb3dSolid* TrinityGeometryBuilder::build(const TrinityNeuron& detail) {
    switch (detail.processCode) {
        case 0: return buildBox(detail);       // щит
        case 2: return buildSidewall(detail);  // боковая стенка
        case 3: return buildRib(detail);       // планка
        default: return buildBox(detail);
    }
}

// ============================================
// ЩИТ (прямоугольник)
// ============================================
AcDb3dSolid* TrinityGeometryBuilder::buildBox(const TrinityNeuron& d) {
    AcDb3dSolid* solid = new AcDb3dSolid();
    solid->createBox(d.width, d.height, d.thickness);

    AcGeMatrix3d mat;
    mat.setToIdentity();
    mat.setTranslation(AcGeVector3d(d.width / 2.0, d.height / 2.0, d.thickness / 2.0));
    solid->transformBy(mat);

    std::string layer = TrinityLayerManager::layerName(d.material);
    wchar_t layerW[256];
    MultiByteToWideChar(CP_UTF8, 0, layer.c_str(), -1, layerW, 256);
    solid->setLayer(layerW);

    return solid;
}

// ============================================
// БОКОВАЯ СТЕНКА (щит со сквозными отверстиями)
// ============================================
AcDb3dSolid* TrinityGeometryBuilder::buildSidewall(const TrinityNeuron& d) {
    // Просто щит. Все сверления делает buildDetail.
    return buildBox(d);
}

// ============================================
// ПЛАНКА (со гнёздами) — ПАРАМЕТРИЧЕСКАЯ
// ============================================
// Габариты полностью из нейрона: width — длина вдоль Y, height —
// ширина вдоль X, thickness — экструзия по Z. Седла (полукруглые
// вырезы под трубу) — у верхних углов коротких кромок, как в исходном
// профиле D.S.3.425.125.10: левое седло у вершины (0, W), правое — у
// (H, 0). Все параметры седел масштабируются от W и H, поэтому планка
// любой конфигурации строится тем же кодом.
AcDb3dSolid* TrinityGeometryBuilder::buildRib(const TrinityNeuron& d) {
    const double W = d.width;      // длина планки (вдоль оси Y, как в исходном профиле)
    const double H = d.height;     // высота планки (вдоль оси X)
    const double T = d.thickness;  // толщина (Z, экструзия)

    if (W <= 0 || H <= 0 || T <= 0) return nullptr;

    const double centerY = H / 2.0; // центр высоты — ось седел

    // Параметры седла масштабируются от габаритов (для D.S.3.425.125.10
    // дают исторические значения: half=4, depth~23.6 при W=425, H=125)
    const double SLOT_HALF = std::max(1.0, std::min(4.0, H / 8.0));   // полуширина прорези на кромках Y=0 и Y=W
    const double SLOT_DEPTH = std::max(1.0, std::min(W / 18.0, H / 4.0)); // насколько седло ниже кромки Y=W
    const double SLOT_RISE = std::max(1.0, std::min(W / 8.0, H / 4.0));   // насколько седло выше кромки Y=0

    if (centerY - SLOT_HALF <= 0)
        return nullptr;

    struct ArcData {
        AcGePoint2d center;
        double radius;
        double startAng;
        double endAng;
    };
    auto buildArc = [](const AcGePoint2d& p0, const AcGePoint2d& pm, const AcGePoint2d& p1) {
        ArcData a;
        AcGeCircArc2d ge(p0, pm, p1);
        a.center = ge.center();
        a.radius = ge.radius();
        AcGeVector2d v0(p0.x - a.center.x, p0.y - a.center.y);
        AcGeVector2d v1(p1.x - a.center.x, p1.y - a.center.y);
        a.startAng = v0.angle();
        a.endAng = v1.angle();
        if (a.startAng > a.endAng) a.startAng -= 2.0 * M_PI;
        return a;
    };

    // --------------------------------------------------
    // ЛЕВОЕ СЕДЛО: у вершины (0, W), прогиб вниз
    // (совпадает с исходным контуром: точки на кромке Y=W,
    //  середина дуги на x=centerY, y=W-HOLE_OFFSET)
    // --------------------------------------------------
    // midpoint дуги: исторически 53.0 при W=425 → масштабируем от W
    const double HOLE_L = std::min(53.0 / 425.0 * W, W - 1.0);
    ArcData left = buildArc(
        AcGePoint2d(centerY - SLOT_HALF, W - SLOT_DEPTH),
        AcGePoint2d(centerY, W - HOLE_L),
        AcGePoint2d(centerY + SLOT_HALF, W - SLOT_DEPTH));

    // --------------------------------------------------
    // ПРАВОЕ СЕДЛО: у вершины (H, 0), зеркально
    // --------------------------------------------------
    const double HOLE_R = std::min(SLOT_RISE * 2.0, W - 1.0);
    ArcData right = buildArc(
        AcGePoint2d(centerY + SLOT_HALF, SLOT_DEPTH),
        AcGePoint2d(centerY, HOLE_R),
        AcGePoint2d(centerY - SLOT_HALF, SLOT_DEPTH));

    auto onArc = [](const ArcData& a, double ang) {
        return AcGePoint2d(a.center.x + a.radius * cos(ang),
                           a.center.y + a.radius * sin(ang));
    };
    AcGePoint2d L0 = onArc(left, left.startAng);
    AcGePoint2d L1 = onArc(left, left.endAng);
    AcGePoint2d R0 = onArc(right, right.startAng);
    AcGePoint2d R1 = onArc(right, right.endAng);

    // --------------------------------------------------
    // ПОЛИЛИНИЯ КОНТУРА (12 вершин, замкнутая) — обход против часовой
    // --------------------------------------------------
    AcDbPolyline* pPoly = new AcDbPolyline(12);

    pPoly->addVertexAt(0, AcGePoint2d(0.0, W));
    pPoly->addVertexAt(1, AcGePoint2d(centerY - SLOT_HALF, W));
    pPoly->addVertexAt(2, L0, tan((left.endAng - left.startAng) / 4.0));
    pPoly->addVertexAt(3, L1);
    pPoly->addVertexAt(4, AcGePoint2d(centerY + SLOT_HALF, W));
    pPoly->addVertexAt(5, AcGePoint2d(H, W));
    pPoly->addVertexAt(6, AcGePoint2d(H, 0.0));
    pPoly->addVertexAt(7, AcGePoint2d(centerY + SLOT_HALF, 0.0));
    pPoly->addVertexAt(8, R0, tan((right.endAng - right.startAng) / 4.0));
    pPoly->addVertexAt(9, R1);
    pPoly->addVertexAt(10, AcGePoint2d(centerY - SLOT_HALF, 0.0));
    pPoly->addVertexAt(11, AcGePoint2d(0.0, 0.0));

    if (!pPoly->isClosed()) pPoly->setClosed(true);

    TCHAR layerName[64];
    _stprintf_s(layerName, _T("%hs"), TrinityLayerManager::layerName(d.material).c_str());
    pPoly->setLayer(layerName);

    // Контур лежит в XY: X — высота (H), Y — длина (W), экструзия по Z на T.
    // Габарит солида: H x W x T — точно из нейрона.
    AcDbVoidPtrArray lines;
    pPoly->explode(lines);
    AcDbVoidPtrArray regions;
    Acad::ErrorStatus es = AcDbRegion::createFromCurves(lines, regions);
    for (int i = 0; i < lines.length(); i++) delete (AcRxObject*)lines[i];
    pPoly->erase();
    pPoly->close();

    if (es != Acad::eOk || regions.length() != 1) {
        for (int i = 0; i < regions.length(); i++) delete (AcRxObject*)regions[i];
        return nullptr;
    }

    AcDbRegion* pRegion = AcDbRegion::cast((AcRxObject*)regions[0]);
    if (!pRegion) {
        for (int i = 0; i < regions.length(); i++) delete (AcRxObject*)regions[i];
        return nullptr;
    }

    AcDb3dSolid* solid = new AcDb3dSolid();
    solid->extrude(pRegion, T, 0.0);
    delete (AcRxObject*)regions[0];

    solid->setLayer(layerName);
    return solid;
}

// ============================================
// ЭКСТРУЗИЯ ПРОФИЛЯ (фолбэк, если нужен)
// ============================================
AcDb3dSolid* TrinityGeometryBuilder::extrudeProfile(const AcGePoint3dArray& pts, double height) {
    AcDbPolyline* pPoly = new AcDbPolyline();
    for (int i = 0; i < pts.length(); i++) pPoly->addVertexAt(i, AcGePoint2d(pts[i].x, pts[i].y));
    pPoly->setClosed(true);

    AcDbVoidPtrArray lines, regions;
    pPoly->explode(lines);
    AcDbRegion::createFromCurves(lines, regions);
    AcDbRegion* pRegion = AcDbRegion::cast((AcRxObject*)regions[0]);
    pPoly->erase();
    pPoly->close();

    AcDb3dSolid* solid = new AcDb3dSolid();
    solid->extrude(pRegion, height, 0.0);

    for (int i = 0; i < lines.length(); i++) delete (AcRxObject*)lines[i];
    for (int i = 0; i < regions.length(); i++) delete (AcRxObject*)regions[i];

    return solid;
}

// ============================================
// БОЛТЫ
// ============================================
std::vector<AcGePoint3d> TrinityGeometryBuilder::getBoltPositions(const TrinityNeuron& d) {
    // Болты — по концам планки вдоль её оси (ось Y в профиле rib),
    // отступ 5 мм от кромок, центр по высоте и толщине.
    std::vector<AcGePoint3d> positions;
    const double inset = 5.0;
    const double cx = d.width / 2.0;
    const double cz = d.thickness / 2.0;
    const double y0 = std::min(inset, d.height * 0.25);
    const double y1 = std::max(d.height - inset, d.height * 0.75);
    positions.push_back(AcGePoint3d(cx, y0, cz));
    positions.push_back(AcGePoint3d(cx, y1, cz));
    return positions;
}

void TrinityGeometryBuilder::drawBoltMarkers(const TrinityNeuron& d,
                                               AcDbBlockTableRecord* pMs,
                                               AcDbObjectIdArray& ids) {
    double boltRadius = 3.0;
    auto positions = getBoltPositions(d);
    for (const auto& pos : positions) {
        AcDbCircle* pCircle = new AcDbCircle();
        pCircle->setCenter(pos);
        pCircle->setRadius(boltRadius);
        pCircle->setNormal(AcGeVector3d(1, 0, 0));  // плоскость YZ
        pCircle->setLayer(_T("_bolt"));

        AcDbObjectId circleId;
        pMs->appendAcDbEntity(circleId, pCircle);
        pCircle->close();
        ids.append(circleId);
    }
}

// ============================================
// ТРАНСФОРМАЦИЯ (compound rotation)
// ============================================
void TrinityGeometryBuilder::transform(AcDb3dSolid* solid,
                                        const TrinityPosition& pos,
                                        const TrinityRotationCompound& rot) {
    AcGeMatrix3d mat;
    mat.setToIdentity();

    for (int i = 0; i < rot.count; i++) {
        const auto& r = rot.rotations[i];
        if (r.angle != 0) {
            AcGeVector3d axis(r.x, r.y, r.z);
            if (!axis.isZeroLength()) {
                axis.normalize();
                AcGeMatrix3d rotMat;
                rotMat.setToRotation(r.angle * M_PI / 180.0, axis, AcGePoint3d::kOrigin);
                mat = mat * rotMat;
            }
        }
    }

    mat.setTranslation(AcGeVector3d(pos.x, pos.y, pos.z));
    solid->transformBy(mat);
}