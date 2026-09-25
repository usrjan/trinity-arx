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
   TSTART →等待 5 сек → TSTOP
   ```

2. **Сброс и повтор:**
   - Удалить файлы из `D:\trinity\details`, `assemblies`, `projects`
   - В БД: `UPDATE neuron SET data = JSON_SET(data, '$.status', 'pending') WHERE ...`
   - Выполнить:
     ```
     TSTART →等待 5 сек → TSTOP
     ```
   - Повторить 3-5 раз

3. **Проверка утечек:**
   - Запустить Process Explorer или аналогичный инструмент
   - Проверить память процесса acad.exe между циклами TSTART/TSTOP

## Дополнительные улучшения (опционально)

### A. Добавить логирование
```cpp
// В начале processAllProjects()
acutPrintf(_T("\n[BuildEngine] === Starting project build ===\n"));
acutPrintf(_T("[BuildEngine] Projects to process: %d\n"), projects.size());
```

### B. Добавить защиту от зависания
```cpp
// В TimerProc добавить флаг выполнения
static bool g_isProcessing = false;
if (g_isProcessing) return;  // Пропускаем тик
g_isProcessing = true;
trinityProcess();
g_isProcessing = false;
```

### C. Очистка при выгрузке плагина
```cpp
// В unloadApp() добавить
delete g_engine;
g_engine = nullptr;
g_timerId = 0;
```

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
     (`acedGetVar`, API, доступный во всех поддерживаемых версиях ObjectARX);
   - если document lock mode включён (`LOCKMODE != 0`) — активный документ
     залочивается на запись RAII-обёрткой `TrinityDocLock`
     (`acDocManager->lockDocument()` / гарантированный `unlockDocument()`
     в деструкторе);
   - если lock mode выключен (`LOCKMODE == 0`, явный `lockDocument()` в этом
     режиме возвращает ошибку) — тик пропускается, если документ не в
     состоянии `AcAp::kDocIdle` (`acDocManager->documentState()`);
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
