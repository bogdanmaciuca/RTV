#include "arena.h"

#include "log.h"

#define ALIGN 8

bool arena_make(Arena* a, size_t capacity) {
    a->data = malloc(capacity);
    if (!a->data) {
        ERROR("Could not allocate memory.");
        return false;
    }
    a->size = 0;
    a->capacity = capacity;
    return true;
}

void arena_destroy(Arena* a) {
    if (a->data)
        free(a->data);
    a->data = NULL;
}

void* arena_alloc(Arena* a, size_t size) {
    size = (size + ALIGN - 1) & ~(ALIGN - 1);
    if (a->size + size > a->capacity && !arena_grow(a))
        return NULL;
    void* ptr = (char*)a->data + a->size;
    a->size += size;
    return ptr;
}

bool arena_grow(Arena* a) {
    size_t new_cap = a->capacity * 2;
    void* new_data = realloc(a->data, new_cap);
    if (!new_data) {
        ERROR("Could not grow arena.");
        return false;
    }
    a->data = new_data;
    a->capacity = new_cap;
    return true;
}

void arena_freeall(Arena* a) {
    a->size = 0;
}


