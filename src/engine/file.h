#pragma once

typedef struct File {
    const char* path;
    SDL_Time    cached_last_mod;
    byte*       content;
    size_t      content_length;
} File;

File file_create(const char* path);
void file_destroy(File* self);

SDL_Time file_get_modified_time(File* self);
bool file_check_modified(File* self);

bool file_load_content(File* self);
void file_free_content(File* self);

