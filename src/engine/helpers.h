#pragma once

typedef struct Engine Engine;

bool engine_upload_to_texture(Engine* self, SDL_GPUTexture* texture, u32 width, u32 height, void* data, size_t size);

bool engine_upload_to_buffer(Engine* self, SDL_GPUBuffer* buffer, void* data, size_t size);

