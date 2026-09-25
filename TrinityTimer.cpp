// TrinityTimer.cpp
//
// Реализация безопасного таймера см. комментарии в TrinityTimer.h.
// Ключевой инвариант: ЛЮБАЯ работа с базой данных документа (чтение/запись)
// выполняется только внутри write-lock, захваченного на главном потоке
// AutoCAD из обработчика сообщения, а не из колбэка SetTimer.
#include "StdAfx.h"
#include "TrinityTimer.h"

// ============================================================
// ВНУТРЕННЕЕ СОСТОЯНИЕ (файл-локальное — наружу не торчит)
// ============================================================
namespace {

UINT_PTR          g_trnTimerId      = 0;      // id Windows-таймера
void (*g_trnCallback)()             = nullptr;
WNDPROC           g_trnOldWndProc   = nullptr;
bool              g_trnTickPending  = false;  // защита от наложения тиков
constexpr UINT    kTrnTimerIdMagic  = 0x5452; // «TR» — опознавательный id

// ------------------------------------------------------------
// RAII-обёртка document lock.
// AcEditorReactor::lockDocument() — единственный легальный способ
// залочить чужой/фоновый документ из стороннего кода. Деструктор
// ГАРАНТИРОВАННО снимает лок — исключение или ранний return не могут
// оставить документ заблокированным (иначе AutoCAD «зависнет» на локе).
// ------------------------------------------------------------
class TrinityDocLock
{
public:
    TrinityDocLock(AcApDocument* pDoc, bool readLock = false)
        : m_pDoc(pDoc), m_locked(false)
    {
        if (!pDoc) return;
        Acad::ErrorStatus es = acDocManager->lockDocument(
            pDoc,
            readLock ? AcAp::kRead : AcAp::kWrite,
            nullptr, nullptr, false);
        m_locked = (es == Acad::eOk);
    }

    ~TrinityDocLock()
    {
        if (m_locked && m_pDoc) {
            acDocManager->unlockDocument(m_pDoc);
        }
    }

    bool isLocked() const { return m_locked; }

private:
    // lock/unlock не копируются
    TrinityDocLock(const TrinityDocLock&) = delete;
    TrinityDocLock& operator=(const TrinityDocLock&) = delete;

    AcApDocument* m_pDoc;
    bool          m_locked;
};

// ------------------------------------------------------------
// Обработка одного тика при УЖЕ захваченном write-lock.
// Отдельная функция нужна потому, что C++-объекты с деструкторами
// (AcDbSmartPtr и т.п.) нельзя объявлять в блоке __try с __except
// (ошибка компиляции C2712): guard-function раскручивает lock здесь,
// а SEH охватывает только этот вызов.
// ------------------------------------------------------------
void trnRunCallback()
{
    if (!g_trnCallback) return;

    __try {
        g_trnCallback();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Страховка: если внутренний код всё же упадёт, exception filter
        // не даст краху унести весь acad.exe; лок снимает RAII-деструктор
        // в trnDispatchTick.
        acutPrintf(_T("\n[Trinity] Timer tick aborted by exception.\n"));
    }
}

// ------------------------------------------------------------
// Непосредственно «тик»: вызывается ТОЛЬКО из обработчика
// WM_TRINITY_TICK на главном потоке AutoCAD.
// ------------------------------------------------------------
void trnDispatchTick()
{
    if (!g_trnCallback) return;

    // Если в AutoCAD включён document lock mode, сам механизм локов
    // обеспечивает корректную сериализацию доступа к документу.
    // Если же lock mode ОТКЛЮЧЁН (например, монопольный доступ другого
    // приложения), попытка lockDocument недопустима — работать можно
    // только когда документ простаивает (idle). В этом случае проверяем
    // состояние и при занятости пропускаем тик.
    AcApDocument* pDoc = acDocManager->curDocument();
    if (!pDoc) return;

    if (!acDocManager->isLockModeEnabled()) {
        AcApDocument::DocumentStatus st = acDocManager->isDocumentIdle(pDoc);
        if (st != AcApDocument::eIsIdle) {
            // eIsNotIdle / eIsLocked / eIsBeingDestroyed —
            // писать в документ НЕЛЬЗЯ, пропускаем тик.
            return;
        }
    }

    // ГЛАВНАЯ ЗАЩИТА: write-lock документа на время всей работы с БД.
    // Держим лок строго вокруг trnRunCallback(): ни до, ни после
    // обращение к документу не происходит.
    {
        TrinityDocLock lock(pDoc, /*readLock=*/false);
        if (!lock.isLocked()) {
            // Lock получить не удалось (другое приложение / VBA / .NET
            // держит документ). НИКАКОЙ записи в документ — выходим
            // молча, ждём следующий тик.
            return;
        }

        // С этого момента мы — единственный владелец документа и находимся
        // на главном потоке AutoCAD. Работа с AcDbDatabase разрешена
        // протоколом ObjectARX.
        trnRunCallback();
    }   // <- unlockDocument() гарантированно здесь, до любого
        //    acedUpdateDisplay()/вывода в командную строку внутри callback
}

// ------------------------------------------------------------
// Подклассирование окна AutoCAD: ловим WM_TRINITY_TICK в очереди
// сообщений главного потока.
// ------------------------------------------------------------
LRESULT CALLBACK TrnWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_TRINITY_TICK) {
        trnDispatchTick();
        g_trnTickPending = false;
        return 0;
    }
    return CallWindowProc(g_trnOldWndProc, hWnd, msg, wParam, lParam);
}

// ------------------------------------------------------------
// Колбэк Windows-таймера. НЕ делает НИЧЕГО с документами —
// только переносит работу в главный поток через PostMessage.
// (При hwnd = adsw_acadMainWnd() это и так главный поток, но
// PostMessage дополнительно гарантирует, что мы не находимся
// внутри разбора критического сообщения AutoCAD.)
// ------------------------------------------------------------
VOID CALLBACK TrnTimerProc(HWND hWnd, UINT /*uMsg*/, UINT_PTR /*idEvent*/,
                           DWORD /*dwTime*/)
{
    if (g_trnTickPending) return;  // предыдущий тик ещё не обработан
    if (PostMessage(hWnd, WM_TRINITY_TICK, 0, 0)) {
        g_trnTickPending = true;
    }
}

} // anonymous namespace

// ============================================================
// ПУБЛИЧНЫЙ API
// ============================================================
UINT_PTR StartTrinityTimer(UINT intervalMs, void (*callback)())
{
    // Уже активен — сначала корректно снимаем старый.
    if (g_trnTimerId != 0 || callback == nullptr) {
        if (g_trnTimerId != 0) StopTrinityTimer();
        if (callback == nullptr) return 0;
    }

    g_trnCallback = callback;

    HWND hAcad = adsw_acadMainWnd();
    if (!hAcad) {
        acutPrintf(_T("\n[Trinity] Cannot resolve AutoCAD main window.\n"));
        g_trnCallback = nullptr;
        return 0;
    }

    // Подклассируем окно AutoCAD для приёма WM_TRINITY_TICK.
    g_trnOldWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtr(hAcad, GWLP_WNDPROC,
                         reinterpret_cast<LONG_PTR>(TrnWndProc)));
    if (!g_trnOldWndProc) {
        acutPrintf(_T("\n[Trinity] SetWindowLongPtr failed.\n"));
        g_trnCallback = nullptr;
        return 0;
    }

    // Таймер привязываем к окну AutoCAD (не NULL!), чтобы WM_TIMER
    // доставлялся в очередь его потока.
    UINT_PTR id = SetTimer(hAcad, kTrnTimerIdMagic, intervalMs, TrnTimerProc);
    if (id == 0) {
        // Откатываем подклассирование — иначе окно останется висеть на
        // нашем WndProc без таймера.
        SetWindowLongPtr(hAcad, GWLP_WNDPROC,
                         reinterpret_cast<LONG_PTR>(g_trnOldWndProc));
        g_trnOldWndProc = nullptr;
        g_trnCallback = nullptr;
        acutPrintf(_T("\n[Trinity] SetTimer failed.\n"));
        return 0;
    }

    g_trnTimerId = id;
    return id;
}

void StopTrinityTimer()
{
    HWND hAcad = adsw_acadMainWnd();

    if (g_trnTimerId != 0) {
        if (hAcad) KillTimer(hAcad, g_trnTimerId);
        g_trnTimerId = 0;
    }

    if (hAcad && g_trnOldWndProc) {
        SetWindowLongPtr(hAcad, GWLP_WNDPROC,
                         reinterpret_cast<LONG_PTR>(g_trnOldWndProc));
        g_trnOldWndProc = nullptr;
    }

    // Вычищаем возможное «зависшее» сообщение из очереди, чтобы оно
    // не сработало после остановки.
    if (hAcad) {
        MSG m;
        while (PeekMessage(&m, hAcad, WM_TRINITY_TICK, WM_TRINITY_TICK,
                           PM_REMOVE)) { /* drain */ }
    }

    g_trnTickPending = false;
    g_trnCallback = nullptr;
}

bool IsTrinityTimerActive()
{
    return g_trnTimerId != 0;
}
