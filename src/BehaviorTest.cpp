#include "Sim.hpp"
#include "FlyModel.hpp"
#include "SignalBuilder.hpp"
#include "Environment.hpp"
#include <iostream>
#include <cstdio>
#include <cmath>
#include <functional>

int runBehaviorTest() {
    auto dataOpt = loadBrainData();
    if (!dataOpt) {
        std::cerr << "no data/ — run etl.py first\n";
        return 1;
    }
    const auto& data = *dataOpt;
    Size2D bounds{1512, 982};
    float dt = 1.0f / 60.0f;
    int failures = 0;

    auto scenario = [&](const std::string& name,
                        std::function<void(LIFSim&)> stim,
                        float hold,
                        std::function<void(Fly&)> setup,
                        std::function<bool(const Fly&)> check,
                        std::function<std::string(const Fly&)> describe)
    {
        LIFSim sim(data.circuit, nullptr);
        SignalBuilder builder;
        Fly fly(Point2D{0, 0});
        fly.state = Fly::State::Idle;
        fly.speed = 0.0f;
        if (setup) setup(fly);

        // settle the network, drain startup latch
        sim.step(400);
        sim.consumeGF();
        stim(sim);

        bool passed = false;
        int frames = static_cast<int>(hold / dt);
        while (frames > 0) {
            frames--;
            sim.step(static_cast<int>(std::round(dt * 1000.0f)));
            BrainSignals s = builder.make(sim, dt);
            fly.update(dt, bounds, std::nullopt, s);
            if (check(fly)) {
                passed = true;
                break;
            }
        }
        if (!passed) failures++;
        std::printf("%s  %s: %s\n", passed ? "PASS" : "FAIL", name.c_str(), describe(fly).c_str());
    };

    scenario("GF stim -> escape flight",
             [](LIFSim& s) { s.stimulate(s.gf, 0.5f, 40); }, 0.5f,
             nullptr,
             [](const Fly& f) { return f.state == Fly::State::Flying; },
             [](const Fly& f) { return (f.state == Fly::State::Flying) ? "flying" : "not flying"; });

    scenario("DNg11 stim -> grooming",
             [](LIFSim& s) { s.stimulate(s.groom, 0.25f, 600); }, 1.5f,
             nullptr,
             [](const Fly& f) { return f.state == Fly::State::Grooming; },
             [](const Fly& f) { return (f.state == Fly::State::Grooming) ? "grooming" : "not grooming"; });

    scenario("DNp09 stim -> walks, speed rises (capped)",
             [](LIFSim& s) { s.stimulate(s.fwd, 0.25f, 1200); }, 1.5f,
             nullptr,
             [](const Fly& f) { return f.state == Fly::State::Walking && f.speed > 40.0f && f.speed < 100.0f; },
             [](const Fly& f) {
                 char buf[64];
                 std::snprintf(buf, sizeof(buf), "state=%d speed=%.1f", static_cast<int>(f.state), f.speed);
                 return std::string(buf);
             });

    scenario("MDN stim (from idle) -> backward walk",
             [](LIFSim& s) { s.stimulate(s.mdn, 0.3f, 600); }, 1.2f,
             nullptr,
             [](const Fly& f) { return f.backwardTimer > 0.0f; },
             [](const Fly& f) {
                 char buf[64];
                 std::snprintf(buf, sizeof(buf), "backwardTimer=%.2f", f.backwardTimer);
                 return std::string(buf);
             });

    float heading0 = 0.0f;
    scenario("DNa-left stim -> left (CCW) turn while walking",
             [](LIFSim& s) { s.stimulate(s.dnaL, 0.3f, 900); }, 1.4f,
             [&](Fly& f) {
                 f.state = Fly::State::Walking;
                 f.speed = 30.0f;
                 f.heading = 0.0f;
                 heading0 = 0.0f;
             },
             [&](const Fly& f) { return (f.heading - heading0) > 0.25f; },
             [&](const Fly& f) {
                 char buf[64];
                 std::snprintf(buf, sizeof(buf), "heading change %+.2f rad", f.heading - heading0);
                 return std::string(buf);
             });

    scenario("moderate loom -> fear response (dart or escape)",
             [](LIFSim& s) { s.loomL = 0.45f; s.loomR = 0.45f; }, 1.0f,
             nullptr,
             [](const Fly& f) { return (f.state == Fly::State::Walking && f.speed > 100.0f) || f.state == Fly::State::Flying; },
             [](const Fly& f) {
                 char buf[64];
                 std::snprintf(buf, sizeof(buf), "state=%d speed=%.1f", static_cast<int>(f.state), f.speed);
                 return std::string(buf);
             });

    scenario("tap near fly -> startle escape via sensory pathway",
             [](LIFSim& s) { s.stimulate(s.sens, 0.45f, 150); }, 0.8f,
             nullptr,
             [](const Fly& f) { return f.state == Fly::State::Flying; },
             [](const Fly& f) { return (f.state == Fly::State::Flying) ? "flying" : "not flying"; });

    // Body-level environment checks
    auto bodyCheck = [&](const std::string& name, std::function<std::pair<bool, std::string>()> run) {
        auto [ok, detail] = run();
        if (!ok) failures++;
        std::printf("%s  %s: %s\n", ok ? "PASS" : "FAIL", name.c_str(), detail.c_str());
    };

    BrainSignals walkSignals;
    walkSignals.walkDrive = 0.6f;

    bodyCheck("ledge attach + follow window edge", [&]() -> std::pair<bool, std::string> {
        Fly fly(Point2D{0, -55.0f});
        fly.state = Fly::State::Walking;
        fly.speed = 30.0f;
        fly.heading = 0.0f;
        fly.terrain = {Ledge{-40.0f, -300.0f, 300.0f, 1}};
        for (int i = 0; i < 240; ++i) {
            fly.update(dt, bounds, std::nullopt, walkSignals);
            if (fly.ledge.has_value() && std::abs(fly.pos.y + 40.0f) < 8.0f) {
                return {true, "attached, y=" + std::to_string(static_cast<int>(fly.pos.y))};
            }
        }
        return {false, "state=" + std::to_string(static_cast<int>(fly.state))};
    });

    bodyCheck("window closes underfoot -> takeoff", [&]() -> std::pair<bool, std::string> {
        Fly fly(Point2D{0, -40.0f});
        fly.state = Fly::State::Walking;
        fly.speed = 25.0f;
        fly.heading = 0.0f;
        fly.terrain = {Ledge{-40.0f, -300.0f, 300.0f, 1}};
        fly.ledge = fly.terrain[0];
        fly.terrain.clear();
        for (int i = 0; i < 60; ++i) {
            fly.update(dt, bounds, std::nullopt, walkSignals);
            if (fly.state == Fly::State::Flying) return {true, "took off"};
        }
        return {false, "state=" + std::to_string(static_cast<int>(fly.state))};
    });

    bodyCheck("sleep signal -> sleeping; wake -> grooming", [&]() -> std::pair<bool, std::string> {
        Fly fly(Point2D{0, 0});
        fly.state = Fly::State::Idle;
        BrainSignals s;
        s.sleep = true;
        for (int i = 0; i < 60; ++i) fly.update(dt, bounds, std::nullopt, s);
        if (fly.state != Fly::State::Sleeping) return {false, "no sleep"};
        s.sleep = false;
        fly.update(dt, bounds, std::nullopt, s);
        return {fly.state == Fly::State::Grooming, "woke to grooming"};
    });

    bodyCheck("thermal tempo scales walking speed", [&]() -> std::pair<bool, std::string> {
        Fly fly(Point2D{0, 0});
        fly.state = Fly::State::Walking;
        fly.speed = 20.0f;
        fly.heading = 0.0f;
        BrainSignals cool = walkSignals; cool.tempo = 1.0f;
        for (int i = 0; i < 120; ++i) fly.update(dt, bounds, std::nullopt, cool);
        float coolSpeed = fly.speed;
        BrainSignals hot = walkSignals; hot.tempo = 1.5f;
        for (int i = 0; i < 120; ++i) fly.update(dt, bounds, std::nullopt, hot);
        float hotSpeed = fly.speed;
        return {fly.state == Fly::State::Walking && hotSpeed > coolSpeed + 10.0f,
                "cool " + std::to_string(static_cast<int>(coolSpeed)) + " -> hot " + std::to_string(static_cast<int>(hotSpeed)) + " pt/s"};
    });

    bodyCheck("flight: altitude drives scale; escape flies higher than casual", [&]() -> std::pair<bool, std::string> {
        auto flightTest = [&](bool escape, std::optional<float> effort) {
            Fly fly(Point2D{0, 0});
            fly.state = Fly::State::Idle;
            fly.startFlight(bounds, std::nullopt, escape, effort);
            float maxAlt = 0.0f, maxScale = 0.0f;
            int frames = 0;
            while (fly.state == Fly::State::Flying && frames < 400) {
                frames++;
                fly.update(dt, bounds, std::nullopt, BrainSignals{});
                maxAlt = std::max(maxAlt, fly.alt);
                maxScale = std::max(maxScale, fly.currentScale.x);
            }
            return std::make_pair(maxAlt, maxScale);
        };
        auto [escAlt, escScale] = flightTest(true, std::nullopt);
        auto [casAlt, casScale] = flightTest(false, 0.45f);
        bool ok = (escAlt > casAlt + 0.15f) && (escScale > FLY_SCALE * 1.5f) &&
                  (std::abs(escScale - FLY_SCALE * (1.0f + 0.8f * escAlt)) < 0.15f);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "esc alt %.2f scale %.2f | cas alt %.2f scale %.2f", escAlt, escScale, casAlt, casScale);
        return {ok, std::string(buf)};
    });

    bodyCheck("flight: wings actually beat", [&]() -> std::pair<bool, std::string> {
        Fly fly(Point2D{0, 0});
        fly.state = Fly::State::Idle;
        fly.startFlight(bounds, std::nullopt, false, 0.8f);
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 30 && fly.state == Fly::State::Flying; ++i) {
            fly.update(dt, bounds, std::nullopt, BrainSignals{});
            float z = fly.model.leftWingRot.z;
            lo = std::min(lo, z);
            hi = std::max(hi, z);
        }
        return {hi - lo > 0.25f, "wing sweep " + std::to_string(hi - lo) + " rad"};
    });

    bodyCheck("escape-DN activity mid-flight raises wing-beat effort", [&]() -> std::pair<bool, std::string> {
        Fly fly(Point2D{0, 0});
        fly.state = Fly::State::Idle;
        fly.startFlight(bounds, std::nullopt, false, 0.5f);
        BrainSignals calm;
        for (int i = 0; i < 12; ++i) fly.update(dt, bounds, std::nullopt, calm);
        float calmEffort = fly.effortCurrent;
        BrainSignals hot; hot.wingDrive = 1.0f; hot.arousal = 0.6f;
        for (int i = 0; i < 12 && fly.state == Fly::State::Flying; ++i) {
            fly.update(dt, bounds, std::nullopt, hot);
        }
        float hotEffort = fly.effortCurrent;
        return {fly.state == Fly::State::Flying && hotEffort > calmEffort + 0.2f,
                "effort " + std::to_string(calmEffort) + " -> " + std::to_string(hotEffort)};
    });

    bodyCheck("threat while grounded raises the wings (no takeoff)", [&]() -> std::pair<bool, std::string> {
        Fly fly(Point2D{0, 0});
        fly.state = Fly::State::Walking;
        fly.speed = 20.0f;
        fly.dartCooldown = 99.0f;
        BrainSignals threat; threat.wingDrive = 0.9f; threat.walkDrive = 0.4f;
        for (int i = 0; i < 40; ++i) fly.update(dt, bounds, std::nullopt, threat);
        float x = fly.model.leftWingRot.x;
        return {fly.state != Fly::State::Flying && fly.wingRaise > 0.6f && x < -0.2f,
                "raise " + std::to_string(fly.wingRaise) + " wing tilt " + std::to_string(x) + " rad"};
    });

    bodyCheck("landing is smooth: no scale/height snap at touchdown", [&]() -> std::pair<bool, std::string> {
        Fly fly(Point2D{0, 0});
        fly.state = Fly::State::Idle;
        fly.startFlight(bounds, std::nullopt, true, std::nullopt);
        float prevScale = fly.currentScale.x;
        float prevZ = fly.currentPos3D.z;
        float maxDS = 0.0f, maxDZ = 0.0f;
        int post = 20, frames = 0;
        bool landed = false;
        while (post > 0 && frames < 600) {
            frames++;
            fly.update(dt, bounds, std::nullopt, BrainSignals{});
            maxDS = std::max(maxDS, std::abs(fly.currentScale.x - prevScale));
            maxDZ = std::max(maxDZ, std::abs(fly.currentPos3D.z - prevZ));
            prevScale = fly.currentScale.x;
            prevZ = fly.currentPos3D.z;
            if (fly.state != Fly::State::Flying) {
                landed = true;
                post--;
            }
        }
        return {landed && maxDS < 0.2f && maxDZ < 25.0f,
                "max dScale " + std::to_string(maxDS) + ", max dZ " + std::to_string(maxDZ)};
    });

    bodyCheck("circadian curve: siesta + night dips, dawn/dusk peaks", [&]() -> std::pair<bool, std::string> {
        float night = circadianActivity(3.0);
        float dawn = circadianActivity(9.0);
        float siesta = circadianActivity(14.0);
        float dusk = circadianActivity(18.0);
        bool ok = (night < 0.4f && dawn > 0.9f && siesta < 0.7f && siesta > 0.3f && dusk > 0.9f);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "3h %.2f, 9h %.2f, 14h %.2f, 18h %.2f", night, dawn, siesta, dusk);
        return {ok, std::string(buf)};
    });

    if (failures == 0) {
        std::cout << "ALL BEHAVIOR TESTS PASS\n";
    } else {
        std::cout << failures << " FAILURES\n";
    }
    return (failures == 0) ? 0 : 1;
}
