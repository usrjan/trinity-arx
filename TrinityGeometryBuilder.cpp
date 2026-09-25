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
// ПЛАНКА (со гнёздами)
// ============================================
AcDb3dSolid* TrinityGeometryBuilder::buildRib(const TrinityNeuron& d) {
    double W = d.width;
    double H = 125.0;
    double T = d.thickness;
    double centerY = H / 2.0;

    const double SLOT_HALF = 4.0;
    const double SLOT_DEPTH = 23.54316771;
    const double HOLE_OFFSET = 53.0;

    // --------------------------------------------------
    // ЛЕВОЕ ОТВЕРСТИЕ (X=0, дуга поперёк прорези)
    // --------------------------------------------------
    AcGePoint2d pt1Start(centerY - SLOT_HALF, W - SLOT_DEPTH);
    AcGePoint2d pt1OnArc(centerY, W - HOLE_OFFSET);
    AcGePoint2d pt1End(centerY + SLOT_HALF, W - SLOT_DEPTH);

    AcGeCircArc2d ge1Arc(pt1Start, pt1OnArc, pt1End);
    AcGePoint2d pt1Center = ge1Arc.center();
    double radius1 = ge1Arc.radius();

    AcGeVector2d vec1Start(pt1Start.x - pt1Center.x, pt1Start.y - pt1Center.y);
    AcGeVector2d vec1End(pt1End.x - pt1Center.x, pt1End.y - pt1Center.y);
    double start1Angle = vec1Start.angle();
    double end1Angle = vec1End.angle();

    AcGePoint2d pt11, pt12;
    pt11.x = pt1Center.x + radius1 * cos(start1Angle);
    pt11.y = pt1Center.y + radius1 * sin(start1Angle);
    pt12.x = pt1Center.x + radius1 * cos(end1Angle);
    pt12.y = pt1Center.y + radius1 * sin(end1Angle);

    double new1Start = (start1Angle > end1Angle)
        ? (start1Angle - 2.0 * M_PI) : start1Angle;

    // --------------------------------------------------
    // ПРАВОЕ ОТВЕРСТИЕ (X=W)
    // --------------------------------------------------
    AcGePoint2d pt2Start(centerY + SLOT_HALF, SLOT_DEPTH);
    AcGePoint2d pt2OnArc(centerY, HOLE_OFFSET);
    AcGePoint2d pt2End(centerY - SLOT_HALF, SLOT_DEPTH);

    AcGeCircArc2d ge2Arc(pt2Start, pt2OnArc, pt2End);
    AcGePoint2d pt2Center = ge2Arc.center();
    double radius2 = ge2Arc.radius();

    AcGeVector2d vec2Start(pt2Start.x - pt2Center.x, pt2Start.y - pt2Center.y);
    AcGeVector2d vec2End(pt2End.x - pt2Center.x, pt2End.y - pt2Center.y);
    double start2Angle = vec2Start.angle();
    double end2Angle = vec2End.angle();

    AcGePoint2d pt21, pt22;
    pt21.x = pt2Center.x + radius2 * cos(start2Angle);
    pt21.y = pt2Center.y + radius2 * sin(start2Angle);
    pt22.x = pt2Center.x + radius2 * cos(end2Angle);
    pt22.y = pt2Center.y + radius2 * sin(end2Angle);

    double new2Start = (start2Angle > end2Angle)
        ? (start2Angle - 2.0 * M_PI) : start2Angle;

    // --------------------------------------------------
    // ПОЛИЛИНИЯ КОНТУРА (12 точек)
    // --------------------------------------------------
    AcDbPolyline* pPoly = new AcDbPolyline(12);

    pPoly->addVertexAt(0, AcGePoint2d(0.0, W), 0, 0, 0);
    pPoly->addVertexAt(1, AcGePoint2d(centerY - 4.0, W), 0, 0, 0);
    pPoly->addVertexAt(2, pt11, tan((end1Angle - new1Start) / 4.0), 0, 0);
    pPoly->addVertexAt(3, pt12, 0, 0, 0);
    pPoly->addVertexAt(4, AcGePoint2d(centerY + 4.0, W), 0, 0, 0);
    pPoly->addVertexAt(5, AcGePoint2d(H, W), 0, 0, 0);
    pPoly->addVertexAt(6, AcGePoint2d(H, 0.0), 0, 0, 0);
    pPoly->addVertexAt(7, AcGePoint2d(centerY + 4.0, 0.0), 0, 0, 0);
    pPoly->addVertexAt(8, pt21, tan((end2Angle - new2Start) / 4.0), 0, 0);
    pPoly->addVertexAt(9, pt22, 0, 0, 0);
    pPoly->addVertexAt(10, AcGePoint2d(centerY - 4.0, 0.0), 0, 0, 0);
    pPoly->addVertexAt(11, AcGePoint2d(0.0, 0.0), 0, 0, 0);

    if (!pPoly->isClosed()) pPoly->setClosed(true);

    // --------------------------------------------------
    // EXTRUDE (поворот контура на -90° вокруг X)
    // --------------------------------------------------
    TCHAR layerName[64];
    _stprintf_s(layerName, _T("%hs"), TrinityLayerManager::layerName(d.material).c_str());
    pPoly->setLayer(layerName);

    AcGePoint3d p1(0.0, 0.0, 0.0);
    AcGeVector3d v1(0.0, 0.0, 1.0);
    AcGeMatrix3d mat;
    mat.setToRotation(-(90.0 * (M_PI / 180.0)), v1, p1);
    mat.setTranslation(AcGeVector3d(0, H, 0));
    pPoly->transformBy(mat);

    AcDbVoidPtrArray lines;
    pPoly->explode(lines);
    AcDbVoidPtrArray regions;
    AcDbRegion::createFromCurves(lines, regions);

    // ВАЖНО (фикс Access Violation): раньше здесь были assert() — в Release-сборке
    // они выключены, и при пустом массиве regions[0] читался мусорный указатель,
    // а AcDbRegion::cast по нему = AV. Проверяем явно и выходим безопасно.
    if (regions.length() != 1) {
        acutPrintf(_T("\n[GeometryBuilder] createFromCurves failed: regions=%d\n"),
                   (int)regions.length());
        for (int i = 0; i < lines.length(); i++) delete (AcRxObject*)lines[i];
        for (int i = 0; i < regions.length(); i++) delete (AcRxObject*)regions[i];
        delete pPoly;   // полилиния НЕ состоит в базе — удаляем напрямую
        return nullptr;
    }
    AcDbRegion* pRegion = AcDbRegion::cast((AcRxObject*)regions[0]);
    if (pRegion == NULL) {
        acutPrintf(_T("\n[GeometryBuilder] region cast failed\n"));
        for (int i = 0; i < lines.length(); i++) delete (AcRxObject*)lines[i];
        for (int i = 0; i < regions.length(); i++) delete (AcRxObject*)regions[i];
        delete pPoly;
        return nullptr;
    }
    // ВАЖНО: erase() перед close() бессмысленен для объекта вне базы (нет ownerId)
    // и оставляет его «висящим» в памяти. Полилиния не добавлялась ни в одну
    // базу — освобождаем её корректным delete().
    delete pPoly;

    AcDb3dSolid* solid = new AcDb3dSolid();
    Acad::ErrorStatus esExt = solid->extrude(pRegion, T, 0.0);

    for (int i = 0; i < lines.length(); i++) delete (AcRxObject*)lines[i];
    for (int i = 0; i < regions.length(); i++) delete (AcRxObject*)regions[i];

    if (esExt != Acad::eOk) {
        acutPrintf(_T("\n[GeometryBuilder] extrude failed: %d\n"), (int)esExt);
        delete solid;
        return nullptr;
    }

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

    // Фикс Access Violation: явная проверка вместо assert (в Release assert выключен,
    // regions[0] при пустом массиве = чтение мусора -> AV в cast/extrude).
    if (regions.length() != 1) {
        for (int i = 0; i < lines.length(); i++) delete (AcRxObject*)lines[i];
        for (int i = 0; i < regions.length(); i++) delete (AcRxObject*)regions[i];
        delete pPoly;   // объект вне базы — освобожаем напрямую
        return nullptr;
    }
    AcDbRegion* pRegion = AcDbRegion::cast((AcRxObject*)regions[0]);
    if (pRegion == NULL) {
        for (int i = 0; i < lines.length(); i++) delete (AcRxObject*)lines[i];
        for (int i = 0; i < regions.length(); i++) delete (AcRxObject*)regions[i];
        delete pPoly;
        return nullptr;
    }
    delete pPoly;       // вместо некорректного erase()+close() для объекта вне базы

    AcDb3dSolid* solid = new AcDb3dSolid();
    Acad::ErrorStatus esExt = solid->extrude(pRegion, height, 0.0);

    for (int i = 0; i < lines.length(); i++) delete (AcRxObject*)lines[i];
    for (int i = 0; i < regions.length(); i++) delete (AcRxObject*)regions[i];

    if (esExt != Acad::eOk) {
        delete solid;
        return nullptr;
    }

    return solid;
}

// ============================================
// БОЛТЫ
// ============================================
std::vector<AcGePoint3d> TrinityGeometryBuilder::getBoltPositions(const TrinityNeuron& d) {
    std::vector<AcGePoint3d> positions;
    double bolt_y = d.height / 2.0;
    double bolt_x_left = -5.0;
    double bolt_x_right = d.width + 5.0;
    double z_center = d.thickness / 2.0;
    positions.push_back(AcGePoint3d(bolt_x_left, bolt_y, z_center));
    positions.push_back(AcGePoint3d(bolt_x_right, bolt_y, z_center));
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
        pCircle->setLayer(TrinityLayerManager::LAYER_BOLT_W);

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