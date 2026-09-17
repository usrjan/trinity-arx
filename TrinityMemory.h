// TrinityMemory.h
// RAII-обёртки для безопасного управления памятью (этап разработки)
#pragma once
#include "StdAfx.h"

// ============================================================
// УМНЫЙ УКАЗАТЕЛЬ ДЛЯ TrinityNeuron
// ============================================================
// Автоматически удаляет нейрон при выходе из области видимости
// Пример использования:
//   TrinityNeuronPtr pNeuron = m_core.loadNeuronByCode(code);
//   if (!pNeuron) return;
//   // Не нужно вызывать delete - удалится автоматически
// ============================================================
class TrinityNeuronPtr {
private:
    TrinityNeuron* m_ptr;

public:
    explicit TrinityNeuronPtr(TrinityNeuron* ptr = nullptr) : m_ptr(ptr) {}
    
    // Запрет копирования
    TrinityNeuronPtr(const TrinityNeuronPtr&) = delete;
    TrinityNeuronPtr& operator=(const TrinityNeuronPtr&) = delete;
    
    // Разрешение перемещения
    TrinityNeuronPtr(TrinityNeuronPtr&& other) noexcept : m_ptr(other.m_ptr) {
        other.m_ptr = nullptr;
    }
    
    TrinityNeuronPtr& operator=(TrinityNeuronPtr&& other) noexcept {
        if (this != &other) {
            delete m_ptr;
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
        }
        return *this;
    }
    
    ~TrinityNeuronPtr() {
        delete m_ptr;
    }
    
    // Операторы доступа
    TrinityNeuron* get() const { return m_ptr; }
    TrinityNeuron* operator->() const { return m_ptr; }
    TrinityNeuron& operator*() const { return *m_ptr; }
    operator bool() const { return m_ptr != nullptr; }
    
    // Освобождение без удаления
    TrinityNeuron* release() {
        TrinityNeuron* temp = m_ptr;
        m_ptr = nullptr;
        return temp;
    }
    
    // Сброс и замена
    void reset(TrinityNeuron* ptr = nullptr) {
        delete m_ptr;
        m_ptr = ptr;
    }
};

// ============================================================
// RAII-ОБЁРТКА ДЛЯ AcDbDatabase
// ============================================================
// Автоматически удаляет базу данных при выходе из области видимости
// Пример использования:
//   DatabasePtr tempDb = new AcDbDatabase(Adesk::kTrue, Adesk::kTrue);
//   if (!tempDb) return nullptr;
//   // Не нужно вызывать delete - удалится автоматически
// ============================================================
class DatabasePtr {
private:
    AcDbDatabase* m_ptr;

public:
    explicit DatabasePtr(AcDbDatabase* ptr = nullptr) : m_ptr(ptr) {}
    
    // Запрет копирования
    DatabasePtr(const DatabasePtr&) = delete;
    DatabasePtr& operator=(const DatabasePtr&) = delete;
    
    // Разрешение перемещения
    DatabasePtr(DatabasePtr&& other) noexcept : m_ptr(other.m_ptr) {
        other.m_ptr = nullptr;
    }
    
    DatabasePtr& operator=(DatabasePtr&& other) noexcept {
        if (this != &other) {
            delete m_ptr;
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
        }
        return *this;
    }
    
    ~DatabasePtr() {
        delete m_ptr;
    }
    
    // Операторы доступа
    AcDbDatabase* get() const { return m_ptr; }
    AcDbDatabase* operator->() const { return m_ptr; }
    AcDbDatabase& operator*() const { return *m_ptr; }
    operator bool() const { return m_ptr != nullptr; }
    
    // Освобождение без удаления (для wblock и других операций)
    AcDbDatabase* release() {
        AcDbDatabase* temp = m_ptr;
        m_ptr = nullptr;
        return temp;
    }
    
    // Сброс и замена
    void reset(AcDbDatabase* ptr = nullptr) {
        delete m_ptr;
        m_ptr = ptr;
    }
};

// ============================================================
// RAII-ОБЁРТКА ДЛЯ AcDbEntity (базовый класс)
// ============================================================
// ВНИМАНИЕ: Использовать с осторожностью!
// ObjectARX требует вызова close() перед erase()/delete
// Эта обёртка только для случаев, когда entity ещё не добавлен в базу
// ============================================================
template<typename T>
class EntityPtr {
private:
    T* m_ptr;

public:
    explicit EntityPtr(T* ptr = nullptr) : m_ptr(ptr) {}
    
    // Запрет копирования
    EntityPtr(const EntityPtr&) = delete;
    EntityPtr& operator=(const EntityPtr&) = delete;
    
    // Разрешение перемещения
    template<typename U>
    EntityPtr(EntityPtr<U>&& other) noexcept : m_ptr(static_cast<T*>(other.release())) {}
    
    ~EntityPtr() {
        if (m_ptr) {
            // Если объект ещё не закрыт - закрываем
            // Если объект уже в базе - он будет удалён через erase()
            if (m_ptr->isWriteEnabled() || m_ptr->isReadEnabled()) {
                m_ptr->close();
            }
            delete m_ptr;
        }
    }
    
    T* get() const { return m_ptr; }
    T* operator->() const { return m_ptr; }
    T& operator*() const { return *m_ptr; }
    operator bool() const { return m_ptr != nullptr; }
    
    T* release() {
        T* temp = m_ptr;
        m_ptr = nullptr;
        return temp;
    }
    
    void reset(T* ptr = nullptr) {
        if (m_ptr && (m_ptr->isWriteEnabled() || m_ptr->isReadEnabled())) {
            m_ptr->close();
        }
        delete m_ptr;
        m_ptr = ptr;
    }
};

// Специализация для AcDb3dSolid
using SolidPtr = EntityPtr<AcDb3dSolid>;

// Специализация для AcDbPolyline
using PolylinePtr = EntityPtr<AcDbPolyline>;

// Специализация для AcDbCircle
using CirclePtr = EntityPtr<AcDbCircle>;

// Специализация для AcDbBlockReference
using BlockReferencePtr = EntityPtr<AcDbBlockReference>;

// Специализация для AcDbAttributeDefinition
using AttributeDefPtr = EntityPtr<AcDbAttributeDefinition>;

// Специализация для AcDbLayerTableRecord
using LayerRecordPtr = EntityPtr<AcDbLayerTableRecord>;
