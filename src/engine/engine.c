#include "engine.h"

#include "../core/arena.h"
#include "../core/log.h"
#include "camera.h"
#include "input.h"
#include "internal.h"
#include "rtv_math.h"

#define ARENA_CAPACITY 4 * 1024 * 1024
#define FRAME_ARENA_CAPACITY 1024 * 1024

static void engine_draw(Engine* self);
static void engine_draw_screen_quad(Engine* self, SDL_GPUCommandBuffer* command_buffer);

Engine* engine_create() {
    Engine* self;
    if (!CHECK(self = malloc(sizeof(Engine)), "Could not allocate Engine instance")) {
        return NULL;
    };
    ZERO_MEM(self);

    arena_create(&self->arena, ARENA_CAPACITY);
    arena_create(&self->frame_arena, FRAME_ARENA_CAPACITY);

    if (!engine_init_window_and_device(self)) { return NULL; }
    if (!engine_init_screen_renderer(self)) { return NULL; }
    if (!engine_compute_init(self)) { return NULL; }
    if (!engine_camera_init(self)) { return NULL; }
    if (!engine_input_init(self)) { return NULL; }
    if (!engine_text_init(self)) { return NULL; }

    return self;
}

void engine_destroy(Engine* self) {
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
                    engine_camera_update_speed(self, event.wheel.integer_y);
                    break;
            }
        }

        engine_input_update(self);
        engine_camera_update(self);

        engine_text_print(self, "Hello world!");
        engine_text_print(self, "New line I hope!");

        engine_compute_reload(self);

        engine_draw(self);
    }
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

    engine_compute_dispatch(self, command_buffer);
    engine_text_update_vertex_buffer(self);
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
    SDL_BindGPUGraphicsPipeline(render_pass, self->text.pipeline);
    SDL_GPUBufferBinding text_vertex_buffer_binding = { .buffer = self->text.vertex_buffer };
    SDL_BindGPUVertexBuffers(render_pass, 0, &text_vertex_buffer_binding, 1);
    Mat4 ortho_projection = proj_ortho(self->swapchain_texture_width, self->swapchain_texture_height);
    SDL_PushGPUVertexUniformData(command_buffer, 0, &ortho_projection, sizeof(ortho_projection));
    SDL_GPUTextureSamplerBinding font_atlas_sampler_binding = {
        .texture = self->text.atlas_texture,
        .sampler = self->text.atlas_sampler
    };
    SDL_BindGPUFragmentSamplers(render_pass, 0, &font_atlas_sampler_binding, 1);
    SDL_DrawGPUPrimitives(render_pass, self->text.frame_text_len * 6, 1, 0, 0);

    SDL_EndGPURenderPass(render_pass);
}

