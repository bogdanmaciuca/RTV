#pragma once
#include "rtv_math.h"

typedef struct Engine Engine;

typedef struct MouseButtons {
    bool left;
    bool right;
} MouseButtons;

typedef struct Input {
    const bool*  keys;
    int          keys_num;
    Vec2         mouse;
    Vec2         mouse_delta;
    MouseButtons mouse_buttons;
} Input;

bool engine_input_init(Engine* self);
void engine_input_update(Engine* self);
bool engine_key_down(Engine* self, SDL_Scancode key);
