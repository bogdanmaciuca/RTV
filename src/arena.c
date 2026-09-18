#include "arena.h"
#include "log.h"

bool arena_create(Arena* self, size_t cap) {
    if (!CHECK(self->mem = malloc(cap), "")) {
        return false;
    }
    self->offset = 0;
    self->cap = cap;

    return true;
}

void arena_destroy(Arena* self) {
    if (self->mem) {
        free(self->mem);
    }
    ZERO_MEM(self);
}

void* arena_alloc(Arena* self, size_t size) {
    return arena_alloc_align(self, size, ARENA_DEFAULT_ALIGN);
}

void* arena_alloc_align(Arena* self, size_t size, size_t align) {
    void* ptr = ALIGN((uintptr_t)self->mem + self->offset, align);

    if (!CHECK((uintptr_t)ptr + size <= (uintptr_t)self->mem + self->cap, "Not enough memory: %lu (desired) > %lu (actual)", (uintptr_t)ptr + size - (uintptr_t)self->mem, self->cap)) {
        return NULL;
    }

    self->offset = (uintptr_t)ptr - (uintptr_t)self->mem + size;
    return ptr;
}

void arena_reset(Arena* self) {
    self->offset = 0;
}

