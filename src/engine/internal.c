#include "internal.h"
#include "../core/log.h"
#include "generated/shaders_bytecode.h"

#define WINDOW_DEFAULT_WIDTH 1200
#define WINDOW_DEFAULT_HEIGHT 675

#define TRANSFER_BUFFER_DEFAULT_SIZE (4 * 1024 * 1024)

bool engine_init_window_and_device(Engine* self) {
    // Initialize SDL
    u32 init_flags = SDL_INIT_VIDEO;
    if (!SDL_WasInit(init_flags) && !SDL_CHECK(SDL_Init(init_flags))) {
        return false;
    }

    // Create window
    if (!SDL_CHECK(self->window = SDL_CreateWindow("Voxel RT", WINDOW_DEFAULT_WIDTH, WINDOW_DEFAULT_HEIGHT, 0)))
        return false;

    // Create GPU device
    if (!SDL_CHECK(self->device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, RTV_DEBUG, NULL))) {
        return false;
    }

    // Claim window
    if (!SDL_CHECK(SDL_ClaimWindowForGPUDevice(self->device, self->window))) {
        return false;
    }
    self->swapchain_texture_format = SDL_GetGPUSwapchainTextureFormat(self->device, self->window);

    // Transfer buffer
    SDL_GPUTransferBufferCreateInfo transfer_buffer_create_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = TRANSFER_BUFFER_DEFAULT_SIZE,
    };
    if (!SDL_CHECK(self->transfer_buffer = SDL_CreateGPUTransferBuffer(self->device, &transfer_buffer_create_info))) {
        return false;
    }

    return true;
}

bool engine_init_screen_renderer(Engine* self) {
    // Create graphics pipeline
    SDL_GPUShader* screen_quad_vert_shader;
    SDL_GPUShaderCreateInfo screen_quad_vs_create_info = {
        .code_size  = screen_quad_vs_bytecode_size,
        .code       = (u8*)screen_quad_vs_bytecode,
        .entrypoint = "main",
        .format     = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage      = SDL_GPU_SHADERSTAGE_VERTEX,
    };
    if (!SDL_CHECK(screen_quad_vert_shader = SDL_CreateGPUShader(self->device, &screen_quad_vs_create_info))) {
        return false;
    }

    SDL_GPUShader* screen_quad_frag_shader;
    SDL_GPUShaderCreateInfo screen_quad_fs_create_info = {
        .code_size    = screen_quad_fs_bytecode_size,
        .code         = (u8*)screen_quad_fs_bytecode,
        .entrypoint   = "main",
        .format       = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage        = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1,
    };
    if (!SDL_CHECK(screen_quad_frag_shader = SDL_CreateGPUShader(self->device, &screen_quad_fs_create_info))) {
        return false;
    }

    SDL_GPUGraphicsPipelineCreateInfo graphics_pipeline_create_info = {
        .target_info = {
            .num_color_targets = 1,
            .color_target_descriptions = (SDL_GPUColorTargetDescription[]){{
                .format = self->swapchain_texture_format
            }},
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .vertex_shader = screen_quad_vert_shader,
        .fragment_shader = screen_quad_frag_shader
    };
    if (!SDL_CHECK(self->graphics_pipeline = SDL_CreateGPUGraphicsPipeline(self->device, &graphics_pipeline_create_info))) {
        return false;
    }
    SDL_ReleaseGPUShader(self->device, screen_quad_vert_shader);
    SDL_ReleaseGPUShader(self->device, screen_quad_frag_shader);

    // Screen texture
    self->screen_texture_width = WINDOW_DEFAULT_WIDTH;
    self->screen_texture_height = WINDOW_DEFAULT_HEIGHT;
    SDL_GPUTextureCreateInfo screen_texture_create_info = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .width = self->screen_texture_width,
        .height = self->screen_texture_height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE
    };
    if (!SDL_CHECK(self->screen_texture = SDL_CreateGPUTexture(self->device, &screen_texture_create_info))) {
        return false;
    }

    // Screen texture sampler
    SDL_GPUSamplerCreateInfo screen_texture_sampler_create_info = {};
    if (!SDL_CHECK(self->screen_texture_sampler = SDL_CreateGPUSampler(self->device, &screen_texture_sampler_create_info))) {
        return false;
    }

    return true;
}

