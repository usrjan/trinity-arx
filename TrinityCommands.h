// TrinityCommands.h
#pragma once

// ============================================
// Команды AutoCAD, регистрируемые в acrxEntry:
//   TSTART — запуск таймера автосборки (5 сек)
//   TSTOP  — остановка таймера
// Реализация — в TrinityCommands.cpp.
// ============================================
void trinityRegisterCommands();

// Остановка таймера/движка — вызывается из acrxEntry при выгрузке ARX
void trinityShutdown();