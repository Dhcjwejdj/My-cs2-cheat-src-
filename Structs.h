#pragma once
#include <cmath>

struct Vector3 {
    float x, y, z;

    Vector3 operator+(const Vector3& v) const { return { x + v.x, y + v.y, z + v.z }; }
    Vector3 operator-(const Vector3& v) const { return { x - v.x, y - v.y, z - v.z }; }
    Vector3 operator*(float f) const { return { x * f, y * f, z * f }; }
    float Length() const { return std::sqrt(x * x + y * y + z * z); }
    float Distance(const Vector3& v) const { return (*this - v).Length(); }
};

struct Vector2 {
    float x, y;
};

struct view_matrix_t {
    float matrix[4][4];
};