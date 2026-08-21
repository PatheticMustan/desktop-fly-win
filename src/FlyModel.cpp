#include "FlyModel.hpp"
#include <cmath>
#include <algorithm>

constexpr float PI = 3.14159265358979323846f;

static float rnd(std::mt19937& rng, float lo, float hi) {
    std::uniform_real_distribution<float> d(lo, hi);
    return d(rng);
}

static float angleDiff(float from, float to) {
    float d = std::fmod(to - from, 2.0f * PI);
    if (d > PI) d -= 2.0f * PI;
    if (d < -PI) d += 2.0f * PI;
    return d;
}

static float smoothstep(float t) {
    float x = clampf(t, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

Leg::Leg(DirectX::XMFLOAT3 attachIn, float baseYawIn, float swingSignIn, float phaseIn,
         bool isFrontIn, float femur, float tibia, float tarsus)
    : attach(attachIn), baseYaw(baseYawIn), swingSign(swingSignIn), phase(phaseIn),
      isFront(isFrontIn), femurH(femur), tibiaH(tibia), tarsusH(tarsus)
{}

void Leg::Render(RendererD3D11& renderer, const DirectX::XMMATRIX& bodyWorld) {
    using namespace DirectX;

    // Root transform
    XMMATRIX rootRot = XMMatrixRotationRollPitchYaw(0.0f, -lift, baseYaw + swingSign * angle);
    XMMATRIX rootWorld = rootRot * XMMatrixTranslation(attach.x, attach.y, attach.z) * bodyWorld;

    XMFLOAT4 legColor(0.33f, 0.24f, 0.14f, 1.0f);
    XMFLOAT4 tarsusColor(0.25f, 0.18f, 0.10f, 1.0f);
    XMFLOAT4 specColor(0.25f, 0.25f, 0.25f, 1.0f);

    // Femur
    XMMATRIX femurScale = XMMatrixScaling(0.48f, femurH * 0.5f, 0.48f);
    XMMATRIX femurOrient = XMMatrixRotationZ(-PI * 0.5f);
    XMMATRIX femurOffset = XMMatrixTranslation(femurH * 0.5f, 0.0f, 0.0f);
    XMMATRIX femurWorld = femurScale * femurOrient * femurOffset * rootWorld;

    renderer.SetObjectConstants(femurWorld, legColor, specColor, 0.25f);
    renderer.DrawMesh(renderer.capsuleMesh);

    // Knee
    XMMATRIX kneeRot = XMMatrixRotationRollPitchYaw(0.0f, 0.75f, -0.30f * swingSign);
    XMMATRIX kneeWorld = kneeRot * XMMatrixTranslation(femurH, 0.0f, 0.0f) * rootWorld;

    // Tibia
    XMMATRIX tibiaScale = XMMatrixScaling(0.38f, tibiaH * 0.5f, 0.38f);
    XMMATRIX tibiaOrient = XMMatrixRotationZ(-PI * 0.5f);
    XMMATRIX tibiaOffset = XMMatrixTranslation(tibiaH * 0.5f, 0.0f, 0.0f);
    XMMATRIX tibiaWorld = tibiaScale * tibiaOrient * tibiaOffset * kneeWorld;

    renderer.SetObjectConstants(tibiaWorld, legColor, specColor, 0.25f);
    renderer.DrawMesh(renderer.capsuleMesh);

    // Ankle
    XMMATRIX ankleRot = XMMatrixRotationRollPitchYaw(0.0f, 0.35f, -0.15f * swingSign);
    XMMATRIX ankleWorld = ankleRot * XMMatrixTranslation(tibiaH, 0.0f, 0.0f) * kneeWorld;

    // Tarsus
    XMMATRIX tarsusScale = XMMatrixScaling(0.24f, tarsusH * 0.5f, 0.24f);
    XMMATRIX tarsusOrient = XMMatrixRotationZ(-PI * 0.5f);
    XMMATRIX tarsusOffset = XMMatrixTranslation(tarsusH * 0.5f, 0.0f, 0.0f);
    XMMATRIX tarsusWorld = tarsusScale * tarsusOrient * tarsusOffset * ankleWorld;

    renderer.SetObjectConstants(tarsusWorld, tarsusColor, specColor, 0.25f);
    renderer.DrawMesh(renderer.capsuleMesh);
}

Fly::Fly(Point2D p) : pos(p), rng(std::random_device{}()) {
    heading = rnd(rng, 0.0f, 2.0f * PI);
    stateTimer = rnd(rng, 1.5f, 4.0f);
    gaitPhase = rnd(rng, 0.0f, 1.0f);
    time = rnd(rng, 0.0f, 100.0f);

    // Leg specs
    float z = 4.5f;
    struct Spec { float side; DirectX::XMFLOAT3 attach; float yawOff; float phase; bool isFront; float f, t, ta; };
    Spec specs[] = {
        {  1.0f, DirectX::XMFLOAT3( 3.1f,  5.3f, z),  0.95f, 0.0f, true,  4.2f, 4.8f, 3.2f },
        { -1.0f, DirectX::XMFLOAT3(-3.1f,  5.3f, z),  0.95f, 0.5f, true,  4.2f, 4.8f, 3.2f },
        {  1.0f, DirectX::XMFLOAT3( 3.7f,  2.0f, z), -0.10f, 0.5f, false, 4.8f, 5.6f, 3.8f },
        { -1.0f, DirectX::XMFLOAT3(-3.7f,  2.0f, z), -0.10f, 0.0f, false, 4.8f, 5.6f, 3.8f },
        {  1.0f, DirectX::XMFLOAT3( 3.3f, -1.2f, z), -0.95f, 0.0f, false, 5.8f, 7.0f, 4.6f },
        { -1.0f, DirectX::XMFLOAT3(-3.3f, -1.2f, z), -0.95f, 0.5f, false, 5.8f, 7.0f, 4.6f }
    };

    for (const auto& sp : specs) {
        float baseYaw = (sp.side > 0.0f) ? sp.yawOff : (PI - sp.yawOff);
        model.legs.emplace_back(sp.attach, baseYaw, sp.side, sp.phase, sp.isFront, sp.f, sp.t, sp.ta);
    }

    syncNode();
}

float Fly::walkingIntensity() const {
    if (state != State::Walking) return 0.0f;
    float v = (backwardTimer > 0.0f) ? 22.0f : speed;
    return clampf(std::abs(v) / 60.0f, 0.0f, 1.0f);
}

float Fly::effectiveSpeed() const {
    return (backwardTimer > 0.0f) ? -22.0f : speed;
}

void Fly::syncNode() {
    currentPos3D = DirectX::XMFLOAT3(pos.x, pos.y, currentPos3D.z);
}

void Fly::setState(State s) {
    if (s == state) return;
    state = s;
    stateAge = 0.0f;
}

void Fly::land() {
    state = State::Idle;
    stateTimer = rnd(rng, 0.3f, 0.8f);
    speed = 0.0f;
    alt = 0.0f;
    pitch = 0.0f;
    currentScale = DirectX::XMFLOAT3(FLY_SCALE, FLY_SCALE, FLY_SCALE);
    currentPos3D.z = 0.0f;

    model.leftWingRot = DirectX::XMFLOAT3(0, 0, -0.13f);
    model.rightWingRot = DirectX::XMFLOAT3(0, 0, 0.13f);
    model.blurWingsVisible = false;
}

void Fly::applyAltitude() {
    float s = FLY_SCALE * (1.0f + 0.8f * alt);
    currentScale = DirectX::XMFLOAT3(s, s, s);
    currentPos3D.z = 90.0f * alt;
}

void Fly::startFlight(Size2D bounds, std::optional<Point2D> awayFrom, bool escape, std::optional<float> effort) {
    state = State::Flying;
    ledge = std::nullopt;
    flightEffort = clampf(effort.value_or(escape ? 1.0f : rnd(rng, 0.4f, 0.75f)), 0.25f, 1.0f);
    effortCurrent = flightEffort;
    flapPhase = 0.0f;
    wingRaise = 0.0f;
    flightFrom = pos;

    float hw = bounds.width * 0.5f - EDGE_MARGIN;
    float hh = bounds.height * 0.5f - EDGE_MARGIN;
    Point2D target{0, 0};
    bool chosen = false;

    if (!escape && !awayFrom.has_value() && !terrain.empty() && rnd(rng, 0.0f, 1.0f) < 0.45f) {
        const auto& L = terrain[std::uniform_int_distribution<size_t>(0, terrain.size() - 1)(rng)];
        if (L.x1 - L.x0 > 90.0f) {
            target = Point2D{rnd(rng, L.x0 + 25.0f, L.x1 - 25.0f), L.y};
            chosen = std::hypot(target.x - pos.x, target.y - pos.y) > 180.0f;
        }
    }

    if (!chosen) {
        for (int i = 0; i < 16; ++i) {
            target = Point2D{rnd(rng, -hw, hw), rnd(rng, -hh, hh)};
            float dist = std::hypot(target.x - pos.x, target.y - pos.y);
            if (dist <= (escape ? 350.0f : 260.0f)) continue;

            if (awayFrom.has_value()) {
                Point2D a = *awayFrom;
                float toTx = target.x - pos.x, toTy = target.y - pos.y;
                float toAx = a.x - pos.x, toAy = a.y - pos.y;
                if (toTx * toAx + toTy * toAy > 0.0f) continue;
            }
            break;
        }
    }

    flightTo = target;
    float dist = std::hypot(target.x - pos.x, target.y - pos.y);
    flightDur = escape ? clampf(dist / 650.0f, 0.45f, 1.2f) : clampf(dist / 420.0f, 0.7f, 2.0f);
    flightT = 0.0f;
    scareCooldown = escape ? 2.0f : 2.5f;

    model.blurWingsVisible = true;
}

void Fly::pickNextState() {
    switch (state) {
    case State::Walking: {
        float r = rnd(rng, 0.0f, 1.0f);
        if (r < 0.30f) {
            state = State::Idle;
            stateTimer = rnd(rng, 0.8f, 3.0f);
            speed = 0.0f;
        } else if (r < 0.55f) {
            stateTimer = rnd(rng, 0.3f, 0.8f);
            speed = rnd(rng, 95.0f, 150.0f);
            heading += rnd(rng, -1.2f, 1.2f);
        } else {
            stateTimer = rnd(rng, 1.5f, 5.0f);
            speed = rnd(rng, 18.0f, 45.0f);
        }
        break;
    }
    case State::Idle: {
        float r = rnd(rng, 0.0f, 1.0f);
        if (r < 0.35f) {
            state = State::Grooming;
            stateTimer = rnd(rng, 1.0f, 2.5f);
        } else {
            state = State::Walking;
            stateTimer = rnd(rng, 1.5f, 5.0f);
            speed = rnd(rng, 18.0f, 45.0f);
            heading += rnd(rng, -1.5f, 1.5f);
        }
        break;
    }
    case State::Grooming:
        state = State::Idle;
        stateTimer = rnd(rng, 0.3f, 1.0f);
        break;
    case State::Flying:
    case State::Sleeping:
        break;
    }
}

void Fly::brainBehavior(const BrainSignals& s, float dt, Size2D bounds, std::optional<Point2D> mouse) {
    if (s.escape && scareCooldown == 0.0f) {
        startFlight(bounds, mouse, true);
        return;
    }

    if (s.sleep) {
        if (state != State::Sleeping) {
            setState(State::Sleeping);
            speed = 0.0f;
            dartTimer = 0.0f;
            backwardTimer = 0.0f;
        }
        return;
    } else if (state == State::Sleeping) {
        setState(State::Grooming);
        return;
    }

    if (s.nervous > 0.40f && dartCooldown == 0.0f) {
        ledge = std::nullopt;
        setState(State::Walking);
        if (mouse.has_value()) {
            heading = std::atan2(pos.y - mouse->y, pos.x - mouse->x) + rnd(rng, -0.4f, 0.4f);
        } else {
            heading += rnd(rng, -1.5f, 1.5f);
        }
        speed = rnd(rng, 110.0f, 155.0f);
        dartTimer = rnd(rng, 0.4f, 0.9f);
        dartCooldown = 1.2f;
    }

    if (state != State::Walking || dartTimer == 0.0f) {
        if (state != State::Grooming && s.groomDrive > 0.5f && s.nervous < 0.3f && stateAge > 0.4f) {
            setState(State::Grooming);
        } else if (state == State::Grooming && s.groomDrive < 0.3f && stateAge > 0.6f) {
            setState(State::Idle);
        }
    }

    if (state == State::Idle && s.walkDrive > 0.22f && stateAge > 0.4f) {
        setState(State::Walking);
        heading += rnd(rng, -0.8f, 0.8f);
    } else if (state == State::Walking && dartTimer == 0.0f && s.walkDrive < 0.08f && stateAge > 0.5f) {
        setState(State::Idle);
        speed = 0.0f;
    }

    if (s.backward && backwardTimer == 0.0f && dartTimer == 0.0f) {
        if (state != State::Walking) {
            setState(State::Walking);
            speed = 0.0f;
        }
        backwardTimer = 0.5f;
    }

    if (state == State::Walking) {
        if (dartTimer == 0.0f && backwardTimer == 0.0f) {
            float target = (14.0f + s.walkDrive * 55.0f) * s.tempo;
            speed += (target - speed) * std::min(1.0f, 3.0f * dt);
        }
        if (!ledge.has_value()) {
            heading += s.turnBias * dt;
        }
    }

    float flightChance = (s.arousal > 0.5f) ? 0.6f : 0.005f;
    if (state == State::Walking && rnd(rng, 0.0f, 1.0f) < flightChance * dt) {
        startFlight(bounds, std::nullopt, false, 0.35f + s.arousal * 0.6f);
    }
}

void Fly::updateWalk(float dt, Size2D bounds) {
    if (ledge.has_value()) {
        auto it = std::find_if(terrain.begin(), terrain.end(), [this](const Ledge& l) {
            return l.id == ledge->id;
        });
        if (it != terrain.end() && std::abs(it->y - ledge->y) < 40.0f) {
            ledge = *it;
        } else {
            ledge = std::nullopt;
            startFlight(bounds);
            return;
        }
    }

    if (ledge.has_value()) {
        heading += rnd(rng, -1.0f, 1.0f) * 0.2f * dt;
        float along = (std::cos(heading) >= 0.0f) ? 0.0f : PI;
        heading += angleDiff(heading, along) * std::min(1.0f, 6.0f * dt);
        pos.x += std::cos(heading) * effectiveSpeed() * dt;
        pos.y += (ledge->y - pos.y) * std::min(1.0f, 10.0f * dt);
        if (pos.x <= ledge->x0 + 6.0f && std::cos(heading) < 0.0f) heading = 0.0f;
        if (pos.x >= ledge->x1 - 6.0f && std::cos(heading) > 0.0f) heading = PI;
        pos.x = clampf(pos.x, ledge->x0, ledge->x1);
        if (rnd(rng, 0.0f, 1.0f) < 0.05f * dt) ledge = std::nullopt;
    } else {
        heading += rnd(rng, -1.0f, 1.0f) * 1.6f * dt;
        float hw = bounds.width * 0.5f - EDGE_MARGIN;
        float hh = bounds.height * 0.5f - EDGE_MARGIN;
        if (std::abs(pos.x) > hw || std::abs(pos.y) > hh) {
            float toCenter = std::atan2(-pos.y, -pos.x);
            heading += angleDiff(heading, toCenter) * std::min(1.0f, 4.0f * dt);
        }
        float v = effectiveSpeed();
        pos.x += std::cos(heading) * v * dt;
        pos.y += std::sin(heading) * v * dt;
        pos.x = clampf(pos.x, -bounds.width * 0.5f + 20.0f, bounds.width * 0.5f - 20.0f);
        pos.y = clampf(pos.y, -bounds.height * 0.5f + 20.0f, bounds.height * 0.5f - 20.0f);

        for (const auto& L : terrain) {
            if (pos.x > L.x0 - 8.0f && pos.x < L.x1 + 8.0f && std::abs(pos.y - L.y) < 20.0f) {
                if (rnd(rng, 0.0f, 1.0f) < 0.9f * dt) {
                    ledge = L;
                    heading = (std::cos(heading) >= 0.0f) ? 0.0f : PI;
                    break;
                }
            }
        }
    }

    currentPos3D.z = 0.35f * std::abs(std::sin(gaitPhase * PI * 2.0f));
}

void Fly::updateFlight(float dt) {
    flightT = std::min(1.0f, flightT + dt / flightDur);
    if (flightT >= 1.0f) {
        pos.x = flightTo.x + std::sin(time * 26.0f) * 1.2f;
        pos.y = flightTo.y + std::cos(time * 22.0f) * 1.0f;
        pitch = clampf(alt * 0.4f, 0.0f, 0.35f);
        alt += (0.0f - alt) * std::min(1.0f, 9.0f * dt);
        applyAltitude();
        if (alt < 0.035f) {
            pos = flightTo;
            land();
        }
        return;
    }

    float e = smoothstep(flightT);
    float dx = flightTo.x - flightFrom.x;
    float dy = flightTo.y - flightFrom.y;
    float len = std::max(1.0f, std::hypot(dx, dy));
    float px = -dy / len;
    float py = dx / len;
    float wob = std::sin(time * 32.0f) * 4.0f * std::sin(flightT * PI);

    pos.x = flightFrom.x + dx * e + px * wob;
    pos.y = flightFrom.y + dy * e + py * wob;
    heading = std::atan2(dy, dx) + std::sin(time * 18.0f) * 0.12f;

    effortCurrent = brainLive
        ? clampf(std::max(flightEffort, flightEffort * 0.55f + liveArousal * 0.25f + liveWing * 0.6f), 0.25f, 1.3f)
        : flightEffort;

    float riseEnv = std::min(flightT / 0.25f, 1.0f);
    float fallEnv = std::min((1.0f - flightT) / 0.3f, 1.0f);
    float target = effortCurrent * std::min(riseEnv, fallEnv) * (0.85f + 0.15f * std::sin(time * 7.0f));

    pitch = clampf((target - alt) * 2.5f, -0.45f, 0.45f);
    alt += (target - alt) * std::min(1.0f, 6.0f * dt);

    applyAltitude();
}

void Fly::updateLegs(float dt) {
    float v = std::abs(effectiveSpeed());
    bool walking = (state == State::Walking && v > 1.0f);

    if (walking) {
        float amp = clampf(0.20f + v * 0.0022f, 0.20f, 0.50f);
        float stride = std::max(5.0f, 2.0f * amp * 13.0f);
        float freq = clampf(v / stride, 3.0f, 11.0f);
        gaitPhase = std::fmod(gaitPhase + freq * dt, 1.0f);
        float stanceFrac = 0.6f;

        for (auto& leg : model.legs) {
            float p = std::fmod(gaitPhase + leg.phase, 1.0f);
            if (p < stanceFrac) {
                leg.angle = amp * (1.0f - 2.0f * (p / stanceFrac));
                leg.lift = 0.0f;
            } else {
                float s = (p - stanceFrac) / (1.0f - stanceFrac);
                leg.angle = -amp + 2.0f * amp * smoothstep(s);
                leg.lift = std::sin(s * PI) * 0.55f;
            }
            if (backwardTimer > 0.0f) leg.angle = -leg.angle;
        }
    } else if (state == State::Grooming) {
        for (auto& leg : model.legs) {
            if (leg.isFront) {
                leg.angle = 0.45f + 0.25f * std::sin(time * 20.0f + leg.swingSign * 1.3f);
                leg.lift = 0.55f + 0.15f * std::sin(time * 22.0f);
            } else {
                leg.angle += (0.0f - leg.angle) * std::min(1.0f, 8.0f * dt);
                leg.lift += (0.0f - leg.lift) * std::min(1.0f, 8.0f * dt);
            }
        }
    } else if (state == State::Flying) {
        for (auto& leg : model.legs) {
            leg.angle += (-0.35f - leg.angle) * std::min(1.0f, 6.0f * dt);
            leg.lift += (0.5f - leg.lift) * std::min(1.0f, 6.0f * dt);
        }
    } else {
        for (auto& leg : model.legs) {
            leg.angle += (0.0f - leg.angle) * std::min(1.0f, 10.0f * dt);
            leg.lift += (0.0f - leg.lift) * std::min(1.0f, 10.0f * dt);
        }
    }
}

void Fly::updateWings(float dt) {
    if (state != State::Flying) {
        float raiseTarget = (state != State::Sleeping && (liveWing > 0.7f || (brainLive && dartTimer > 0.0f))) ? 1.0f : 0.0f;
        wingRaise += (raiseTarget - wingRaise) * std::min(1.0f, 8.0f * dt);
        if (wingRaise > 0.01f) {
            model.leftWingRot = DirectX::XMFLOAT3(-0.5f * wingRaise, 0.0f, -0.13f - 0.3f * wingRaise);
            model.rightWingRot = DirectX::XMFLOAT3(-0.5f * wingRaise, 0.0f, 0.13f + 0.3f * wingRaise);
        } else {
            model.leftWingRot = DirectX::XMFLOAT3(0, 0, -0.13f);
            model.rightWingRot = DirectX::XMFLOAT3(0, 0, 0.13f);
        }
        return;
    }

    flapPhase = std::fmod(flapPhase + dt * (14.0f + 10.0f * effortCurrent), 1.0f);
    float stroke = std::sin(flapPhase * 2.0f * PI);

    model.leftWingRot = DirectX::XMFLOAT3(stroke * 0.35f, 0.0f, -0.45f - 0.35f * (0.5f + 0.5f * stroke));
    model.rightWingRot = DirectX::XMFLOAT3(stroke * 0.35f, 0.0f, 0.45f + 0.35f * (0.5f + 0.5f * stroke));

    float flick = 0.10f + 0.14f * std::abs(stroke);
    model.blurWingOpacity = flick;
    model.leftBlurRot = DirectX::XMFLOAT3(0, 0, 0.45f + stroke * 0.2f);
    model.rightBlurRot = DirectX::XMFLOAT3(0, 0, -0.45f - stroke * 0.2f);
}

void Fly::update(float dt, Size2D bounds, std::optional<Point2D> mouse, const std::optional<BrainSignals>& signals) {
    time += dt;
    scareCooldown = std::max(0.0f, scareCooldown - dt);
    dartCooldown = std::max(0.0f, dartCooldown - dt);
    backwardTimer = std::max(0.0f, backwardTimer - dt);
    stateAge += dt;
    dartTimer = std::max(0.0f, dartTimer - dt);

    brainLive = signals.has_value();
    liveArousal = signals ? signals->arousal : 0.0f;
    liveWing = signals ? signals->wingDrive : 0.0f;

    if (state == State::Flying) {
        updateFlight(dt);
    } else if (signals.has_value()) {
        brainBehavior(*signals, dt, bounds, mouse);
        if (state == State::Walking) updateWalk(dt, bounds);
    } else {
        if (scareCooldown == 0.0f && mouse.has_value()) {
            float mouseDist = std::hypot(mouse->x - pos.x, mouse->y - pos.y);
            if (mouseDist < SCARE_RADIUS) {
                startFlight(bounds, mouse);
            } else if (mouseDist < NERVOUS_RADIUS && state != State::Walking) {
                setState(State::Walking);
                heading = std::atan2(pos.y - mouse->y, pos.x - mouse->x) + rnd(rng, -0.4f, 0.4f);
                speed = rnd(rng, 110.0f, 150.0f);
                stateTimer = rnd(rng, 0.4f, 0.9f);
                scareCooldown = 1.0f;
            }
        }
        if (state != State::Flying) {
            stateTimer -= dt;
            if (stateTimer <= 0.0f) {
                if (state == State::Walking && rnd(rng, 0.0f, 1.0f) < 0.10f) {
                    startFlight(bounds);
                } else {
                    pickNextState();
                }
            }
            if (state == State::Walking) updateWalk(dt, bounds);
        }
    }

    updateLegs(dt);
    updateWings(dt);

    float breathe = (state == State::Sleeping) ? (1.0f + 0.05f * std::sin(time * 1.1f))
                                               : (1.0f + 0.03f * std::sin(time * 3.0f));
    model.abdomenScale.z = 0.75f * breathe;

    syncNode();
}

void Fly::Render(RendererD3D11& renderer) {
    using namespace DirectX;

    // Body World Matrix
    XMMATRIX scaleMat = XMMatrixScaling(currentScale.x, currentScale.y, currentScale.z);
    XMMATRIX rotMat = XMMatrixRotationRollPitchYaw(pitch, 0.0f, heading - PI * 0.5f);
    XMMATRIX transMat = XMMatrixTranslation(currentPos3D.x, currentPos3D.y, currentPos3D.z);
    XMMATRIX bodyWorld = scaleMat * rotMat * transMat;

    XMFLOAT4 bodyBrown(0.50f, 0.38f, 0.22f, 1.0f);
    XMFLOAT4 headBrown(0.55f, 0.45f, 0.30f, 1.0f);
    XMFLOAT4 eyeRed(0.62f, 0.10f, 0.07f, 1.0f);
    XMFLOAT4 antColor(0.30f, 0.22f, 0.13f, 1.0f);
    XMFLOAT4 probColor(0.35f, 0.26f, 0.16f, 1.0f);
    XMFLOAT4 wingColor(0.92f, 0.92f, 0.92f, 0.28f);
    XMFLOAT4 blurColor(0.85f, 0.85f, 0.85f, model.blurWingOpacity);

    // Thorax
    XMMATRIX thoraxW = XMMatrixScaling(4.6f * model.thoraxScale.x, 4.6f * model.thoraxScale.y, 4.6f * model.thoraxScale.z) *
                       XMMatrixTranslation(model.thoraxPos.x, model.thoraxPos.y, model.thoraxPos.z) * bodyWorld;
    renderer.SetObjectConstants(thoraxW, bodyBrown, XMFLOAT4(0.35f, 0.35f, 0.35f, 1.0f), 0.4f);
    renderer.DrawMesh(renderer.sphereMesh);

    // Abdomen (Procedural striped texture)
    XMMATRIX abdW = XMMatrixScaling(5.0f * model.abdomenScale.x, 5.0f * model.abdomenScale.y, 5.0f * model.abdomenScale.z) *
                    XMMatrixTranslation(model.abdomenPos.x, model.abdomenPos.y, model.abdomenPos.z) * bodyWorld;
    renderer.DrawAbdomen(renderer.sphereMesh, abdW);

    // Head
    XMMATRIX headW = XMMatrixScaling(3.0f * model.headScale.x, 3.0f * model.headScale.y, 3.0f * model.headScale.z) *
                     XMMatrixTranslation(model.headPos.x, model.headPos.y, model.headPos.z) * bodyWorld;
    renderer.SetObjectConstants(headW, headBrown, XMFLOAT4(0.35f, 0.35f, 0.35f, 1.0f), 0.4f);
    renderer.DrawMesh(renderer.sphereMesh);

    // Eyes
    for (float side : {-1.0f, 1.0f}) {
        XMMATRIX eyeW = XMMatrixScaling(2.0f * 0.8f, 2.0f * 1.0f, 2.0f * 1.15f) *
                        XMMatrixTranslation(side * 2.1f, 9.7f, 6.4f) * bodyWorld;
        renderer.SetObjectConstants(eyeW, eyeRed, XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f), 0.9f);
        renderer.DrawMesh(renderer.sphereMesh);
    }

    // Antennae
    for (float side : {-1.0f, 1.0f}) {
        XMMATRIX antW = XMMatrixScaling(0.16f, 2.2f * 0.5f, 0.16f) *
                        XMMatrixRotationRollPitchYaw(-1.15f, 0.0f, side * 0.35f) *
                        XMMatrixTranslation(side * 0.9f, 11.6f, 6.3f) * bodyWorld;
        renderer.SetObjectConstants(antW, antColor, XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f), 0.2f);
        renderer.DrawMesh(renderer.capsuleMesh);
    }

    // Proboscis
    XMMATRIX probW = XMMatrixScaling(0.6f, 2.4f * 0.5f, 0.6f) *
                     XMMatrixRotationRollPitchYaw(-0.5f, 0.0f, 0.0f) *
                     XMMatrixTranslation(0.0f, 10.4f, 4.6f) * bodyWorld;
    renderer.SetObjectConstants(probW, probColor, XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f), 0.2f);
    renderer.DrawMesh(renderer.coneMesh);

    // Legs
    for (auto& leg : model.legs) {
        leg.Render(renderer, bodyWorld);
    }

    // Folded / Flapping Wings
    for (int i = 0; i < 2; ++i) {
        float side = (i == 0) ? -1.0f : 1.0f;
        const auto& rot = (i == 0) ? model.leftWingRot : model.rightWingRot;
        XMMATRIX wingW = XMMatrixRotationRollPitchYaw(rot.x, rot.y, rot.z) *
                         XMMatrixTranslation(side * 1.6f, 0.5f, (side > 0.0f) ? 7.7f : 7.55f) * bodyWorld;
        renderer.SetObjectConstants(wingW, wingColor, XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f), 0.9f);
        renderer.DrawMesh(renderer.wingMesh, true, true);
    }

    // Motion Blur Wings
    if (model.blurWingsVisible) {
        for (int i = 0; i < 2; ++i) {
            float side = (i == 0) ? -1.0f : 1.0f;
            const auto& rot = (i == 0) ? model.leftBlurRot : model.rightBlurRot;
            XMMATRIX blurW = XMMatrixScaling(5.5f, 2.4f, 0.3f) *
                             XMMatrixRotationRollPitchYaw(rot.x, rot.y, rot.z) *
                             XMMatrixTranslation(side * 6.0f, 1.5f, 8.2f) * bodyWorld;
            renderer.SetObjectConstants(blurW, blurColor, XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f), 0.5f);
            renderer.DrawMesh(renderer.sphereMesh, true, true);
        }
    }
}
