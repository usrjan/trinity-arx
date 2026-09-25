// TrinityCommands.cpp
#include "StdAfx.h"
#include "TrinityCommands.h"
#include "TrinityConfig.h"

TrinityBuildEngine* g_engine = nullptr;
UINT_PTR g_timerId = 0;

// ============================================
// CALLBACK ТАЙМЕРА
// ============================================
void CALLBACK TimerProc(HWND, UINT, UINT_PTR, DWORD) {
    // Защита от реентерабельности: если предыдущий тик ещё выполняется
    // (или завис в базе/файловой системе), новый тик пропускается.
    // Без этого processAllProjects() мог вызываться повторно из своего же
    // стека (например через acedUpdateDisplay), что приводило к повторной
    // вставке XREF и падению AutoCAD.
    // Флаг — обычный long с атомарными операциями InterlockedXxx:
    // реентерабельность таймера в AutoCAD (вложенная обработка сообщений)
    // защищается корректно и без исключений C++ внутри SEF-блока.
    static long g_isProcessing = 0;
    if (InterlockedCompareExchange(&g_isProcessing, 1, 0) != 0) return;

    __try {
        trinityProcess();
    }
    __finally {
        InterlockedExchange(&g_isProcessing, 0);
    }
}

// ============================================
// TRINITY_START — запуск таймера
// ============================================
void trinityStart() {
    // Полный сброс предыдущего состояния (таймер + движок).
    // trinityStop() идемпотентен: корректно работает и при g_timerId == 0,
    // и при g_engine == nullptr.
    if (g_timerId != 0 || g_engine) {
        acutPrintf(_T("\n[Trinity] Restarting...\n"));
        trinityStop();
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

    g_timerId = SetTimer(NULL, NULL, 5000, TimerProc);

    acutPrintf(_T("\n[Trinity] Timer started. Every 5 seconds.\n"));
}

// ============================================
// TRINITY_STOP — остановка
// ============================================
void trinityStop() {
    // Сначала останавливаем таймер
    if (g_timerId != 0) {
        KillTimer(NULL, g_timerId);
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
void trinityProcess() {
    if (!g_engine) return;

    AcDbDatabase* db = acdbHostApplicationServices()->workingDatabase();
    int processed = g_engine->processAllProjects(db);

    if (processed > 0) acedUpdateDisplay();
}