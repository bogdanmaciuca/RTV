#pragma once
#include "rtv_math.h"

#define FONT_PATH         "res/SFMonoRegular-ASCII.otf"
#define FONT_SIZE         22
#define FONT_LINE_SPACING 0

#define FONT_ATLAS_CHAR_NUM     96
#define FONT_ATLAS_WIDTH        196
#define FONT_ATLAS_HEIGHT       196
#define FRAME_TEXT_CAPACITY     1024
#define FONT_VERTEX_BUFFER_SIZE (FRAME_TEXT_CAPACITY * 6 * sizeof(f32))

typedef struct Engine Engine;

typedef struct TextVertex {
    Vec2 pos;
    Vec2 uv;
} TextVertex;

typedef struct Text {
    stbtt_bakedchar baked[FONT_ATLAS_CHAR_NUM];

    SDL_GPUTexture*          atlas_texture;
    SDL_GPUSampler*          atlas_sampler;
    SDL_GPUGraphicsPipeline* pipeline;
    SDL_GPUBuffer*           vertex_buffer;
    u32                      vertices_num;
    char*                    frame_text;
    u32                      frame_text_len;
} Text;

bool engine_text_init(Engine* self);

void engine_text_reset(Engine* self);
void engine_text_update_vertex_buffer(Engine* self);
void engine_text_print(Engine* self, const char* fmt, ...);

