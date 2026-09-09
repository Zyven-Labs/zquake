#pragma once

#include "core/fixed/fixed_t.hpp"

namespace zq::math {

// Trigonometric functions (fixed-point)
fixed_t Sin(fixed_t angle);
fixed_t Cos(fixed_t angle);
fixed_t Tan(fixed_t angle);

// Inverse trigonometric functions
fixed_t ATan2(fixed_t y, fixed_t x);

// Square root (integer only, fast)
fixed_t Sqrt(fixed_t x);

// Power of two helpers
fixed_t Pow2(fixed_t x, int exponent);

// Clamp to range
fixed_t Clamp(fixed_t value, fixed_t min, fixed_t max);

// Lerp (linear interpolation)
fixed_t Lerp(fixed_t a, fixed_t b, fixed_t t);

// Angle normalization (0 to 65536 Quake units)
fixed_t AngleNormalize(fixed_t angle);

// Degrees to radians
fixed_t DegToRad(fixed_t deg);

// Radians to degrees
fixed_t RadToDeg(fixed_t rad);

// Vector3 (fixed-point)
struct Vec3 {
    fixed_t x, y, z;
    
    constexpr Vec3() : x(0), y(0), z(0) {}
    constexpr explicit Vec3(fixed_t v) : x(v), y(v), z(v) {}
    constexpr Vec3(fixed_t x, fixed_t y, fixed_t z) : x(x), y(y), z(z) {}
    
    // Element access
    constexpr fixed_t& operator[](int i) { return (*(&x + i)); }
    constexpr const fixed_t& operator[](int i) const { return (*(&x + i)); }
    
    // Operators
    Vec3 operator+(Vec3 b) const;
    Vec3 operator-(Vec3 b) const;
    Vec3 operator*(fixed_t s) const;
    Vec3 operator/(fixed_t s) const;
    
    Vec3& operator+=(Vec3 b);
    Vec3& operator-=(Vec3 b);
    Vec3& operator*=(fixed_t s);
    Vec3& operator/=(fixed_t s);
    
    Vec3 operator-() const;
    
    // Comparison
    bool operator==(Vec3 b) const;
    bool operator!=(Vec3 b) const;
    
    // Length
    fixed_t Length() const;
    fixed_t Length2D() const;
    
    // Dot product
    fixed_t Dot(Vec3 b) const;
    
    // Cross product
    Vec3 Cross(Vec3 b) const;
    
    // Normalize
    void Normalize();
    void Normalize2D();
    
    // Clamp to min/max
    void Clamp(fixed_t min, fixed_t max);
    
    // Lerp
    static Vec3 Lerp3(Vec3 a, Vec3 b, fixed_t t);
};

// Matrix4x4 (fixed-point)
struct Mat4 {
    fixed_t m[4][4];
    
    Mat4() {
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                m[i][j] = (i == j) ? kFixedOne : kFixedZero;
    }
    
    static Mat4 Identity();
    static Mat4 Translation(fixed_t x, fixed_t y, fixed_t z);
    static Mat4 Scale(fixed_t x, fixed_t y, fixed_t z);
    static Mat4 RotationX(fixed_t angle);
    static Mat4 RotationY(fixed_t angle);
    static Mat4 RotationZ(fixed_t angle);
    
    Mat4 operator*(Mat4 b) const;
    Vec3 TransformVec3(Vec3 v) const;
    Vec3 TransformPoint3(Vec3 v) const;
};

// Utility
constexpr fixed_t FixedFromInt(int i) { return fixed_t(i << 8); }
constexpr fixed_t FixedRoundToZero(fixed_t x) { return fixed_t(x.value >> 8); }
fixed_t FixedSqrt(fixed_t x);

} // namespace zq::math
