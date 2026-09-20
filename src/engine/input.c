#include "input.h"
#include "internal.h"

bool engine_input_init(Engine* self) {
    self->input.keys = SDL_GetKeyboardState(&self->input.keys_num);
    return true;
}

void engine_input_update(Engine* self) {
    // Position
    u32 button_flags = SDL_GetMouseState(&self->input.mouse.x, &self->input.mouse.y);

    static float prev_x = 0.0f;
    static float prev_y = 0.0f;
    static bool is_first_sample = true;

    if (is_first_sample) {
        is_first_sample = false;
        self->input.mouse_delta = (Vec2){ 0.0f, 0.0f };
    }
    else {
        self->input.mouse_delta = (Vec2){ self->input.mouse.x - prev_x, self->input.mouse.y - prev_y };
    }

    prev_x = self->input.mouse.x;
    prev_y = self->input.mouse.y;

    // Buttons
    self->input.mouse_buttons.left  = button_flags & SDL_BUTTON_MASK(SDL_BUTTON_LEFT);
    self->input.mouse_buttons.right = button_flags & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT);
}

bool engine_key_down(Engine* self, SDL_Scancode key) {
    if (key < self->input.keys_num && self->input.keys[key]) {
        return true;
    }
    return false;
}

