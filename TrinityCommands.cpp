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
// TRIB — нарисовать все планки (category='rib')
// ============================================
// Вспомогательная команда этапа разработки: берёт из базы все детали
// с 'category' = 'rib' и рисует их геометрию прямо в текущем открытом
// чертеже (Model Space активного документа), раскладывая слева направо
// с зазором. Позволяет проверять динамические свойства планки
// (width/height/thickness и rib.*) без полной пересборки DWG-кэша.
void trinityRib() {
    // Конфиг и подключение к БД — локальные, таймер не трогаем
    TrinityDbConfig cfg;
    if (!loadTrinityConfig(cfg)) {
        acutPrintf(_T("\n[Trinity] TRIB: config load failed.\n"));
        return;
    }

    TrinityCore core;
    if (!core.connect(cfg.host.c_str(), cfg.user.c_str(),
                      cfg.pass.c_str(), cfg.db.c_str())) {
        acutPrintf(_T("\n[Trinity] TRIB: cannot connect to database.\n"));
        return;
    }

    auto ribs = core.loadDetailsByCategory("rib");
    if (ribs.empty()) {
        acutPrintf(_T("\n[Trinity] TRIB: no details with category='rib' found.\n"));
        return;
    }

    // Точка вставки первой планки (по умолчанию 0,0,0)
    AcGePoint3d basePnt(0.0, 0.0, 0.0);
    resbuf* promptRb = acedBuildResult(RTSTR, _T("\nInsertion point <0,0,0>: "));
    int ret = acedGetPoint(nullptr, promptRb, asDblArray(basePnt));
    acedRelResult(promptRb);
    if (ret == RTCAN) return;   // Esc — выходим без изменений чертежа

    // Шаг раскладки — по фактической длине каждой планки + зазор,
    // чтобы детали не накладывались друг на друга.
    const double GAP = 100.0;

    // Движок нужен только ради buildDetailToDb (геометрия + слои + атрибуты).
    // Файлы он создавать не будет, поэтому init() (подключение к БД) не вызываем —
    // рёбра уже загружены локальным TrinityCore выше.
    TrinityBuildEngine engine(cfg.basePath);

    AcDbDatabase* db = acdbHostApplicationServices()->workingDatabase();

    int drawn = 0, skipped = 0;
    double cursorX = basePnt.x;

    for (const auto& rib : ribs) {
        wchar_t* wCode = utf2uni(rib.code.c_str());

        // Печатаем фактические размеры — видно, как свойства влияют на деталь
        acutPrintf(_T("\n[Trinity] TRIB: %s  W=%.1f H=%.1f T=%.1f (rib.height=%.1f slotHalf=%.2f slotDepth=%.2f holeOffset=%.1f)"),
                   wCode, rib.width, rib.height, rib.thickness,
                   rib.rib.height, rib.rib.slotHalf, rib.rib.slotDepth, rib.rib.holeOffset);
        free(wCode);

        AcGePoint3d offset(cursorX - basePnt.x, basePnt.y, basePnt.z);

        if (!engine.buildDetailToDb(rib, db, offset)) {
            acutPrintf(_T(" -> SKIPPED (invalid geometry params)\n"));
            skipped++;
            continue;
        }

        acutPrintf(_T(" -> OK\n"));
        drawn++;
        cursorX += rib.width + GAP;   // следующая планка правее текущей
    }

    if (drawn > 0) {
        // ZOOM Extents, чтобы результат был виден
        acedCommandS(RTSTR, _T("_.ZOOM"), RTSTR, _T("_E"), RTNONE);
    }

    acutPrintf(_T("\n[Trinity] TRIB done: drawn=%d, skipped=%d\n"), drawn, skipped);
}