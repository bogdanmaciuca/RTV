#include "engine.h"

// TODO:
// - make arrays of resource pointers which are automatically released at the end of the program
// - create pipeline for drawing text

int main() {
    Engine engine;
    if (!engine_create(&engine, "shaders/rtv.comp", "res/SFMonoRegular-ASCII.otf"))
        return 1;

    engine_run(&engine);

    engine_destroy(&engine);
    return 0;
}

