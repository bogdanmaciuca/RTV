#include "engine/engine.h"

// TODO:
// - import some voxel models
// - do DDA

int main() {
    Engine* engine = engine_create();
    if (!engine)
        return 1;

    engine_run(engine);

    engine_destroy(engine);
    return 0;
}

