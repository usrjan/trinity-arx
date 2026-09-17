#include "StdAfx.h"
#include "DynamicShieldBuilder.h"
#include "TrinityLayerManager.h"
#include <dbpolylne.h>
#include <dbregion.h>
#include <dbcircle.h>
#include <dbcurve.h>
#include <dbobjptr.h>
#include <aced.h>

DynamicShieldBuilder::DynamicShieldBuilder() {}

DynamicShieldBuilder::~DynamicShieldBuilder() {}

AcDbBlockTableRecord* DynamicShieldBuilder::createDynamicShield(
    double length, 
    double width, 
    double thickness, 
    bool hasHoles)
{
    AcDbBlockTableRecord* pBlockRec = new AcDbBlockTableRecord();
    
    // Устанавливаем имя блока
    CString blockName;
    blockName.Format(_T("TRINITY_SHIELD_L%.0f_W%.0f_T%.0f"), length, width, thickness);
    if (hasHoles)
        blockName += _T("_HOLES");
    
    pBlockRec->setName(blockName);
    
    // Добавляем геометрию щита
    addShieldGeometry(pBlockRec, length, width, thickness, hasHoles);
    
    // Добавляем параметр длины
    addLengthParameter(pBlockRec, length);
    
    // Примечание: Действие растягивания требует более сложной реализации
    // с использованием AcDbConstraintGroup и AcDbGeomConstraint3d
    // В текущей версии ObjectARX это делается через ACAD_DYNAMICBLOCK实体
    
    return pBlockRec;
}

void DynamicShieldBuilder::addShieldGeometry(
    AcDbBlockTableRecord* pBlockRec, 
    double length, 
    double width, 
    double thickness, 
    bool hasHoles)
{
    // Создаем внешний контур щита (полилиния)
    AcDbPolyline* pOuter = new AcDbPolyline(4);
    pOuter->setClosed(Adesk::kTrue);
    
    // Координаты вершин прямоугольника (центр в 0,0)
    double halfL = length / 2.0;
    double halfW = width / 2.0;
    
    pOuter->addVertexAt(0, AcGePoint2d(-halfL, -halfW));
    pOuter->addVertexAt(1, AcGePoint2d(halfL, -halfW));
    pOuter->addVertexAt(2, AcGePoint2d(halfL, halfW));
    pOuter->addVertexAt(3, AcGePoint2d(-halfL, halfW));
    
    // Назначаем слой
    TrinityLayerManager layerMgr;
    layerMgr.ensureLayerExists(pBlockRec, _T("TRINITY_SHIELDS"));
    pOuter->setLayer(_T("TRINITY_SHIELDS"));
    
    pBlockRec->appendAcDbEntity(pOuter);
    pOuter->close();
    
    // Если нужны отверстия - добавляем круги
    if (hasHoles)
    {
        // Типовые отверстия для стяжек (диаметр 20мм)
        double holeRadius = 10.0;
        double offset = 50.0; // Отступ от края
        
        // Левое отверстие
        AcDbCircle* pHole1 = new AcDbCircle(
            AcGePoint3d(-halfL + offset, 0.0, 0.0),
            AcGeVector3d::kZAxis,
            holeRadius
        );
        pHole1->setLayer(_T("TRINITY_HOLES"));
        pBlockRec->appendAcDbEntity(pHole1);
        pHole1->close();
        
        // Правое отверстие
        AcDbCircle* pHole2 = new AcDbCircle(
            AcGePoint3d(halfL - offset, 0.0, 0.0),
            AcGeVector3d::kZAxis,
            holeRadius
        );
        pHole2->setLayer(_T("TRINITY_HOLES"));
        pBlockRec->appendAcDbEntity(pHole2);
        pHole2->close();
    }
}

void DynamicShieldBuilder::addLengthParameter(
    AcDbBlockTableRecord* pBlockRec, 
    double length)
{
    // В ObjectARX 2026 динамические параметры создаются через
    // AcDbDynBlockReferenceProperty и связанные с ними действия
    
    // Это упрощенная реализация - полная требует работы с
    // AcDbBlockTableRecord::getAnonymousBlockId() и записью
    // специальных XRecords для параметров динамики
    
    // Для полноценной реализации нужно:
    // 1. Создать AcDbDynBlockReferenceProperty с типом "Distance"
    // 2. Связать его с точками растягивания
    // 3. Добавить AcDbStretchAction для изменения геометрии
    
    // В данной версии создаем базовый блок, а динамические свойства
    // можно добавить вручную через BEDIT в AutoCAD или расширить код
}

void DynamicShieldBuilder::addStretchAction(
    AcDbBlockTableRecord* pBlockRec, 
    const AcDbObjectId& paramId, 
    double length)
{
    // Реализация действия растягивания
    // Требует создания AcDbStretchAction и связи с параметром
    // Это сложная операция, зависящая от версии ObjectARX
}

AcDbBlockReference* DynamicShieldBuilder::insertDynamicShield(
    AcDbBlockTableRecord* pBlockRec,
    const AcGePoint3d& insertionPoint,
    double rotation)
{
    // Получаем таблицу блоков текущего документа
    AcDbDatabase* pDb = acdbCurDwg();
    AcDbBlockTable* pBlockTable = nullptr;
    pDb->getBlockTable(pBlockTable, AcDb::kForRead);
    
    // Проверяем, есть ли уже такой блок в таблице
    AcDbObjectId blockId;
    if (pBlockRec->name().length() > 0)
    {
        Acad::ErrorStatus es = pBlockTable->getAt(pBlockRec->name(), blockId);
        if (es == Acad::eKeyNotFound)
        {
            // Блока нет - добавляем новый
            pBlockTable->close();
            pDb->getBlockTable(pBlockTable, AcDb::kForWrite);
            pBlockTable->add(pBlockRec);
            blockId = pBlockRec->objectId();
        }
        else
        {
            // Блок уже существует - удаляем созданную запись
            delete pBlockRec;
        }
    }
    else
    {
        // Безымянный блок - добавляем как есть
        pBlockTable->close();
        pDb->getBlockTable(pBlockTable, AcDb::kForWrite);
        pBlockTable->add(pBlockRec);
        blockId = pBlockRec->objectId();
    }
    
    pBlockTable->close();
    
    // Создаем ссылку на блок
    AcDbBlockReference* pBlockRef = new AcDbBlockReference(insertionPoint, blockId);
    pBlockRef->setRotation(rotation);
    
    return pBlockRef;
}
