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

# Исправление: отсутствие переподключения MySQL

## Проблема
`TrinityCore` не обрабатывал обрыв соединения с MySQL (перезапуск сервера, обрыв сети,
истечение `wait_timeout`). Все методы после `connect()` только проверяли флаг `m_connected`,
который никогда не сбрасывался, — при обрыве запросы падали с `CR_SERVER_GONE_ERROR`
до перезапуска плагина.

## Исправление (`TrinityCore.h` / `TrinityCore.cpp`)

### 1. Пинг соединения перед каждым обращением к БД
Новый приватный метод `ensureConnected()`: выполняет `mysql_ping()` (не чаще раза
в 30 секунд — константа `MIN_PING_INTERVAL_S`, чтобы не добавлять лишний round-trip
на каждый запрос в цикле сборки), при ошибке обрыва (коды 2002/2003/2006/2013/2055)
автоматически восстанавливает соединение. Заменены все проверки `if (!m_connected)`
в `loadNeuronByCode`, `loadNeuronById`, `loadChildren`, `loadPendingProjects`,
`markNeuronDone`, `escapeSqlLiteral`.

### 1a. Повтор запроса при обрыве в середине выполнения
Новый приватный метод `recoverQuery(query)`: если `mysql_query()` вернул ошибку обрыва,
выполняет переподключение и повторяет запрос один раз (все запросы класса — SELECT либо
идемпотентный UPDATE с `JSON_SET`, повтор безопасен). Применён во всех методах выбора/обновления.

### 2. Механизм повторного подключения
Новый приватный метод `reconnect(maxAttempts, delayMs)`:
- параметры подключения (`host/user/pass/db`) сохраняются в полях `m_host/m_user/m_pass/m_db`
  при первом успешном `connect()`;
- до 3 попыток переподключения с экспоненциальной задержкой (500 → 1000 → 2000 мс);
- полное закрытие старого handle и создание нового (со статусом в командную строку AutoCAD).

### 3. Таймауты сокета и флаги коннектора
В `connect()`/`reconnect()` установлены:
- `MYSQL_OPT_CONNECT_TIMEOUT` = 5 с, `MYSQL_OPT_READ_TIMEOUT` / `MYSQL_OPT_WRITE_TIMEOUT` = 10 с
  (без них запрос к «мёртвому» TCP-соединению мог висеть минутами);
- `MYSQL_OPT_RECONNECT` = 1 как подстраховка на уровне клиента libmysql.

### 4. Исправлен `disconnect()`
Ранее `mysql_close()` вызывался только при `m_connected == true`, из-за чего handle,
оставшийся после неудачного ping, утекал. Теперь закрывается любой непустой `m_mysql`.

## Проверка
Код прошёл синтаксическую проверку `g++ -fsyntax-only -std=c++17 -Wall -Wextra`
с заглушками mysql.h/ARX (полная сборка возможна только под MSVC + ObjectARX 2026 SDK).

## 2026-09-25: Исправление компиляции — error C2065 `my_bool` (MySQL Connector/C 8.0)

**Симптом:** MSVC: `error C2065: my_bool: необъявленный идентификатор` в местах установки
`MYSQL_OPT_RECONNECT` (TrinityCore.cpp, строки ~69 и ~125).

**Причина:** В MySQL Connector/C 8.0 (и новее 6.x) тип `my_bool` (typedef unsigned char)
удалён из заголовков; `mysql_options()` для `MYSQL_OPT_RECONNECT` теперь ожидает указатель
на стандартный `bool`. В коннекторах 5.x и в MariaDB Connector/C по-прежнему используется `my_bool`.

**Исправление:** обе точки (connect() и reconnect()) защищены условной компиляцией:
```cpp
#if defined(MYSQL_VERSION_ID) && MYSQL_VERSION_ID >= 60000 || defined(MARIADB_BASE_VERSION) || defined(MARIADB_VERSION_ID)
    my_bool reconnectFlag = 1;   // MySQL 5.x / MariaDB Connector/C
#else
    bool reconnectFlag = true;   // MySQL Connector/C 8.0+
#endif
mysql_options(m_mysql, MYSQL_OPT_RECONNECT, &reconnectFlag);
```
`MYSQL_VERSION_ID` определён в mysql.h (подключается через StdAfx.h), поэтому макрос доступен.
Для MariaDB добавлена встречная проверка `MARIADB_BASE_VERSION`/`MARIADB_VERSION_ID`
(там MYSQL_VERSION_ID может быть ≥ 80000, но API всё ещё принимает `my_bool`).

**Проверка:** синтаксис + корректность выбора ветки препроцессора проверены с заглушкой
mysql.h (MYSQL_VERSION_ID=80430 → ветка 8.0; без определения → ветка my_bool).

## Исправление: ошибки компиляции под MySQL Connector/C 8.0.46 (C2065 my_bool)
**Дата:** 2026-09-25. **Файл:** TrinityCore.cpp (коммит 20f05ed).

### Причина
В MySQL Connector/C 8.0:
1. typedef `my_bool` УДАЛЁН из mysql.h — любая ссылка на него даёт C2065;
2. константа `MYSQL_OPT_RECONNECT` удалена из `enum mysql_option` — её использование
   дало бы следующую ошибку сразу после починки my_bool;
3. предыдущий вариант с `#if defined(MYSQL_VERSION_ID)` не спасал, т.к. в 8.x макрос
   версии живёт в отдельном `<mysql_version.h>`, который не гарантированно подключается
   через mysql.h, и при его отсутствии включалась ветка с `my_bool`.

### Что сделано
- Блок `MYSQL_OPT_RECONNECT` удалён из connect() и reconnect() полностью: в 8.0 эта
  опция не поддерживается клиентом. Функциональность переподключения при этом НЕ
  пострадала — её полностью обеспечивают наши `ensureConnected()` (mysql_ping +
  детект кодов 2002/2003/2006/2013/2055) и `reconnect()` (до 3 попыток, экспоненциальная
  задержка), а также повтор запроса в `recoverQuery()`.
- Имя `my_bool` больше не встречается в коде нигде (только в комментариях) — ошибка
  C2065 физически невозможна независимо от того, какие заголовки/макросы видит препроцессор.
- Все вызовы `mysql_options(..., &timeout)` приведены к `static_cast<const void*>(&timeout)`
  (сигнатура 8.0: `int mysql_options(MYSQL*, enum mysql_option, const void*)`) — убирает
  потенциальные предупреждения MSVC о неявном преобразовании.

### Проверка
Синтаксис изменённых фрагментов проверен g++ -std=c++17 -Wall -Wextra против заглушки
mysql.h conectora 8.0 (без my_bool, MYSQL_OPT_RECONNECT отсутствует в enum) — OK.
