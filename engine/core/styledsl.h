// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
//
// The style DSL compiler, ported from legacy-web/js/styledsl.js (format described there). Tested against
// engine/tests/fixtures/styles_compiled.json.
#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "json.h"
#include "style.h"

namespace mm {

// "C4" → 60, "Bb2" → 46; -1 on a bad token.
int noteNumber(std::string_view token);

bool compileStyle(const json::Value& src, Style& out, std::string* error = nullptr);

// A pack: a JSON array of style sources (or one style object). Styles that fail are skipped and reported.
bool compilePack(std::string_view jsonText, std::vector<Style>& out, std::string* error = nullptr);

}  // namespace mm
