// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
//
// A small JSON reader for the style packs and the test fixtures. Objects keep their key order (the style compiler
// depends on it, exactly as the JS Object.entries order does). Never used on the audio thread.
#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mm::json {

struct Value {
    enum class Type : unsigned char { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;

    bool isNull() const { return type == Type::Null; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }
    // JS truthiness, for `src.x || default`.
    bool truthy() const;

    const Value* find(std::string_view key) const;
    const Value& operator[](std::string_view key) const;  // a null value when missing
    const Value& operator[](size_t i) const;              // a null value when out of range
    size_t size() const { return isArray() ? array.size() : isObject() ? object.size() : 0; }
    double num(double fallback = 0) const { return isNumber() ? number : fallback; }
    int integer(int fallback = 0) const { return isNumber() ? static_cast<int>(number) : fallback; }
    const std::string& str() const;
};

// Parses `text`; on failure returns false and sets `error`.
bool parse(std::string_view text, Value& out, std::string* error = nullptr);

}  // namespace mm::json
