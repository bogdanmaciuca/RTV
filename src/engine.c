#include "engine.h"

#include "log.h"
#include "rtv_math.h"
#include "generated/screen_quad_shaders.h"

#define ARENA_CAPACITY 4 * 1024 * 1024

#define DEFAULT_WND_WIDTH 1200
#define DEFAULT_WND_HEIGHT 675

#define TRANSFER_BUFFER_SIZE (4 * 1024 * 1024)

#define DEFAULT_STORAGE_BUFFER_SIZE (512*512)

#define FONT_ATLAS_WIDTH  128
#define FONT_ATLAS_HEIGHT 128
#define FONT_SIZE         16

#define WORLD_UP ((Vec3){ .x = 0.0f, .y = 1.0f, .z = 0.0f })
#define DEFAULT_CAMERA_SENSITIVITY 0.01f
#define DEFAULT_CAMERA_SPEED 0.01f
#define CAMERA_SPEED_CHANGE_SENSITIVITY 0.1f

typedef struct TextVertex_t {
    Vec2 pos;
    Vec2 uv;
} TextVertex;

static bool check_if_file_changed(const char* path, SDL_Time* cached_time);
static bool engine_hot_reload_compute(Engine* self);
static void engine_draw(Engine* self);
static void engine_update_camera(Engine* self);
static void engine_update_mouse(Engine* self);
static bool engine_key_down(Engine* self, SDL_Scancode key);
static bool engine_upload_to_texture(Engine* self, SDL_GPUTexture* texture, u32 width, u32 height, void* data, size_t size);

bool engine_create(Engine* self, const char* compute_shader_path, const char* debug_font_path) {
    ZERO_MEM(self);

    arena_create(&self->arena, ARENA_CAPACITY);

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

    // Transfer buffer
    SDL_GPUTransferBufferCreateInfo transfer_buffer_create_info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = TRANSFER_BUFFER_SIZE,
    };
    if (!SDL_CHECK(self->transfer_buffer = SDL_CreateGPUTransferBuffer(self->device, &transfer_buffer_create_info))) {
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

    // Storage buffer
    SDL_GPUBufferCreateInfo storage_buffer_create_info = {
        .usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
        .size  = DEFAULT_STORAGE_BUFFER_SIZE
    };
    if (!SDL_CHECK(self->storage_buffer = SDL_CreateGPUBuffer(self->device, &storage_buffer_create_info))) {
        return false;
    }

    // Font atlas
    self->font_atlas.width = FONT_ATLAS_WIDTH;
    self->font_atlas.height = FONT_ATLAS_HEIGHT;
    if (!CHECK(self->font_atlas.pixels = malloc(sizeof(u8) * FONT_ATLAS_WIDTH * FONT_ATLAS_HEIGHT), "Could not allocate font atlas memory")) {
        return false;
    }
    // Load font file
    size_t font_file_size;
    byte* font_file_data;
    if (!SDL_CHECK(font_file_data = SDL_LoadFile(debug_font_path, &font_file_size))) {
        return false;
    }
    stbtt_BakeFontBitmap(
        font_file_data,
        0,
        FONT_SIZE,
        self->font_atlas.pixels,
        FONT_ATLAS_WIDTH,
        FONT_ATLAS_HEIGHT,
        32,
        FONT_ATLAS_CHAR_NUM,
        self->font_atlas.data
    );
    SDL_free(font_file_data);
    // Create GPU texture and upload to it
    SDL_GPUTextureCreateInfo font_atlas_texture_create_info = {
        .type                 = SDL_GPU_TEXTURETYPE_2D,
        .format               = SDL_GPU_TEXTUREFORMAT_A8_UNORM,
        .usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width                = self->font_atlas.width,
        .height               = self->font_atlas.height,
        .layer_count_or_depth = 1,
        .num_levels           = 1,
    };
    if (!SDL_CHECK(self->font_atlas_texture = SDL_CreateGPUTexture(self->device, &font_atlas_texture_create_info))) {
        return false;
    }
    engine_upload_to_texture(
        self,
        self->font_atlas_texture,
        self->font_atlas.width,
        self->font_atlas.height,
        self->font_atlas.pixels,
        self->font_atlas.width * self->font_atlas.height * sizeof(u8)
    );
    free(self->font_atlas.pixels);
    self->font_atlas.pixels = NULL;

    // Keyboard state pointer
    self->keys = SDL_GetKeyboardState(&self->keys_num);

    // Camera initial state
    self->camera.forward.z   = 1.0f;
    self->camera.fov_y       = RADIANS(40.0f);
    self->camera.sensitivity = DEFAULT_CAMERA_SENSITIVITY;
    self->camera.speed       = DEFAULT_CAMERA_SPEED;

    return true;
}

void engine_destroy(Engine* self) {
    SDL_ReleaseGPUTexture(self->device, self->font_atlas_texture);

    SDL_ReleaseGPUBuffer(self->device, self->storage_buffer);
    SDL_ReleaseGPUComputePipeline(self->device, self->compute_pipeline);

    shaderc_compile_options_release(self->shader_compile_options);
    shaderc_compiler_release(self->shader_compiler);

    SDL_ReleaseGPUTransferBuffer(self->device, self->transfer_buffer);
    SDL_ReleaseGPUSampler(self->device, self->screen_texture_sampler);
    SDL_ReleaseGPUTexture(self->device, self->screen_texture);
    SDL_ReleaseGPUGraphicsPipeline(self->device, self->graphics_pipeline);

    SDL_ReleaseWindowFromGPUDevice(self->device, self->window);
    SDL_WaitForGPUIdle(self->device);
    SDL_DestroyGPUDevice(self->device);
    SDL_DestroyWindow(self->window);

    SDL_Quit();

    arena_destroy(&self->arena);
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
                case SDL_EVENT_MOUSE_WHEEL:
                    self->camera.speed +=CAMERA_SPEED_CHANGE_SENSITIVITY * event.wheel.integer_y;
                    break;
            }
        }

        engine_update_mouse(self);
        engine_update_camera(self);

        engine_hot_reload_compute(self);

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
    byte* source;
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
        INFO("\nCompilation error:\n%s\n", shaderc_result_get_error_message(compilation_result));

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
        .num_readonly_storage_buffers   = 1,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers            = 1,
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
        SDL_BindGPUComputeStorageBuffers(compute_pass, 0, &self->storage_buffer, 1);
        SDL_PushGPUComputeUniformData(command_buffer, 0, &self->frame_data, sizeof(self->frame_data));
        SDL_DispatchGPUCompute(compute_pass, (self->screen_texture_width + 7) / 8, (self->screen_texture_height + 7) / 8, 1);
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

void engine_update_camera(Engine* self) {
    EngineCamera* cam = &self->camera;

    // Process mouse input
    if (self->mouse_buttons.left) {
        cam->yaw   -= cam->sensitivity * self->mouse_delta.x;
        cam->pitch -= cam->sensitivity * self->mouse_delta.y;
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

    self->frame_data.vp_top_left     = vec4_from_vec3(tl, 1.0f);
    self->frame_data.vp_top_right    = vec4_from_vec3(tr, 1.0f);
    self->frame_data.vp_bottom_left  = vec4_from_vec3(bl, 1.0f);
    self->frame_data.vp_bottom_right = vec4_from_vec3(br, 1.0f);

    self->frame_data.origin          = vec4_from_vec3(cam->pos, 1.0f);
}

void engine_update_mouse(Engine* self) {
    // Position
    u32 button_flags = SDL_GetMouseState(&self->mouse.x, &self->mouse.y);

    static float prev_x = 0.0f;
    static float prev_y = 0.0f;
    static bool is_first_sample = true;

    if (is_first_sample) {
        is_first_sample = false;
        self->mouse_delta = (Vec2){ 0.0f, 0.0f };
    }
    else {
        self->mouse_delta = (Vec2){ self->mouse.x - prev_x, self->mouse.y - prev_y };
    }

    prev_x = self->mouse.x;
    prev_y = self->mouse.y;

    // Buttons
    self->mouse_buttons.left  = button_flags & SDL_BUTTON_MASK(SDL_BUTTON_LEFT);
    self->mouse_buttons.right = button_flags & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT);
}

bool engine_key_down(Engine* self, SDL_Scancode key) {
    if (key < self->keys_num && self->keys[key]) {
        return true;
    }
    return false;
}

static bool engine_upload_to_texture(Engine* self, SDL_GPUTexture* texture, u32 width, u32 height, void* data, size_t size) {
    void* mapped_memory;
    if (!SDL_CHECK(mapped_memory = SDL_MapGPUTransferBuffer(self->device, self->transfer_buffer, false))) {
        return false;
    }
    memcpy(mapped_memory, data, size);
    SDL_UnmapGPUTransferBuffer(self->device, self->transfer_buffer);

    SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(self->device);

    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    SDL_UploadToGPUTexture(
        copy_pass,
        &(SDL_GPUTextureTransferInfo){
            .transfer_buffer = self->transfer_buffer,
            .offset = 0,
        },
        &(SDL_GPUTextureRegion){
            .texture = texture,
            .w = width,
            .h = height,
            .d = 1
        },
        false
    );
    SDL_EndGPUCopyPass(copy_pass);

    if (!SDL_CHECK(SDL_SubmitGPUCommandBuffer(command_buffer))) {
        return false;
    }
    return true;
}

