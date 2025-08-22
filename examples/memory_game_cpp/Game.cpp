#include "Game.h"

// Factory function to attach the script to a GameObject
MemoryGameController* AttachMemoryGame(Scene& scene, GameObject& root) {
    auto* script = &root.AddScript<MemoryGameController>();
    (void)scene; // not needed now; scene is accessible through gameObject if needed later
    return script;
}

// Helper for drawing without exposing the full class in a header
void MemoryGame_Draw(MemoryGameController* ctrl, AmeScene2DBatch* batch) {
    if (ctrl) ctrl->Draw(batch);
}

