// TrinityJson.cpp
#include "StdAfx.h"
#include "TrinityJson.h"

// ============================================
// Статический Null-значение для «не найденного» доступа
// ============================================
static const TrinityJson::Value& nullValue() {
    static const TrinityJson::Value s_null;
    return s_null;
}

const TrinityJson::Value& TrinityJson::Value::operator[](const std::string& key) const {
    if (type != Type::Object) return nullValue();
    auto it = objectValue.find(key);
    return it != objectValue.end() ? it->second : nullValue();
}

const TrinityJson::Value& TrinityJson::Value::operator[](size_t index) const {
    if (type != Type::Array || index >= arrayValue.size()) return nullValue();
    return arrayValue[index];
}

size_t TrinityJson::Value::size() const {
    switch (type) {
        case Type::Array:  return arrayValue.size();
        case Type::Object: return objectValue.size();
        case Type::String: return stringValue.size();
        default:           return 0;
    }
}

double TrinityJson::Value::asDouble(double def) const {
    if (type == Type::Number) return numberValue;
    if (type == Type::Bool)   return boolValue ? 1.0 : 0.0;
    return def;
}

int TrinityJson::Value::asInt(int def) const {
    if (type == Type::Number) return static_cast<int>(numberValue);
    if (type == Type::Bool)   return boolValue ? 1 : 0;
    return def;
}

std::string TrinityJson::Value::asString(const std::string& def) const {
    return type == Type::String ? stringValue : def;
}

// ============================================
// РЕКУРСИВНЫЙ СПУСК ПОСЛЕДОВАТЕЛЬНОСТИ
// ============================================
namespace {

constexpr int kMaxDepth = 64;

struct Cursor {
    const char* p   = nullptr;
    const char* end = nullptr;
    bool        ok  = true;

    void skipWs() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    bool eof() const { return p >= end; }
    char peek() const { return p < end ? *p : '\0'; }
    void advance(size_t n = 1) { p += n; if (p > end) p = end; }
};

bool parseValue(Cursor& c, TrinityJson::Value& out, int depth);

bool parseStringRaw(Cursor& c, std::string& out) {
    if (c.peek() != '"') { c.ok = false; return false; }
    c.advance();
    out.clear();
    while (!c.eof()) {
        char ch = *c.p;
        if (ch == '"') { c.advance(); return true; }
        if (ch == '\\') {
            c.advance();
            if (c.eof()) break;
            char esc = *c.p++;
            switch (esc) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    // \uXXXX — декодируем в UTF-8 (без суррогатных пар: U+FFFD)
                    unsigned cp = 0;
                    for (int i = 0; i < 4 && !c.eof(); ++i) {
                        char h = *c.p++;
                        unsigned v;
                        if (h >= '0' && h <= '9')      v = h - '0';
                        else if (h >= 'a' && h <= 'f') v = h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') v = h - 'A' + 10;
                        else { c.ok = false; return false; }
                        cp = cp * 16 + v;
                    }
                    if (cp < 0x80) {
                        out += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        out += static_cast<char>(0xC0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: c.ok = false; return false;
            }
        } else {
            out += ch;
            c.advance();
        }
    }
    c.ok = false;
    return false;
}

bool parseNumber(Cursor& c, double& out) {
    const char* start = c.p;
    if (c.peek() == '-' || c.peek() == '+') c.advance();
    while (!c.eof()) {
        char ch = *c.p;
        if ((ch >= '0' && ch <= '9') || ch == '.' || ch == 'e' || ch == 'E' ||
            ch == '+' || ch == '-') {
            c.advance();
        } else {
            break;
        }
    }
    std::string num(start, static_cast<size_t>(c.p - start));
    if (num.empty() || num == "-") { c.ok = false; return false; }
    try {
        size_t pos = 0;
        out = std::stod(num, &pos);
        if (pos != num.size()) { c.ok = false; return false; }
    } catch (...) {
        c.ok = false;
        return false;
    }
    return true;
}

bool parseArray(Cursor& c, TrinityJson::Array& out, int depth) {
    if (c.peek() != '[') { c.ok = false; return false; }
    c.advance();
    c.skipWs();
    if (c.peek() == ']') { c.advance(); return true; }

    while (c.ok && !c.eof()) {
        TrinityJson::Value item;
        if (!parseValue(c, item, depth + 1)) return false;
        out.push_back(std::move(item));
        c.skipWs();
        if (c.peek() == ',') { c.advance(); c.skipWs(); continue; }
        if (c.peek() == ']') { c.advance(); return true; }
        c.ok = false;
        return false;
    }
    c.ok = false;
    return false;
}

bool parseObject(Cursor& c, TrinityJson::Object& out, int depth) {
    if (c.peek() != '{') { c.ok = false; return false; }
    c.advance();
    c.skipWs();
    if (c.peek() == '}') { c.advance(); return true; }

    while (c.ok && !c.eof()) {
        std::string key;
        if (!parseStringRaw(c, key)) return false;
        c.skipWs();
        if (c.peek() != ':') { c.ok = false; return false; }
        c.advance();
        c.skipWs();
        TrinityJson::Value val;
        if (!parseValue(c, val, depth + 1)) return false;
        out[key] = std::move(val);
        c.skipWs();
        if (c.peek() == ',') { c.advance(); c.skipWs(); continue; }
        if (c.peek() == '}') { c.advance(); return true; }
        c.ok = false;
        return false;
    }
    c.ok = false;
    return false;
}

bool parseLiteral(Cursor& c, TrinityJson::Value& out) {
    auto match = [&](const char* lit) {
        size_t n = strlen(lit);
        if (static_cast<size_t>(c.end - c.p) >= n && strncmp(c.p, lit, n) == 0) {
            c.advance(n);
            return true;
        }
        return false;
    };
    if (match("true"))  { out.type = TrinityJson::Type::Bool;   out.boolValue = true;  return true; }
    if (match("false")) { out.type = TrinityJson::Type::Bool;   out.boolValue = false; return true; }
    if (match("null"))  { out.type = TrinityJson::Type::Null;   return true; }
    c.ok = false;
    return false;
}

bool parseValue(Cursor& c, TrinityJson::Value& out, int depth) {
    if (depth > kMaxDepth) { c.ok = false; return false; }
    c.skipWs();
    switch (c.peek()) {
        case '"': {
            out.type = TrinityJson::Type::String;
            return parseStringRaw(c, out.stringValue);
        }
        case '{': {
            out.type = TrinityJson::Type::Object;
            return parseObject(c, out.objectValue, depth);
        }
        case '[': {
            out.type = TrinityJson::Type::Array;
            return parseArray(c, out.arrayValue, depth);
        }
        case 't': case 'f': case 'n':
            return parseLiteral(c, out);
        default: {
            out.type = TrinityJson::Type::Number;
            return parseNumber(c, out.numberValue);
        }
    }
}

} // namespace

TrinityJson::Value TrinityJson::parse(const std::string& text) {
    Value result;
    Cursor c;
    c.p   = text.c_str();
    c.end = text.c_str() + text.size();
    if (!parseValue(c, result, 0) || !c.ok) {
        return Value{}; // Null — значение по умолчанию для ошибок
    }
    return result;
}
