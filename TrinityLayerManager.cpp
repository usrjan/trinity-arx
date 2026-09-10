// TrinityLayerManager.cpp
#include "StdAfx.h"
#include "TrinityLayerManager.h"

std::string TrinityLayerManager::layerName(const std::string& materialCode) {
    return "TRINITY_MAT_" + materialCode;
}

int TrinityLayerManager::colorIndex(const std::string& materialCode) {
    if (materialCode.find("PLYWOOD") != std::string::npos) return 31;
    if (materialCode.find("STEEL") != std::string::npos) return 8;
    if (materialCode.find("CONCRETE") != std::string::npos) return 254;
    if (materialCode.find("ALUMINIUM") != std::string::npos) return 9;
    return 7;
}

AcDbObjectId TrinityLayerManager::createOrGetLayer(AcDbDatabase* db, const std::string& materialCode) {
    std::string name = layerName(materialCode);
    wchar_t layerNameW[256];
    MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, layerNameW, 256);
    AcDbLayerTable* pLayerTable = nullptr;
    if (db->getSymbolTable(pLayerTable, AcDb::kForWrite) != Acad::eOk) return AcDbObjectId::kNull;
    AcDbObjectId layerId;
    if (!pLayerTable->has(layerNameW)) {
        AcDbLayerTableRecord* pRecord = new AcDbLayerTableRecord();
        pRecord->setName(layerNameW);
        AcCmColor color;
        color.setColorIndex(colorIndex(materialCode));
        pRecord->setColor(color);
        pLayerTable->add(layerId, pRecord);
        pRecord->close();
    } else {
        pLayerTable->getAt(layerNameW, layerId);
    }
    pLayerTable->close();
    return layerId;
}

void TrinityLayerManager::ensureTagLayer(AcDbDatabase* db) {
    AcDbLayerTable* pLayerTable = nullptr;
    if (db->getSymbolTable(pLayerTable, AcDb::kForWrite) != Acad::eOk) return;
    if (!pLayerTable->has(_T("_tag"))) {
        AcDbLayerTableRecord* pRecord = new AcDbLayerTableRecord();
        pRecord->setName(_T("_tag"));
        AcCmColor color; color.setColorIndex(1);
        pRecord->setColor(color);
        pRecord->setIsOff(true);
        AcDbObjectId layerId = AcDbObjectId::kNull;
        pLayerTable->add(layerId, pRecord);
        pRecord->close();
    }
    pLayerTable->close();
}

void TrinityLayerManager::ensureBoltLayer(AcDbDatabase* db) {
    AcDbLayerTable* pLayerTable = nullptr;
    if (db->getSymbolTable(pLayerTable, AcDb::kForWrite) != Acad::eOk) return;
    if (!pLayerTable->has(_T("_bolt"))) {
        AcDbLayerTableRecord* pRecord = new AcDbLayerTableRecord();
        pRecord->setName(_T("_bolt"));
        AcCmColor color; color.setColorIndex(1);
        pRecord->setColor(color);
        pRecord->setIsOff(true);
        AcDbObjectId layerId = AcDbObjectId::kNull;
        pLayerTable->add(layerId, pRecord);
        pRecord->close();
    }
    pLayerTable->close();
}