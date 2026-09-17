// TrinityCommands.cpp
#include "StdAfx.h"
#include "TrinityCommands.h"
#include "TrinityConfig.h"
#include "DynamicShieldBuilder.h"
#include <aced.h>
#include <dbents.h>
#include <dbobjptr.h>
#include <geassign.h>

TrinityBuildEngine* g_engine = nullptr;
UINT_PTR g_timerId = 0;

// Forward declaration
Acad::ErrorStatus postToModelSpace(AcDbEntity* pEnt, AcGePoint3d insPoint = AcGePoint3d::kOrigin);

/// <summary>
/// Тестовая команда для создания динамического щита
/// Команда: TRINITY_TEST_SHIELD
/// </summary>
static void TrinityTestShield()
{
    acutPrintf(_T("\n--- Тест динамического щита ---\n"));

    // Параметры тестового щита
    double length = 2000.0;   // Длина (L)
    double width = 500.0;     // Ширина (W)
    double thickness = 40.0;  // Толщина (T)
    
    // Добавляем два сквозных отверстия для теста
    std::vector<AcGePoint2d> holes;
    holes.push_back(AcGePoint2d(500.0, 250.0)); // Отверстие 1
    holes.push_back(AcGePoint2d(1500.0, 250.0)); // Отверстие 2

    try 
    {
        // 1. Создаем блок в базе данных
        AcDbObjectId blockId = DynamicShieldBuilder::createDynamicShieldBlock(
            length, 
            width, 
            thickness, 
            holes, 
            _T("TEST_SHIELD_2000x500")
        );

        if (blockId.isNull()) {
            acutPrintf(_T("\nОшибка: Не удалось создать блок.\n"));
            return;
        }

        acutPrintf(_T("\nБлок создан успешно. ID: %ld\n"), blockId.asLong());

        // 2. Вставляем блок в модель
        AcGePoint3d insertionPoint(0.0, 0.0, 0.0);
        AcDbBlockReference* pRef = new AcDbBlockReference(insertionPoint, blockId);
        
        if (pRef) {
            Acad::ErrorStatus es = postToModelSpace(pRef, insertionPoint);
            if (es == Acad::eOk) {
                acutPrintf(_T("\nЩит вставлен в чертеж!\n"));
                acutPrintf(_T("Размеры: %.0f x %.0f x %.0f мм\n"), length, width, thickness);
                acutPrintf(_T("Отверстий: %d\n"), (int)holes.size());
                acutPrintf(_T("\nВыберите объект и посмотрите палитру Свойства (Ctrl+1).\n"));
                acutPrintf(_T("Попробуйте изменить параметр 'Length' в палитре свойств.\n"));
            } else {
                acutPrintf(_T("\nОшибка вставки в модель: %d\n"), es);
                delete pRef;
            }
        }
    }
    catch (const std::exception& e) {
        acutPrintf(_T("\nИсключение: %s\n"), e.what());
    }
    catch (...) {
        acutPrintf(_T("\nНеизвестная ошибка при создании щита.\n"));
    }
}

// ============================================
// CALLBACK ТАЙМЕРА
// ============================================
void CALLBACK TimerProc(HWND, UINT, UINT_PTR, DWORD) {
    trinityProcess();
}

// ============================================
// TRINITY_START — запуск таймера
// ============================================
void trinityStart() {
    if (g_timerId != 0) {
        acutPrintf(_T("\n[Trinity] Timer already running\n"));
        return;
    }

    // Загружаем конфигурацию из файла
    const std::string configPath = "trinity_config.json";
    TrinityConfig::loadFromFile(configPath);

    if (!g_engine) {
        const auto& pathConfig = TrinityConfig::getPathConfig();
        g_engine = new TrinityBuildEngine(pathConfig.basePath);

        const auto& dbConfig = TrinityConfig::getDbConfig();
        if (!g_engine->init(dbConfig.host.c_str(), 
                            dbConfig.user.c_str(), 
                            dbConfig.password.c_str(), 
                            dbConfig.database.c_str())) {
            acutPrintf(_T("\n[Trinity] Failed to connect to database\n"));
            delete g_engine;
            g_engine = nullptr;
            return;
        }
    }

    const auto& settingsConfig = TrinityConfig::getSettingsConfig();
    g_timerId = SetTimer(NULL, NULL, settingsConfig.timerIntervalMs, TimerProc);

    acutPrintf(_T("\n[Trinity] Timer started. Interval: %d ms\n"), settingsConfig.timerIntervalMs);
}

// ============================================
// TRINITY_STOP — остановка
// ============================================
void trinityStop() {
    if (g_timerId == 0) {
        acutPrintf(_T("\n[Trinity] Timer not running\n"));
        return;
    }

    KillTimer(NULL, g_timerId);
    g_timerId = 0;

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