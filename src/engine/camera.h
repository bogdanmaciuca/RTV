#pragma once
#include "rtv_math.h"

typedef struct Engine Engine;

typedef struct Camera {
    Vec3 pos;
    Vec3 forward, right, up;
    f32  yaw, pitch;
    f32  fov_y;
    f32  speed;
    f32  sensitivity;
} Camera;

typedef struct CameraViewport {
    Vec4 top_left;     // World space
    Vec4 top_right;    // Vec4 because of std140 layout
    Vec4 bottom_right;
    Vec4 bottom_left;
    Vec4 origin;
} CameraViewport;

bool engine_camera_init(Engine* self);

void engine_camera_update(Engine* self);
void engine_camera_update_speed(Engine* self, int delta);

