# TrinityARX

Плагин ObjectARX для AutoCAD 2026. Рекурсивный сборщик опалубки из базы данных Trinity.

## Что это

TrinityARX читает **нейроны** и **синапсы** из MySQL и
материализует их в **DWG-чертежи** внутри AutoCAD.  
Каждый проект, конструкция и деталь — это запись в базе. Плагин сам строит всё, чего нет на диске, и вставляет как XREF.

- **База данных — источник истины.** Никакого хардкода в плагине.
- **Рекурсивная сборка.** Деталь → конструкция → проект. Автоматически.
- **Кэш на диске.** Если файл уже есть — XREF вставляется мгновенно.
- **Один проект — один DWG.** Плюс отдельные DWG для деталей и конструкций.

## Архитектура

```
┌────────────────────────────────────────┐  
│  AutoCAD 2026                          │  
│  TRINITY_START / TRINITY_STOP          │  
└────────────────┬───────────────────────┘  
                 │  
┌────────────────┴───────────────────────┐  
│  TrinityBuildEngine                    │  
│  Рекурсивный ensureExists /            │  
│  ensureFileExists                      │  
└────────────────┬───────────────────────┘  
                 │  
┌────────────────┴───────────────────────┐  
│  TrinityCore / TrinityGeometryBuilder  │  
│  / TrinityFileManager / LayerManager   │  
│  / AttributeBuilder                    │  
└────────────────┬───────────────────────┘  
                 │  
┌────────────────┴───────────────────────┐  
│  MySQL (trinity_core)                  │  
│  neuron, synapse, text                 │  
└────────────────────────────────────────┘
```

## Требования


| Компонент | Версия |
|--------------------|--------------|
| Visual Studio      | 2026         |
| AutoCAD            | 2026         |
| ObjectARX SDK      | 2026         |
| MySQL Server       |              |
| 8.0.46             |


**Пути по умолчанию:**

- ObjectARX: `D:\devel\ObjectARX\`
- MySQL: `D:\devel\MySQL\`

## Сборка

1. Открыть `Trinity.vcxproj` в Visual Studio
2. Проверить в свойствах проекта:3. **C/C++ → General → Additional Include Directories:**  
  `D:\devel\ObjectARX\inc;D:\devel\MySQL\include`
4. **Linker → General → Additional Library Directories:**  
  `D:\devel\ObjectARX\lib-x64;D:\devel\MySQL\lib`
5. **Linker → Input → Additional Dependencies:**  
  `accore.lib;acad.lib;acui25.lib;adui25.lib;acpal.lib;acdb25.lib;acge25.lib;acgiapi.lib;acISMobj25.lib;rxapi.lib;acgeoment.lib;libmysql.lib`
6. **Linker → General → Output File:** `$(OutDir)_$(ProjectName)$(TargetExt)` (для `_Trinity.arx`)
7. Собрать **Debug x64** или **Release x64**
8. Получится `_Trinity.arx`


## Загрузка в AutoCAD

1. `APPLOAD` в AutoCAD
2. Выбрать `_Trinity.arx`
3. Команды:4. `TRINITY_START` — запустить таймер (проверка базы каждые 5 сек)
5. `TRINITY_STOP` — остановить таймер


## Настройка базы

Учетные данные **не хранятся в коде**. Создайте файл `trinity.ini` в папке с `_Trinity.arx` (образец — `trinity.ini.example`):

```ini
[database]  
host     = 10.250.11.112  
user     = user_name  
password = user_password  
database = database_name  

[paths]  
base = D:\trinity
```

Файл `trinity.ini` добавлен в `.gitignore` и не коммитится.

**Структура БД:** таблицы `neuron`, `synapse`, `text`.

## Структура файлов

Плагин создаёт DWG-кэш по пути `D:\trinity\`:

```
D:\trinity\  
├── details\      — DWG деталей (щиты, планки, боковые стенки)  
├── assemblies\   — DWG конструкций  
└── projects\     — DWG проектов
```

## Типы нейронов


| Тип         | Что это                                | Пример         |
|----------------|----------------------------------------------|----------------------|
| `detail`       | Деталь                                 | `D.S.0.425.850.10`   |
| `construction` | Конструкция (сборка)        | `C.S.0.3.425.850`    |
| `project`      | Проект (верхний уровень) | `PROJ-TEST-001`      |
| `item`         | Материал, прайс                 | `MAT.PLYWOOD-FSF.10` |
| `tree`         | Раздел каталога                | `CATALOG`, `SHIELDS` |


## Коды деталей

**D.S.процесс.ширина.высота.толщина**


| Префикс | Значение                                                                |
|----------------|---------------------------------------------------------------------------------|
| `D.S.0`        | Щит без обработки                                                |
| `D.S.1`        | Щит с вырезом                                                        |
| `D.S.2`        | Щит со сквозным отверстием (боковая стенка) |
| `D.S.3`        | Щит с отверстием-гнездом (планка)                    |
| `D.B.0`        | Профиль без обработки                                        |
| `D.B.1`        | Профиль с вырезом                                                |


**C.S.0.3.ширина.высота** — крышка-дно: щит D.S.0 + планки D.S.3.

## Слои AutoCAD


| Слой                       | Что на нём                                 | Цвет                |
|--------------------------------|----------------------------------------------------|-------------------------|
| `TRINITY_MAT_<material>` | Солиды (по материалу)             | по материалу |
| `_tag`                         | Атрибуты `DETAIL_CODE`                     | красный, off     |
| `_bolt`                        | Маркеры болтов (кружочки Ø6) | красный, off     |


**Материалы по умолчанию:**

- `TRINITY_MAT_PLYWOOD-FSF` — 31 (коричневый)
- `TRINITY_MAT_STEEL` — 8
- `TRINITY_MAT_CONCRETE` — 254
- `TRINITY_MAT_ALUMINIUM` — 9

## Как это работает

### 1. Таймер

Каждые 5 секунд `TRINITY_START` вызывает `processAllProjects()`.

### 2. Рекурсивная сборка

```
processAllProjects  
│  
└─ ensureFileExists(PROJ-TEST-001)  
│  
└─ buildDwg(PROJ-TEST-001)  
│  
├─ ensureFileExists(C.S.0.3.425.850)  
│    │  
│    └─ buildDwg(C.S.0.3.425.850)  
│         │  
│         ├─ ensureFileExists(D.S.0.425.850.10)  → buildDetail  
│         ├─ ensureFileExists(D.S.3.425.125.10)  → buildDetail  
│         └─ wblock + saveAs  
│  
├─ ensureFileExists(C.S.0.3.425.425)  
├─ ensureFileExists(C.S.0.3.850.850)  
├─ ensureFileExists(C.S.0.3.425.850.ASSY)  
├─ ensureFileExists(D.S.2.425.425.10)  
└─ ...  
│  
└─ wblock + saveAs
```

### 3. `wblock` для чистых DWG

Для каждой детали:

1. Создаём `tempDb`, строим солид
2. Добавляем болты / сверлим отверстия
3. Добавляем атрибут `DETAIL_CODE`
4. `wblock` → `cleanDb`
5. Сохраняем `cleanDb` на диск
6. Вставляем XREF в родительскую базу


### 4. XREF-иерархия

- Деталь → DWG детали
- Конструкция → DWG конструкции (XREF деталей)
- Проект → DWG проекта (XREF конструкций и деталей)

## Compound rotation

Каждый `synapse` может содержать **до 2 поворотов** — массив `rot`:

```json
"rot": [[0, 1, 0, 90], [0, 0, 1, 90]]
```

Первый поворот применяется, потом второй. Повороты задаются как `[ось_x, ось_y, ось_z, угол_в_градусах]`.

## Болты

Болт — это **маркер (кружочек Ø6 мм)**, а не солид. Рисуется в плоскости YZ.  
Слой `_bolt`, красный, выключен.

**Спецификация:**

- 1 кружочек в точке → болт **60 мм**
- 2 кружочка в одной точке → болт **80 мм**

## Атрибуты

Каждая деталь имеет скрытый атрибут `DETAIL_CODE` на слое `_tag`.  
Он попадает в итоговый проект через XREF.

**Спецификация через `DATAEXTRACTION` в AutoCAD:**

1. `DATAEXTRACTION`
2. Выбрать блоки
3. Извлечь `DETAIL_CODE`
4. Сгруппировать по коду
5. Подтянуть материал, цену, вес из базы


## Спецификации (SQL)

Количество каждой детали в проекте:

```sql
SELECT   
JSON_UNQUOTE(JSON_EXTRACT(n.data, '$.code')) AS code,  
MAX(JSON_UNQUOTE(JSON_EXTRACT(n.data, '$.category'))) AS category,  
COUNT(*) AS quantity  
FROM synapse s  
JOIN neuron n ON s.child = n.id  
WHERE s.parent = (  
SELECT id FROM neuron   
WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'PROJ-TEST-001'  
)  
GROUP BY code;
```

## Сброс проекта

Чтобы собрать заново, нужно:

1. Удалить старые DWG:		D:\trinity\details\*.dwg  
D:\trinity\assemblies\*.dwg  
D:\trinity\projects\*.dwg
2. Сбросить статус проекта:		UPDATE neuron   
SET data = JSON_SET(data, '$.status', 'pending')  
WHERE JSON_UNQUOTE(JSON_EXTRACT(data, '$.code')) = 'PROJ-TEST-001';
3. `TRINITY_START` в AutoCAD.


## Файлы плагина

```
TrinityARX/  
├── StdAfx.h / StdAfx.cpp              — прекомпилированные заголовки + utf2uni  
├── TrinityCore.h / TrinityCore.cpp    — БД, нейроны, синапсы  
├── TrinityLayerManager.h / .cpp       — слои материалов, _tag, _bolt  
├── TrinityAttributeBuilder.h / .cpp   — атрибут DETAIL_CODE  
├── TrinityGeometryBuilder.h / .cpp    — щит, планка, боковая стенка  
├── TrinityFileManager.h / .cpp        — файлы, wblock, XREF  
├── TrinityBuildEngine.h / .cpp        — рекурсивный сборщик  
├── TrinityCommands.h / .cpp           — TRINITY_START / TRINITY_STOP  
└── acrxEntry.cpp                      — точка входа AutoCAD
```

## Лицензия

Внутренний проект. Для команды Trinity.

 
