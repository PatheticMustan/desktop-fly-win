#pragma once

#include "Sim.hpp"
#include <algorithm>

// Converts sim population rates into body commands. Shared by the app loop
// and --behaviortest so both exercise the identical mapping.
class SignalBuilder {
public:
    BrainSignals make(LIFSim& sim, float dt) {
        float diff = sim.rateDNaL - sim.rateDNaR;
        // Slow adaptation (tau ~8 s): the connectome's persistent left/right
        // wiring asymmetry is adapted out, so steady-state walking is straight
        // and only transient DNa asymmetries (visual, stimulation) steer.
        dnaBaseline += (diff - dnaBaseline) * std::min(1.0f, dt / 8.0f);
        
        BrainSignals s;
        s.escape = sim.consumeGF();
        s.nervous = clampf(sim.rateLoom / 80.0f, 0.0f, 1.0f);
        s.turnBias = clampf((diff - dnaBaseline) * 0.04f, -1.0f, 1.0f);
        s.backward = sim.rateMDN > 8.0f;
        s.walkDrive = clampf(sim.rateFwd / 10.0f, 0.0f, 1.3f);
        s.groomDrive = sim.rateGroom / 8.0f;
        s.wingDrive = clampf(sim.rateEscW / 10.0f, 0.0f, 1.3f);
        s.arousal = clampf(sim.ratePop / 20.0f, 0.0f, 1.0f);
        return s;
    }

private:
    float dnaBaseline = 0.0f;
};
