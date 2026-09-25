// TrinityAttributeBuilder.cpp
#include "StdAfx.h"
#include "TrinityAttributeBuilder.h"
#include "TrinityLayerManager.h"

AcDbObjectId TrinityAttributeBuilder::addDetailCode(
        AcDbBlockTableRecord* pRecord,
        const std::string& code) {
    if (!pRecord || code.empty()) return AcDbObjectId::kNull;

    const std::wstring codeW = toWide(code);

    AcDbAttributeDefinition* pAttdef = new AcDbAttributeDefinition();
    pAttdef->setPosition(AcGePoint3d::kOrigin);
    pAttdef->setTextString(codeW.c_str());
    pAttdef->setTag(TAG_NAME);
    pAttdef->setPrompt(L"Detail Code");
    pAttdef->setHeight(30);
    pAttdef->setRotation(0);
    pAttdef->setHorizontalMode(AcDb::kTextLeft);
    pAttdef->setVerticalMode(AcDb::kTextTop);
    pAttdef->setWidthFactor(0.75);
    pAttdef->setFieldLength(50);
    pAttdef->setInvisible(Adesk::kTrue);   // скрытый
    pAttdef->setConstant(Adesk::kTrue);    // фиксированное значение
    pAttdef->setVerifiable(Adesk::kFalse);
    pAttdef->setPreset(Adesk::kFalse);
    pAttdef->setLayer(TrinityLayerManager::LAYER_TAG);

    AcDbObjectId attrId;
    if (pRecord->appendAcDbEntity(attrId, pAttdef) != Acad::eOk)
        return AcDbObjectId::kNull;
    pAttdef->close();

    return attrId;
}
