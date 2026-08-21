#pragma once

#include <vector>
#include <set>
#include <utility>
#include <windows.h>
#include "FlyModel.hpp"

class WindowSense {
public:
    struct NewWindowInfo {
        Point2D center;
        float size;
    };

    struct Snapshot {
        std::vector<Ledge> ledges;
        std::vector<NewWindowInfo> newWindows;
    };

    Snapshot poll(RECT screenRect);

private:
    std::set<HWND> knownHWNDs;
    bool first = true;
    DWORD myPID = GetCurrentProcessId();
};

float circadianActivity(double hour);
float userIdleSeconds();
float thermalTempo();
