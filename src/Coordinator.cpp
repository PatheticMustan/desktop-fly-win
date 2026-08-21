#include "Coordinator.hpp"
#include <cmath>
#include <algorithm>
#if defined(_OPENMP)
#include <omp.h>
#endif

Coordinator::Coordinator(Size2D boundsIn, std::shared_ptr<LIFSim> simIn)
    : bounds(boundsIn), sim(std::move(simIn))
{
    Enqueue([](Coordinator& c) {
        float hw = c.bounds.width * 0.5f - 100.0f;
        float hh = c.bounds.height * 0.5f - 100.0f;
        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> dx(-hw, hw);
        std::uniform_real_distribution<float> dy(-hh, hh);
        c.flies.emplace_back(Point2D{dx(rng), dy(rng)});
    });
}

void Coordinator::Enqueue(std::function<void(Coordinator&)> action) {
    std::lock_guard<std::mutex> guard(lock_);
    pending_.push_back(std::move(action));
}

void Coordinator::AddFly() {
    AddFlies(1);
}

void Coordinator::AddFlies(int count) {
    Enqueue([count](Coordinator& c) {
        float hw = c.bounds.width * 0.5f - 100.0f;
        float hh = c.bounds.height * 0.5f - 100.0f;
        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<float> dx(-hw, hw);
        std::uniform_real_distribution<float> dy(-hh, hh);
        for (int i = 0; i < count; ++i) {
            c.flies.emplace_back(Point2D{dx(rng), dy(rng)});
        }
    });
}

void Coordinator::RemoveFly() {
    Enqueue([](Coordinator& c) {
        if (c.flies.size() > 1) {
            c.flies.pop_back();
        }
    });
}

void Coordinator::ScareAll() {
    Enqueue([](Coordinator& c) {
        c.loomOverride_ = 0.6f;
        for (size_t i = 1; i < c.flies.size(); ++i) {
            if (c.flies[i].state != Fly::State::Flying) {
                c.flies[i].startFlight(c.bounds);
            }
        }
    });
}

void Coordinator::EscapeTest() {
    Enqueue([](Coordinator& c) {
        c.loomOverride_ = 0.6f;
    });
}

void Coordinator::SetMouse(std::optional<Point2D> p) {
    std::lock_guard<std::mutex> guard(lock_);
    mouseScene_ = p;
}

void Coordinator::SetTerrain(const std::vector<Ledge>& ledges) {
    Enqueue([ledges](Coordinator& c) {
        c.terrain_ = ledges;
    });
}

void Coordinator::SetAmbient(float typing, bool sleepy, float tempo, float activity) {
    Enqueue([typing, sleepy, tempo, activity](Coordinator& c) {
        c.typingLevel_ = typing;
        c.sleepy_ = sleepy;
        c.tempo_ = tempo;
        c.activity_ = activity;
    });
}

void Coordinator::InjectWindowLoom(float strength, Point2D center) {
    Enqueue([strength, center](Coordinator& c) {
        if (c.flies.empty()) return;
        const auto& fly = c.flies[0];
        Point2D rel{center.x - fly.pos.x, center.y - fly.pos.y};
        float dist = std::max(1.0f, std::hypot(rel.x, rel.y));
        Point2D f{std::cos(fly.heading), std::sin(fly.heading)};
        float crossZ = (f.x * rel.y - f.y * rel.x) / dist;
        c.windowLoomL_ = std::max(c.windowLoomL_, strength * clampf(0.5f + 0.5f * crossZ, 0.12f, 1.0f));
        c.windowLoomR_ = std::max(c.windowLoomR_, strength * clampf(0.5f - 0.5f * crossZ, 0.12f, 1.0f));
    });
}

void Coordinator::InjectTap(Point2D p) {
    Enqueue([p](Coordinator& c) {
        if (!c.sim || c.flies.empty()) return;
        const auto& fly = c.flies[0];
        float d = std::hypot(p.x - fly.pos.x, p.y - fly.pos.y);
        float strength = clampf(1.0f - d / 520.0f, 0.0f, 1.0f);
        if (strength > 0.05f) {
            c.sim->stimulate(c.sim->sens, 0.15f + strength * 0.35f, 130);
        }
    });
}

void Coordinator::Retarget(Size2D newSize) {
    Enqueue([newSize](Coordinator& c) {
        c.bounds = newSize;
        c.terrain_.clear();
        for (auto& fly : c.flies) {
            fly.ledge = std::nullopt;
            fly.pos.x = clampf(fly.pos.x, -newSize.width * 0.5f + 40.0f, newSize.width * 0.5f - 40.0f);
            fly.pos.y = clampf(fly.pos.y, -newSize.height * 0.5f + 40.0f, newSize.height * 0.5f - 40.0f);
        }
    });
}

Point2D Coordinator::GetFlyPosition() {
    std::lock_guard<std::mutex> guard(lock_);
    return lastFlyPos_;
}

RECT Coordinator::ComputeFlyRect(const Fly& fly) const {
    float cx = fly.pos.x + bounds.width * 0.5f;
    float cy = bounds.height * 0.5f - fly.pos.y;
    float radius = 110.0f * (fly.currentScale.x / 1.5f);

    RECT r;
    r.left = std::max(0L, static_cast<LONG>(std::floor(cx - radius)));
    r.top = std::max(0L, static_cast<LONG>(std::floor(cy - radius)));
    r.right = std::min(static_cast<LONG>(bounds.width), static_cast<LONG>(std::ceil(cx + radius)));
    r.bottom = std::min(static_cast<LONG>(bounds.height), static_cast<LONG>(std::ceil(cy + radius)));
    return r;
}

std::vector<RECT> Coordinator::GetDirtyRects() {
    const size_t flyCount = flies.size();
    if (flyCount == 0) return {};

    if (flyCount > 30) {
        return { RECT{ 0, 0, static_cast<LONG>(bounds.width), static_cast<LONG>(bounds.height) } };
    }

    std::vector<RECT> currRects(flyCount);
    for (size_t i = 0; i < flyCount; ++i) {
        currRects[i] = ComputeFlyRect(flies[i]);
    }

    std::vector<RECT> dirty;
    dirty.reserve(flyCount);

    for (size_t i = 0; i < flyCount; ++i) {
        RECT r = currRects[i];
        if (i < prevFlyRects1_.size()) {
            r.left = std::min(r.left, prevFlyRects1_[i].left);
            r.top = std::min(r.top, prevFlyRects1_[i].top);
            r.right = std::max(r.right, prevFlyRects1_[i].right);
            r.bottom = std::max(r.bottom, prevFlyRects1_[i].bottom);
        }
        if (i < prevFlyRects2_.size()) {
            r.left = std::min(r.left, prevFlyRects2_[i].left);
            r.top = std::min(r.top, prevFlyRects2_[i].top);
            r.right = std::max(r.right, prevFlyRects2_[i].right);
            r.bottom = std::max(r.bottom, prevFlyRects2_[i].bottom);
        }
        dirty.push_back(r);
    }

    prevFlyRects2_ = prevFlyRects1_;
    prevFlyRects1_ = std::move(currRects);

    return dirty;
}

Coordinator::LoomSensory Coordinator::ComputeLoom(const Fly& fly, std::optional<Point2D> mouse, float dt) {
    if (!mouse.has_value()) {
        if (loomOverride_ > 0.0f) {
            float l = loomOverride_;
            float r = loomOverride_;
            return LoomSensory{l, r, 0.0f};
        }
        return {0, 0, 0};
    }
    Point2D m = *mouse;

    if (prevMouse_.has_value() && dt > 0.0f) {
        Point2D v{(m.x - prevMouse_->x) / dt, (m.y - prevMouse_->y) / dt};
        mouseVel_.x += (v.x - mouseVel_.x) * 0.4f;
        mouseVel_.y += (v.y - mouseVel_.y) * 0.4f;
    }
    prevMouse_ = m;

    Point2D rel{m.x - fly.pos.x, m.y - fly.pos.y};
    float dist = std::max(20.0f, std::hypot(rel.x, rel.y));
    float approach = -(rel.x * mouseVel_.x + rel.y * mouseVel_.y) / dist;

    float loom = clampf(approach / dist * 6.0f, 0.0f, 1.0f) * clampf(1.0f - dist / 800.0f, 0.0f, 1.0f);
    loom += clampf((130.0f - dist) / 130.0f, 0.0f, 1.0f) * 0.5f;
    loom = clampf(loom + loomOverride_, 0.0f, 1.0f);

    Point2D f{std::cos(fly.heading), std::sin(fly.heading)};
    Point2D rd{rel.x / dist, rel.y / dist};
    float crossZ = f.x * rd.y - f.y * rd.x;

    float lw = clampf(0.5f + 0.5f * crossZ, 0.12f, 1.0f);
    float rw = clampf(0.5f - 0.5f * crossZ, 0.12f, 1.0f);
    float puff = clampf(std::hypot(mouseVel_.x, mouseVel_.y) / 1500.0f, 0.0f, 1.0f) * clampf(1.0f - dist / 500.0f, 0.0f, 1.0f);

    return LoomSensory{loom * lw, loom * rw, puff};
}

void Coordinator::UpdateAndRender(RendererD3D11& renderer, float dt) {
    // Drain pending actions
    std::vector<std::function<void(Coordinator&)>> actions;
    std::optional<Point2D> mouse;
    {
        std::lock_guard<std::mutex> guard(lock_);
        actions = std::move(pending_);
        pending_.clear();
        mouse = mouseScene_;
    }
    for (auto& a : actions) a(*this);

    std::optional<BrainSignals> signals;
    if (sim && !flies.empty()) {
        auto& first = flies[0];
        auto sensory = ComputeLoom(first, mouse, dt);
        float decayF = std::exp(-4.0f * dt);
        windowLoomL_ *= decayF;
        windowLoomR_ *= decayF;

        sim->loomL = std::max(sensory.l, windowLoomL_);
        sim->loomR = std::max(sensory.r, windowLoomR_);
        sim->airPuff = std::max(sensory.puff, typingLevel_ * 0.30f);
        sim->gaitDrive = first.walkingIntensity();
        sim->gaitPhase = first.gaitPhasePublic();

        sim->activityScale = (1.0f - (1.0f - activity_) * 0.35f) * (sleepy_ ? 0.75f : 1.0f);
        sim->sensoryGate = sleepy_ ? 0.55f : 1.0f;
        loomOverride_ = std::max(0.0f, loomOverride_ - dt * 1.2f);

        msAccumulator_ += static_cast<double>(dt) * 1000.0;
        int steps = std::min(50, static_cast<int>(msAccumulator_));
        msAccumulator_ -= steps;
        sim->step(steps);

        BrainSignals s = signalBuilder_.make(*sim, dt);
        s.tempo = tempo_;
        s.sleep = sleepy_;
        signals = s;
    }

    const size_t flyCount = flies.size();
    if (flyCount == 0) return;

    if (flyCount >= 300) {
#if defined(_OPENMP)
        #pragma omp parallel for schedule(static)
        for (intptr_t i = 0; i < static_cast<intptr_t>(flyCount); ++i) {
            flies[i].terrain = terrain_;
            flies[i].update(dt, bounds, mouse, (i == 0) ? signals : std::nullopt);
        }
#else
        for (size_t i = 0; i < flyCount; ++i) {
            flies[i].terrain = terrain_;
            flies[i].update(dt, bounds, mouse, (i == 0) ? signals : std::nullopt);
        }
#endif
    } else {
        for (size_t i = 0; i < flyCount; ++i) {
            flies[i].terrain = terrain_;
            flies[i].update(dt, bounds, mouse, (i == 0) ? signals : std::nullopt);
        }
    }

    if (!flies.empty()) {
        std::lock_guard<std::mutex> guard(lock_);
        lastFlyPos_ = flies[0].pos;
    }

    // Set 3D Camera & Lights
    using namespace DirectX;
    XMVECTOR eyePos = XMVectorSet(0.0f, 0.0f, 300.0f, 1.0f);
    XMVECTOR targetPos = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtRH(eyePos, targetPos, up);
    XMMATRIX proj = XMMatrixOrthographicRH(bounds.width, bounds.height, 1.0f, 600.0f);
    XMMATRIX viewProj = view * proj;

    XMFLOAT3 lightDir(-0.35f, 0.30f, 0.8f);
    XMFLOAT4 lightColor(1.0f, 1.0f, 1.0f, 1.0f);
    XMFLOAT4 ambientColor(0.55f, 0.55f, 0.55f, 1.0f);
    XMFLOAT3 eyePosF3(0.0f, 0.0f, 300.0f);

    renderer.SetFrameConstants(viewProj, lightDir, lightColor, ambientColor, eyePosF3);

    batches_.Clear();
    batches_.ReserveForFlies(flyCount);

    for (auto& fly : flies) {
        fly.CollectBatches(batches_);
    }

    renderer.DrawMeshInstanced(renderer.sphereMesh, batches_.opaqueSpheres.data(), batches_.opaqueSpheres.size());
    renderer.DrawMeshInstanced(renderer.sphereMesh, batches_.abdomenSpheres.data(), batches_.abdomenSpheres.size(), false, false, true);
    renderer.DrawMeshInstanced(renderer.capsuleMesh, batches_.opaqueCapsules.data(), batches_.opaqueCapsules.size());
    renderer.DrawMeshInstanced(renderer.coneMesh, batches_.opaqueCones.data(), batches_.opaqueCones.size());
    renderer.DrawMeshInstanced(renderer.wingMesh, batches_.translucentWings.data(), batches_.translucentWings.size(), true, true);
    if (!batches_.motionBlurWings.empty()) {
        renderer.DrawMeshInstanced(renderer.sphereMesh, batches_.motionBlurWings.data(), batches_.motionBlurWings.size(), true, true);
    }
}
