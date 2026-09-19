#pragma once

#define RADIANS(x) (x * M_PI / 180.0)

typedef struct Vec2 {
    f32 x, y;
} Vec2;

typedef struct Vec3 {
    f32 x, y, z;
} Vec3;

typedef struct Vec4 {
    f32 x, y, z, w;
} Vec4;

typedef union Mat4 {
    float data[16];

    float m[4][4];

    Vec4 cols[4];

    struct {
        float m00, m01, m02, m03;
        float m10, m11, m12, m13;
        float m20, m21, m22, m23;
        float m30, m31, m32, m33;
    };
} Mat4;

static inline f32 clamp(f32 x, f32 min, f32 max) {
    if (x < min) {
        return min;
    }
    else if (x > max) {
        return max;
    }
    return x;
}

static inline Vec3 vec3_add(Vec3 a, Vec3 b) {
    return (Vec3){
        .x = a.x + b.x,
        .y = a.y + b.y,
        .z = a.z + b.z,
    };
}

static inline Vec3 vec3_sub(Vec3 a, Vec3 b) {
    return (Vec3){
        .x = a.x - b.x,
        .y = a.y - b.y,
        .z = a.z - b.z,
    };
}

static inline Vec3 vec3_mul(f32 s, Vec3 v) {
    return (Vec3){
        .x = s * v.x,
        .y = s * v.y,
        .z = s * v.z,
    };
}

static inline Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return (Vec3){
        .x = a.y * b.z - a.z * b.y,
        .y = a.z * b.x - a.x * b.z,
        .z = a.x * b.y - a.y * b.x
    };
}

static inline f32 vec3_len(Vec3 v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static inline Vec3 vec3_norm(Vec3 v) {
    f32 len = vec3_len(v);
    return (Vec3){
        .x = v.x / len,
        .y = v.y / len,
        .z = v.z / len,
    };
}

static inline Vec4 vec4_from_vec3(Vec3 v, f32 w) {
    return (Vec4){ .x = v.x, .y = v.y, .z = v.z, .w = w };
}

