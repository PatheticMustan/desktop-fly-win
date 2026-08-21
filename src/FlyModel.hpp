#pragma once

#include <vector>
#include <memory>
#include <random>
#include <optional>
#include <DirectXMath.h>
#include "Sim.hpp"
#include "Geometry.hpp"
#include "RendererD3D11.hpp"

constexpr float FLY_SCALE = 1.50f;
constexpr float EDGE_MARGIN = 50.0f;
constexpr float SCARE_RADIUS = 110.0f;
constexpr float NERVOUS_RADIUS = 240.0f;

struct Ledge {
    float y = 0.0f;
    float x0 = 0.0f;
    float x1 = 0.0f;
    int id = 0;
};

struct Point2D {
    float x = 0.0f;
    float y = 0.0f;
};

struct Size2D {
    float width = 0.0f;
    float height = 0.0f;
};

struct FlyRenderBatches {
    std::vector<InstanceData> opaqueSpheres;
    std::vector<InstanceData> abdomenSpheres;
    std::vector<InstanceData> opaqueCapsules;
    std::vector<InstanceData> opaqueCones;
    std::vector<InstanceData> translucentWings;
    std::vector<InstanceData> motionBlurWings;

    void Clear() {
        opaqueSpheres.clear();
        abdomenSpheres.clear();
        opaqueCapsules.clear();
        opaqueCones.clear();
        translucentWings.clear();
        motionBlurWings.clear();
    }

    void ReserveForFlies(size_t flyCount) {
        opaqueSpheres.reserve(flyCount * 4);
        abdomenSpheres.reserve(flyCount);
        opaqueCapsules.reserve(flyCount * 20);
        opaqueCones.reserve(flyCount);
        translucentWings.reserve(flyCount * 2);
        motionBlurWings.reserve(flyCount * 2);
    }

    void Append(const FlyRenderBatches& other) {
        opaqueSpheres.insert(opaqueSpheres.end(), other.opaqueSpheres.begin(), other.opaqueSpheres.end());
        abdomenSpheres.insert(abdomenSpheres.end(), other.abdomenSpheres.begin(), other.abdomenSpheres.end());
        opaqueCapsules.insert(opaqueCapsules.end(), other.opaqueCapsules.begin(), other.opaqueCapsules.end());
        opaqueCones.insert(opaqueCones.end(), other.opaqueCones.begin(), other.opaqueCones.end());
        translucentWings.insert(translucentWings.end(), other.translucentWings.begin(), other.translucentWings.end());
        motionBlurWings.insert(motionBlurWings.end(), other.motionBlurWings.begin(), other.motionBlurWings.end());
    }
};

class Leg {
public:
    DirectX::XMFLOAT3 attach;
    float baseYaw;
    float swingSign;
    float phase;
    bool isFront;
    float femurH;
    float tibiaH;
    float tarsusH;

    float angle = 0.0f;
    float lift = 0.0f;

    Leg(DirectX::XMFLOAT3 attach, float baseYaw, float swingSign, float phase,
        bool isFront, float femur, float tibia, float tarsus);

    void Render(RendererD3D11& renderer, const DirectX::XMMATRIX& bodyWorld);
    void CollectBatches(std::vector<InstanceData>& capsuleBatch, const DirectX::XMMATRIX& bodyWorld);
};

struct FlyModel {
    std::vector<Leg> legs;
    DirectX::XMFLOAT3 thoraxPos{0, 2.5f, 6.2f};
    DirectX::XMFLOAT3 thoraxScale{0.95f, 1.15f, 0.85f};
    DirectX::XMFLOAT3 abdomenPos{0, -6.5f, 5.6f};
    DirectX::XMFLOAT3 abdomenScale{0.9f, 1.5f, 0.75f};
    DirectX::XMFLOAT3 headPos{0, 9.0f, 6.0f};
    DirectX::XMFLOAT3 headScale{1.0f, 0.85f, 0.9f};

    DirectX::XMFLOAT3 leftWingRot{0, 0, -0.13f};
    DirectX::XMFLOAT3 rightWingRot{0, 0, 0.13f};

    bool blurWingsVisible = false;
    float blurWingOpacity = 0.3f;
    DirectX::XMFLOAT3 leftBlurRot{0, 0, 0.45f};
    DirectX::XMFLOAT3 rightBlurRot{0, 0, -0.45f};
};

class Fly {
public:
    enum class State { Walking, Idle, Grooming, Flying, Sleeping };

    FlyModel model;
    Point2D pos{0, 0};
    float heading = 0.0f;
    float speed = 30.0f;
    State state = State::Walking;
    float stateTimer = 2.0f;
    float gaitPhase = 0.0f;
    float time = 0.0f;
    float scareCooldown = 0.0f;
    float dartCooldown = 0.0f;
    float backwardTimer = 0.0f;
    float dartTimer = 0.0f;
    float stateAge = 0.0f;

    std::vector<Ledge> terrain;
    std::optional<Ledge> ledge;

    float gaitPhasePublic() const { return gaitPhase; }
    float walkingIntensity() const;

    Point2D flightFrom{0, 0};
    Point2D flightTo{0, 0};
    float flightT = 0.0f;
    float flightDur = 1.0f;
    float flightEffort = 0.6f;
    float effortCurrent = 0.6f;
    float alt = 0.0f;
    float pitch = 0.0f;
    float flapPhase = 0.0f;
    float wingRaise = 0.0f;

    DirectX::XMFLOAT3 currentScale{FLY_SCALE, FLY_SCALE, FLY_SCALE};
    DirectX::XMFLOAT3 currentPos3D{0, 0, 0};

    Fly(Point2D p);

    void startFlight(Size2D bounds, std::optional<Point2D> awayFrom = std::nullopt,
                     bool escape = false, std::optional<float> effort = std::nullopt);
    void update(float dt, Size2D bounds, std::optional<Point2D> mouse, const std::optional<BrainSignals>& signals);
    void Render(RendererD3D11& renderer);
    void CollectBatches(FlyRenderBatches& batches);

private:
    bool brainLive = false;
    float liveArousal = 0.0f;
    float liveWing = 0.0f;

    std::mt19937 rng;

    void land();
    void pickNextState();
    void setState(State s);
    void brainBehavior(const BrainSignals& s, float dt, Size2D bounds, std::optional<Point2D> mouse);
    float effectiveSpeed() const;
    void updateWalk(float dt, Size2D bounds);
    void applyAltitude();
    void updateFlight(float dt);
    void updateLegs(float dt);
    void updateWings(float dt);
    void syncNode();
};
