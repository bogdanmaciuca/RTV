#include "engine.h"

#include "log.h"
#include "generated/screen_quad_shaders.h"

#define DEFAULT_WND_WIDTH 1200
#define DEFAULT_WND_HEIGHT 675

static bool check_if_file_changed(const char* path, SDL_Time* cached_time);
static bool engine_hot_reload_compute(Engine* self);
static void engine_draw(Engine* self);

bool engine_initialize(Engine* self, const char* compute_shader_path) {
    ZERO_MEM(self);

    // Initialize SDL
    u32 init_flags = SDL_INIT_VIDEO;
    if (!SDL_WasInit(init_flags) && !SDL_CHECK(SDL_Init(init_flags))) {
        return false;
    }

    // Create window
    if (!SDL_CHECK(self->window = SDL_CreateWindow("Voxel RT", DEFAULT_WND_WIDTH, DEFAULT_WND_HEIGHT, 0)))
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

    // Create graphics pipeline
    SDL_GPUShader* screen_quad_vert_shader;
    SDL_GPUShaderCreateInfo screen_quad_vs_create_info = {
        .code_size = sizeof(screen_quad_vs_bytecode),
        .code = (u8*)screen_quad_vs_bytecode,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
    };
    if (!SDL_CHECK(screen_quad_vert_shader = SDL_CreateGPUShader(self->device, &screen_quad_vs_create_info))) {
        return false;
    }

    SDL_GPUShader* screen_quad_frag_shader;
    SDL_GPUShaderCreateInfo screen_quad_fs_create_info = {
        .code_size = sizeof(screen_quad_fs_bytecode),
        .code = (u8*)screen_quad_fs_bytecode,
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
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
    self->screen_texture_width = DEFAULT_WND_WIDTH;
    self->screen_texture_height = DEFAULT_WND_HEIGHT;
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

    // Shaderc
    if (!CHECK(self->shader_compiler = shaderc_compiler_initialize(), "")) {
        return false;
    }
    if (!CHECK(self->shader_compile_options = shaderc_compile_options_initialize(), "")) {
        return false;
    }
    shaderc_compile_options_set_target_env(self->shader_compile_options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
    shaderc_compile_options_set_optimization_level(self->shader_compile_options, shaderc_optimization_level_performance);

    // Compute pipeline
    self->compute_shader_path = compute_shader_path;
    if (!engine_hot_reload_compute(self)) {
        return false;
    }

    return true;
}

void engine_shutdown(Engine* self) {
    SDL_ReleaseGPUComputePipeline(self->device, self->compute_pipeline);

    shaderc_compile_options_release(self->shader_compile_options);
    shaderc_compiler_release(self->shader_compiler);

    SDL_ReleaseGPUSampler(self->device, self->screen_texture_sampler);
    SDL_ReleaseGPUTexture(self->device, self->screen_texture);
    SDL_ReleaseGPUGraphicsPipeline(self->device, self->graphics_pipeline);

    SDL_ReleaseWindowFromGPUDevice(self->device, self->window);
    SDL_WaitForGPUIdle(self->device);
    SDL_DestroyGPUDevice(self->device);
    SDL_DestroyWindow(self->window);

    SDL_Quit();
}

void engine_run(Engine* self) {
    bool should_close = false;

    while (!should_close) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    should_close = true;
                    break;
            }
        }
        engine_draw(self);
    }
}

bool check_if_file_changed(const char* path, SDL_Time* cached_time) {
    SDL_PathInfo info;
    SDL_CHECK(SDL_GetPathInfo(path, &info));
    if (info.modify_time > *cached_time) {
        *cached_time = info.modify_time;
        return true;
    }

    return false;
}

bool engine_hot_reload_compute(Engine* self) {
    if (!check_if_file_changed(self->compute_shader_path, &self->compute_shader_modified_time)) {
        return true;
    }

    // Load source code
    u8* source;
    size_t source_size;
    if (!SDL_CHECK(source = SDL_LoadFile(self->compute_shader_path, &source_size))) {
        return false;
    }

    // Compile to SPIRV
    shaderc_compilation_result_t compilation_result = shaderc_compile_into_spv(
        self->shader_compiler,
        (char*)source,
        source_size,
        shaderc_compute_shader,
        self->compute_shader_path,
        "main",
        self->shader_compile_options
    );
    shaderc_compilation_status status = shaderc_result_get_compilation_status(compilation_result);
    if (status != shaderc_compilation_status_success) {
        fprintf(stderr, "Compilation error:\n%s\n", shaderc_result_get_error_message(compilation_result));

        shaderc_result_release(compilation_result);
        SDL_free(source);
        return false;
    }

    u8* spirv_bytecode = (u8*)shaderc_result_get_bytes(compilation_result);
    size_t spirv_bytecode_size = shaderc_result_get_length(compilation_result);

    // Create pipeline
    SDL_GPUComputePipeline* new_pipeline;
    SDL_GPUComputePipelineCreateInfo compute_pipeline_create_info = {
        .code_size                      = spirv_bytecode_size,
        .code                           = spirv_bytecode,
        .entrypoint                     = "main",
        .format                         = SDL_GPU_SHADERFORMAT_SPIRV,
        .num_readwrite_storage_textures = 1,
        .threadcount_x                  = 8,
        .threadcount_y                  = 8,
        .threadcount_z                  = 1,
    };
    if (!SDL_CHECK(new_pipeline = SDL_CreateGPUComputePipeline(self->device, &compute_pipeline_create_info))) {
        shaderc_result_release(compilation_result);
        SDL_free(source);
        return false;
    }

    // Release old pipeline
    if (self->compute_pipeline) {
        SDL_ReleaseGPUComputePipeline(self->device, self->compute_pipeline);
    }
    self->compute_pipeline = new_pipeline;

    shaderc_result_release(compilation_result);
    SDL_free(source);
    return true;
}

void engine_draw(Engine* self) {
    SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(self->device);

    SDL_GPUTexture* swapchain_texture;
    SDL_CHECK(SDL_WaitAndAcquireGPUSwapchainTexture(
        command_buffer,
        self->window,
        &swapchain_texture,
        &self->swapchain_texture_width,
        &self->swapchain_texture_height
    ));
    if (swapchain_texture) {
        // Dispatch compute
        SDL_GPUStorageTextureReadWriteBinding texture_rw_binding = { .texture = self->screen_texture };
        SDL_GPUComputePass* compute_pass = SDL_BeginGPUComputePass(command_buffer, &texture_rw_binding, 1, NULL, 0);
        SDL_BindGPUComputePipeline(compute_pass, self->compute_pipeline);
        SDL_DispatchGPUCompute(compute_pass, self->screen_texture_width / 8 + 1, self->screen_texture_height / 8 + 1, 1);
        SDL_EndGPUComputePass(compute_pass);

        // Draw quad to screen
        SDL_GPUColorTargetInfo color_target_info = {
            .texture = swapchain_texture,
            .load_op = SDL_GPU_LOADOP_DONT_CARE,
            .store_op = SDL_GPU_STOREOP_STORE
        };
        SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target_info, 1, NULL);
        SDL_BindGPUGraphicsPipeline(render_pass, self->graphics_pipeline);
        SDL_GPUTextureSamplerBinding screen_texture_sampler_binding = {
            .texture = self->screen_texture,
            .sampler = self->screen_texture_sampler
        };
        SDL_BindGPUFragmentSamplers(render_pass, 0, &screen_texture_sampler_binding, 1);
        SDL_DrawGPUPrimitives(render_pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(render_pass);
    }

    SDL_SubmitGPUCommandBuffer(command_buffer);
}

