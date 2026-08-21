#include "Sim.hpp"
#include <iostream>
#include <cstdio>
#include <limits>

int runSimtest() {
    auto dataOpt = loadBrainData();
    if (!dataOpt) {
        std::cerr << "no data/ — run etl.py first" << std::endl;
        return 1;
    }
    const auto& data = *dataOpt;
    LIFSim sim(data.circuit, nullptr);

    std::printf("circuit: %d neurons | loom L/R: %zu/%zu | GF: %zu | DNa L/R: %zu/%zu | MDN: %zu | DNp09: %zu | DNg11: %zu | escW: %zu | ascend: %zu | sens: %zu\n",
                sim.n, sim.loomLeft.size(), sim.loomRight.size(), sim.gf.size(),
                sim.dnaL.size(), sim.dnaR.size(), sim.mdn.size(), sim.fwd.size(),
                sim.groom.size(), sim.escw.size(), sim.ascend.size(), sim.sens.size());

    // Phase 1: 4 s spontaneous activity
    int gfSpont = 0;
    for (int step = 0; step < 40; ++step) {
        sim.step(100);
        if (sim.consumeGF()) gfSpont++;
    }
    float popHz = static_cast<float>(sim.totalSpikes) / 4.0f / static_cast<float>(sim.n);
    std::printf("spontaneous 4s: pop %.2f Hz/neuron, LC %.1f Hz, DNa02 L/R %.1f/%.1f Hz, MDN %.1f Hz, GF spikes: %d\n",
                popHz, sim.rateLoom, sim.rateDNaL, sim.rateDNaR, sim.rateMDN, gfSpont);

    // Phase 2: abrupt loom, as produced by a cursor lunge (step, not ramp)
    int gfLatencyMs = -1;
    int gfLoom = 0;
    for (int ms = 0; ms < 400; ++ms) {
        sim.loomL = 1.0f;
        sim.loomR = 0.5f;
        sim.step(1);
        if (sim.consumeGF()) {
            gfLoom++;
            if (gfLatencyMs < 0) gfLatencyMs = ms;
        }
    }
    sim.loomL = 0; sim.loomR = 0;
    std::printf("abrupt loom 0.4s: LC rate %.1f Hz, GF spikes %d, first at %d ms\n",
                sim.rateLoom, gfLoom, gfLatencyMs);

    // Phase 3: 20 s with walking proprioception; do behavior states emerge?
    int walkOn = 0, groomOn = 0, samples = 0;
    float fwdMin = std::numeric_limits<float>::infinity();
    float fwdMax = 0.0f;
    for (int ms = 0; ms < 20000; ++ms) {
        sim.gaitDrive = 0.5f;
        sim.gaitPhase = static_cast<float>(ms % 125) / 125.0f; // 8 Hz gait
        sim.step(1);
        if (ms % 10 == 0) {
            samples++;
            if (sim.rateFwd / 10.0f > 0.22f) walkOn++;
            if (sim.rateGroom / 8.0f > 0.5f) groomOn++;
            fwdMin = std::min(fwdMin, sim.rateFwd);
            fwdMax = std::max(fwdMax, sim.rateFwd);
        }
    }
    std::printf("behavior 20s: walk-drive on %.0f%%, groom-drive on %.0f%%, DNp09 %.1f-%.1f Hz, pop %.1f Hz\n",
                100.0f * static_cast<float>(walkOn) / static_cast<float>(samples),
                100.0f * static_cast<float>(groomOn) / static_cast<float>(samples),
                fwdMin, fwdMax, sim.ratePop);

    // Phase 3b: midday siesta must slow the fly down, not paralyze it
    sim.activityScale = 1.0f - (1.0f - 0.55f) * 0.35f; // = 0.84, the compressed siesta scale
    int siestaWalkOn = 0, siestaSamples = 0;
    for (int ms = 0; ms < 15000; ++ms) {
        sim.step(1);
        if (ms % 10 == 0) {
            siestaSamples++;
            if (sim.rateFwd / 10.0f > 0.22f) siestaWalkOn++;
        }
    }
    sim.activityScale = 1.0f;
    float siestaPct = 100.0f * static_cast<float>(siestaWalkOn) / static_cast<float>(siestaSamples);
    std::printf("siesta 15s (scale 0.84): walk-drive on %.0f%%\n", siestaPct);

    // Phase 4: air puff (fast cursor whoosh) for 1 s — wind startle pathway
    int gfPuff = 0;
    for (int ms = 0; ms < 1000; ++ms) {
        sim.airPuff = 1.0f;
        sim.step(1);
        if (sim.consumeGF()) gfPuff++;
    }
    sim.airPuff = 0.0f;
    std::printf("air puff 1s: GF spikes %d\n", gfPuff);

    // Phase 5: gentle left-eye-only loom 1 s — steering response probe
    for (int ms = 0; ms < 500; ++ms) {
        sim.step(1);
        sim.consumeGF();
    }
    float diff0 = sim.rateDNaL - sim.rateDNaR;
    for (int ms = 0; ms < 1000; ++ms) {
        sim.loomL = 0.30f;
        sim.loomR = 0.0f;
        sim.step(1);
        sim.consumeGF();
    }
    float diff1 = sim.rateDNaL - sim.rateDNaR;
    sim.loomL = 0.0f;
    std::printf("left-eye loom: DNa L-R rate diff %+.1f -> %+.1f Hz, LC %.1f Hz\n",
                diff0, diff1, sim.rateLoom);

    // Phase 6: click-stimulation probes
    sim.stimulate(sim.gf, 0.5f, 40);
    sim.step(60);
    bool gfStim = sim.consumeGF();

    sim.stimulate(sim.groom, 0.25f, 400);
    sim.step(400);
    float groomStim = sim.rateGroom;
    sim.consumeGF();
    std::printf("click probes: GF cluster -> spike %s, DNg11 cluster -> groom rate %.0f Hz\n",
                gfStim ? "yes" : "NO", groomStim);

    bool pass = (gfSpont == 0 && gfLoom > 0 && walkOn > 0 && gfStim && siestaPct > 3.0f);
    std::printf("%s: GF silent at rest, fires on loom; locomotor drive fluctuates; stim works; siesta alive\n",
                pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
