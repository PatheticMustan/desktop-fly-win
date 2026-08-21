#pragma once

#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <random>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <algorithm>
#include <DirectXMath.h>

inline float clampf(float v, float lo, float hi) {
    return std::clamp(v, lo, hi);
}

struct BrainSignals {
    bool escape = false;        // giant fiber spiked -> takeoff NOW
    float nervous = 0.0f;       // looming-detector population rate, 0..1
    float turnBias = 0.0f;      // rad/s steering from DNa01/DNa02 left-right rate difference
    bool backward = false;      // MDN burst -> backward walking
    float walkDrive = 0.0f;     // DNp09 forward-walking command rate, ~0..1.5
    float groomDrive = 0.0f;    // DNg11 grooming command rate, ~0..1.5
    float wingDrive = 0.0f;     // DNp02/04/11 escape-maneuver DN rate, ~0..1.3
    float arousal = 0.0f;       // whole-population activity, ~0..1
    float tempo = 1.0f;         // thermal "temperature" scaling of locomotion
    bool sleep = false;         // circadian + idle -> sleep-like state
};

struct BrainPoint {
    float x, y, z;
    int classIndex;
};

struct BrainPointsFile {
    std::vector<std::string> classes;
    std::vector<BrainPoint> points;
};

struct CircuitNeuronFile {
    std::string id;
    std::string type;
    std::string role;          // lc4 | lplc2 | gf | dna01 | dna02 | mdn | dnp09 | dng11 | escw | other
    std::string side;          // left | right | center
    DirectX::XMFLOAT3 pos{0, 0, 0};
};

struct CircuitEdgeFile {
    int pre;
    int post;
    float signedSynCount;
};

struct CircuitFile {
    std::vector<CircuitNeuronFile> neurons;
    std::vector<CircuitEdgeFile> edges;
};

struct BrainData {
    BrainPointsFile points;
    CircuitFile circuit;
};

std::optional<std::filesystem::path> findDataDir();
std::optional<BrainData> loadBrainData();

// Thread-safe spike hand-off from the sim (fly render loop) to the brain window.
class SpikeBus {
public:
    struct SpikeEvent {
        int neuron;
        bool isGF;
    };

    void push(const std::vector<SpikeEvent>& e);
    std::vector<SpikeEvent> popAll();

private:
    std::mutex lock_;
    std::vector<SpikeEvent> events_;
};

class LIFSim {
public:
    int n = 0;
    std::vector<std::string> roles;
    std::vector<std::string> types;
    std::vector<DirectX::XMFLOAT3> positions;

    // groups
    std::vector<int> loomLeft;
    std::vector<int> loomRight;
    std::vector<int> gf;
    std::vector<int> dnaL;      // DNa01 + DNa02, left
    std::vector<int> dnaR;      // DNa01 + DNa02, right
    std::vector<int> mdn;
    std::vector<int> fwd;       // DNp09
    std::vector<int> groom;     // DNg11
    std::vector<int> escw;      // DNp02/04/11 escape-maneuver (wing) DNs
    std::vector<int> ascend;    // ascending partners (leg proprioception)
    std::vector<int> sens;      // sensory partners (air-puff pathway)

    // inputs (0..1), set each frame by coordinator
    float loomL = 0.0f;
    float loomR = 0.0f;
    float gaitDrive = 0.0f;     // body walking intensity -> ascending neurons
    float gaitPhase = 0.0f;     // body gait phase 0..1 -> rhythmic proprioception
    float airPuff = 0.0f;       // fast cursor motion near the fly -> sensory neurons
    float activityScale = 1.0f; // circadian / sleep neuromodulation of baseline+noise
    float sensoryGate = 1.0f;   // sleep gates sensory input (raised arousal threshold)

    // outputs (Hz per neuron, EMA)
    float rateLoom = 0.0f;
    float rateDNaL = 0.0f;
    float rateDNaR = 0.0f;
    float rateMDN = 0.0f;
    float rateFwd = 0.0f;
    float rateGroom = 0.0f;
    float rateEscW = 0.0f;
    float ratePop = 0.0f;

    int simMs = 0;
    int totalSpikes = 0;

    std::shared_ptr<SpikeBus> spikeBus;

    LIFSim(const CircuitFile& circuit, std::shared_ptr<SpikeBus> spikeBus = nullptr);

    bool consumeGF();
    void step(int ms);
    void stimulate(const std::vector<int>& indices, float strength, int durationMs);

private:
    // LIF state
    std::vector<float> v;
    std::vector<float> refr;
    std::vector<float> baseline;

    // CSR adjacency, weights pre-scaled
    std::vector<int> rowStart;
    std::vector<int32_t> colIdx;
    std::vector<float> w;

    std::vector<float> ascendPhase;

    bool gfLatch = false;

    // delayed inhibition ring buffer
    static constexpr int inhDelayMs = 4;
    std::vector<std::vector<float>> inhQueue;
    int qHead = 0;

    // params
    static constexpr float decay = 0.9512f;     // exp(-1/20): 20 ms membrane tau, 1 ms step
    static constexpr float threshold = 1.0f;
    static constexpr float refractoryMs = 2.0f;
    static constexpr float weightScale = 0.0008f;
    static constexpr float pNoise = 0.0022f;
    static constexpr float noiseKick = 0.42f;
    static constexpr float loomGain = 0.30f;
    static constexpr float rateAlpha = 1.0f / 120.0f;

    int burstUntil = 0;
    int burstNext = 12000;

    std::mt19937 rng;
    std::uniform_real_distribution<float> dist01{0.0f, 1.0f};

    struct Stim {
        std::vector<int> idx;
        float strength = 0.0f;
        int durationMs = 0;
        int untilMs = 0;
    };
    std::vector<Stim> pendingStims;
    std::vector<Stim> activeStims;
    std::mutex stimLock;
};
