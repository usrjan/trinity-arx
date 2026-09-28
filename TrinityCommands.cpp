// TrinityCommands.cpp
#include "StdAfx.h"
#include "TrinityCommands.h"
#include "TrinityConfig.h"
#include "TrinityTimer.h"   // безопасный таймер с document lock (см. TrinityTimer.h)

TrinityBuildEngine* g_engine = nullptr;
UINT_PTR g_timerId = 0;

// Флаг «тик идёт прямо сейчас». Защита от наложения: если обработка
// предыдущего тика ещё не завершилась (долгая сборка), новые тики
// пропускаются, а не залезают в БД поверх работающего движка.
static bool g_isProcessing = false;

// ============================================
// CALLBACK ТАЙМЕРА
// ============================================
// ВАЖНО: этот вызов приходит НЕ из колбэка SetTimer, а из обработчика
// WM_TRINITY_TICK на главном потоке AutoCAD, и активный документ УЖЕ
// залочен на запись (write-lock) внутри TrinityTimer. Именно поэтому
// здесь легально обращаться к workingDatabase() и модифицировать её.
// Запись в документ без этого лока нарушала протокол AutoCAD и приводила
// к Access Violation (крах acad.exe).
// Сигнатура — простой void(): сигнатура Win32 TIMERPROC здесь не нужна,
// т.к. низкоуровневый таймер-процедур инкапсулирован в TrinityTimer.
void TimerProc() {
    trinityProcess();
}

// ============================================
// TRINITY_START — запуск таймера
// ============================================
void trinityStart() {
    // Если таймер уже запущен — сначала останавливаем его
    if (g_timerId != 0) {
        acutPrintf(_T("\n[Trinity] Timer already running. Restarting...\n"));
        trinityStop();
    }

    // Очищаем глобальный указатель на движок
    if (g_engine) {
        delete g_engine;
        g_engine = nullptr;
    }

    // Загружаем настройки из trinity.ini в папке библиотеки
    TrinityDbConfig cfg;
    if (!loadTrinityConfig(cfg)) {
        acutPrintf(_T("\n[Trinity] Config load failed. Startup aborted.\n"));
        return;
    }

    // Создаём новый движок
    g_engine = new TrinityBuildEngine(cfg.basePath);

    if (!g_engine->init(cfg.host.c_str(), cfg.user.c_str(),
                        cfg.pass.c_str(), cfg.db.c_str())) {
        acutPrintf(_T("\n[Trinity] Failed to connect to database\n"));
        delete g_engine;
        g_engine = nullptr;
        return;
    }

    // Безопасный таймер: тик приходит в message pump главного потока
    // AutoCAD, активный документ залочен на запись (см. TrinityTimer.cpp).
    // Прямой SetTimer(NULL, ...) с записью в БД без lock нарушал протокол
    // AutoCAD и вызывал Access Violation.
    g_timerId = StartTrinityTimer(5000, TimerProc);

    if (g_timerId == 0) {
        acutPrintf(_T("\n[Trinity] Failed to start timer.\n"));
        delete g_engine;
        g_engine = nullptr;
        return;
    }

    acutPrintf(_T("\n[Trinity] Timer started. Every 5 seconds (document-lock protected).\n"));
}

// ============================================
// TRINITY_STOP — остановка
// ============================================
void trinityStop() {
    // Сначала останавливаем таймер
    if (g_timerId != 0) {
        StopTrinityTimer();
        g_timerId = 0;
    }

    // Очищаем движок
    if (g_engine) {
        g_engine->shutdown();
        delete g_engine;
        g_engine = nullptr;
    }

    acutPrintf(_T("\n[Trinity] Timer stopped.\n"));
}

// ============================================
// ОБРАБОТКА ОДНОГО ТИКА
// ============================================
// Вызывается ТОЛЬКО из TimerProc, то есть с главного потока AutoCAD
// при захваченном write-lock активного документа (гарантия TrinityTimer).
// Именно поэтому здесь разрешено брать workingDatabase() и писать в неё.
void trinityProcess() {
    if (!g_engine) return;

    // Защита от реентерабельности/наложения тиков (долгая сборка > интервала).
    if (g_isProcessing) return;
    g_isProcessing = true;

    __try {
        AcDbDatabase* db = acdbHostApplicationServices()->workingDatabase();
        int processed = g_engine->processAllProjects(db);

        // Обновление дисплея — тоже легально: мы под локом, на главном потоке.
        if (processed > 0) acedUpdateDisplay();
    }
    __finally {
        g_isProcessing = false;
    }
}

// ============================================
// TRIB — разработка: нарисовать все планки (category='rib')
// ============================================
// Команда для этапа разработки: берёт подключение к базе из trinity.ini,
// читает все детали с category='rib' и рисует их прямо в Model Space
// текущего открытого чертежа (в ряд, с зазором). Размеры и толщина
// каждой планки — динамические, из свойств нейрона.
void trinityTrib() {
    // Если движок с таймером уже запущен — переиспользуем его соединение
    bool ownEngine = false;
    TrinityBuildEngine* engine = g_engine;

    if (!engine) {
        TrinityDbConfig cfg;
        if (!loadTrinityConfig(cfg)) {
            acutPrintf(_T("\n[TRIB] Config load failed (trinity.ini). Command aborted.\n"));
            return;
        }
        engine = new TrinityBuildEngine(cfg.basePath);
        if (!engine->init(cfg.host.c_str(), cfg.user.c_str(),
                          cfg.pass.c_str(), cfg.db.c_str())) {
            acutPrintf(_T("\n[TRIB] Failed to connect to database.\n"));
            delete engine;
            return;
        }
        ownEngine = true;
    }

    AcDbDocument* pDoc = acDocManager->curDocument();
    if (!pDoc || pDoc->isReadOnly()) {
        acutPrintf(_T("\n[TRIB] No writable document is open.\n"));
        if (ownEngine) { engine->shutdown(); delete engine; }
        return;
    }

    AcDbDatabase* db = acdbHostApplicationServices()->workingDatabase();

    // Трибуны не трогаем во время тика таймера — ждём, пока он закончит
    if (g_isProcessing) {
        acutPrintf(_T("\n[TRIB] Timer tick in progress, try again in a few seconds.\n"));
        if (ownEngine) { engine->shutdown(); delete engine; }
        return;
    }

    int drawn = engine->drawAllRibs(db);

    acutPrintf(_T("\n[TRIB] Done. Ribs drawn: %d\n"), drawn);
    if (drawn > 0) acedUpdateDisplay();

    if (ownEngine) {
        engine->shutdown();
        delete engine;
    }
}