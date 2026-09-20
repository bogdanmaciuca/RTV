#pragma once

typedef struct Engine Engine;

Engine* engine_create();
void engine_destroy(Engine* self);

void engine_run(Engine* self);

