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

    // Фикс AV: точный размер вместо wchar_t[256], остававшегося
    // неинициализированным при переполнении буфера.
    std::wstring codeW = utf8ToWide(code);
    if (codeW.empty()) return AcDbObjectId::kNull;

    AcDbAttributeDefinition* pAttdef = new AcDbAttributeDefinition();

    pAttdef->setPosition(AcGePoint3d::kOrigin);
    pAttdef->setTextString(codeW.c_str());
    pAttdef->setTag(_T("DETAIL_CODE"));
    pAttdef->setPrompt(_T("Detail Code"));
    pAttdef->setHeight(30);
    pAttdef->setRotation(0);
    pAttdef->setHorizontalMode(AcDb::kTextLeft);
    pAttdef->setVerticalMode(AcDb::kTextTop);
    pAttdef->setWidthFactor(0.75);
    pAttdef->setFieldLength(50);
    pAttdef->setInvisible(true);
    pAttdef->setConstant(true);
    pAttdef->setVerifiable(false);
    pAttdef->setPreset(false);

    pAttdef->setLayer(_T("_tag"));

    AcDbObjectId attrId;
    pRecord->appendAcDbEntity(attrId, pAttdef);
    pAttdef->close();

    return attrId;
}

// ============================================
// СЛОЙ _tag — делегирован в TrinityLayerManager
// (раньше здесь была третья копия цикла LayerTable has/add)
// ============================================
void TrinityAttributeBuilder::ensureTagLayer(AcDbDatabase* db) {
    TrinityLayerManager::ensureTagLayer(db);
}