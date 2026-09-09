#pragma once

#include <cstdint>
#include <type_traits>

// 24.8 fixed-point format: 24 bits integer, 8 bits fractional
// Matches Quake's original fixed_t implementation
class fixed_t {
public:
    int32_t value;
    
    // Constructors
    fixed_t() : value(0) {}
    explicit fixed_t(int32_t v) : value(v) {}
    explicit fixed_t(double d);
    explicit fixed_t(float f);
    
    // Convert to int
    int32_t asInt() const { return value >> 8; }
    
    // Convert to double
    double asDouble() const;
    
    // Comparison operators
    constexpr bool operator==(fixed_t other) const { return value == other.value; }
    constexpr bool operator!=(fixed_t other) const { return value != other.value; }
    constexpr bool operator<(fixed_t other) const { return value < other.value; }
    constexpr bool operator>(fixed_t other) const { return value > other.value; }
    constexpr bool operator<=(fixed_t other) const { return value <= other.value; }
    constexpr bool operator>=(fixed_t other) const { return value >= other.value; }
};

// Arithmetic operators
fixed_t operator+(fixed_t a, fixed_t b);
fixed_t operator-(fixed_t a, fixed_t b);
fixed_t operator*(fixed_t a, fixed_t b);
fixed_t operator/(fixed_t a, fixed_t b);

// Compound assignment
fixed_t& operator+=(fixed_t& a, fixed_t b);
fixed_t& operator-=(fixed_t& a, fixed_t b);
fixed_t& operator*=(fixed_t& a, fixed_t b);
fixed_t& operator/=(fixed_t& a, fixed_t b);

// Unary minus
fixed_t operator-(fixed_t a);

// Utility functions
fixed_t FixedRound(fixed_t a);
fixed_t Abs(fixed_t a);
fixed_t Floor(fixed_t a);
fixed_t Ceil(fixed_t a);

// Constants
extern const fixed_t kFixedZero;
extern const fixed_t kFixedOne;
extern const fixed_t kFixedTwo;
extern const fixed_t kFixedHalf;
extern const fixed_t kFixedQuarter;
