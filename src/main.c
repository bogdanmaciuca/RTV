#include "engine.h"

// TODO:
// - create font atlas texture
// - check in renderdoc if uploaded correctly
// - write a basic draw_line() function
// - create pipeline for drawing text

int main() {
    Engine engine;
    if (!engine_create(&engine, "shaders/rtv.comp", "res/SFMonoRegular-ASCII.otf"))
        return 1;

    engine_run(&engine);

    engine_destroy(&engine);
    return 0;
}

