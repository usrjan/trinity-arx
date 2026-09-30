// TrinityCommands.cpp
#include "StdAfx.h"
#include "TrinityCommands.h"
#include "TrinityConfig.h"
#include "TrinityTimer.h"   // безопасный таймер с document lock (см. TrinityTimer.h)

// ============================================================
// ФАЙЛОВОЕ СОСТОЯНИЕ КОМАНД (наружу не торчит)
// ============================================================
// Раньше id таймера был глобалом `extern UINT_PTR g_timerId` в заго-
// ловке — любой TU мог его испортить. Теперь это приватная деталь ре-
// ализации команд: видна только здесь. Сам движок — синглтон engine()
// из TrinityCommands.h (владение статическим объектом, без new/delete).
static UINT_PTR s_timerId = 0;

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
static void TimerProc() {
    trinityProcess();
}

// ============================================
// TRINITY_START — запуск таймера
// ============================================
void trinityStart() {
    // Если таймер уже запущен — сначала останавливаем его
    if (s_timerId != 0) {
        acutPrintf(_T("\n[Trinity] Timer already running. Restarting...\n"));
        trinityStop();
    }

    // Загружаем настройки из trinity.ini в папке библиотеки
    TrinityConfig::DbConfig cfg;
    if (!TrinityConfig::load(cfg)) {
        acutPrintf(_T("\n[Trinity] Config load failed. Startup aborted.\n"));
        return;
    }

    // Настраиваем единственный экземпляр движка (синглтон) и подключаемся.
    // Пул объектов живёт всю жизнь DLL — повторные TSTART/TSTOP безопасны:
    // connect()/disconnect() идемпотентны, никаких new/delete указателей.
    if (!engine().init(cfg.host.c_str(), cfg.user.c_str(),
                      cfg.pass.c_str(), cfg.db.c_str())) {
        acutPrintf(_T("\n[Trinity] Failed to connect to database\n"));
        return;
    }

    // Безопасный таймер: тик приходит в message pump главного потока
    // AutoCAD, активный документ залочен на запись (см. TrinityTimer.cpp).
    // Прямой SetTimer(NULL, ...) с записью в БД без lock нарушал протокол
    // AutoCAD и вызывал Access Violation.
    s_timerId = StartTrinityTimer(5000, TimerProc);

    if (s_timerId == 0) {
        acutPrintf(_T("\n[Trinity] Failed to start timer.\n"));
        engine().shutdown();   // соединение уже открыто — закрываем обратно
        return;
    }

    acutPrintf(_T("\n[Trinity] Timer started. Every 5 seconds (document-lock protected).\n"));
}

// ============================================
// TRINITY_STOP — остановка
// ============================================
void trinityStop() {
    // Сначала останавливаем таймер
    if (s_timerId != 0) {
        StopTrinityTimer();
        s_timerId = 0;
    }

    // Останавливаем движок (идемпотентно: shutdown() можно звать повторно
    // даже при незапущенном соединении — объект синглтона существует всегда).
    engine().shutdown();

    acutPrintf(_T("\n[Trinity] Timer stopped.\n"));
}

// ============================================
// ОБРАБОТКА ОДНОГО ТИКА
// ============================================
// Вызывается ТОЛЬКО из TimerProc, то есть с главного потока AutoCAD
// при захваченном write-lock активного документа (гарантия TrinityTimer).
// Именно поэтому здесь разрешено брать workingDatabase() и писать в неё.
void trinityProcess() {
    // Движок считается готовым к работе, только когда соединение реально
    // установлено (см. init/connect). Синглтон-ссылка не бывает null —
    // признак «не запущено» проверяется через состояние ядра.
    if (!engine().isConnected()) return;

    // Защита от реентерабельности/наложения тиков (долгая сборка > интервала).
    if (g_isProcessing) return;
    g_isProcessing = true;

    __try {
        AcDbDatabase* db = acdbHostApplicationServices()->workingDatabase();
        int processed = engine().processTick();

        // Обновление дисплея — тоже легально: мы под локом, на главном потоке.
        if (processed > 0) acedUpdateDisplay();
    }
    __finally {
        g_isProcessing = false;
    }
}