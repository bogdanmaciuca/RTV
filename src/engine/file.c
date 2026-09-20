#include "file.h"
#include "../core/log.h"

File file_create(const char* path) {
    File f;
    ZERO_MEM(&f);
    f.path = path;
    f.cached_last_mod = file_get_modified_time(&f);
    return f;
}

void file_destroy(File* self) {
    if (self->content) {
        SDL_free(self->content);
    }
    ZERO_MEM(self);
}

SDL_Time file_get_modified_time(File* self) {
    SDL_PathInfo info;
    SDL_CHECK(SDL_GetPathInfo(self->path, &info));
    return info.modify_time;
}

bool file_check_modified(File* self) {
    SDL_Time modified = file_get_modified_time(self);
    if (modified > self->cached_last_mod) {
        self->cached_last_mod = modified;
        return true;
    }
    return false;
}

bool file_load_content(File* self) {
    return SDL_CHECK(self->content = SDL_LoadFile(self->path, &self->content_length));
}

void file_free_content(File* self) {
    SDL_free(self->content);
    self->content_length = 0;
}

