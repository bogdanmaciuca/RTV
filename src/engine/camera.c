#include "camera.h"
#include "internal.h"

#define WORLD_UP ((Vec3){ .x = 0.0f, .y = 1.0f, .z = 0.0f })
#define CAMERA_DEFAULT_SENSITIVITY      0.01f
#define CAMERA_DEFAULT_SPEED            0.02f
#define CAMERA_SPEED_CHANGE_SENSITIVITY 0.025f
#define CAMERA_MIN_SPEED                0.01f
#define CAMERA_MAX_SPEED                0.1f

bool engine_camera_init(Engine* self) {
    self->camera.forward.z   = 1.0f;
    self->camera.fov_y       = RADIANS(40.0f);
    self->camera.sensitivity = CAMERA_DEFAULT_SENSITIVITY;
    self->camera.speed       = CAMERA_DEFAULT_SPEED;
    return true;
}

void engine_camera_update(Engine* self) {
    Camera* cam = &self->camera;

    // Process mouse input
    if (self->input.mouse_buttons.left) {
        cam->yaw   -= cam->sensitivity * self->input.mouse_delta.x;
        cam->pitch -= cam->sensitivity * self->input.mouse_delta.y;
        cam->pitch = clamp(cam->pitch, RADIANS(-89.0f), RADIANS(89.0f));
    }

    // Update camera vectors
    cam->forward = (Vec3){
        .x = cosf(cam->pitch) * sinf(cam->yaw),
        .y = sinf(cam->pitch),
        .z = cosf(cam->pitch) * cos(cam->yaw),
    };

    cam->right = vec3_norm(vec3_cross(cam->forward, WORLD_UP));
    cam->up = vec3_cross(cam->right, cam->forward);

    // Update position
    if (engine_key_down(self, SDL_SCANCODE_W)) {
        cam->pos = vec3_add(cam->pos, vec3_mul(cam->speed, cam->forward));
    }
    else if (engine_key_down(self, SDL_SCANCODE_S)) {
        cam->pos = vec3_sub(cam->pos, vec3_mul(cam->speed, cam->forward));
    }
    if (engine_key_down(self, SDL_SCANCODE_A)) {
        cam->pos = vec3_sub(cam->pos, vec3_mul(cam->speed, cam->right));
    }
    else if (engine_key_down(self, SDL_SCANCODE_D)) {
        cam->pos = vec3_add(cam->pos, vec3_mul(cam->speed, cam->right));
    }
    if (engine_key_down(self, SDL_SCANCODE_Q)) {
        cam->pos = vec3_add(cam->pos, vec3_mul(cam->speed, WORLD_UP));
    }
    else if (engine_key_down(self, SDL_SCANCODE_E)) {
        cam->pos = vec3_sub(cam->pos, vec3_mul(cam->speed, WORLD_UP));
    }

    // Fill frame_data for compute shader
    f32 half_height = tanf(cam->fov_y / 2);
    f32 half_width = half_height * (f32)self->screen_texture_width / (f32)self->screen_texture_height;

    Vec3 half_right_vec = vec3_mul(half_width, cam->right);
    Vec3 half_up_vec = vec3_mul(half_height, cam->up);

    // Center of the image plane in world space (distance = 1.0)
    Vec3 center = vec3_add(cam->pos, cam->forward);

    Vec3 top_row    = vec3_add(center, half_up_vec);
    Vec3 bottom_row = vec3_sub(center, half_up_vec);

    Vec3 tl = vec3_sub(top_row,    half_right_vec);
    Vec3 tr = vec3_add(top_row,    half_right_vec);
    Vec3 bl = vec3_sub(bottom_row, half_right_vec);
    Vec3 br = vec3_add(bottom_row, half_right_vec);

    self->compute.camera_viewport.top_left     = vec4_from_vec3(tl, 1.0f);
    self->compute.camera_viewport.top_right    = vec4_from_vec3(tr, 1.0f);
    self->compute.camera_viewport.bottom_left  = vec4_from_vec3(bl, 1.0f);
    self->compute.camera_viewport.bottom_right = vec4_from_vec3(br, 1.0f);
    self->compute.camera_viewport.origin       = vec4_from_vec3(cam->pos, 1.0f);
}

void engine_camera_update_speed(Engine* self, int delta) {
    self->camera.speed += CAMERA_SPEED_CHANGE_SENSITIVITY * delta;
    self->camera.speed = clamp(self->camera.speed, CAMERA_MIN_SPEED, CAMERA_MAX_SPEED);
}

