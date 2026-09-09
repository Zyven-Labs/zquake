#pragma once
#include <cmath>
#include <cstring>

namespace zq::mathf {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
inline Vec3 operator*(const Vec3& a, float s) { return {a.x*s, a.y*s, a.z*s}; }
inline Vec3 operator*(float s, const Vec3& a) { return a * s; }
inline float Dot(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vec3 Cross(const Vec3& a, const Vec3& b) {
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
inline float Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }
inline Vec3 Normalize(const Vec3& a) {
    float len = Length(a);
    if (len < 1e-8f) return Vec3{0,0,0};
    return a * (1.0f / len);
}

// Column-major 4x4 matrix (Vulkan convention)
struct Mat4 {
    float m[16]{};

    static Mat4 Identity() {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
    static Mat4 Perspective(float fov_y_degrees, float aspect, float near, float far) {
        Mat4 r;
        float f = 1.0f / std::tan(fov_y_degrees * 3.14159265f / 360.0f);
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (far + near) / (near - far);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * far * near) / (near - far);
        r.m[15] = 0.0f;
        return r;
    }
    // Right-handed look-at for Vulkan (camera looks down -Z)
    static Mat4 LookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = Normalize(center - eye);
        Vec3 s = Normalize(Cross(f, up));
        Vec3 u = Cross(s, f);
        Mat4 r;
        r.m[0]  = s.x; r.m[1]  = u.x; r.m[2]  = -f.x; r.m[3]  = 0.0f;
        r.m[4]  = s.y; r.m[5]  = u.y; r.m[6]  = -f.y; r.m[7]  = 0.0f;
        r.m[8]  = s.z; r.m[9]  = u.z; r.m[10] = -f.z; r.m[11] = 0.0f;
        r.m[12] = -Dot(s, eye);
        r.m[13] = -Dot(u, eye);
        r.m[14] = Dot(f, eye);
        r.m[15] = 1.0f;
        return r;
    }
    static Mat4 Translate(const Vec3& t) {
        Mat4 r = Identity();
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }
    static Mat4 RotationY(float angle_rad) {
        Mat4 r = Identity();
        float c = std::cos(angle_rad), s = std::sin(angle_rad);
        r.m[0] = c; r.m[2] = s;
        r.m[8] = -s; r.m[10] = c;
        return r;
    }
    static Mat4 RotationZ(float angle_rad) {
        Mat4 r = Identity();
        float c = std::cos(angle_rad), s = std::sin(angle_rad);
        r.m[0] = c; r.m[1] = s;
        r.m[4] = -s; r.m[5] = c;
        return r;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0;
            for (int k = 0; k < 4; k++) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            r.m[col * 4 + row] = sum;
        }
    }
    return r;
}

} // namespace zq::mathf