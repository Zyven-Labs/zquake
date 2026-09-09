#pragma once
#include <cstdint>

namespace zq::render {

// 8x8 bitmap font covering ASCII 32-126
// Each character is 8 bytes, one byte per row, MSB = left pixel
extern const uint8_t font8x8_basic[95][8];

}