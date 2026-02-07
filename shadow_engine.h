#pragma once
#include "engine.h"

struct ShadowEngine {
    EngineState state;
    MatchingEngine engine;

    ShadowEngine() : engine(state) {}

    void apply(const EngineEvent& ev) {
        engine.apply(ev);
    }
};