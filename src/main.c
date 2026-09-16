#include "engine.h"

int main() {
    Engine engine;
    if (!engine_initialize(&engine, "shaders/rtv.comp"))
        return 1;

    engine_run(&engine);

    engine_shutdown(&engine);
    return 0;
}

