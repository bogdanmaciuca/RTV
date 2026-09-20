#pragma once
#include "camera.h"
#include "file.h"

typedef struct Engine Engine;

typedef struct Compute {
    SDL_GPUComputePipeline*   pipeline;
    SDL_GPUBuffer*            storage_buffer;
    File                      shader_file;
    shaderc_compiler_t        shader_compiler;
    shaderc_compile_options_t shader_compile_options;
    CameraViewport            camera_viewport;
} Compute;

bool engine_compute_init(Engine* self);
void engine_compute_destroy(Engine* self);

bool engine_compute_reload(Engine* self);
void engine_compute_dispatch(Engine* self, SDL_GPUCommandBuffer* command_buffer);

