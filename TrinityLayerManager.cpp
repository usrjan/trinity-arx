// TrinityLayerManager.cpp
#include "StdAfx.h"
#include "TrinityLayerManager.h"

// ============================================
// Имя слоя по материалу
// ============================================
std::string TrinityLayerManager::layerName(const std::string& materialCode) {
    return "TRINITY_MAT_" + materialCode;
}

// ============================================
// Цвет ACI по материалу
// ============================================
int TrinityLayerManager::colorIndex(const std::string& materialCode) {
    if (materialCode.find("PLYWOOD")  != std::string::npos) return 31;   // коричневый
    if (materialCode.find("STEEL")    != std::string::npos) return 8;    // серый
    if (materialCode.find("CONCRETE") != std::string::npos) return 254;  // серый
    if (materialCode.find("ALUMINIUM")!= std::string::npos) return 9;    // серебристый
    return 7; // белый по умолчанию
}

// ============================================
// Создать или получить слой материала
// ============================================
AcDbObjectId TrinityLayerManager::createOrGetLayer(AcDbDatabase* db,
                                                   const std::string& materialCode) {
    if (!db) return AcDbObjectId::kNull;

    const std::wstring name = toWide(layerName(materialCode));

    AcDbLayerTable* pLayerTable = nullptr;
    if (db->getSymbolTable(pLayerTable, AcDb::kForWrite) != Acad::eOk)
        return AcDbObjectId::kNull;

    AcDbObjectId layerId;
    if (pLayerTable->has(name.c_str())) {
        pLayerTable->getAt(name.c_str(), layerId);
    } else {
        AcDbLayerTableRecord* pRecord = new AcDbLayerTableRecord();
        pRecord->setName(name.c_str());
        AcCmColor color;
        color.setColorIndex(static_cast<Adesk::UInt16>(colorIndex(materialCode)));
        pRecord->setColor(color);
        pLayerTable->add(layerId, pRecord);
        pRecord->close();
    }

    pLayerTable->close();
    return layerId;
}

// ============================================
// Общий код для скрытых красных слоёв
// ============================================
void TrinityLayerManager::ensureHiddenRedLayer(AcDbDatabase* db, const wchar_t* name) {
    if (!db) return;

    AcDbLayerTable* pLayerTable = nullptr;
    if (db->getSymbolTable(pLayerTable, AcDb::kForWrite) != Acad::eOk) return;

    if (!pLayerTable->has(name)) {
        AcDbLayerTableRecord* pRecord = new AcDbLayerTableRecord();
        pRecord->setName(name);
        AcCmColor color;
        color.setColorIndex(1);          // красный
        pRecord->setColor(color);
        pRecord->setIsOff(true);         // выключен
        AcDbObjectId layerId;
        pLayerTable->add(layerId, pRecord);
        pRecord->close();
    }

    pLayerTable->close();
}

void TrinityLayerManager::ensureTagLayer(AcDbDatabase* db) {
    ensureHiddenRedLayer(db, LAYER_TAG);
}

void TrinityLayerManager::ensureBoltLayer(AcDbDatabase* db) {
    ensureHiddenRedLayer(db, LAYER_BOLT);
}
