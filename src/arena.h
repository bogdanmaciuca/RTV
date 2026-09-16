#pragma once

typedef struct Arena_t {
    void*  data;
    size_t size;
    size_t capacity;
} Arena;

bool arena_make(Arena* a, size_t capacity);
void arena_destroy(Arena* a);

void* arena_alloc(Arena* a, size_t size);
bool arena_grow(Arena* a);
void arena_freeall(Arena* a);

