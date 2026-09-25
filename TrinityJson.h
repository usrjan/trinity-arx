// TrinityJson.h
#pragma once
#include "StdAfx.h"

// ============================================
// Минимальный парсер подмножества JSON
// (объекты, массивы, строки, числа, true/false/null).
// Заменяет ручные поиски подстрок вида "\"pos\"" / sscanf_s
// по всему коду плагина.
// ============================================
class TrinityJson {
public:
    struct Value;
    using Object = std::map<std::string, Value>;
    using Array  = std::vector<Value>;

    enum class Type { Null, Bool, Number, String, Array, Object };

    struct Value {
        Type    type  = Type::Null;
        bool    boolValue   = false;
        double  numberValue = 0.0;
        std::string stringValue;
        Array   arrayValue;
        Object  objectValue;

        bool isNull()   const { return type == Type::Null; }
        bool isObject() const { return type == Type::Object; }
        bool isArray()  const { return type == Type::Array; }
        bool isString() const { return type == Type::String; }
        bool isNumber() const { return type == Type::Number; }

        // Доступ к полям объекта (Null, если поля нет)
        const Value& operator[](const std::string& key) const;
        // Доступ к элементам массива (Null, если индекса нет)
        const Value& operator[](size_t index) const;

        size_t size() const;  // элементов массива / полей объекта

        double       asDouble(double def = 0.0) const;
        int          asInt(int def = 0) const;
        std::string  asString(const std::string& def = {}) const;
};

    // Парсит строку; при ошибке возвращает Value типа Null.
    static Value parse(const std::string& text);

private:
    struct Parser;
};
