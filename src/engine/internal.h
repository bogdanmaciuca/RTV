#pragma once
#include "../core/arena.h"
#include "camera.h"
#include "compute.h"
#include "input.h"
#include "resource.h"
#include "text.h"

struct Engine {
    SDL_Window*              window;
    SDL_GPUDevice*           device;
    SDL_GPUTexture*          swapchain_texture;
    SDL_GPUTextureFormat     swapchain_texture_format;
    SDL_GPUGraphicsPipeline* graphics_pipeline;
    SDL_GPUTexture*          screen_texture;
    SDL_GPUSampler*          screen_texture_sampler;
    SDL_GPUTransferBuffer*   transfer_buffer;

    u32 swapchain_texture_width;
    u32 swapchain_texture_height;
    u32 screen_texture_width;
    u32 screen_texture_height;

    Arena arena;
    Arena frame_arena;
    ResourceTracker resource_tracker;
    Text text;
    Input input;
    Camera camera;
    Compute compute;
};

bool engine_init_window_and_device(Engine* self);
bool engine_init_screen_renderer(Engine* self);

