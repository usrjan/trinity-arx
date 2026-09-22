// TrinityMemory.cpp
// Реализация методов умных указателей для TrinityNeuron
#include "StdAfx.h"
#include "TrinityMemory.h"
#include "TrinityCore.h"  // Для полного определения TrinityNeuron

// Реализация деструктора TrinityNeuronPtr
TrinityNeuronPtr::~TrinityNeuronPtr() {
    delete m_ptr;
}

// Реализация оператора перемещения TrinityNeuronPtr
TrinityNeuronPtr& TrinityNeuronPtr::operator=(TrinityNeuronPtr&& other) noexcept {
    if (this != &other) {
        delete m_ptr;
        m_ptr = other.m_ptr;
        other.m_ptr = nullptr;
    }
    return *this;
}

// Реализация метода reset для TrinityNeuronPtr
void TrinityNeuronPtr::reset(TrinityNeuron* ptr) {
    delete m_ptr;
    m_ptr = ptr;
}
