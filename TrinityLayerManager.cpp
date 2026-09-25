// TrinityLayerManager.cpp
#include "StdAfx.h"
#include "TrinityLayerManager.h"

// ============================================
// ИМЯ СЛОЯ ПО МАТЕРИАЛУ
// ============================================
std::string TrinityLayerManager::layerName(const std::string& materialCode) {
    return "TRINITY_MAT_" + materialCode;
}

// ============================================
// ЦВЕТ ПО МАТЕРИАЛУ
// ============================================
int TrinityLayerManager::colorIndex(const std::string& materialCode) {
    if (materialCode.find("PLYWOOD") != std::string::npos) return 31;   // коричневый
    if (materialCode.find("STEEL") != std::string::npos) return 8;      // серый
    if (materialCode.find("CONCRETE") != std::string::npos) return 254; // серый
    if (materialCode.find("ALUMINIUM") != std::string::npos) return 9;  // серебристый
    return 7; // белый по умолчанию
}

// ============================================
// СОЗДАТЬ ИЛИ ПОЛУЧИТЬ СЛОЙ МАТЕРИАЛА
// ============================================
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

// ============================================
// ТЕХНИЧЕСКИЕ СЛОИ — общая логика (ensureTechLayer)
// Красный (цвет 1), выключен по умолчанию.
// ============================================
void TrinityLayerManager::ensureTechLayer(AcDbDatabase* db, const wchar_t* layerName) {
    AcDbLayerTable* pLayerTable = nullptr;
    if (db->getSymbolTable(pLayerTable, AcDb::kForWrite) != Acad::eOk) return;

    if (!pLayerTable->has(layerName)) {
        AcDbLayerTableRecord* pRecord = new AcDbLayerTableRecord();
        pRecord->setName(layerName);

        AcCmColor color;
        color.setColorIndex(1);   // красный
        pRecord->setColor(color);
        pRecord->setIsOff(true);  // выключен по умолчанию

        AcDbObjectId layerId = AcDbObjectId::kNull;
        pLayerTable->add(layerId, pRecord);
        pRecord->close();
    }

    pLayerTable->close();
}

// Слой _tag (атрибуты DETAIL_CODE)
void TrinityLayerManager::ensureTagLayer(AcDbDatabase* db)  { ensureTechLayer(db, LAYER_TAG_W);  }

// Слой _bolt (маркеры болтов)
void TrinityLayerManager::ensureBoltLayer(AcDbDatabase* db) { ensureTechLayer(db, LAYER_BOLT_W); }