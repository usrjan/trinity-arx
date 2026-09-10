// TrinityGeometryBuilder.cpp
#include "StdAfx.h"
#include "TrinityGeometryBuilder.h"
#include "TrinityLayerManager.h"

AcDb3dSolid* TrinityGeometryBuilder::build(const TrinityNeuron& detail) {
    switch (detail.processCode) {
        case 0: return buildBox(detail);
        case 2: return buildSidewall(detail);
        case 3: return buildRib(detail);
        default: return buildBox(detail);
    }
}

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

AcDb3dSolid* TrinityGeometryBuilder::buildSidewall(const TrinityNeuron& d) {
    AcDb3dSolid* solid = buildBox(d);
    if (!solid) return nullptr;
    double holeDiameter = 8.0, holeRadius = holeDiameter / 2.0;
    double height = d.thickness + 2.0;
    std::vector<AcGePoint3d> holePositions;
    size_t holesPos = d.jsonData.find("\"holes\"");
    if (holesPos != std::string::npos) {
        size_t arrStart = d.jsonData.find('[', holesPos);
        size_t arrEnd = d.jsonData.find(']', arrStart);
        if (arrStart != std::string::npos && arrEnd != std::string::npos) {
            std::string holesStr = d.jsonData.substr(arrStart, arrEnd - arrStart + 1);
            size_t objPos = 0;
            while ((objPos = holesStr.find("\"x\"", objPos)) != std::string::npos) {
                double x = 0, y = 0;
                size_t xVal = holesStr.find(':', objPos);
                if (xVal != std::string::npos) x = atof(holesStr.c_str() + xVal + 1);
                size_t yPos = holesStr.find("\"y\"", objPos);
                if (yPos != std::string::npos) {
                    size_t yVal = holesStr.find(':', yPos);
                    if (yVal != std::string::npos) y = atof(holesStr.c_str() + yVal + 1);
                }
                holePositions.push_back(AcGePoint3d(x, y, 0));
                objPos = yPos + 1;
            }
        }
    }
    if (holePositions.empty()) {
        double holeDistFromEdge = 50.0, holeSpacing = 100.0;
        for (double x = holeDistFromEdge; x < d.width - holeDistFromEdge; x += holeSpacing)
            for (double y = holeDistFromEdge; y < d.height - holeDistFromEdge; y += holeSpacing)
                holePositions.push_back(AcGePoint3d(x, y, 0));
    }
    for (const auto& pos : holePositions) {
        AcDb3dSolid* pCylinder = new AcDb3dSolid();
        pCylinder->createFrustum(height, holeRadius, holeRadius, holeRadius);
        AcGeMatrix3d mat;
        mat.setToIdentity();
        mat.setTranslation(AcGeVector3d(pos.x, pos.y, d.thickness / 2.0));
        pCylinder->transformBy(mat);
        Acad::ErrorStatus es = solid->booleanOper(AcDb::kBoolSubtract, pCylinder);
        if (es == Acad::eOk) pCylinder->erase();
        else delete pCylinder;
    }
    return solid;
}

AcDb3dSolid* TrinityGeometryBuilder::buildRib(const TrinityNeuron& d) {
    double W = d.width, H = 125.0, T = d.thickness, centerY = H / 2.0;
    const double SLOT_HALF = 4.0, SLOT_DEPTH = 23.54316771, HOLE_OFFSET = 53.0;
    AcGePoint3dArray pts;
    pts.setLogicalLength(12);
    pts[0].set(0.0, W, 0.0);
    pts[1].set(centerY - SLOT_HALF, W, 0.0);
    AcGePoint2d p1s(centerY - SLOT_HALF, W - SLOT_DEPTH), p1m(centerY, W - HOLE_OFFSET), p1e(centerY + SLOT_HALF, W - SLOT_DEPTH);
    AcGeCircArc2d arc1(p1s, p1m, p1e);
    pts[2].set(arc1.startPoint().x, arc1.startPoint().y, 0.0);
    pts[3].set(arc1.endPoint().x, arc1.endPoint().y, 0.0);
    pts[4].set(centerY + SLOT_HALF, W, 0.0);
    pts[5].set(H, W, 0.0);
    pts[6].set(H, 0.0, 0.0);
    pts[7].set(centerY + SLOT_HALF, 0.0, 0.0);
    AcGePoint2d p2s(centerY + SLOT_HALF, SLOT_DEPTH), p2m(centerY, HOLE_OFFSET), p2e(centerY - SLOT_HALF, SLOT_DEPTH);
    AcGeCircArc2d arc2(p2s, p2m, p2e);
    pts[8].set(arc2.startPoint().x, arc2.startPoint().y, 0.0);
    pts[9].set(arc2.endPoint().x, arc2.endPoint().y, 0.0);
    pts[10].set(centerY - SLOT_HALF, 0.0, 0.0);
    pts[11].set(0.0, 0.0, 0.0);
    return extrudeProfile(pts, T);
}

AcDb3dSolid* TrinityGeometryBuilder::extrudeProfile(const AcGePoint3dArray& pts, double height) {
    AcDbPolyline* pPoly = new AcDbPolyline();
    for (int i = 0; i < pts.length(); i++) pPoly->addVertexAt(i, AcGePoint2d(pts[i].x, pts[i].y));
    if (pts.length() >= 4) {
        AcGePoint2d a(pts[1].x, pts[1].y), b(pts[2].x, pts[2].y), c(pts[3].x, pts[3].y);
        AcGeCircArc2d arc(a, b, c);
        double startAng = AcGeVector2d(a.x - arc.center().x, a.y - arc.center().y).angle();
        double endAng = AcGeVector2d(c.x - arc.center().x, c.y - arc.center().y).angle();
        if (startAng > endAng) startAng -= 2.0 * M_PI;
        pPoly->setBulgeAt(2, tan((endAng - startAng) / 4.0));
    }
    pPoly->setClosed(true);
    AcGeMatrix3d matRot;
    matRot.setToRotation(-M_PI / 2.0, AcGeVector3d(1, 0, 0), AcGePoint3d(0, 0, 0));
    matRot.setTranslation(AcGeVector3d(0, 125.0, 0));
    pPoly->transformBy(matRot);
    AcDbVoidPtrArray lines, regions;
    pPoly->explode(lines);
    AcDbRegion::createFromCurves(lines, regions);
    AcDbRegion* pRegion = AcDbRegion::cast((AcRxObject*)regions[0]);
    pPoly->erase(); pPoly->close();
    AcDb3dSolid* solid = new AcDb3dSolid();
    solid->extrude(pRegion, height, 0.0);
    for (int i = 0; i < lines.length(); i++) delete (AcRxObject*)lines[i];
    for (int i = 0; i < regions.length(); i++) delete (AcRxObject*)regions[i];
    std::string layer = TrinityLayerManager::layerName("PLYWOOD-FSF");
    wchar_t layerW[256];
    MultiByteToWideChar(CP_UTF8, 0, layer.c_str(), -1, layerW, 256);
    solid->setLayer(layerW);
    return solid;
}

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

void TrinityGeometryBuilder::drawBoltMarkers(const TrinityNeuron& d, AcDbBlockTableRecord* pMs, AcDbObjectIdArray& ids) {
    double boltRadius = 3.0;
    auto positions = getBoltPositions(d);
    for (const auto& pos : positions) {
        AcDbCircle* pCircle = new AcDbCircle();
        pCircle->setCenter(pos);
        pCircle->setRadius(boltRadius);
        pCircle->setNormal(AcGeVector3d(1, 0, 0));
        pCircle->setLayer(_T("_bolt"));
        AcDbObjectId circleId;
        pMs->appendAcDbEntity(circleId, pCircle);
        pCircle->close();
        ids.append(circleId);
    }
}

void TrinityGeometryBuilder::transform(AcDb3dSolid* solid, const TrinityPosition& pos, const TrinityRotationCompound& rot) {
    AcGeMatrix3d mat; mat.setToIdentity();
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