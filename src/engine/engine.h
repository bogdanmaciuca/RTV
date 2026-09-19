#pragma once

typedef struct Engine Engine;

Engine* engine_create(const char* compute_shader_path);
void engine_destroy(Engine* self);

void engine_run(Engine* self);

