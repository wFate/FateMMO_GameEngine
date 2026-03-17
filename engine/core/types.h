#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <cstring>

namespace fate {

// ============================================================================
// Basic Types
// ============================================================================
using EntityId = uint32_t;
constexpr EntityId INVALID_ENTITY = 0;

// Forward declare EntityHandle (full definition in engine/ecs/entity_handle.h)
struct EntityHandle;

// ============================================================================
// Vec2
// ============================================================================
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    Vec2() = default;
    Vec2(float x, float y) : x(x), y(y) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator*(const Vec2& o) const { return {x * o.x, y * o.y}; }
    Vec2 operator/(float s) const { return {x / s, y / s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2& o) const { return !(*this == o); }

    float length() const { return std::sqrt(x * x + y * y); }
    float lengthSq() const { return x * x + y * y; }
    float dot(const Vec2& o) const { return x * o.x + y * o.y; }
    float distance(const Vec2& o) const { return (*this - o).length(); }

    Vec2 normalized() const {
        float len = length();
        if (len < 0.0001f) return {0, 0};
        return {x / len, y / len};
    }

    static Vec2 zero() { return {0, 0}; }
    static Vec2 one() { return {1, 1}; }
};

// ============================================================================
// Vec3
// ============================================================================
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

// ============================================================================
// Vec4 / Color
// ============================================================================
struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    Vec4() = default;
    Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
};

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;

    Color() = default;
    Color(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

    static Color white()  { return {1, 1, 1, 1}; }
    static Color black()  { return {0, 0, 0, 1}; }
    static Color red()    { return {1, 0, 0, 1}; }
    static Color green()  { return {0, 1, 0, 1}; }
    static Color blue()   { return {0, 0, 1, 1}; }
    static Color yellow() { return {1, 1, 0, 1}; }
    static Color clear()  { return {0, 0, 0, 0}; }

    static Color fromHex(uint32_t hex) {
        return {
            ((hex >> 24) & 0xFF) / 255.0f,
            ((hex >> 16) & 0xFF) / 255.0f,
            ((hex >> 8)  & 0xFF) / 255.0f,
            ((hex)       & 0xFF) / 255.0f
        };
    }
};

// ============================================================================
// Rect (for sprite source rects, UI bounds)
// ============================================================================
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    Rect() = default;
    Rect(float x, float y, float w, float h) : x(x), y(y), w(w), h(h) {}

    bool contains(Vec2 point) const {
        return point.x >= x && point.x <= x + w &&
               point.y >= y && point.y <= y + h;
    }

    bool overlaps(const Rect& o) const {
        return x < o.x + o.w && x + w > o.x &&
               y < o.y + o.h && y + h > o.y;
    }
};

// ============================================================================
// Mat4 (column-major, OpenGL convention)
// ============================================================================
struct Mat4 {
    float m[16];

    Mat4() {
        std::memset(m, 0, sizeof(m));
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }

    const float* data() const { return m; }

    static Mat4 identity() { return Mat4(); }

    static Mat4 ortho(float left, float right, float bottom, float top,
                      float near_val = -1.0f, float far_val = 1.0f) {
        Mat4 result;
        result.m[0]  =  2.0f / (right - left);
        result.m[5]  =  2.0f / (top - bottom);
        result.m[10] = -2.0f / (far_val - near_val);
        result.m[12] = -(right + left) / (right - left);
        result.m[13] = -(top + bottom) / (top - bottom);
        result.m[14] = -(far_val + near_val) / (far_val - near_val);
        result.m[15] = 1.0f;
        return result;
    }

    static Mat4 translate(float x, float y, float z = 0.0f) {
        Mat4 result;
        result.m[12] = x;
        result.m[13] = y;
        result.m[14] = z;
        return result;
    }

    static Mat4 scale(float sx, float sy, float sz = 1.0f) {
        Mat4 result;
        result.m[0]  = sx;
        result.m[5]  = sy;
        result.m[10] = sz;
        return result;
    }

    static Mat4 rotate_z(float radians) {
        Mat4 result;
        float c = std::cos(radians);
        float s = std::sin(radians);
        result.m[0] = c;
        result.m[1] = s;
        result.m[4] = -s;
        result.m[5] = c;
        return result;
    }

    Mat4 operator*(const Mat4& b) const {
        Mat4 result;
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                result.m[col * 4 + row] =
                    m[0 * 4 + row] * b.m[col * 4 + 0] +
                    m[1 * 4 + row] * b.m[col * 4 + 1] +
                    m[2 * 4 + row] * b.m[col * 4 + 2] +
                    m[3 * 4 + row] * b.m[col * 4 + 3];
            }
        }
        return result;
    }

    // General 4x4 matrix inverse (Cramer's rule)
    Mat4 inverse() const {
        const float* src = m;
        float inv[16];

        inv[0]  =  src[5]*src[10]*src[15] - src[5]*src[11]*src[14] - src[9]*src[6]*src[15] + src[9]*src[7]*src[14] + src[13]*src[6]*src[11] - src[13]*src[7]*src[10];
        inv[4]  = -src[4]*src[10]*src[15] + src[4]*src[11]*src[14] + src[8]*src[6]*src[15] - src[8]*src[7]*src[14] - src[12]*src[6]*src[11] + src[12]*src[7]*src[10];
        inv[8]  =  src[4]*src[9]*src[15]  - src[4]*src[11]*src[13] - src[8]*src[5]*src[15] + src[8]*src[7]*src[13] + src[12]*src[5]*src[11] - src[12]*src[7]*src[9];
        inv[12] = -src[4]*src[9]*src[14]  + src[4]*src[10]*src[13] + src[8]*src[5]*src[14] - src[8]*src[6]*src[13] - src[12]*src[5]*src[10] + src[12]*src[6]*src[9];
        inv[1]  = -src[1]*src[10]*src[15] + src[1]*src[11]*src[14] + src[9]*src[2]*src[15] - src[9]*src[3]*src[14] - src[13]*src[2]*src[11] + src[13]*src[3]*src[10];
        inv[5]  =  src[0]*src[10]*src[15] - src[0]*src[11]*src[14] - src[8]*src[2]*src[15] + src[8]*src[3]*src[14] + src[12]*src[2]*src[11] - src[12]*src[3]*src[10];
        inv[9]  = -src[0]*src[9]*src[15]  + src[0]*src[11]*src[13] + src[8]*src[1]*src[15] - src[8]*src[3]*src[13] - src[12]*src[1]*src[11] + src[12]*src[3]*src[9];
        inv[13] =  src[0]*src[9]*src[14]  - src[0]*src[10]*src[13] - src[8]*src[1]*src[14] + src[8]*src[2]*src[13] + src[12]*src[1]*src[10] - src[12]*src[2]*src[9];
        inv[2]  =  src[1]*src[6]*src[15]  - src[1]*src[7]*src[14]  - src[5]*src[2]*src[15] + src[5]*src[3]*src[14] + src[13]*src[2]*src[7]  - src[13]*src[3]*src[6];
        inv[6]  = -src[0]*src[6]*src[15]  + src[0]*src[7]*src[14]  + src[4]*src[2]*src[15] - src[4]*src[3]*src[14] - src[12]*src[2]*src[7]  + src[12]*src[3]*src[6];
        inv[10] =  src[0]*src[5]*src[15]  - src[0]*src[7]*src[13]  - src[4]*src[1]*src[15] + src[4]*src[3]*src[13] + src[12]*src[1]*src[7]  - src[12]*src[3]*src[5];
        inv[14] = -src[0]*src[5]*src[14]  + src[0]*src[6]*src[13]  + src[4]*src[1]*src[14] - src[4]*src[2]*src[13] - src[12]*src[1]*src[6]  + src[12]*src[2]*src[5];
        inv[3]  = -src[1]*src[6]*src[11]  + src[1]*src[7]*src[10]  + src[5]*src[2]*src[11] - src[5]*src[3]*src[10] - src[9]*src[2]*src[7]   + src[9]*src[3]*src[6];
        inv[7]  =  src[0]*src[6]*src[11]  - src[0]*src[7]*src[10]  - src[4]*src[2]*src[11] + src[4]*src[3]*src[10] + src[8]*src[2]*src[7]   - src[8]*src[3]*src[6];
        inv[11] = -src[0]*src[5]*src[11]  + src[0]*src[7]*src[9]   + src[4]*src[1]*src[11] - src[4]*src[3]*src[9]  - src[8]*src[1]*src[7]   + src[8]*src[3]*src[5];
        inv[15] =  src[0]*src[5]*src[10]  - src[0]*src[6]*src[9]   - src[4]*src[1]*src[10] + src[4]*src[2]*src[9]  + src[8]*src[1]*src[6]   - src[8]*src[2]*src[5];

        float det = src[0]*inv[0] + src[1]*inv[4] + src[2]*inv[8] + src[3]*inv[12];
        if (det == 0.0f) return identity();

        float invDet = 1.0f / det;
        Mat4 result;
        for (int i = 0; i < 16; i++) result.m[i] = inv[i] * invDet;
        return result;
    }
};

// ============================================================================
// Direction (TWOM-style cardinal)
// ============================================================================
enum class Direction : uint8_t {
    None  = 0,
    Up    = 1,
    Down  = 2,
    Left  = 3,
    Right = 4
};

inline Vec2 directionToVec(Direction dir) {
    switch (dir) {
        case Direction::Up:    return { 0,  1};
        case Direction::Down:  return { 0, -1};
        case Direction::Left:  return {-1,  0};
        case Direction::Right: return { 1,  0};
        default: return {0, 0};
    }
}

// ============================================================================
// Coordinate conversion (pixel <-> tile)
// ============================================================================
namespace Coords {
    constexpr float TILE_SIZE = 32.0f;

    inline Vec2 toTile(const Vec2& pixel) {
        return { pixel.x / TILE_SIZE, pixel.y / TILE_SIZE };
    }

    inline Vec2 toPixel(const Vec2& tile) {
        return { tile.x * TILE_SIZE, tile.y * TILE_SIZE };
    }

    inline int tileX(float pixelX) { return (int)(pixelX / TILE_SIZE); }
    inline int tileY(float pixelY) { return (int)(pixelY / TILE_SIZE); }
}

} // namespace fate
