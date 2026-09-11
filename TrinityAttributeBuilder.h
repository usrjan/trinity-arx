// TrinityAttributeBuilder.h
#pragma once
#include "StdAfx.h"
#include <string>

class TrinityAttributeBuilder {
public:
    // Добавить атрибут с кодом детали (возвращает ObjectId)
    static AcDbObjectId addDetailCode(
        AcDbBlockTableRecord* pRecord,
        const std::string& code
    );

    // Создать слой _tag (если не существует)
    static void ensureTagLayer(AcDbDatabase* db);
};