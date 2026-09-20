#include "text.h"
#include "../core/log.h"
#include "file.h"
#include "helpers.h"
#include "internal.h"
#include "generated/shaders_bytecode.h"

bool engine_text_init(Engine* self) {
    Text* text = &self->text;

    // Load font file
    File font_file = file_create(FONT_PATH);
    if (!file_load_content(&font_file)) {
        return false;
    }

    u8* atlas_pixels;
    if (!CHECK(atlas_pixels = malloc(sizeof(u8) * FONT_ATLAS_WIDTH * FONT_ATLAS_HEIGHT), "Could not allocate font atlas memory")) {
        return false;
    }
    stbtt_BakeFontBitmap(
        font_file.content,
        0,
        FONT_SIZE,
        atlas_pixels,
        FONT_ATLAS_WIDTH,
        FONT_ATLAS_HEIGHT,
        32,
        FONT_ATLAS_CHAR_NUM,
        text->baked
    );
    file_free_content(&font_file);

    // Create GPU texture and upload to it
    SDL_GPUTextureCreateInfo atlas_texture_create_info = {
        .type                 = SDL_GPU_TEXTURETYPE_2D,
        .format               = SDL_GPU_TEXTUREFORMAT_R8_UNORM,
        .usage                = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width                = FONT_ATLAS_WIDTH,
        .height               = FONT_ATLAS_HEIGHT,
        .layer_count_or_depth = 1,
        .num_levels           = 1,
    };
    if (!SDL_CHECK(text->atlas_texture = SDL_CreateGPUTexture(self->device, &atlas_texture_create_info))) {
        return NULL;
    }
    engine_upload_to_texture(
        self,
        text->atlas_texture,
        FONT_ATLAS_WIDTH,
        FONT_ATLAS_HEIGHT,
        atlas_pixels,
        FONT_ATLAS_WIDTH * FONT_ATLAS_HEIGHT * sizeof(u8)
    );
    free(atlas_pixels);
    atlas_pixels = NULL;

    // Atlas sampler
    SDL_GPUSamplerCreateInfo font_sampler_create_info = {
        .min_filter = SDL_GPU_FILTER_LINEAR,
        .mag_filter = SDL_GPU_FILTER_LINEAR,
    };
    if (!SDL_CHECK(text->atlas_sampler = SDL_CreateGPUSampler(self->device, &font_sampler_create_info))) {
        return false;
    }

    // Vertex shader
    SDL_GPUShader* vert_shader;
    SDL_GPUShaderCreateInfo vs_create_info = {
        .code_size           = text_vs_bytecode_size,
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
        .code_size    = text_fs_bytecode_size,
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
    if (!SDL_CHECK(text->pipeline = SDL_CreateGPUGraphicsPipeline(self->device, &graphics_pipeline_create_info))) {
        return false;
    }
    SDL_ReleaseGPUShader(self->device, vert_shader);
    SDL_ReleaseGPUShader(self->device, frag_shader);

    // Vertex buffer
    SDL_GPUBufferCreateInfo vertex_buffer_create_info = {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX,
        .size  = FONT_VERTEX_BUFFER_SIZE
    };
    if (!SDL_CHECK(text->vertex_buffer = SDL_CreateGPUBuffer(self->device, &vertex_buffer_create_info))) {
        return false;
    }

    return true;
}

void engine_text_reset(Engine* self) {
    self->text.frame_text = arena_alloc(&self->frame_arena, FRAME_TEXT_CAPACITY);
    self->text.frame_text_len = 0;
}

void engine_text_update_vertex_buffer(Engine* self) {
    Text* text = &self->text;
    TextVertex* vertices = arena_alloc(&self->frame_arena, FONT_VERTEX_BUFFER_SIZE);
    text->vertices_num = 0;

    float offset_x = 0;
    float offset_y = FONT_SIZE;

    for (int i = 0; i < text->frame_text_len; i++) {
        char c = text->frame_text[i];

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
            text->baked,
            FONT_ATLAS_WIDTH,
            FONT_ATLAS_HEIGHT,
            c - 32,
            &offset_x,
            &offset_y,
            &q,
            1
        );

        // TODO: use quads directly
        vertices[text->vertices_num++] = (TextVertex){ {q.x0, q.y0}, {q.s0, q.t0} };
        vertices[text->vertices_num++] = (TextVertex){ {q.x0, q.y1}, {q.s0, q.t1} };
        vertices[text->vertices_num++] = (TextVertex){ {q.x1, q.y1}, {q.s1, q.t1} };
        vertices[text->vertices_num++] = (TextVertex){ {q.x0, q.y0}, {q.s0, q.t0} };
        vertices[text->vertices_num++] = (TextVertex){ {q.x1, q.y1}, {q.s1, q.t1} };
        vertices[text->vertices_num++] = (TextVertex){ {q.x1, q.y0}, {q.s1, q.t0} };
    }

    engine_upload_to_buffer(self, text->vertex_buffer, vertices, text->vertices_num * sizeof(TextVertex));
}

void engine_text_print(Engine* self, const char* fmt, ...) {
    Text* text = &self->text;

    va_list args;
    va_start(args, fmt);
    int msg_len = vsnprintf(text->frame_text + text->frame_text_len, FRAME_TEXT_CAPACITY - text->frame_text_len, fmt, args);
    va_end(args);

    // Old text + new text + '\n' + '\0'
    if (text->frame_text_len + msg_len + 2 > FRAME_TEXT_CAPACITY) {
        WARN("Debug text exceeds capacity: %d > %d", text->frame_text_len + msg_len + 2, FRAME_TEXT_CAPACITY);
    }

    text->frame_text_len += msg_len;
    text->frame_text[text->frame_text_len++] = '\n';
    text->frame_text[text->frame_text_len] = '\0';
}

