#include "Environment.hpp"
#include <algorithm>
#include <cmath>

struct EnumData {
    WindowSense* sense;
    RECT screenRect;
    std::vector<Ledge> ledges;
    std::vector<WindowSense::NewWindowInfo> newWins;
    std::set<HWND> currentHWNDs;
    DWORD myPID;
    bool first;
};

static BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    auto* data = reinterpret_cast<EnumData*>(lParam);

    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) {
        return TRUE;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == data->myPID) {
        return TRUE;
    }

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) {
        return TRUE;
    }

    RECT r;
    if (!GetWindowRect(hwnd, &r)) {
        return TRUE;
    }

    int width = r.right - r.left;
    int height = r.bottom - r.top;
    if (width < 160 || height < 60) {
        return TRUE;
    }

    // Check intersection with the screen
    RECT screen = data->screenRect;
    RECT isect;
    if (!IntersectRect(&isect, &r, &screen)) {
        return TRUE;
    }

    data->currentHWNDs.insert(hwnd);

    float screenW = static_cast<float>(screen.right - screen.left);
    float screenH = static_cast<float>(screen.bottom - screen.top);
    float midX = (screen.left + screen.right) * 0.5f;
    float midY = (screen.top + screen.bottom) * 0.5f;

    // Scene coordinates: centered on this display, +Y up
    float topY = midY - static_cast<float>(r.top);
    float x0 = std::max(static_cast<float>(r.left) - midX, -screenW * 0.5f + 15.0f);
    float x1 = std::min(static_cast<float>(r.right) - midX, screenW * 0.5f - 15.0f);

    int id = static_cast<int>(reinterpret_cast<uintptr_t>(hwnd));

    if (topY < screenH * 0.5f - 8.0f && topY > -screenH * 0.5f + 8.0f &&
        (x1 - x0) > 100.0f && data->ledges.size() < 12) {
        data->ledges.push_back(Ledge{topY, x0, x1, id});
    }

    if (!data->first && !data->sense->poll({}).newWindows.empty()) {
        // Will be checked after collection
    }

    return TRUE;
}

WindowSense::Snapshot WindowSense::poll(RECT screenRect) {
    EnumData data;
    data.sense = this;
    data.screenRect = screenRect;
    data.myPID = myPID;
    data.first = first;

    EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&data));

    float midX = (screenRect.left + screenRect.right) * 0.5f;
    float midY = (screenRect.top + screenRect.bottom) * 0.5f;

    for (HWND hwnd : data.currentHWNDs) {
        if (!first && knownHWNDs.find(hwnd) == knownHWNDs.end()) {
            RECT r;
            if (GetWindowRect(hwnd, &r)) {
                Point2D center{
                    (r.left + r.right) * 0.5f - midX,
                    midY - (r.top + r.bottom) * 0.5f
                };
                float size = static_cast<float>(std::max(r.right - r.left, r.bottom - r.top));
                data.newWins.push_back(NewWindowInfo{center, size});
            }
        }
    }

    knownHWNDs = std::move(data.currentHWNDs);
    first = false;

    return Snapshot{std::move(data.ledges), std::move(data.newWins)};
}

float circadianActivity(double hour) {
    struct Pt { double h; float v; };
    static const Pt pts[] = {
        {0, 0.25f}, {5, 0.25f}, {8, 1.0f}, {10, 1.0f}, {13, 0.55f},
        {15, 0.55f}, {17, 1.0f}, {20, 1.0f}, {23, 0.3f}, {24, 0.25f}
    };
    for (size_t i = 0; i < _countof(pts) - 1; ++i) {
        if (hour >= pts[i].h && hour <= pts[i + 1].h) {
            float t = static_cast<float>((hour - pts[i].h) / std::max(0.001, pts[i + 1].h - pts[i].h));
            return pts[i].v + (pts[i + 1].v - pts[i].v) * t;
        }
    }
    return 0.25f;
}

float userIdleSeconds() {
    LASTINPUTINFO lii = {};
    lii.cbSize = sizeof(LASTINPUTINFO);
    if (GetLastInputInfo(&lii)) {
        DWORD now = GetTickCount();
        return static_cast<float>(now - lii.dwTime) / 1000.0f;
    }
    return 0.0f;
}

float thermalTempo() {
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) {
        if (sps.ACLineStatus == 1) return 1.15f; // Plugged in / active
    }
    return 1.0f;
}
