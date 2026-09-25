// TrinityAttributeBuilder.cpp
#include "StdAfx.h"
#include "TrinityAttributeBuilder.h"
#include "TrinityLayerManager.h"

// ============================================
// ДОБАВИТЬ АТРИБУТ С КОДОМ ДЕТАЛИ
// ============================================
AcDbObjectId TrinityAttributeBuilder::addDetailCode(
    AcDbBlockTableRecord* pRecord,
    const std::string& code) {

    if (!pRecord || code.empty()) return AcDbObjectId::kNull;

    wchar_t codeW[256];
    MultiByteToWideChar(CP_UTF8, 0, code.c_str(), -1, codeW, 256);

    AcDbAttributeDefinition* pAttdef = new AcDbAttributeDefinition();

    pAttdef->setPosition(AcGePoint3d::kOrigin);
    pAttdef->setTextString(codeW);
    pAttdef->setTag(_T("DETAIL_CODE"));
    pAttdef->setPrompt(_T("Detail Code"));
    pAttdef->setHeight(30);
    pAttdef->setRotation(0);
    pAttdef->setHorizontalMode(AcDb::kTextLeft);
    pAttdef->setVerticalMode(AcDb::kTextTop);
    pAttdef->setWidthFactor(0.75);
    pAttdef->setFieldLength(50);
    pAttdef->setInvisible(Adesk::kTrue);
    pAttdef->setConstant(Adesk::kTrue);
    pAttdef->setVerifiable(Adesk::kFalse);
    pAttdef->setPreset(Adesk::kFalse);

    pAttdef->setLayer(TrinityLayerManager::LAYER_TAG_W);

    AcDbObjectId attrId;
    pRecord->appendAcDbEntity(attrId, pAttdef);
    pAttdef->close();

    return attrId;
}

// ============================================
// СЛОЙ _tag — дубликат удалён: общая логика в TrinityLayerManager
// (тонкая обёртка для сохранения публичного API этого класса)
// ============================================
void TrinityAttributeBuilder::ensureTagLayer(AcDbDatabase* db) {
    TrinityLayerManager::ensureTagLayer(db);
}
