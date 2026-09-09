#include "core/fixed/fixed_t.hpp"

// Fixed-point to double conversion
static double kInv256 = 1.0 / 256.0;

fixed_t::fixed_t(double d)
    : value(static_cast<int32_t>(d * 256.0)) {}

fixed_t::fixed_t(float f)
    : value(static_cast<int32_t>(f * 256.0f)) {}

double fixed_t::asDouble() const {
    return static_cast<double>(value) * kInv256;
}

// Arithmetic operators
fixed_t operator+(fixed_t a, fixed_t b) {
    return fixed_t(a.value + b.value);
}

fixed_t operator-(fixed_t a, fixed_t b) {
    return fixed_t(a.value - b.value);
}

fixed_t operator*(fixed_t a, fixed_t b) {
    return fixed_t(static_cast<int32_t>((static_cast<int64_t>(a.value) * b.value) >> 8));
}

fixed_t operator/(fixed_t a, fixed_t b) {
    if (b.value == 0) return fixed_t(0x7FFFFFFF);
    return fixed_t(static_cast<int32_t>((static_cast<int64_t>(a.value) << 8) / b.value));
}

// Compound assignment
fixed_t& operator+=(fixed_t& a, fixed_t b) {
    a.value += b.value;
    return a;
}

fixed_t& operator-=(fixed_t& a, fixed_t b) {
    a.value -= b.value;
    return a;
}

fixed_t& operator*=(fixed_t& a, fixed_t b) {
    a.value = static_cast<int32_t>((static_cast<int64_t>(a.value) * b.value) >> 8);
    return a;
}

fixed_t& operator/=(fixed_t& a, fixed_t b) {
    if (b.value != 0) {
        a.value = static_cast<int32_t>((static_cast<int64_t>(a.value) << 8) / b.value);
    }
    return a;
}

// Unary minus
fixed_t operator-(fixed_t a) {
    return fixed_t(-a.value);
}

// Utility functions
fixed_t FixedRound(fixed_t a) {
    if (a.value >= 0) {
        return fixed_t((a.value + 128) >> 8);
    } else {
        return fixed_t((a.value - 128) >> 8);
    }
}

fixed_t Abs(fixed_t a) {
    return fixed_t(a.value < 0 ? -a.value : a.value);
}

fixed_t Floor(fixed_t a) {
    return fixed_t(a.value >> 8);
}

fixed_t Ceil(fixed_t a) {
    int32_t result = a.value >> 8;
    if ((a.value & 0xFF) != 0 && a.value >= 0) {
        result++;
    }
    return fixed_t(result);
}

// Constants
const fixed_t kFixedZero(0);
const fixed_t kFixedOne(256);
const fixed_t kFixedTwo(512);
const fixed_t kFixedHalf(128);
const fixed_t kFixedQuarter(64);
