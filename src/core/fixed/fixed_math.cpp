#include "core/fixed/fixed_math.hpp"
#include "core/fixed/fixed_tables.hpp"

namespace zq::math {

fixed_t Sin(fixed_t angle) {
    const auto& tables = FixedTables_Get();
    int32_t index = (angle.value >> 16) & 0x3FFF;
    if (index < 0) index += 16384;
    
    int32_t frac = (angle.value >> 8) & 0xFF;
    if (frac == 0) {
        return fixed_t(tables.sin[index]);
    }
    
    int32_t next = tables.sin[(index + 1) & 0x3FFF];
    return fixed_t((tables.sin[index] * (256 - frac)) + (next * frac));
}

fixed_t Cos(fixed_t angle) {
    const auto& tables = FixedTables_Get();
    int32_t index = (angle.value >> 16) & 0x3FFF;
    if (index < 0) index += 16384;
    int32_t frac = (angle.value >> 8) & 0xFF;
    if (frac == 0) {
        return fixed_t(tables.cos[index]);
    }
    int32_t next = tables.cos[(index + 1) & 0x3FFF];
    return fixed_t((tables.cos[index] * (256 - frac)) + (next * frac));
}

fixed_t Tan(fixed_t angle) {
    fixed_t s = Sin(angle);
    fixed_t c = Cos(angle);
    return s / c;
}

fixed_t ATan2(fixed_t y, fixed_t x) {
    const auto& tables = FixedTables_Get();
    
    if (x.value == 0 && y.value == 0) return kFixedZero;
    if (x.value == 0) {
        return y.value > 0 ? fixed_t(8192) : fixed_t(24576);
    }
    
    fixed_t absX = Abs(x);
    fixed_t absY = Abs(y);
    
    fixed_t atan;
    if (absX.value > 0) {
        if (absX.value > absY.value) {
            fixed_t ratio = fixed_t((absY.value * tables.inv[absX.value > 16383 ? 16383 : absX.value]) >> 8);
            int32_t idx = (ratio.value * 16384) >> 8;
            if (idx > 16383) idx = 16383;
            atan = fixed_t(tables.sin[idx]);
        } else {
            fixed_t ratio = fixed_t((absX.value * tables.inv[absY.value > 16383 ? 16383 : absY.value]) >> 8);
            int32_t idx = (ratio.value * 16384) >> 8;
            if (idx > 16383) idx = 16383;
            atan = fixed_t(8192 - tables.sin[idx]);
        }
    } else {
        atan = fixed_t(8192);
    }
    
    if (x.value < 0) {
        atan = fixed_t(16384 - atan.value);
    }
    if (y.value < 0) {
        atan = -atan;
    }
    
    return atan;
}

fixed_t Sqrt(fixed_t x) {
    if (x.value <= 0) return kFixedZero;
    
    int32_t n = x.value << 8;
    int32_t low = 0, high = 0x10000;
    
    while (low < high) {
        int32_t mid = (low + high + 1) >> 1;
        if ((int64_t)mid * mid <= (int64_t)n * 256) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }
    
    return fixed_t(low >> 4);
}

fixed_t Pow2(fixed_t x, int exponent) {
    if (exponent == 0) return kFixedOne;
    if (exponent > 0) {
        fixed_t result = x;
        for (int i = 1; i < exponent; i++) {
            result = result * x;
        }
        return result;
    }
    return kFixedOne / Pow2(x, -exponent);
}

fixed_t Clamp(fixed_t value, fixed_t min, fixed_t max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

fixed_t Lerp(fixed_t a, fixed_t b, fixed_t t) {
    return a + (b - a) * t;
}

fixed_t AngleNormalize(fixed_t angle) {
    while (angle.value < 0) angle += fixed_t(65536);
    while (angle.value >= 65536) angle -= fixed_t(65536);
    return angle;
}

fixed_t DegToRad(fixed_t deg) {
    return (deg * fixed_t(256)) / fixed_t(180);
}

fixed_t RadToDeg(fixed_t rad) {
    return (rad * fixed_t(180)) / fixed_t(256);
}

Vec3 Vec3::operator+(Vec3 b) const {
    return Vec3(x + b.x, y + b.y, z + b.z);
}

Vec3 Vec3::operator-(Vec3 b) const {
    return Vec3(x - b.x, y - b.y, z - b.z);
}

Vec3 Vec3::operator*(fixed_t s) const {
    return Vec3(x * s, y * s, z * s);
}

Vec3 Vec3::operator/(fixed_t s) const {
    return Vec3(x / s, y / s, z / s);
}

Vec3& Vec3::operator+=(Vec3 b) {
    x += b.x; y += b.y; z += b.z;
    return *this;
}

Vec3& Vec3::operator-=(Vec3 b) {
    x -= b.x; y -= b.y; z -= b.z;
    return *this;
}

Vec3& Vec3::operator*=(fixed_t s) {
    x *= s; y *= s; z *= s;
    return *this;
}

Vec3& Vec3::operator/=(fixed_t s) {
    x /= s; y /= s; z /= s;
    return *this;
}

Vec3 Vec3::operator-() const {
    return Vec3(-x, -y, -z);
}

bool Vec3::operator==(Vec3 b) const {
    return x == b.x && y == b.y && z == b.z;
}

bool Vec3::operator!=(Vec3 b) const {
    return !(*this == b);
}

fixed_t Vec3::Length() const {
    return Sqrt(x * x + y * y + z * z);
}

fixed_t Vec3::Length2D() const {
    return Sqrt(x * x + y * y);
}

fixed_t Vec3::Dot(Vec3 b) const {
    return x * b.x + y * b.y + z * b.z;
}

Vec3 Vec3::Cross(Vec3 b) const {
    return Vec3(
        y * b.z - z * b.y,
        z * b.x - x * b.z,
        x * b.y - y * b.x
    );
}

void Vec3::Normalize() {
    fixed_t len = Length();
    if (len.value > 0) {
        x /= len;
        y /= len;
        z /= len;
    }
}

void Vec3::Normalize2D() {
    fixed_t len = Length2D();
    if (len.value > 0) {
        x /= len;
        y /= len;
    }
}

void Vec3::Clamp(fixed_t min, fixed_t max) {
    x = zq::math::Clamp(x, min, max);
    y = zq::math::Clamp(y, min, max);
    z = zq::math::Clamp(z, min, max);
}

Vec3 Vec3::Lerp3(Vec3 a, Vec3 b, fixed_t t) {
    return Vec3(
        Lerp(a.x, b.x, t),
        Lerp(a.y, b.y, t),
        Lerp(a.z, b.z, t)
    );
}

Mat4 Mat4::Identity() {
    return Mat4();
}

Mat4 Mat4::Translation(fixed_t x, fixed_t y, fixed_t z) {
    Mat4 result;
    result.m[0][3] = x;
    result.m[1][3] = y;
    result.m[2][3] = z;
    return result;
}

Mat4 Mat4::Scale(fixed_t x, fixed_t y, fixed_t z) {
    Mat4 result;
    result.m[0][0] = x;
    result.m[1][1] = y;
    result.m[2][2] = z;
    return result;
}

Mat4 Mat4::RotationX(fixed_t angle) {
    Mat4 result;
    result.m[1][1] = Cos(angle);
    result.m[1][2] = Sin(angle);
    result.m[2][1] = -Sin(angle);
    result.m[2][2] = Cos(angle);
    return result;
}

Mat4 Mat4::RotationY(fixed_t angle) {
    Mat4 result;
    result.m[0][0] = Cos(angle);
    result.m[0][2] = -Sin(angle);
    result.m[2][0] = Sin(angle);
    result.m[2][2] = Cos(angle);
    return result;
}

Mat4 Mat4::RotationZ(fixed_t angle) {
    Mat4 result;
    result.m[0][0] = Cos(angle);
    result.m[0][1] = Sin(angle);
    result.m[1][0] = -Sin(angle);
    result.m[1][1] = Cos(angle);
    return result;
}

Mat4 Mat4::operator*(Mat4 b) const {
    Mat4 result;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result.m[i][j] = kFixedZero;
            for (int k = 0; k < 4; k++) {
                result.m[i][j] += m[i][k] * b.m[k][j];
            }
        }
    }
    return result;
}

Vec3 Mat4::TransformVec3(Vec3 v) const {
    return Vec3(
        m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
        m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
        m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z
    );
}

Vec3 Mat4::TransformPoint3(Vec3 v) const {
    fixed_t w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3];
    return Vec3(
        (m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3]) / w,
        (m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3]) / w,
        (m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3]) / w
    );
}

fixed_t FixedSqrt(fixed_t x) {
    return Sqrt(x);
}

} // namespace zq::math
