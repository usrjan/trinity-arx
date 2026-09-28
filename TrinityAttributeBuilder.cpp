// TrinityAttributeBuilder.cpp
#include "StdAfx.h"
#include "TrinityAttributeBuilder.h"

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
    pAttdef->setInvisible(Adesk::kTrue);
    pAttdef->setConstant(Adesk::kTrue);
    pAttdef->setVerifiable(Adesk::kFalse);
    pAttdef->setPreset(Adesk::kFalse);

    pAttdef->setLayer(_T("_tag"));

    AcDbObjectId attrId;
    pRecord->appendAcDbEntity(attrId, pAttdef);
    pAttdef->close();

    return attrId;
}

// ============================================
// СЛОЙ _tag
// ============================================
void TrinityAttributeBuilder::ensureTagLayer(AcDbDatabase* db) {
    if (!db) return;

    AcDbLayerTable* pLayerTable = nullptr;
    Acad::ErrorStatus es = db->getSymbolTable(pLayerTable, AcDb::kForWrite);
    if (es != Acad::eOk) return;

    if (!pLayerTable->has(_T("_tag"))) {
        AcDbLayerTableRecord* pRecord = new AcDbLayerTableRecord();
        pRecord->setName(_T("_tag"));

        AcCmColor color;
        color.setColorIndex(1);
        pRecord->setColor(color);
        pRecord->setIsOff(true);

        // НЕКОНСТАНТНАЯ переменная для add()
        AcDbObjectId layerId = AcDbObjectId::kNull;
        pLayerTable->add(layerId, pRecord);
        pRecord->close();
    }

    pLayerTable->close();
}