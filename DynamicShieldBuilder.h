#pragma once

#include <dbblockreference.h>
#include <dbdict.h>
#include <dbxrecard.h>
#include <geassign.h>
#include <acdbcurv.h>
#include <dblayer.h>
#include <aced.h>
#include <rxobject.h>
#include <dbdynblk.h>
#include <DbBlockTableRecord.h>
#include <DbDynBlockReferenceProperty.h>
#include <DbDynBlockReference.h>

/// <summary>
/// Класс для создания динамических щитов опалубки.
/// Позволяет управлять размерами через палитру свойств AutoCAD.
/// </summary>
class DynamicShieldBuilder
{
public:
    DynamicShieldBuilder();
    ~DynamicShieldBuilder();

    /// <summary>
    /// Создает динамический блок щита.
    /// </summary>
    /// <param name="length">Начальная длина щита (мм)</param>
    /// <param name="width">Ширина щита (мм)</param>
    /// <param name="thickness">Толщина щита (мм)</param>
    /// <param name="hasHoles">Наличие сквозных отверстий</param>
    /// <returns>Указатель на запись блока в таблице блоков</returns>
    AcDbBlockTableRecord* createDynamicShield(
        double length, 
        double width, 
        double thickness, 
        bool hasHoles = false);

    /// <summary>
    /// Вставляет динамический блок в модель.
    /// </summary>
    /// <param name="pBlockRec">Запись динамического блока</param>
    /// <param name="insertionPoint">Точка вставки</param>
    /// <param name="rotation">Угол поворота</param>
    /// <returns>Указатель на ссылку на блок (BlockReference)</returns>
    AcDbBlockReference* insertDynamicShield(
        AcDbBlockTableRecord* pBlockRec,
        const AcGePoint3d& insertionPoint,
        double rotation = 0.0);

private:
    /// <summary>
    /// Создает геометрию щита (контуры).
    /// </summary>
    void addShieldGeometry(AcDbBlockTableRecord* pBlockRec, double length, double width, double thickness, bool hasHoles);

    /// <summary>
    /// Добавляет линейный параметр длины.
    /// </summary>
    void addLengthParameter(AcDbBlockTableRecord* pBlockRec, double length);

    /// <summary>
    /// Добавляет действие растягивания (Stretch) для изменения длины.
    /// </summary>
    void addStretchAction(AcDbBlockTableRecord* pBlockRec, const AcDbObjectId& paramId, double length);
};
