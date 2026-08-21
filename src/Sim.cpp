#include "Sim.hpp"
#include <fstream>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <windows.h>
#include "../external/nlohmann/json.hpp"

using json = nlohmann::json;

std::optional<std::filesystem::path> findDataDir() {
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        std::filesystem::path p(exePath);
        auto exeDirData = p.parent_path() / "data";
        if (std::filesystem::exists(exeDirData / "circuit.json")) {
            return exeDirData;
        }
    }

    auto cwdData = std::filesystem::current_path() / "data";
    if (std::filesystem::exists(cwdData / "circuit.json")) {
        return cwdData;
    }

    // Also try parent directories (useful during debug/build folder execution)
    auto parentData = std::filesystem::current_path().parent_path() / "data";
    if (std::filesystem::exists(parentData / "circuit.json")) {
        return parentData;
    }

    auto grandParentData = std::filesystem::current_path().parent_path().parent_path() / "data";
    if (std::filesystem::exists(grandParentData / "circuit.json")) {
        return grandParentData;
    }

    return std::nullopt;
}

std::optional<BrainData> loadBrainData() {
    auto dataDirOpt = findDataDir();
    if (!dataDirOpt) return std::nullopt;

    auto dataDir = *dataDirOpt;
    auto pointsPath = dataDir / "brain_points.json";
    auto circuitPath = dataDir / "circuit.json";

    try {
        std::ifstream pf(pointsPath);
        if (!pf.is_open()) return std::nullopt;
        json pj = json::parse(pf);

        BrainPointsFile points;
        points.classes = pj["classes"].get<std::vector<std::string>>();
        for (const auto& pt : pj["points"]) {
            if (pt.size() >= 4) {
                points.points.push_back({
                    pt[0].get<float>(),
                    pt[1].get<float>(),
                    pt[2].get<float>(),
                    pt[3].get<int>()
                });
            }
        }

        std::ifstream cf(circuitPath);
        if (!cf.is_open()) return std::nullopt;
        json cj = json::parse(cf);

        CircuitFile circuit;
        for (const auto& n : cj["neurons"]) {
            CircuitNeuronFile cnf;
            cnf.id = n.value("id", "");
            cnf.type = n.value("type", "");
            cnf.role = n.value("role", "");
            cnf.side = n.value("side", "");
            if (n.contains("pos") && n["pos"].size() == 3) {
                cnf.pos = DirectX::XMFLOAT3(
                    n["pos"][0].get<float>(),
                    n["pos"][1].get<float>(),
                    n["pos"][2].get<float>()
                );
            }
            circuit.neurons.push_back(cnf);
        }

        for (const auto& e : cj["edges"]) {
            if (e.size() >= 3) {
                circuit.edges.push_back({
                    e[0].get<int>(),
                    e[1].get<int>(),
                    e[2].get<float>()
                });
            }
        }

        return BrainData{std::move(points), std::move(circuit)};
    } catch (const std::exception& ex) {
        std::cerr << "Failed to parse brain data: " << ex.what() << std::endl;
        return std::nullopt;
    }
}

void SpikeBus::push(const std::vector<SpikeEvent>& e) {
    std::lock_guard<std::mutex> guard(lock_);
    events_.insert(events_.end(), e.begin(), e.end());
    if (events_.size() > 256) {
        events_.erase(events_.begin(), events_.begin() + (events_.size() - 256));
    }
}

std::vector<SpikeBus::SpikeEvent> SpikeBus::popAll() {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<SpikeEvent> e = std::move(events_);
    events_.clear();
    return e;
}

LIFSim::LIFSim(const CircuitFile& circuit, std::shared_ptr<SpikeBus> spikeBusIn)
    : spikeBus(std::move(spikeBusIn)), rng(std::random_device{}())
{
    n = static_cast<int>(circuit.neurons.size());
    roles.reserve(n);
    types.reserve(n);
    positions.reserve(n);

    for (const auto& nr : circuit.neurons) {
        roles.push_back(nr.role);
        types.push_back(nr.type);
        positions.push_back(nr.pos);
    }

    v.assign(n, 0.0f);
    refr.assign(n, 0.0f);
    inhQueue.assign(5, std::vector<float>(n, 0.0f));

    for (int i = 0; i < n; ++i) {
        const auto& r = roles[i];
        const auto& side = circuit.neurons[i].side;
        if (r == "lc4" || r == "lplc2") {
            if (side == "left") loomLeft.push_back(i);
            else loomRight.push_back(i);
        } else if (r == "gf") {
            gf.push_back(i);
        } else if (r == "dna01" || r == "dna02") {
            if (side == "left") dnaL.push_back(i);
            else dnaR.push_back(i);
        } else if (r == "mdn") {
            mdn.push_back(i);
        } else if (r == "dnp09") {
            fwd.push_back(i);
        } else if (r == "dng11") {
            groom.push_back(i);
        } else if (r == "escw") {
            escw.push_back(i);
        } else if (r == "other") {
            const auto& t = types[i];
            if (t == "ascending") ascend.push_back(i);
            else if (t == "sensory") sens.push_back(i);
        }
    }

    ascendPhase.resize(ascend.size());
    std::uniform_real_distribution<float> piDist(0.0f, 2.0f * 3.14159265358979323846f);
    for (size_t k = 0; k < ascend.size(); ++k) {
        ascendPhase[k] = piDist(rng);
    }

    // Heterogeneous baseline drive
    baseline.assign(n, 0.0f);
    std::uniform_real_distribution<float> otherBaseDist(0.010f, 0.070f);
    for (int i = 0; i < n; ++i) {
        const auto& r = roles[i];
        if (r == "other") {
            baseline[i] = otherBaseDist(rng);
        } else if (r == "lc4" || r == "lplc2") {
            baseline[i] = 0.004f;
        } else if (r == "dna01" || r == "dna02" || r == "mdn" || r == "dng11" || r == "escw") {
            baseline[i] = 0.036f;
        } else if (r == "dnp09") {
            baseline[i] = 0.038f;
        } else {
            baseline[i] = 0.002f; // gf: quiet unless synaptically driven
        }
    }

    // CSR adjacency
    std::vector<int> counts(n, 0);
    for (const auto& e : circuit.edges) {
        if (e.pre >= 0 && e.pre < n) {
            counts[e.pre]++;
        }
    }

    rowStart.assign(n + 1, 0);
    for (int i = 0; i < n; ++i) {
        rowStart[i + 1] = rowStart[i] + counts[i];
    }

    colIdx.assign(circuit.edges.size(), 0);
    w.assign(circuit.edges.size(), 0.0f);

    constexpr float gapJunctionBoost = 6.0f;
    std::vector<int> fill = rowStart;

    for (const auto& e : circuit.edges) {
        int pre = e.pre;
        int post = e.post;
        float weight = e.signedSynCount * weightScale;
        bool electrical = (roles[pre] == "lc4" || roles[pre] == "lplc2" ||
                           (roles[pre] == "other" && types[pre] == "sensory"));
        if (electrical && roles[post] == "gf") {
            weight *= gapJunctionBoost;
        }
        int idx = fill[pre]++;
        colIdx[idx] = static_cast<int32_t>(post);
        w[idx] = weight;
    }
}

bool LIFSim::consumeGF() {
    bool s = gfLatch;
    gfLatch = false;
    return s;
}

void LIFSim::stimulate(const std::vector<int>& indices, float strength, int durationMs) {
    if (indices.empty()) return;
    std::lock_guard<std::mutex> guard(stimLock);
    pendingStims.push_back(Stim{indices, strength, durationMs, 0});
    if (pendingStims.size() > 8) {
        pendingStims.erase(pendingStims.begin());
    }
}

void LIFSim::step(int ms) {
    if (ms <= 0) return;

    {
        std::lock_guard<std::mutex> guard(stimLock);
        for (auto& p : pendingStims) {
            p.untilMs = simMs + p.durationMs;
            activeStims.push_back(p);
        }
        pendingStims.clear();
    }

    activeStims.erase(
        std::remove_if(activeStims.begin(), activeStims.end(), [this](const Stim& s) {
            return simMs >= s.untilMs;
        }),
        activeStims.end()
    );

    std::vector<SpikeBus::SpikeEvent> spikedNow;
    std::uniform_int_distribution<int> burstDist(15000, 40000);

    for (int step = 0; step < ms; ++step) {
        simMs += 1;
        if (simMs >= burstNext) {
            burstUntil = simMs + 400;
            burstNext = simMs + burstDist(rng);
        }
        float p = (simMs < burstUntil ? pNoise * 6.0f : pNoise) * activityScale;

        for (int i = 0; i < n; ++i) {
            if (refr[i] > 0.0f) {
                refr[i] -= 1.0f;
                v[i] *= decay;
                continue;
            }
            float vi = v[i] * decay + baseline[i] * activityScale;
            if (dist01(rng) < p) {
                vi += noiseKick;
            }
            v[i] = vi;
        }

        if (loomL > 0.001f) {
            for (int i : loomLeft) v[i] += loomL * loomGain * sensoryGate;
        }
        if (loomR > 0.001f) {
            for (int i : loomRight) v[i] += loomR * loomGain * sensoryGate;
        }

        // body -> brain: gait rhythm into ascending (proprioceptive) neurons
        if (gaitDrive > 0.001f) {
            float ph = gaitPhase * 2.0f * 3.14159265358979323846f;
            for (size_t k = 0; k < ascend.size(); ++k) {
                int i = ascend[k];
                v[i] += gaitDrive * 0.09f * (0.5f + 0.5f * std::sin(ph + ascendPhase[k]));
            }
        }

        // fast air movement near the fly -> sensory pathway
        if (airPuff > 0.001f) {
            for (int i : sens) v[i] += airPuff * 0.12f * sensoryGate;
        }

        // brain-window click stimulation
        for (const auto& s : activeStims) {
            if (simMs < s.untilMs) {
                for (int i : s.idx) v[i] += s.strength;
            }
        }

        // deliver delayed inhibition scheduled for this millisecond
        for (int j = 0; j < n; ++j) {
            if (inhQueue[qHead][j] != 0.0f) {
                v[j] = std::max(-2.0f, v[j] + inhQueue[qHead][j]);
                inhQueue[qHead][j] = 0.0f;
            }
        }

        std::vector<int> spiked;
        for (int i = 0; i < n; ++i) {
            if (refr[i] <= 0.0f && v[i] >= threshold) {
                v[i] = 0.0f;
                refr[i] = refractoryMs;
                spiked.push_back(i);
            }
        }
        totalSpikes += static_cast<int>(spiked.size());

        int inhSlot = (qHead + inhDelayMs) % static_cast<int>(inhQueue.size());
        for (int i : spiked) {
            for (int k = rowStart[i]; k < rowStart[i + 1]; ++k) {
                int j = colIdx[k];
                if (w[k] >= 0.0f) {
                    v[j] = std::max(-2.0f, v[j] + w[k]);
                } else {
                    inhQueue[inhSlot][j] += w[k];
                }
            }
        }
        qHead = (qHead + 1) % static_cast<int>(inhQueue.size());

        // group rates (Hz per neuron, EMA)
        int cLoom = 0, cDL = 0, cDR = 0, cM = 0, cF = 0, cG = 0, cW = 0;
        for (int i : spiked) {
            const auto& r = roles[i];
            if (r == "lc4" || r == "lplc2") {
                cLoom++;
            } else if (r == "dna01" || r == "dna02") {
                if (std::find(dnaL.begin(), dnaL.end(), i) != dnaL.end()) cDL++;
                else cDR++;
            } else if (r == "mdn") {
                cM++;
            } else if (r == "dnp09") {
                cF++;
            } else if (r == "dng11") {
                cG++;
            } else if (r == "escw") {
                cW++;
            } else if (r == "gf") {
                gfLatch = true;
            }
        }

        float nLoom = static_cast<float>(std::max<size_t>(1, loomLeft.size() + loomRight.size()));
        rateLoom += (static_cast<float>(cLoom) * 1000.0f / nLoom - rateLoom) * rateAlpha;
        rateDNaL += (static_cast<float>(cDL) * 1000.0f / static_cast<float>(std::max<size_t>(1, dnaL.size())) - rateDNaL) * rateAlpha;
        rateDNaR += (static_cast<float>(cDR) * 1000.0f / static_cast<float>(std::max<size_t>(1, dnaR.size())) - rateDNaR) * rateAlpha;
        rateMDN  += (static_cast<float>(cM) * 1000.0f / static_cast<float>(std::max<size_t>(1, mdn.size())) - rateMDN) * rateAlpha;
        rateFwd  += (static_cast<float>(cF) * 1000.0f / static_cast<float>(std::max<size_t>(1, fwd.size())) - rateFwd) * rateAlpha;
        rateGroom += (static_cast<float>(cG) * 1000.0f / static_cast<float>(std::max<size_t>(1, groom.size())) - rateGroom) * rateAlpha;
        rateEscW += (static_cast<float>(cW) * 1000.0f / static_cast<float>(std::max<size_t>(1, escw.size())) - rateEscW) * rateAlpha;
        ratePop  += (static_cast<float>(spiked.size()) * 1000.0f / static_cast<float>(std::max(1, n)) - ratePop) * rateAlpha;

        if (spikeBus) {
            int stride = std::max(1, static_cast<int>(spiked.size()) / 12);
            int i = 0;
            while (i < static_cast<int>(spiked.size())) {
                spikedNow.push_back({spiked[i], roles[spiked[i]] == "gf"});
                i += stride;
            }
        }
    }

    if (spikeBus && !spikedNow.empty()) {
        spikeBus->push(spikedNow);
    }
}
