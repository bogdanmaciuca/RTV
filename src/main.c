#include "engine/engine.h"

// TODO:
// - create pipeline for drawing text

int main() {
    Engine* engine = engine_create("shaders/rtv.comp");
    if (!engine)
        return 1;

    engine_run(engine);

    engine_destroy(engine);
    return 0;
}

