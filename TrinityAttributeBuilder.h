// TrinityAttributeBuilder.h
#pragma once
#include "stdafx.h"
#include <string>

class TrinityAttributeBuilder {
public:
    // Добавить один скрытый атрибут с кодом детали
    static AcDbObjectId addDetailCode(
        AcDbBlockTableRecord* pRecord,
        const std::string& code
    );
    
    // Создать слой _tag (красный, выключен)
    static void ensureTagLayer(AcDbDatabase* db);
};