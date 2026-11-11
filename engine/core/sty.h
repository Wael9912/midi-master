// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
//
// Yamaha style files (.sty / .prs / .sst, SFF1 and SFF2) → compiled styles. Ported from legacy-web/js/sty.js (format
// notes there) and tested against engine/tests/fixtures/sty.json.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "style.h"

namespace mm {

// "yamaha" | "org24" | "korg" | "unknown"
const char* sniffFormat(const uint8_t* b, size_t n);

// One or two styles (A/B and, when the file has Main C, a second "(C/D)" style). On failure returns false and sets
// `error` to "org24", "korg", "notYamaha" (the app shows a friendly message for those) or a parse message.
bool convertSty(const uint8_t* b, size_t n, const std::string& id, const std::string& name, std::vector<Style>& out,
                std::string* error = nullptr);

}  // namespace mm
