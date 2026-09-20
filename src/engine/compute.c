#include "compute.h"
#include "../core/log.h"
#include "internal.h"

#define COMPUTE_SHADER_PATH "shaders/rtv.comp"

#define STORAGE_BUFFER_DEFAULT_SIZE (512 * 512)

bool engine_compute_init(Engine* self) {
    // Shader file
    self->compute.shader_file = file_create(COMPUTE_SHADER_PATH);
    self->compute.shader_file.cached_last_mod--;

    // ShaderC
    if (!CHECK(self->compute.shader_compiler = shaderc_compiler_initialize(), "")) {
        return false;
    }
    if (!CHECK(self->compute.shader_compile_options = shaderc_compile_options_initialize(), "")) {
        return false;
    }
    shaderc_compile_options_set_target_env(self->compute.shader_compile_options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
    shaderc_compile_options_set_optimization_level(self->compute.shader_compile_options, shaderc_optimization_level_performance);

    // Storage buffer
    SDL_GPUBufferCreateInfo storage_buffer_create_info = {
        .usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
        .size  = STORAGE_BUFFER_DEFAULT_SIZE
    };
    if (!SDL_CHECK(self->compute.storage_buffer = SDL_CreateGPUBuffer(self->device, &storage_buffer_create_info))) {
        return NULL;
    }

    // Compute pipeline
    return engine_compute_reload(self);
}

void engine_compute_destroy(Engine* self) {
    if (self->compute.pipeline) {
        SDL_ReleaseGPUComputePipeline(self->device, self->compute.pipeline);
    }
    shaderc_compile_options_release(self->compute.shader_compile_options);
    shaderc_compiler_release(self->compute.shader_compiler);
    file_destroy(&self->compute.shader_file);
}

bool engine_compute_reload(Engine* self) {
    Compute* comp = &self->compute;
    if (!file_check_modified(&comp->shader_file)) {
        return true;
    }

    // Load source code
    if (!file_load_content(&comp->shader_file)) {
        return false;
    }

    // Compile to SPIRV
    shaderc_compilation_result_t compilation_result = shaderc_compile_into_spv(
        comp->shader_compiler,
        (char*)comp->shader_file.content,
        comp->shader_file.content_length,
        shaderc_compute_shader,
        comp->shader_file.path,
        "main",
        comp->shader_compile_options
    );
    shaderc_compilation_status status = shaderc_result_get_compilation_status(compilation_result);
    if (status != shaderc_compilation_status_success) {
        INFO("\nShader compilation error:\n%s\n", shaderc_result_get_error_message(compilation_result));

        shaderc_result_release(compilation_result);
        file_free_content(&comp->shader_file);
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
        .num_readonly_storage_buffers   = 1,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers            = 1,
        .threadcount_x                  = 8,
        .threadcount_y                  = 8,
        .threadcount_z                  = 1,
    };
    if (!SDL_CHECK(new_pipeline = SDL_CreateGPUComputePipeline(self->device, &compute_pipeline_create_info))) {
        shaderc_result_release(compilation_result);
        file_free_content(&comp->shader_file);
        return false;
    }

    // Release old pipeline
    if (comp->pipeline) {
        SDL_ReleaseGPUComputePipeline(self->device, comp->pipeline);
    }
    comp->pipeline = new_pipeline;

    shaderc_result_release(compilation_result);
    file_free_content(&comp->shader_file);
    return true;
}

void engine_compute_dispatch(Engine* self, SDL_GPUCommandBuffer* command_buffer) {
    Compute* comp = &self->compute;
    SDL_GPUStorageTextureReadWriteBinding texture_rw_binding = { .texture = self->screen_texture };
    SDL_GPUComputePass* compute_pass = SDL_BeginGPUComputePass(command_buffer, &texture_rw_binding, 1, NULL, 0);
    SDL_BindGPUComputePipeline(compute_pass, comp->pipeline);
    SDL_BindGPUComputeStorageBuffers(compute_pass, 0, &comp->storage_buffer, 1);
    SDL_PushGPUComputeUniformData(command_buffer, 0, &comp->camera_viewport, sizeof(comp->camera_viewport));
    SDL_DispatchGPUCompute(compute_pass, (self->screen_texture_width + 7) / 8, (self->screen_texture_height + 7) / 8, 1);
    SDL_EndGPUComputePass(compute_pass);
}

