#include "engine.h"

// TODO:
// - add a camera
// - raytrace a sphere
// - do some voxel shit for fuck's sake

int main() {
    Engine engine;
    if (!engine_initialize(&engine, "shaders/rtv.comp"))
        return 1;

    engine_run(&engine);

    engine_shutdown(&engine);
    return 0;
}

