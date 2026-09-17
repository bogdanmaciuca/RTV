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
    void* ptr = ALIGN((uintptr_t)self->mem + self->offset, ARENA_DEFAULT_ALIGNMENT);

    if (!CHECK((uintptr_t)ptr + size <= (uintptr_t)self->mem + self->cap, "Not enough memory")) {
        return NULL;
    }

    self->offset = (uintptr_t)ptr - (uintptr_t)self->mem + size;
    return ptr;
}

