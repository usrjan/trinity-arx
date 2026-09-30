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
// СОЗДАТЬ/ПОЛУЧИТЬ СЛОЙ ПО ИМЕНИ (общий хелпер)
// Единственная реализация цикла «открыть LayerTable -> has -> add/getAt»,
// из которой теперь строятся все остальные ensure*-функции.
// ============================================
AcDbObjectId TrinityLayerManager::createOrGetLayerByName(AcDbDatabase* db, const wchar_t* wName, int colorIndex, bool off) {
    if (!db || !wName || !*wName) return AcDbObjectId::kNull;

    AcDbLayerTable* pLayerTable = nullptr;
    if (db->getSymbolTable(pLayerTable, AcDb::kForWrite) != Acad::eOk) return AcDbObjectId::kNull;

    AcDbObjectId layerId = AcDbObjectId::kNull;
    if (!pLayerTable->has(wName)) {
        AcDbLayerTableRecord* pRecord = new AcDbLayerTableRecord();
        pRecord->setName(wName);

        AcCmColor color;
        color.setColorIndex(colorIndex);
        pRecord->setColor(color);
        if (off) pRecord->setIsOff(true);

        pLayerTable->add(layerId, pRecord);
        pRecord->close();
    } else {
        pLayerTable->getAt(wName, layerId);
    }

    pLayerTable->close();
    return layerId;
}

// ============================================
// СОЗДАТЬ ИЛИ ПОЛУЧИТЬ СЛОЙ МАТЕРИАЛА
// ============================================
AcDbObjectId TrinityLayerManager::createOrGetLayer(AcDbDatabase* db, const std::string& materialCode) {
    std::string name = layerName(materialCode);

    // Фикс AV: точный размер вместо буфера wchar_t[256], который при
    // переполнении оставался неинициализированным.
    std::wstring layerNameW = utf8ToWide(name);
    if (layerNameW.empty()) return AcDbObjectId::kNull;

    return createOrGetLayerByName(db, layerNameW.c_str(), colorIndex(materialCode), /*off=*/false);
}

// ============================================
// ТЕХНИЧЕСКИЕ СЛОИ (_tag, _bolt): красный, выключен
// Раньше это были две посимвольно одинаковые копии; ensureTagLayer в
// TrinityAttributeBuilder — третья. Все три ведут в этот хелпер.
// ============================================
static void ensureTechLayer(AcDbDatabase* db, const char* nameUtf8) {
    std::wstring w = utf8ToWide(nameUtf8);
    if (w.empty()) return;
    TrinityLayerManager::createOrGetLayerByName(db, w.c_str(), 1, /*off=*/true);
}

void TrinityLayerManager::ensureTagLayer(AcDbDatabase* db) {
    ensureTechLayer(db, LAYER_TAG);
}

void TrinityLayerManager::ensureBoltLayer(AcDbDatabase* db) {
    ensureTechLayer(db, LAYER_BOLT);
}
