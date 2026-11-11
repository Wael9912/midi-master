// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
#include "json.h"

#include <cstdlib>

namespace mm::json {
namespace {

const Value& nullValue() {
    static const Value v;
    return v;
}

const std::string& emptyString() {
    static const std::string s;
    return s;
}

struct Parser {
    std::string_view s;
    size_t i = 0;
    std::string err;

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
    }
    bool fail(const char* what) {
        if (err.empty()) err = std::string(what) + " at " + std::to_string(i);
        return false;
    }
    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& cp) {
        if (i + 4 > s.size()) return fail("short \\u escape");
        cp = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i++];
            cp <<= 4;
            if (c >= '0' && c <= '9') cp |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') cp |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') cp |= static_cast<unsigned>(c - 'A' + 10);
            else return fail("bad \\u escape");
        }
        return true;
    }
    bool string(std::string& out) {
        ++i;  // opening quote
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) break;
            const char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        unsigned lo = 0;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool value(Value& v, int depth) {
        if (depth > 64) return fail("too deep");
        ws();
        if (i >= s.size()) return fail("unexpected end");
        const char c = s[i];
        if (c == '{') {
            v.type = Value::Type::Object;
            ++i;
            ws();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            for (;;) {
                ws();
                if (i >= s.size() || s[i] != '"') return fail("expected key");
                std::string key;
                if (!string(key)) return false;
                ws();
                if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                ++i;
                Value item;
                if (!value(item, depth + 1)) return false;
                v.object.emplace_back(std::move(key), std::move(item));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.type = Value::Type::Array;
            ++i;
            ws();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            for (;;) {
                Value item;
                if (!value(item, depth + 1)) return false;
                v.array.push_back(std::move(item));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { v.type = Value::Type::String; return string(v.string); }
        if (s.compare(i, 4, "true") == 0) { v.type = Value::Type::Bool; v.boolean = true; i += 4; return true; }
        if (s.compare(i, 5, "false") == 0) { v.type = Value::Type::Bool; i += 5; return true; }
        if (s.compare(i, 4, "null") == 0) { i += 4; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const size_t start = i;
            ++i;
            while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' || s[i] == 'E' || s[i] == '+' ||
                                    s[i] == '-'))
                ++i;
            const std::string num(s.substr(start, i - start));
            char* end = nullptr;
            v.type = Value::Type::Number;
            v.number = std::strtod(num.c_str(), &end);
            if (!end || *end != '\0') return fail("bad number");
            return true;
        }
        return fail("unexpected character");
    }
};

}  // namespace

bool Value::truthy() const {
    switch (type) {
        case Type::Null: return false;
        case Type::Bool: return boolean;
        case Type::Number: return number != 0 && number == number;
        case Type::String: return !string.empty();
        default: return true;
    }
}

const Value* Value::find(std::string_view key) const {
    if (!isObject()) return nullptr;
    for (const auto& kv : object)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

const Value& Value::operator[](std::string_view key) const {
    const Value* v = find(key);
    return v ? *v : nullValue();
}

const Value& Value::operator[](size_t i) const { return isArray() && i < array.size() ? array[i] : nullValue(); }

const std::string& Value::str() const { return isString() ? string : emptyString(); }

bool parse(std::string_view text, Value& out, std::string* error) {
    Parser p;
    p.s = text;
    out = Value{};
    bool ok = p.value(out, 0);
    if (ok) {
        p.ws();
        if (p.i != text.size()) ok = p.fail("trailing characters");
    }
    if (!ok && error) *error = p.err;
    return ok;
}

}  // namespace mm::json
