#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <optional>
#include "FlyModel.hpp"
#include "Sim.hpp"
#include "SignalBuilder.hpp"
#include "RendererD3D11.hpp"

class Coordinator {
public:
    Size2D bounds;
    std::shared_ptr<LIFSim> sim;
    std::vector<Fly> flies;

    Coordinator(Size2D bounds, std::shared_ptr<LIFSim> sim = nullptr);

    void Enqueue(std::function<void(Coordinator&)> action);
    void AddFly();
    void AddFlies(int count);
    void RemoveFly();
    void ScareAll();
    void EscapeTest();
    void SetMouse(std::optional<Point2D> p);
    void SetTerrain(const std::vector<Ledge>& ledges);
    void SetAmbient(float typing, bool sleepy, float tempo, float activity);
    void InjectWindowLoom(float strength, Point2D center);
    void InjectTap(Point2D p);
    void Retarget(Size2D newSize);
    Point2D GetFlyPosition();
    std::vector<RECT> GetDirtyRects();

    void UpdateAndRender(RendererD3D11& renderer, float dt);

private:
    std::mutex lock_;
    std::vector<std::function<void(Coordinator&)>> pending_;

    std::optional<Point2D> mouseScene_;
    std::optional<Point2D> prevMouse_;
    Point2D mouseVel_{0, 0};
    float loomOverride_ = 0.0f;

    std::vector<Ledge> terrain_;
    float typingLevel_ = 0.0f;
    bool sleepy_ = false;
    float tempo_ = 1.0f;
    float activity_ = 1.0f;
    float windowLoomL_ = 0.0f;
    float windowLoomR_ = 0.0f;
    Point2D lastFlyPos_{0, 0};

    std::vector<RECT> prevFlyRects1_;
    std::vector<RECT> prevFlyRects2_;
    RECT ComputeFlyRect(const Fly& fly) const;

    SignalBuilder signalBuilder_;
    double msAccumulator_ = 0.0;
    FlyRenderBatches batches_;

    struct LoomSensory {
        float l = 0.0f;
        float r = 0.0f;
        float puff = 0.0f;
    };
    LoomSensory ComputeLoom(const Fly& fly, std::optional<Point2D> mouse, float dt);
};
