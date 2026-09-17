#pragma once

#define ARENA_DEFAULT_ALIGNMENT 16

typedef struct Arena_t {
    void* mem;
    size_t offset;
    size_t cap;
} Arena;

bool arena_create(Arena* self, size_t cap);
void arena_destroy(Arena* self);

void* arena_alloc(Arena* self, size_t size);

