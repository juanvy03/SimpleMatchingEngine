#include "engine.h"
#include <fstream>

int main() {
    EngineState state{};
    MatchingEngine engine(state);

    std::ifstream log("journal.bin", std::ios::binary);

    EngineEvent event;
    while (log.read(reinterpret_cast<char*>(&event), sizeof(event))) {
        engine.apply(event);
    }

    return 0;
}