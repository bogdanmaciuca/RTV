#pragma once

typedef struct Engine_t {
    SDL_Window*              window;
    SDL_GPUDevice*           device;
    SDL_GPUTextureFormat     swapchain_texture_format;
    SDL_GPUGraphicsPipeline* graphics_pipeline;
    SDL_GPUComputePipeline*  compute_pipeline;
    const char*              compute_shader_path;
    SDL_Time                 compute_shader_modified_time;
    SDL_GPUTexture*          screen_texture;
    SDL_GPUSampler*          screen_texture_sampler;

    shaderc_compiler_t        shader_compiler;
    shaderc_compile_options_t shader_compile_options;

    u32 swapchain_texture_width;
    u32 swapchain_texture_height;

    u32 screen_texture_width;
    u32 screen_texture_height;
} Engine;

bool engine_initialize(Engine* self, const char* compute_shader_path);
void engine_shutdown(Engine* self);

void engine_run(Engine* self);

