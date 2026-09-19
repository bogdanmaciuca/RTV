#pragma once

#define ARENA_DEFAULT_ALIGN 8

typedef struct Arena_t {
    void* mem;
    size_t offset;
    size_t cap;
} Arena;

bool arena_create(Arena* self, size_t cap);
void arena_destroy(Arena* self);

void* arena_alloc(Arena* self, size_t size);
void* arena_alloc_align(Arena* self, size_t size, size_t align);
void arena_reset(Arena* self);

