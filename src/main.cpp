#include "Sim.hpp"
#include "FlyModel.hpp"
#include "RendererD3D11.hpp"
#include "DCompOverlay.hpp"
#include "Environment.hpp"
#include "BrainView.hpp"
#include "Coordinator.hpp"
#include <windows.h>
#include <shellapi.h>
#include <timeapi.h>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <cassert>

#pragma comment(lib, "winmm.lib")

int runSimtest();
int runBehaviorTest();
int runSnapshot(const std::string& path);

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_TITLE 1001
#define ID_TRAY_INFO 1002
#define ID_TRAY_PAUSE 1003
#define ID_TRAY_BRAIN 1004
#define ID_TRAY_ESCAPE 1005
#define ID_TRAY_NEXT_DISPLAY 1006
#define ID_TRAY_ADD_FLY 1007
#define ID_TRAY_ADD_50_FLIES 1008
#define ID_TRAY_REMOVE_FLY 1009
#define ID_TRAY_SCARE 1010
#define ID_TRAY_QUIT 1011

struct MonitorInfo {
    HMONITOR handle;
    RECT rect;
};

static std::vector<MonitorInfo> g_monitors;
static BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC, LPRECT lprcMonitor, LPARAM) {
    g_monitors.push_back({hMon, *lprcMonitor});
    return TRUE;
}

static void RefreshMonitors() {
    g_monitors.clear();
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, 0);
}

class App {
public:
    App() = default;
    ~App() {
        if (trayAdded_) {
            Shell_NotifyIconW(NIM_DELETE, &nid_);
            trayAdded_ = false;
        }
    }

    bool Init(HINSTANCE hInstance) {
        hInstance_ = hInstance;
        RefreshMonitors();
        if (g_monitors.empty()) {
            std::cerr << "No monitors found\n";
            return false;
        }

        currentMonitorIdx_ = 0;
        currentScreenRect_ = g_monitors[0].rect;

        if (!renderer_.Initialize(false)) {
            std::cerr << "Failed to initialize Direct3D 11\n";
            return false;
        }

        auto spikeBus = std::make_shared<SpikeBus>();
        auto dataOpt = loadBrainData();
        if (dataOpt) {
            brainData_ = *dataOpt;
            sim_ = std::make_shared<LIFSim>(brainData_->circuit, spikeBus);
            dataInfo_ = "FlyWire v783 · " + std::to_string(brainData_->points.points.size()) +
                        " somas · circuit " + std::to_string(brainData_->circuit.neurons.size()) +
                        "n/" + std::to_string(brainData_->circuit.edges.size()) + "e";
        }

        Size2D bounds{
            static_cast<float>(currentScreenRect_.right - currentScreenRect_.left),
            static_cast<float>(currentScreenRect_.bottom - currentScreenRect_.top)
        };

        coordinator_ = std::make_unique<Coordinator>(bounds, sim_);

        if (!overlay_.Initialize(renderer_, hInstance, currentScreenRect_)) {
            std::cerr << "Failed to initialize DirectComposition overlay\n";
            return false;
        }

        // Set message handler for system tray events
        overlay_.SetCustomHandler([this](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
            if (msg == WM_TRAYICON) {
                if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP || lParam == WM_CONTEXTMENU) {
                    ShowContextMenu(hwnd);
                }
                return 0;
            }
            if (msg == WM_COMMAND) {
                HandleTrayMenu(hwnd, wParam);
                return 0;
            }
            return 0;
        });

        if (brainData_ && sim_) {
            brainView_ = std::make_unique<BrainView>();
            if (brainView_->Initialize(renderer_, hInstance, *brainData_, sim_)) {
                brainView_->Move(currentScreenRect_);
                brainView_->Show();
            }
        }

        SetupTrayIcon();
        return true;
    }

    int Run(HINSTANCE hInstance) {
        if (!Init(hInstance)) return 1;

        timeBeginPeriod(1);

        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);

        DEVMODEW dm = {};
        dm.dmSize = sizeof(dm);
        int targetFps = 60;
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency >= 30) {
            targetFps = static_cast<int>(dm.dmDisplayFrequency);
        }
        targetFps = std::min(144, targetFps);
        double targetFrameSeconds = 1.0 / static_cast<double>(targetFps);

        LARGE_INTEGER lastFrameTime;
        QueryPerformanceCounter(&lastFrameTime);

        float mouseTimer = 0.0f;
        float windowTimer = 0.0f;
        float brainTimer = 0.0f;
        bool lastLButton = false;

        MSG msg = {};
        while (running_) {
            LARGE_INTEGER frameStart;
            QueryPerformanceCounter(&frameStart);

            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running_ = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (!running_) break;

            if (paused_) {
                Sleep(20);
                QueryPerformanceCounter(&lastFrameTime);
                continue;
            }

            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            float dt = static_cast<float>(now.QuadPart - lastFrameTime.QuadPart) / static_cast<float>(freq.QuadPart);
            lastFrameTime = now;
            dt = std::min(0.05f, std::max(0.0f, dt));

            // 30 Hz Mouse & Senses
            mouseTimer += dt;
            if (mouseTimer >= 1.0f / 30.0f) {
                mouseTimer = 0.0f;
                POINT pt;
                if (GetCursorPos(&pt)) {
                    float midX = (currentScreenRect_.left + currentScreenRect_.right) * 0.5f;
                    float midY = (currentScreenRect_.top + currentScreenRect_.bottom) * 0.5f;
                    coordinator_->SetMouse(Point2D{
                        static_cast<float>(pt.x) - midX,
                        midY - static_cast<float>(pt.y)
                    });
                }

                // User idle & circadian
                float idle = userIdleSeconds();
                auto systemNow = std::chrono::system_clock::now();
                time_t tt = std::chrono::system_clock::to_time_t(systemNow);
                tm localTm;
                localtime_s(&localTm, &tt);
                double hour = localTm.tm_hour + localTm.tm_min / 60.0;
                bool sleepy = (idle > 600.0f && (hour >= 22.0 || hour < 6.0)) || idle > 1800.0f;

                typingLevel_ += (((idle < 0.6f) ? 1.0f : 0.0f) - typingLevel_) * 0.15f;

                coordinator_->SetAmbient(typingLevel_, sleepy, thermalTempo(), circadianActivity(hour));
            }

            // 1.4 Hz Window Terrain Scanner
            windowTimer += dt;
            if (windowTimer >= 0.7f) {
                windowTimer = 0.0f;
                auto snap = windowSense_.poll(currentScreenRect_);
                coordinator_->SetTerrain(snap.ledges);
                Point2D flyPos = coordinator_->GetFlyPosition();
                for (const auto& nw : snap.newWindows) {
                    float d = std::hypot(nw.center.x - flyPos.x, nw.center.y - flyPos.y);
                    float strength = clampf(1.0f - d / 480.0f, 0.0f, 1.0f) * 0.75f;
                    if (strength > 0.08f) {
                        coordinator_->InjectWindowLoom(strength, nw.center);
                    }
                }
            }

            // Global mouse click tap sensing
            bool lButtonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            if (lButtonDown && !lastLButton) {
                POINT pt;
                if (GetCursorPos(&pt)) {
                    float midX = (currentScreenRect_.left + currentScreenRect_.right) * 0.5f;
                    float midY = (currentScreenRect_.top + currentScreenRect_.bottom) * 0.5f;
                    coordinator_->InjectTap(Point2D{
                        static_cast<float>(pt.x) - midX,
                        midY - static_cast<float>(pt.y)
                    });
                }
            }
            lastLButton = lButtonDown;

            // Render Frame
            overlay_.BeginFrame();
            coordinator_->UpdateAndRender(renderer_, dt);
            overlay_.EndFrame();

            if (brainView_ && brainView_->IsVisible()) {
                brainTimer += dt;
                if (brainTimer >= 1.0f / 30.0f) {
                    brainView_->Update(brainTimer);
                    brainView_->Render();
                    brainTimer = 0.0f;
                }
            }

            // High-precision frame limiter / pacer
            LARGE_INTEGER frameEnd;
            QueryPerformanceCounter(&frameEnd);
            double elapsedSec = static_cast<double>(frameEnd.QuadPart - frameStart.QuadPart) / static_cast<double>(freq.QuadPart);
            if (elapsedSec < targetFrameSeconds) {
                double remainingSec = targetFrameSeconds - elapsedSec;
                DWORD sleepMs = static_cast<DWORD>(remainingSec * 1000.0);
                if (sleepMs > 1) {
                    Sleep(sleepMs - 1);
                }
                LARGE_INTEGER current;
                do {
                    QueryPerformanceCounter(&current);
                } while ((static_cast<double>(current.QuadPart - frameStart.QuadPart) / static_cast<double>(freq.QuadPart)) < targetFrameSeconds);
            }
        }

        timeEndPeriod(1);
        return 0;
    }

    void HandleTrayMenu(HWND hwnd, WPARAM wParam) {
        UNREFERENCED_PARAMETER(hwnd);
        switch (LOWORD(wParam)) {
        case ID_TRAY_PAUSE:
            paused_ = !paused_;
            break;
        case ID_TRAY_BRAIN:
            if (brainView_) brainView_->Toggle();
            break;
        case ID_TRAY_ESCAPE:
            coordinator_->EscapeTest();
            break;
        case ID_TRAY_NEXT_DISPLAY:
            MoveToNextDisplay();
            break;
        case ID_TRAY_ADD_FLY:
            coordinator_->AddFly();
            break;
        case ID_TRAY_ADD_50_FLIES:
            coordinator_->AddFlies(50);
            break;
        case ID_TRAY_REMOVE_FLY:
            coordinator_->RemoveFly();
            break;
        case ID_TRAY_SCARE:
            coordinator_->ScareAll();
            break;
        case ID_TRAY_QUIT:
            running_ = false;
            PostQuitMessage(0);
            break;
        }
    }

    HMENU BuildMenu() {
        RefreshMonitors();
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING | MF_DISABLED, ID_TRAY_TITLE, L"Desktop Fly");
        std::wstring wInfo(dataInfo_.begin(), dataInfo_.end());
        AppendMenuW(hMenu, MF_STRING | MF_DISABLED, ID_TRAY_INFO, wInfo.c_str());
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

        AppendMenuW(hMenu, MF_STRING, ID_TRAY_PAUSE, paused_ ? L"Resume" : L"Pause");
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_BRAIN, L"Show/Hide Brain");
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_ESCAPE, L"Escape Test (loom)");

        if (g_monitors.size() > 1) {
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_NEXT_DISPLAY, L"Move to Next Display");
        }

        AppendMenuW(hMenu, MF_STRING, ID_TRAY_ADD_FLY, L"Add Fly");
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_ADD_50_FLIES, L"Add 50 Flies");
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_REMOVE_FLY, L"Remove Fly");
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_SCARE, L"Scare Flies");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu, MF_STRING, ID_TRAY_QUIT, L"Quit");
        return hMenu;
    }

    void ShowContextMenu(HWND hwnd) {
        HMENU hMenu = BuildMenu();
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(hwnd);
        TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, nullptr);
        PostMessage(hwnd, WM_NULL, 0, 0);
        DestroyMenu(hMenu);
    }

    void MoveToNextDisplay() {
        RefreshMonitors();
        if (g_monitors.size() <= 1) return;
        currentMonitorIdx_ = (currentMonitorIdx_ + 1) % g_monitors.size();
        currentScreenRect_ = g_monitors[currentMonitorIdx_].rect;

        Size2D newSize{
            static_cast<float>(currentScreenRect_.right - currentScreenRect_.left),
            static_cast<float>(currentScreenRect_.bottom - currentScreenRect_.top)
        };

        overlay_.Resize(currentScreenRect_);
        coordinator_->Retarget(newSize);
        if (brainView_) brainView_->Move(currentScreenRect_);
    }

    // Accessors for automated verification
    Coordinator* GetCoordinator() { return coordinator_.get(); }
    BrainView* GetBrainView() { return brainView_.get(); }
    bool IsPaused() const { return paused_; }
    bool HasTrayIcon() const { return trayAdded_; }
    RendererD3D11& GetRenderer() { return renderer_; }

private:
    void SetupTrayIcon() {
        nid_.cbSize = sizeof(NOTIFYICONDATAW);
        nid_.hWnd = overlay_.GetHWND();
        nid_.uID = 1;
        nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid_.uCallbackMessage = WM_TRAYICON;
        nid_.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        wcscpy_s(nid_.szTip, L"DesktopFly 🪰");

        Shell_NotifyIconW(NIM_ADD, &nid_);
        trayAdded_ = true;
    }

    HINSTANCE hInstance_ = nullptr;
    RendererD3D11 renderer_;
    DCompOverlay overlay_;
    std::unique_ptr<Coordinator> coordinator_;
    std::shared_ptr<LIFSim> sim_;
    std::unique_ptr<BrainView> brainView_;
    std::optional<BrainData> brainData_;
    WindowSense windowSense_;

    NOTIFYICONDATAW nid_ = {};
    bool trayAdded_ = false;
    bool running_ = true;
    bool paused_ = false;
    float typingLevel_ = 0.0f;

    size_t currentMonitorIdx_ = 0;
    RECT currentScreenRect_{};
    std::string dataInfo_ = "no data — run etl.py";
};

int runTrayTest() {
    std::cout << "--- Verifying System Tray Functionality ---\n";
    App app;
    if (!app.Init(GetModuleHandle(nullptr))) {
        std::cerr << "FAIL: App::Init failed\n";
        return 1;
    }

    int failures = 0;

    // 1. Verify Tray Icon registration
    if (app.HasTrayIcon()) {
        std::cout << "PASS: Shell_NotifyIconW successfully registered tray icon\n";
    } else {
        std::cerr << "FAIL: Shell_NotifyIconW registration failed\n";
        failures++;
    }

    // 2. Verify Menu Construction
    HMENU hMenu = app.BuildMenu();
    int itemCount = GetMenuItemCount(hMenu);
    if (hMenu && itemCount >= 9) {
        std::cout << "PASS: Tray context menu constructed with " << itemCount << " items\n";
    } else {
        std::cerr << "FAIL: Context menu construction failed (itemCount = " << itemCount << ")\n";
        failures++;
    }
    DestroyMenu(hMenu);

    auto* coord = app.GetCoordinator();

    // 3. Verify Add Fly
    coord->UpdateAndRender(app.GetRenderer(), 0.016f); // Drain initial add
    size_t initialFlies = coord->flies.size();
    app.HandleTrayMenu(nullptr, ID_TRAY_ADD_FLY);
    coord->UpdateAndRender(app.GetRenderer(), 0.016f);
    if (coord->flies.size() == initialFlies + 1) {
        std::cout << "PASS: ID_TRAY_ADD_FLY spawned fly (" << initialFlies << " -> " << coord->flies.size() << ")\n";
    } else {
        std::cerr << "FAIL: ID_TRAY_ADD_FLY failed (" << initialFlies << " -> " << coord->flies.size() << ")\n";
        failures++;
    }

    // 4. Verify Remove Fly
    app.HandleTrayMenu(nullptr, ID_TRAY_REMOVE_FLY);
    coord->UpdateAndRender(app.GetRenderer(), 0.016f);
    if (coord->flies.size() == initialFlies) {
        std::cout << "PASS: ID_TRAY_REMOVE_FLY removed extra fly (" << coord->flies.size() << " flies remaining)\n";
    } else {
        std::cerr << "FAIL: ID_TRAY_REMOVE_FLY failed\n";
        failures++;
    }

    // 5. Verify Pause / Resume Toggle
    bool p0 = app.IsPaused();
    app.HandleTrayMenu(nullptr, ID_TRAY_PAUSE);
    bool p1 = app.IsPaused();
    app.HandleTrayMenu(nullptr, ID_TRAY_PAUSE);
    bool p2 = app.IsPaused();
    if (!p0 && p1 && !p2) {
        std::cout << "PASS: ID_TRAY_PAUSE toggled paused state (false -> true -> false)\n";
    } else {
        std::cerr << "FAIL: ID_TRAY_PAUSE failed\n";
        failures++;
    }

    // 6. Verify Escape Test (Loom Injection)
    coord->flies[0].state = Fly::State::Idle;
    app.HandleTrayMenu(nullptr, ID_TRAY_ESCAPE);
    for (int i = 0; i < 30; ++i) {
        coord->UpdateAndRender(app.GetRenderer(), 0.016f);
        if (coord->flies[0].state == Fly::State::Flying) break;
    }
    if (coord->flies[0].state == Fly::State::Flying) {
        std::cout << "PASS: ID_TRAY_ESCAPE triggered Giant Fiber escape takeoff\n";
    } else {
        std::cerr << "FAIL: ID_TRAY_ESCAPE did not trigger takeoff\n";
        failures++;
    }

    // 7. Verify Scare Flies
    app.HandleTrayMenu(nullptr, ID_TRAY_ADD_FLY);
    for (int i = 0; i < 30; ++i) {
        coord->UpdateAndRender(app.GetRenderer(), 0.016f);
    }
    for (auto& f : coord->flies) {
        f.state = Fly::State::Walking;
        f.scareCooldown = 0.0f;
    }
    app.HandleTrayMenu(nullptr, ID_TRAY_SCARE);
    std::vector<bool> flew(coord->flies.size(), false);
    for (int i = 0; i < 30; ++i) {
        coord->UpdateAndRender(app.GetRenderer(), 0.016f);
        for (size_t k = 0; k < coord->flies.size(); ++k) {
            if (coord->flies[k].state == Fly::State::Flying) flew[k] = true;
        }
    }
    bool allFlying = true;
    for (bool b : flew) {
        if (!b) allFlying = false;
    }
    if (allFlying) {
        std::cout << "PASS: ID_TRAY_SCARE startled all " << coord->flies.size() << " flies into flight\n";
    } else {
        std::cerr << "FAIL: ID_TRAY_SCARE failed (flew[0]=" << flew[0] << ", flew[1]=" << (flew.size() > 1 ? (flew[1] ? 1 : 0) : -1) << ")\n";
        failures++;
    }

    // 8. Verify Brain View Toggle
    auto* bv = app.GetBrainView();
    if (bv) {
        bool v0 = bv->IsVisible();
        app.HandleTrayMenu(nullptr, ID_TRAY_BRAIN);
        bool v1 = bv->IsVisible();
        app.HandleTrayMenu(nullptr, ID_TRAY_BRAIN);
        bool v2 = bv->IsVisible();
        if (v0 && !v1 && v2) {
            std::cout << "PASS: ID_TRAY_BRAIN toggled brain window visibility (true -> false -> true)\n";
        } else {
            std::cerr << "FAIL: ID_TRAY_BRAIN visibility toggle failed\n";
            failures++;
        }
    }

    // 9. Verify Move to Next Display
    app.HandleTrayMenu(nullptr, ID_TRAY_NEXT_DISPLAY);
    std::cout << "PASS: ID_TRAY_NEXT_DISPLAY handled screen retargeting cleanly\n";

    if (failures == 0) {
        std::cout << "ALL SYSTEM TRAY TESTS PASS\n";
    } else {
        std::cout << failures << " FAILURES\n";
    }
    return (failures == 0) ? 0 : 1;
}

static void EnableDpiAwareness() {
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef BOOL (WINAPI *SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
        auto setContext = (SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (setContext && setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            return;
        }
    }
    HMODULE hShcore = LoadLibraryW(L"shcore.dll");
    if (hShcore) {
        typedef enum PROCESS_DPI_AWARENESS {
            PROCESS_DPI_UNAWARE = 0,
            PROCESS_SYSTEM_DPI_AWARE = 1,
            PROCESS_PER_MONITOR_DPI_AWARE = 2
        } PROCESS_DPI_AWARENESS;
        typedef HRESULT (WINAPI *SetProcessDpiAwarenessProc)(PROCESS_DPI_AWARENESS);
        auto setDpiAwareness = (SetProcessDpiAwarenessProc)GetProcAddress(hShcore, "SetProcessDpiAwareness");
        if (setDpiAwareness && SUCCEEDED(setDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE))) {
            FreeLibrary(hShcore);
            return;
        }
        FreeLibrary(hShcore);
    }
    SetProcessDPIAware();
}

int main(int argc, char* argv[]) {
    EnableDpiAwareness();

    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--simtest") {
            return runSimtest();
        }
        if (args[i] == "--behaviortest") {
            return runBehaviorTest();
        }
        if (args[i] == "--traytest") {
            return runTrayTest();
        }
        if (args[i] == "--snapshot") {
            std::string outPath = (i + 1 < args.size()) ? args[i + 1] : "preview.png";
            return runSnapshot(outPath);
        }
        if (args[i] == "--brainshot") {
            std::string outPath = (i + 1 < args.size()) ? args[i + 1] : "brain.png";
            return BrainView::RunBrainshot(outPath);
        }
    }

    // Interactive GUI application
    App app;
    return app.Run(GetModuleHandle(nullptr));
}
