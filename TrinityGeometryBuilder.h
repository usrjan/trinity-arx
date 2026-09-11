// TrinityGeometryBuilder.h
#pragma once
#include "StdAfx.h"
#include "TrinityCore.h"

class TrinityGeometryBuilder {
public:
    static AcDb3dSolid* build(const TrinityNeuron& detail);
    static std::vector<AcGePoint3d> getBoltPositions(const TrinityNeuron& d);
    static void drawBoltMarkers(const TrinityNeuron& d,
                                 AcDbBlockTableRecord* pMs,
                                 AcDbObjectIdArray& ids);
    static void transform(AcDb3dSolid* solid,
                           const TrinityPosition& pos,
                           const TrinityRotationCompound& rot);

private:
    static AcDb3dSolid* buildBox(const TrinityNeuron& d);
    static AcDb3dSolid* buildRib(const TrinityNeuron& d);
    static AcDb3dSolid* buildSidewall(const TrinityNeuron& d);
    static AcDb3dSolid* extrudeProfile(const AcGePoint3dArray& pts, double height);
};