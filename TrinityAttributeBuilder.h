// TrinityAttributeBuilder.h
#pragma once
#include "StdAfx.h"
#include <string>

// ============================================
// Атрибут DETAIL_CODE: невидимый, постоянный,
// на слое _tag. Попадает в проект через XREF
// и извлекается командой DATAEXTRACTION.
// ============================================
class TrinityAttributeBuilder {
public:
    static constexpr const wchar_t* TAG_NAME = L"DETAIL_CODE";

    // Добавить атрибут с кодом детали в BlockTableRecord.
    // kNull при ошибке.
    static AcDbObjectId addDetailCode(AcDbBlockTableRecord* pRecord,
                                      const std::string& code);
};
