#pragma once
#include "rtv_math.h"

typedef struct MouseButtons_t {
    bool left;
    bool right;
} MouseButtons;

typedef struct EngineCamera_t {
    Vec3 pos;
    Vec3 forward, right, up;
    f32  yaw, pitch;
    f32  fov_y;
    f32  speed;
    f32  sensitivity;
} EngineCamera;

typedef struct EngineFrameData_t {
    // Camera
    Vec4 vp_top_left;     // World space
    Vec4 vp_top_right;    // Vec4 because of std140 layout
    Vec4 vp_bottom_right;
    Vec4 vp_bottom_left;
    Vec4 origin;
} EngineFrameData;

typedef struct Engine_t {
    SDL_Window*              window;
    SDL_GPUDevice*           device;
    SDL_GPUTextureFormat     swapchain_texture_format;
    SDL_GPUGraphicsPipeline* graphics_pipeline;
    SDL_GPUTexture*          screen_texture;
    SDL_GPUSampler*          screen_texture_sampler;

    SDL_GPUComputePipeline*   compute_pipeline;
    const char*               compute_shader_path;
    SDL_Time                  compute_shader_modified_time;
    shaderc_compiler_t        shader_compiler;
    shaderc_compile_options_t shader_compile_options;
    SDL_GPUBuffer*            storage_buffer;
    EngineFrameData           frame_data;

    u32 swapchain_texture_width;
    u32 swapchain_texture_height;
    u32 screen_texture_width;
    u32 screen_texture_height;

    const bool*  keys;
    int          keys_num;
    Vec2         mouse;
    Vec2         mouse_delta;
    MouseButtons mouse_buttons;
    EngineCamera camera;
} Engine;

bool engine_initialize(Engine* self, const char* compute_shader_path);
void engine_shutdown(Engine* self);

void engine_run(Engine* self);

