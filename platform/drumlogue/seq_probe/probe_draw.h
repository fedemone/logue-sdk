#pragma once
// seq_probe — 16x16 1-bpp bitmaps in the drumlogue format.
//
// platform/drumlogue/README.md, "Bitmaps": 32 bytes, two per row from the
// top; each byte is read from its least significant bit to its most, left
// to right, so pixel x of row y is bit (x & 7) of byte y * 2 + (x >> 3).
// 1 = lit.
#include <cstdint>
#include <cstring>

namespace probe {

// 3x5 digits 0-9, rows top-down, bit 2 = left pixel.  At namespace scope
// (internal linkage): a static member array would need an out-of-class
// definition under the SDK's C++14.
static constexpr uint8_t kDigits[50] = {
    7, 5, 5, 5, 7,   2, 6, 2, 2, 7,   7, 1, 7, 4, 7,   7, 1, 7, 1, 7,
    5, 5, 7, 1, 1,   7, 4, 7, 1, 7,   7, 4, 7, 5, 7,   7, 1, 1, 1, 1,
    7, 5, 7, 5, 7,   7, 5, 7, 1, 7,
};

struct Bmp {
    uint8_t px[32];

    void clear() { std::memset(px, 0, sizeof(px)); }

    void set(int x, int y) {
        if (x < 0 || x > 15 || y < 0 || y > 15) return;
        px[y * 2 + (x >> 3)] |= static_cast<uint8_t>(1u << (x & 7));
    }

    bool get(int x, int y) const {
        return (px[y * 2 + (x >> 3)] >> (x & 7)) & 1u;
    }

    void fill(int x0, int y0, int x1, int y1) {   // inclusive
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) set(x, y);
    }

    // A 3x5 digit with its top-left corner at (x, y).
    void digit(int x, int y, int d) {
        static_assert(sizeof(kDigits) == 50, "ten digits of five rows");
        if (d < 0 || d > 9) return;
        for (int r = 0; r < 5; ++r)
            for (int c = 0; c < 3; ++c)
                if ((kDigits[d * 5 + r] >> (2 - c)) & 1u) set(x + c, y + r);
    }

    // Two digits (00-99), right-aligned to x_right.
    void number2(int x_right, int y, int v) {
        if (v < 0) v = 0;
        if (v > 99) v = 99;
        digit(x_right - 2, y, v % 10);
        digit(x_right - 6, y, v / 10);
    }
};

}  // namespace probe
