// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Wael Abdalla — Midi Master
#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace mm {

// Lock-free single-producer / single-consumer ring (UI thread → audio thread). Capacity must be a power of two.
template <typename T, size_t Capacity>
class SpscQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    bool push(const T& v) {
        const size_t w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) >= Capacity) return false;  // full
        buf_[w & (Capacity - 1)] = v;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& out) {
        const size_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;  // empty
        out = buf_[r & (Capacity - 1)];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Capacity> buf_{};
    alignas(64) std::atomic<size_t> write_{0};
    alignas(64) std::atomic<size_t> read_{0};
};

}  // namespace mm
