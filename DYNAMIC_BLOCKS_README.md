# Динамические щиты опалубки

## Обзор

Реализована базовая поддержка динамических блоков для щитов опалубки. Это позволяет управлять размерами щитов через палитру **Свойства** в AutoCAD без пересоздания геометрии.

## Что реализовано

### Класс `DynamicShieldBuilder`

**Файлы:**
- `DynamicShieldBuilder.h` - заголовочный файл
- `DynamicShieldBuilder.cpp` - реализация

**Возможности:**
1. ✅ Создание параметрического щита с заданными размерами (длина, ширина, толщина)
2. ✅ Опциональные сквозные отверстия для стяжек
3. ✅ Автоматическое назначение слоев (TRINITY_SHIELDS, TRINITY_HOLES)
4. ✅ Вставка блока в модель с указанием точки и угла поворота
5. ✅ Кеширование блоков в таблице блоков AutoCAD

**Параметры щита:**
| Параметр | Описание | Пример |
|----------|----------|--------|
| `length` | Длина щита (мм) | 1200, 1500, 3000 |
| `width` | Ширина щита (мм) | 500, 600 |
| `thickness` | Толщина щита (мм) | 18, 21 |
| `hasHoles` | Наличие отверстий | true/false |

## Как использовать

### 1. Подключение в коде

```cpp
#include "DynamicShieldBuilder.h"

// Создание простого щита
DynamicShieldBuilder builder;
AcDbBlockTableRecord* pShield = builder.createDynamicShield(
    1200.0,  // длина
    500.0,   // ширина
    18.0,    // толщина
    false    // без отверстий
);

// Вставка в модель
AcGePoint3d insertPt(0.0, 0.0, 0.0);
AcDbBlockReference* pRef = builder.insertDynamicShield(pShield, insertPt, 0.0);

// Добавление в базу данных текущего документа
AcDbDatabase* pDb = acdbCurDwg();
AcDbBlockTableRecord* pCurrentSpace = nullptr;
pDb->getBlockTable(pCurrentSpace, AcDb::kForRead);
AcDbObjectId spaceId;
pCurrentSpace->getAt(ACDB_MODEL_SPACE, spaceId);
pCurrentSpace->close();

AcDbBlockTableRecord* pSpace = nullptr;
pDb->openObject((AcDbObject*&)pSpace, spaceId, AcDb::kForWrite);
pSpace->appendAcDbEntity(pRef);
pSpace->close();
pRef->close();
```

### 2. Щит с отверстиями

```cpp
DynamicShieldBuilder builder;
AcDbBlockTableRecord* pShieldWithHoles = builder.createDynamicShield(
    1500.0,  // длина
    600.0,   // ширина
    21.0,    // толщина
    true     // с отверстиями
);
```

## Текущие ограничения

### ⚠️ Динамические параметры (Stretch/Array)

В текущей версии создается **статический блок** с геометрией. Полноценные динамические параметры (растягивание, массивы) требуют:

1. Работы с `AcDbDynBlockReferenceProperty`
2. Создания `AcDbStretchAction` и привязки к параметрам
3. Записи специальных XRecords в блок

**План доработки:**
- Версия 1.1: Добавить линейный параметр длины (Distance Parameter)
- Версия 1.2: Добавить действие растягивания (Stretch Action)
- Версия 1.3: Добавить параметр количества отверстий (Array Action)

### Альтернатива на данный момент

Для изменения размеров щита:
1. Удалить старый блок
2. Создать новый блок с другими размерами через `createDynamicShield()`
3. Вставить новый блок на то же место

Или использовать **BEDIT** в AutoCAD для ручной настройки динамики.

## Интеграция с TrinityBuildEngine

Для использования в сборщике конструкций:

```cpp
// В TrinityBuildEngine.cpp заменить создание полилиний на:
DynamicShieldBuilder shieldBuilder;
AcDbBlockTableRecord* pShield = shieldBuilder.createDynamicShield(
    node.length,
    node.width,
    config.shieldThickness,
    node.hasHoles
);
// Вставить вместо текущей геометрии
```

## Структура блока

```
TRINITY_SHIELD_L1200_W500_T18
├── Полилиния (внешний контур)
│   └── Слой: TRINITY_SHIELDS
└── Круги (отверстия, если hasHoles=true)
    └── Слой: TRINITY_HOLES
    ├── Ø20мм, отступ 50мм от левого края
    └── Ø20мм, отступ 50мм от правого края
```

## Следующие шаги

1. **Интеграция с БД**: Изменить схему хранения - вместо координат каждого щита хранить параметры конструкции (L, W, H)
2. **Динамические действия**: Реализовать Stretch для изменения длины без пересоздания
3. **Планки с вырезом**: Создать аналогичный класс для планок (`DynamicStrapBuilder`)
4. **Конструкции**: Создать класс `DynamicAssembly` для крышек/днищ с планками

## Тестирование

1. Скомпилировать проект в Visual Studio 2022
2. Загрузить `_Trinity.arx` в AutoCAD 2026
3. Выполнить тестовый скрипт создания щита
4. Проверить появление блока в палитре **Блоки**
5. Изменить свойства блока через палитру **Свойства**

## Зависимости

- ObjectARX 2026 SDK
- Библиотеки: `acdb.lib`, `acge.lib`, `aced.lib`
- Слои должны быть созданы через `TrinityLayerManager`
