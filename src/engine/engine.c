#include "engine.h"

#include "../core/arena.h"
#include "../core/log.h"
#include "rtv_math.h"
#include "generated/screen_quad_shaders.h"

// Arenas
#define ARENA_CAPACITY 4 * 1024 * 1024
#define FRAME_ARENA_CAPACITY 1024 * 1024

// Window
#define WINDOW_DEFAULT_WIDTH 1200
#define WINDOW_DEFAULT_HEIGHT 675

// GPU buffer sizes
#define TRANSFER_BUFFER_DEFAULT_SIZE (4 * 1024 * 1024)
#define STORAGE_BUFFER_DEFAULT_SIZE  (512 * 512)

// Debug text rendering
#define FONT_PATH           "res/SFMonoRegular-ASCII.otf"
#define FONT_SIZE           22
#define FONT_ATLAS_CHAR_NUM 96
#define FONT_ATLAS_WIDTH    196
#define FONT_ATLAS_HEIGHT   196
#define FONT_LINE_SPACING   0
#define FRAME_TEXT_CAPACITY 1024
#define FONT_VERTEX_BUFFER_SIZE (FRAME_TEXT_CAPACITY * 6 * sizeof(f32))

// Camera
#define WORLD_UP ((Vec3){ .x = 0.0f, .y = 1.0f, .z = 0.0f })
#define CAMERA_DEFAULT_SENSITIVITY      0.01f
#define CAMERA_DEFAULT_SPEED            0.02f
#define CAMERA_SPEED_CHANGE_SENSITIVITY 0.025f
#define CAMERA_MIN_SPEED                0.01f
#define CAMERA_MAX_SPEED                0.1f

typedef struct TextVertex {
    Vec2 pos;
    Vec2 uv;
} TextVertex;

typedef struct MouseButtons {
    bool left;
    bool right;
} MouseButtons;

typedef struct Camera {
    Vec3 pos;
    Vec3 forward, right, up;
    f32  yaw, pitch;
    f32  fov_y;
    f32  speed;
    f32  sensitivity;
} Camera;

typedef struct FontAtlas {
    u8* pixels;
    int width, height;
    stbtt_bakedchar data[FONT_ATLAS_CHAR_NUM];
} FontAtlas;

typedef struct FrameData {
    // Camera
    Vec4 vp_top_left;     // World space
    Vec4 vp_top_right;    // Vec4 because of std140 layout
    Vec4 vp_bottom_right;
    Vec4 vp_bottom_left;
    Vec4 origin;
} FrameData;

struct Engine {
    // Arenas
    Arena arena;
    Arena frame_arena;

    // Window and screen texture
    SDL_Window*              window;
    SDL_GPUDevice*           device;
    SDL_GPUTexture*          swapchain_texture;
    SDL_GPUTextureFormat     swapchain_texture_format;
    SDL_GPUGraphicsPipeline* graphics_pipeline;
    SDL_GPUTexture*          screen_texture;
    SDL_GPUSampler*          screen_texture_sampler;
    SDL_GPUTransferBuffer*   transfer_buffer;

    // Rendering compute pipeline
    SDL_GPUComputePipeline*   compute_pipeline;
    const char*               compute_shader_path;
    SDL_Time                  compute_shader_modified_time;
    shaderc_compiler_t        shader_compiler;
    shaderc_compile_options_t shader_compile_options;
    SDL_GPUBuffer*            storage_buffer;
    FrameData                 frame_data;

    // Screen dimensions
    u32 swapchain_texture_width;
    u32 swapchain_texture_height;
    u32 screen_texture_width;
    u32 screen_texture_height;

    // Debug text rendering
    FontAtlas                font_atlas;
    SDL_GPUTexture*          font_atlas_texture;
    SDL_GPUSampler*          font_atlas_sampler;
    SDL_GPUGraphicsPipeline* font_pipeline;
    SDL_GPUBuffer*           font_vertex_buffer;
    char*                    frame_text;
    u32                      frame_text_len;

    // Input
    const bool*  keys;
    int          keys_num;
    Vec2         mouse;
    Vec2         mouse_delta;
    MouseButtons mouse_buttons;

    // Camera
    Camera camera;
};

static bool engine_init_window_and_device(Engine* self);
static bool engine_init_screen_renderer(Engine* self);
static bool engine_init_shaderc(Engine* self);
static bool engine_init_text_renderer(Engine* self);
static bool engine_init_camera(Engine* self);

static bool check_if_file_changed(const char* path, SDL_Time* cached_time);
static bool engine_hot_reload_compute(Engine* self);
static void engine_draw(Engine* self);
static void engine_draw_screen_quad(Engine* self, SDL_GPUCommandBuffer* command_buffer);
static void engine_draw_do_compute(Engine* self, SDL_GPUCommandBuffer* command_buffer);
static Mat4 engine_get_ortho(Engine* self);
static void engine_update_camera(Engine* self);
static void engine_update_mouse(Engine* self);
static bool engine_key_down(Engine* self, SDL_Scancode key);
static bool engine_upload_to_texture(Engine* self, SDL_GPUTexture* texture, u32 width, u32 height, void* data, size_t size);
static bool engine_upload_to_buffer(Engine* self, SDL_GPUBuffer* buffer, void* data, size_t size);
static void engine_text_reset(Engine* self);
static void engine_text_update_vertex_buffer(Engine* self);
static void engine_text_print(Engine* self, const char* fmt, ...);

Engine* engine_create(const char* compute_shader_path) {
    Engine* self;
    if (!CHECK(self = malloc(sizeof(Engine)), "Could not allocate Engine instance")) {
        return NULL;
    };
    ZERO_MEM(self);

    arena_create(&self->arena, ARENA_CAPACITY);
    arena_create(&self->frame_arena, FRAME_ARENA_CAPACITY);

    if (!engine_init_window_and_device(self)) { return NULL; }
    if (!engine_init_screen_renderer(self)) { return NULL; }
    if (!engine_init_shaderc(self)) { return NULL; }
    if (!engine_init_text_renderer(self)) { return NULL; }
    if (!engine_init_camera(self)) { return NULL; }

    // Compute pipeline
    self->compute_shader_path = compute_shader_path;
    if (!engine_hot_reload_compute(self)) {
        return NULL;
    }

    // Storage buffer
    SDL_GPUBufferCreateInfo storage_buffer_create_info = {
        .usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
        .size  = STORAGE_BUFFER_DEFAULT_SIZE
    };
    if (!SDL_CHECK(self->storage_buffer = SDL_CreateGPUBuffer(self->device, &storage_buffer_create_info))) {
        return NULL;
    }

    // Keyboard state pointer
    self->keys = SDL_GetKeyboardState(&self->keys_num);

    return self;
}

void engine_destroy(Engine* self) {
    SDL_ReleaseGPUBuffer(self->device, self->font_vertex_buffer);
    SDL_ReleaseGPUGraphicsPipeline(self->device, self->font_pipeline);
    SDL_ReleaseGPUSampler(self->device, self->font_atlas_sampler);
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
        arena_reset(&self->frame_arena);
        engine_text_reset(self);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    should_close = true;
                    break;
                case SDL_EVENT_MOUSE_WHEEL:
                    self->camera.speed += CAMERA_SPEED_CHANGE_SENSITIVITY * event.wheel.integer_y;
                    self->camera.speed = clamp(self->camera.speed, CAMERA_MIN_SPEED, CAMERA_MAX_SPEED);
                    break;
            }
        }

        engine_update_mouse(self);
        engine_update_camera(self);

        engine_text_print(self, "Hello world!");
        engine_text_print(self, "New line I hope!");
        engine_text_update_vertex_buffer(self);

        engine_hot_reload_compute(self);

        engine_draw(self);
    }
}

static bool engine_init_window_and_device(Engine* self) {
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

static bool engine_init_screen_renderer(Engine* self) {
    // Create graphics pipeline
    SDL_GPUShader* screen_quad_vert_shader;
    SDL_GPUShaderCreateInfo screen_quad_vs_create_info = {
        .code_size  = sizeof(screen_quad_vs_bytecode),
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
        .code_size    = sizeof(screen_quad_fs_bytecode),
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

static bool engine_init_shaderc(Engine* self) {
    if (!CHECK(self->shader_compiler = shaderc_compiler_initialize(), "")) {
        return false;
    }
    if (!CHECK(self->shader_compile_options = shaderc_compile_options_initialize(), "")) {
        return false;
    }
    shaderc_compile_options_set_target_env(self->shader_compile_options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
    shaderc_compile_options_set_optimization_level(self->shader_compile_options, shaderc_optimization_level_performance);

    return true;
}

static bool engine_init_text_renderer(Engine* self) {
    // Font atlas
    self->font_atlas.width = FONT_ATLAS_WIDTH;
    self->font_atlas.height = FONT_ATLAS_HEIGHT;
    if (!CHECK(self->font_atlas.pixels = malloc(sizeof(u8) * FONT_ATLAS_WIDTH * FONT_ATLAS_HEIGHT), "Could not allocate font atlas memory")) {
        return NULL;
    }

    // Load font file
    size_t font_file_size;
    byte* font_file_data;
    if (!SDL_CHECK(font_file_data = SDL_LoadFile(FONT_PATH, &font_file_size))) {
        return NULL;
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
        .format               = SDL_GPU_TEXTUREFORMAT_R8_UNORM,
        .usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width                = self->font_atlas.width,
        .height               = self->font_atlas.height,
        .layer_count_or_depth = 1,
        .num_levels           = 1,
    };
    if (!SDL_CHECK(self->font_atlas_texture = SDL_CreateGPUTexture(self->device, &font_atlas_texture_create_info))) {
        return NULL;
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

    // Font atlas sampler
    SDL_GPUSamplerCreateInfo font_sampler_create_info = {
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
    };
    if (!SDL_CHECK(self->font_atlas_sampler = SDL_CreateGPUSampler(self->device, &font_sampler_create_info))) {
        return false;
    }

    // Vertex shader
    SDL_GPUShader* vert_shader;
    SDL_GPUShaderCreateInfo vs_create_info = {
        .code_size           = sizeof(text_vs_bytecode),
        .code                = (u8*)text_vs_bytecode,
        .entrypoint          = "main",
        .format              = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage               = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_uniform_buffers = 1,
    };
    if (!SDL_CHECK(vert_shader = SDL_CreateGPUShader(self->device, &vs_create_info))) {
        return false;
    }

    // Fragment shader
    SDL_GPUShader* frag_shader;
    SDL_GPUShaderCreateInfo fs_create_info = {
        .code_size    = sizeof(text_fs_bytecode),
        .code         = (u8*)text_fs_bytecode,
        .entrypoint   = "main",
        .format       = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage        = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1,
    };
    if (!SDL_CHECK(frag_shader = SDL_CreateGPUShader(self->device, &fs_create_info))) {
        return false;
    }

    SDL_GPUGraphicsPipelineCreateInfo graphics_pipeline_create_info = {
        .target_info = {
            .num_color_targets = 1,
            .color_target_descriptions = (SDL_GPUColorTargetDescription[]){{
                .format = self->swapchain_texture_format,
                .blend_state = {
                    .enable_blend          = true,
                    .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
                    .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                    .color_blend_op        = SDL_GPU_BLENDOP_ADD,
                    .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                    .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
                    .alpha_blend_op        = SDL_GPU_BLENDOP_ADD,
                }
            }},
        },
        .vertex_input_state = (SDL_GPUVertexInputState){
            .num_vertex_buffers = 1,
            .vertex_buffer_descriptions = (SDL_GPUVertexBufferDescription[]){{
                .slot = 0,
                .pitch = sizeof(TextVertex),
                .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
                .instance_step_rate = 0
            }},
            .num_vertex_attributes = 2,
            .vertex_attributes = (SDL_GPUVertexAttribute[]){
                {
                    .location = 0,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                    .offset = offsetof(TextVertex, pos)
                },
                {
                    .location = 1,
                    .buffer_slot = 0,
                    .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                    .offset = offsetof(TextVertex, uv)
                }
            }
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .vertex_shader = vert_shader,
        .fragment_shader = frag_shader
    };
    if (!SDL_CHECK(self->font_pipeline = SDL_CreateGPUGraphicsPipeline(self->device, &graphics_pipeline_create_info))) {
        return false;
    }
    SDL_ReleaseGPUShader(self->device, vert_shader);
    SDL_ReleaseGPUShader(self->device, frag_shader);

    // Vertex buffer
    SDL_GPUBufferCreateInfo vertex_buffer_create_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size  = FONT_VERTEX_BUFFER_SIZE
    };
    if (!SDL_CHECK(self->font_vertex_buffer = SDL_CreateGPUBuffer(self->device, &vertex_buffer_create_info))) {
        return false;
    }

    return true;
}

static bool engine_init_camera(Engine* self) {
    self->camera.forward.z   = 1.0f;
    self->camera.fov_y       = RADIANS(40.0f);
    self->camera.sensitivity = CAMERA_DEFAULT_SENSITIVITY;
    self->camera.speed       = CAMERA_DEFAULT_SPEED;

    return true;
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
    self->swapchain_texture = swapchain_texture;

    engine_draw_do_compute(self, command_buffer);
    engine_draw_screen_quad(self, command_buffer);

    SDL_SubmitGPUCommandBuffer(command_buffer);
}

static void engine_draw_screen_quad(Engine* self, SDL_GPUCommandBuffer* command_buffer) {
    if (!self->swapchain_texture) {
        return;
    }
    SDL_GPUColorTargetInfo color_target_info = {
        .texture = self->swapchain_texture,
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE
    };
    SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(command_buffer, &color_target_info, 1, NULL);

    // Draw compute results
    SDL_BindGPUGraphicsPipeline(render_pass, self->graphics_pipeline);
    SDL_GPUTextureSamplerBinding screen_texture_sampler_binding = {
        .texture = self->screen_texture,
        .sampler = self->screen_texture_sampler
    };
    SDL_BindGPUFragmentSamplers(render_pass, 0, &screen_texture_sampler_binding, 1);
    SDL_DrawGPUPrimitives(render_pass, 3, 1, 0, 0);

    // Draw debug text on top
    SDL_BindGPUGraphicsPipeline(render_pass, self->font_pipeline);
    SDL_GPUBufferBinding text_vertex_buffer_binding = { .buffer = self->font_vertex_buffer };
    SDL_BindGPUVertexBuffers(render_pass, 0, &text_vertex_buffer_binding, 1);
    Mat4 ortho_projection = engine_get_ortho(self);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &ortho_projection, sizeof(ortho_projection));
    SDL_GPUTextureSamplerBinding font_atlas_sampler_binding = {
        .texture = self->font_atlas_texture,
        .sampler = self->font_atlas_sampler
    };
    SDL_BindGPUFragmentSamplers(render_pass, 0, &font_atlas_sampler_binding, 1);
    SDL_DrawGPUPrimitives(render_pass, self->frame_text_len * 6, 1, 0, 0);

    SDL_EndGPURenderPass(render_pass);
}

static void engine_draw_do_compute(Engine* self, SDL_GPUCommandBuffer* command_buffer) {
    SDL_GPUStorageTextureReadWriteBinding texture_rw_binding = { .texture = self->screen_texture };
    SDL_GPUComputePass* compute_pass = SDL_BeginGPUComputePass(command_buffer, &texture_rw_binding, 1, NULL, 0);
    SDL_BindGPUComputePipeline(compute_pass, self->compute_pipeline);
    SDL_BindGPUComputeStorageBuffers(compute_pass, 0, &self->storage_buffer, 1);
    SDL_PushGPUComputeUniformData(command_buffer, 0, &self->frame_data, sizeof(self->frame_data));
    SDL_DispatchGPUCompute(compute_pass, (self->screen_texture_width + 7) / 8, (self->screen_texture_height + 7) / 8, 1);
    SDL_EndGPUComputePass(compute_pass);
}

static Mat4 engine_get_ortho(Engine* self) {
    float width  = (float)self->swapchain_texture_width;
    float height = (float)self->swapchain_texture_height;

    // Near and far depth bounds (0 to 1 for SDL_GPU)
    float near_z = 0.0f;
    float far_z  = 1.0f;
    float fn     = far_z - near_z;

    Mat4 res = {0};

    // Column 0 (X scale & translation)
    res.m[0][0] = 2.0f / width;

    // Column 1 (Y scale & translation: flips Y so 0 is top)
    res.m[1][1] = -2.0f / height;

    // Column 2 (Z mapping: [0, 1] depth range)
    res.m[2][2] = 1.0f / fn;

    // Column 3 (Translation / Offsets)
    res.m[3][0] = -1.0f;
    res.m[3][1] =  1.0f;
    res.m[3][2] = -near_z / fn;
    res.m[3][3] =  1.0f;

    return res;
}

void engine_update_camera(Engine* self) {
    Camera* cam = &self->camera;

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

static bool engine_upload_to_buffer(Engine* self, SDL_GPUBuffer* buffer, void* data, size_t size) {
    void* mapped_memory;
    if (!SDL_CHECK(mapped_memory = SDL_MapGPUTransferBuffer(self->device, self->transfer_buffer, false))) {
        return false;
    }
    memcpy(mapped_memory, data, size);
    SDL_UnmapGPUTransferBuffer(self->device, self->transfer_buffer);

    SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(self->device);
    if (!command_buffer) {
        return false;
    }

    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(command_buffer);
    SDL_UploadToGPUBuffer(
        copy_pass,
        &(SDL_GPUTransferBufferLocation){
            .transfer_buffer = self->transfer_buffer,
            .offset = 0,
        },
        &(SDL_GPUBufferRegion){
            .buffer = buffer,
            .size = (u32)size,
        },
        false
    );
    SDL_EndGPUCopyPass(copy_pass);

    if (!SDL_CHECK(SDL_SubmitGPUCommandBuffer(command_buffer))) {
        return false;
    }
    return true;
}

static void engine_text_reset(Engine* self) {
    self->frame_text = arena_alloc(&self->frame_arena, FRAME_TEXT_CAPACITY);
    self->frame_text_len = 0;
}

void engine_text_update_vertex_buffer(Engine* self) {
    TextVertex* vertices = arena_alloc(&self->frame_arena, FONT_VERTEX_BUFFER_SIZE);
    int vertices_num = 0;

    float offset_x = 0;
    float offset_y = FONT_SIZE;

    for (int i = 0; i < self->frame_text_len; i++) {
        char c = self->frame_text[i];

        if (c == '\n') {
            offset_x = 0.0f;
            offset_y += FONT_SIZE + FONT_LINE_SPACING;
            continue;
        }

        if (c < 32) {
            c = '?';
        }

        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(
            self->font_atlas.data,
            self->font_atlas.width,
            self->font_atlas.height,
            c - 32,
            &offset_x,
            &offset_y,
            &q,
            1
        );

        // TODO: use quads directly
        vertices[vertices_num++] = (TextVertex){ {q.x0, q.y0}, {q.s0, q.t0} };
        vertices[vertices_num++] = (TextVertex){ {q.x0, q.y1}, {q.s0, q.t1} };
        vertices[vertices_num++] = (TextVertex){ {q.x1, q.y1}, {q.s1, q.t1} };
        vertices[vertices_num++] = (TextVertex){ {q.x0, q.y0}, {q.s0, q.t0} };
        vertices[vertices_num++] = (TextVertex){ {q.x1, q.y1}, {q.s1, q.t1} };
        vertices[vertices_num++] = (TextVertex){ {q.x1, q.y0}, {q.s1, q.t0} };
    }

    engine_upload_to_buffer(self, self->font_vertex_buffer, vertices, vertices_num * sizeof(TextVertex));
}

void engine_text_print(Engine* self, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int msg_len = vsnprintf(self->frame_text + self->frame_text_len, FRAME_TEXT_CAPACITY - self->frame_text_len, fmt, args);
    va_end(args);

    // Old text + new text + '\n' + '\0'
    if (self->frame_text_len + msg_len + 2 > FRAME_TEXT_CAPACITY) {
        WARN("Debug text exceeds capacity: %d > %d", self->frame_text_len + msg_len + 2, FRAME_TEXT_CAPACITY);
    }

    self->frame_text_len += msg_len;
    self->frame_text[self->frame_text_len++] = '\n';
    self->frame_text[self->frame_text_len] = '\0';
}

