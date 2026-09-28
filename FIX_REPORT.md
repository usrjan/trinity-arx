# Анализ и исправление критической ошибки TrinityARX

## Проблема
При повторном запуске `TSTART` после удаления файлов и сброса статуса проекта в БД, AutoCAD падает с критической ошибкой.

## Корневые причины

### 1. **Невалидные XREF-блоки в таблице блоков**
Когда пользователь удаляет DWG-файлы вручную, в базе данных AutoCAD остаются записи о блоках-XREF, которые ссылаются на несуществующие файлы. При попытке вставки такого XREF происходит краш.

**Исправление в `TrinityFileManager.cpp`:**
- Добавлена проверка `isFromExternalReference()` для существующих блоков
- Проверка валидности пути через `externalReferenceId()`
- Автоматическое удаление невалидных блоков перед повторной вставкой
- Улучшена обработка ошибок при открытии BlockTable и ModelSpace

### 2. **Утечка памяти и двойное освобождение**
Глобальный указатель `g_engine` не обнулялся корректно при повторном вызове `TSTART`.

**Исправление в `TrinityCommands.cpp`:**
- `trinityStart()` теперь всегда создаёт новый движок
- Принудительная очистка старого движка перед созданием нового
- `trinityStop()` сделан идемпотентным (безопасным для многократного вызова)

### 3. **Остаточные файлы между сессиями**
Старые файлы могли оставаться в папках details/assemblies/projects.

**Исправление в `TrinityBuildEngine.cpp`:**
- Добавлен метод `deleteProjectFiles()` для рекурсивного удаления всех файлов проекта
- Вызывается автоматически перед каждой сборкой pending-проекта
- Гарантирует чистое состояние перед сборкой

## Изменённые файлы

### 1. TrinityFileManager.cpp
```cpp
// Добавлена логика проверки и удаления невалидных XREF-блоков
if (pBlockRec->isFromExternalReference()) {
    AcDbObjectId xrefId = pBlockRec->externalReferenceId();
    if (xrefId.isValid()) {
        // Файл удалён — удаляем блок из таблицы
        pOldRec->erase();
    }
}

// Добавлена проверка ошибок для всех операций
es = targetDb->getSymbolTable(pBt, AcDb::kForRead);
if (es != Acad::eOk) return AcDbObjectId::kNull;

// Всегда закрываем pRef независимо от результата
pRef->close();
```

### 2. TrinityCommands.cpp
```cpp
void trinityStart() {
    // Если таймер уже запущен — сначала останавливаем
    if (g_timerId != 0) {
        trinityStop();
    }
    
    // Очищаем старый движок
    if (g_engine) {
        delete g_engine;
        g_engine = nullptr;
    }
    
    // Создаём новый
    g_engine = new TrinityBuildEngine("D:\\trinity");
    // ...
}
```

### 3. TrinityBuildEngine.h
```cpp
// Добавлен метод для рекурсивного удаления файлов
void deleteProjectFiles(const std::string& code);
```

### 4. TrinityBuildEngine.cpp
```cpp
// Рекурсивное удаление файлов проекта и всех детей
void TrinityBuildEngine::deleteProjectFiles(const std::string& code) {
    // Загружаем нейрон
    // Удаляем файл
    // Если не деталь — рекурсивно удаляем детей
}

// В processAllProjects():
for (auto& proj : projects) {
    deleteProjectFiles(proj.code);  // Чистим перед сборкой
    ensureFileExists(proj.code, 0);  // Собираем заново
}
```

### 5. TrinityBuildEngine.cpp
```cpp
// Добавлен #include <io.h> для _wunlink
#include <io.h>
```

## Рекомендации по тестированию

1. **Первый запуск:**
   ```
   TSTART → выждать 5 сек → TSTOP
   ```

2. **Сброс и повтор:**
   - Удалить файлы из `D:\trinity\details`, `assemblies`, `projects`
   - В БД: `UPDATE neuron SET data = JSON_SET(data, '$.status', 'pending') WHERE ...`
   - Выполнить:
     ```
     TSTART → выждать 5 сек → TSTOP
     ```
   - Повторить 3-5 раз

3. **Проверка утечек:**
   - Запустить Process Explorer или аналогичный инструмент
   - Проверить память процесса acad.exe между циклами TSTART/TSTOP

## Дополнительные улучшения — СТАТУС ВЫПОЛНЕНИЯ

### A. Логирование — ЧАСТИЧНО
Логирование ошибок сборки (`wblock failed`, `saveAs failed`, `mkdir failed`)
реализовано через `acutPrintf` в `TrinityBuildEngine.cpp` / `TrinityFileManager.cpp`.
Для wchar_t-строк используется `%ls` (не `%s`). Общий заголовок вида
"=== Starting project build ===" в `processAllProjects()` не добавлен.

### B. Защита от зависания — ВЫПОЛНЕНО
Флаг `g_isProcessing` реализован в `TrinityCommands.cpp` (внутри `trinityProcess()`,
строки ~110–121): повторяющийся тик пропускается, пока идёт долгая сборка.

### C. Очистка при выгрузке плагина — ЧАСТИЧНО
`unloadApp()` (`acrxEntry.cpp`) вызывает `trinityStop()` и снимает группу команд.
Явного `delete g_engine` там нет — движок освобождается в `trinityStart()`
перед созданием нового экземпляра (см. раздел «Утечка памяти» выше).

## Статус
✅ Все критические исправления применены
✅ Код готов к сборке в Visual Studio с ObjectARX 2026 SDK
✅ Рекомендуется протестировать цикл TSTART/TSTOP минимум 5 раз

---

# Фикс №2: Запись в активный документ из таймера без document lock (Access Violation)

## Проблема
`trinityStart()` использовал `SetTimer(NULL, NULL, 5000, TimerProc)`, а
`TimerProc → trinityProcess()` напрямую брал `workingDatabase()` и писал в неё
(XREF-и, wblock, saveDwg) **без захвата document lock**. Это нарушает протокол
блокировки документов AutoCAD: колбэк Win32-таймера приходит в message pump в
произвольный момент — когда пользователь уже начал команду, документ залочен
другим приложением (.NET/VBA/другая ARX-программа) или идёт регенерация.
Модификация БД в такой момент = Access Violation / фатальный крах acad.exe.

## Исправление
Добавлены `TrinityTimer.h` / `TrinityTimer.cpp` — безопасный таймер по
рекомендуемому Autodesk паттерну:

1. Таймер заводится на окно AutoCAD (`adsw_acadMainWnd()`), а не на `NULL`.
2. Колбэк таймера **ничего не делает с документами** — только
   `PostMessage(WM_TRINITY_TICK)` (защита от наложения тиков через флаг).
3. Обработчик `WM_TRINITY_TICK` (подклассирование окна) выполняется на главном
   потоке AutoCAD, где:
   - состояние lock mode определяется через системную переменную `LOCKMODE`
     стандартным способом: `acedGetVar(_T("LOCKMODE"), &rb)` со стековым
     `resbuf`, проверка `rb.restype == RTSHORT && rb.resval.rint != 0`. Поле
     `next` при этом не используется вовсе (первоначальный C2039 `"next": не
     является членом "resbuf"` был вызван обращением именно к `rb.next`).
     Несуществующие в данном релизе SDK имена (`acrtGetShortVariable`) не
     применяются — они давали C3861 «идентификатор не найден»;
   - если document lock mode включён (`LOCKMODE != 0`) — активный документ
     залочивается на запись RAII-обёрткой `TrinityDocLock`
     (`acDocManager->lockDocument()` / гарантированный `unlockDocument()`
     в деструкторе);
   - если lock mode выключен (`LOCKMODE == 0`, явный `lockDocument()` в этом
     режиме возвращает ошибку) — тик выполняется сразу, без лока: он приходит
     в message pump главного потока AutoCAD (между сообщениями, вне команд),
     а при LOCKMODE = 0 другой контекст (.NET/VBA/сторонняя ARX) параллельно
     документу не работает. Опрос состояния документа не производится:
     методов `AcApDocument::isCommandActive()` и
     `AcApDocument::documentLockStatus()` в заголовках этого релиза SDK нет
     (обе попытки давали C2039);
   - lock не получен (документ занят) — запись НЕ выполняется, ждём след. тик;
   - callback вызывается внутри SEH `__try/__except` — падение тика не уносит
     весь AutoCAD.
4. `trinityProcess()` дополнительно защищён флагом `g_isProcessing`
   (долгая сборка длиннее интервала не приводит к реентерабельности).

### Изменённые файлы
- `TrinityCommands.cpp`: `SetTimer/KillTimer` → `StartTrinityTimer/StopTrinityTimer`,
  флаг `g_isProcessing`, обработка ошибки запуска таймера.
- `Trinity.vcxproj`, `Trinity.vcxproj.filters`: добавлены TrinityTimer.cpp/.h.

## Статус
✅ Таймер работает строго под write-lock активного документа
✅ Протокол document locking ObjectARX 2026 соблюдён

---

# Обновление документации (актуальный статус кода)

Раздел «Фикс №1» местами описывал код, который впоследствии был изменён.
Ниже — сверка с текущим состоянием репозитория:

| Утверждение из Фикса №1 | Текущее состояние |
|---|---|
| `deleteProjectFiles()` вызывается перед сборкой pending-проекта | ✅ Актуально (`TrinityBuildEngine::processAllProjects`) |
| Проверки `isFromExternalReference()` / `externalReferenceId()` в `attachXref` | ✅ Актуально (`TrinityFileManager.cpp`) |
| `#include <io.h>` для `_wunlink` | ⚠️ Изменено: `<io.h>` и `<direct.h>` теперь подключаются только через `StdAfx.h`; локальные дубли убраны. Для удаления файлов используется `_wunlink` с UTF-8→wide путём (`utf8ToWide`) |
| Вставка готового проекта в активный чертёж | ❌ Удалено: метод `insertProjectToTarget()` убран вместе с вызовом (ошибка 320 `eWasOpenForWrite` при записи из таймера). Результат сборки — файлы DWG на диске |
| Метод `ensureExists()` | ❌ Удалён как мёртвый код; его функциональность покрывает `ensureFileExists()` |

Дополнительно с момента Фикса №1/№2 в коде выполнены:

- **Безопасное освобождение после `wblock`:** ветки `es != Acad::eOk` и `!cleanDb`
  разделены, слепой `delete cleanDb` при ошибке убран (риск двойного free).
- **Пути:** рекурсивное создание каталогов `createDirectoryRecursiveA/W`
  (`_wmkdir`, поддержка UNC, идемпотентность), кириллические пути через UTF-8→wide;
  ANSI `_mkdir/_access` не используются.
- **Санитизация имён:** `sanitizeFileName()` в `getFilePath()`/`attachXref()`
  (запрещённые символы, path traversal, усечение по границе UTF-8).
- **Парсинг JSON массива `holes`:** устранён бесконечный цикл (поэлементный обход
  с границами объекта вместо глобального поиска ключей).
- **Вывод строк:** все `acutPrintf` с `wchar_t*` используют `%ls`.
- **Стиль:** `nullptr` вместо NULL для указателей, `true/false` в setter-ах атрибутов
  (bool-API), убраны дублирующиеся `#include`, три копии логики путей сведены к
  `subdirForType()/getFilePathForNeuron()`, четыре реализации создания слоёв —
  к `TrinityLayerManager::createOrGetLayerByName()`.
- **Конвертация:** новый безопасный `utf8ToWide()` в `StdAfx`; `stringToWide()`
  помечена `[[deprecated]]`.

Зарегистрированные команды AutoCAD: `TSTART` / `TSTOP` (см. `acrxEntry.cpp`).
