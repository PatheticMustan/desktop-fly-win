#include "DCompOverlay.hpp"
#include <iostream>

#pragma comment(lib, "dcomp.lib")

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* overlay = reinterpret_cast<DCompOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (overlay && overlay->customHandler_) {
        LRESULT res = overlay->customHandler_(hwnd, msg, wParam, lParam);
        if (msg == (WM_USER + 1) || msg == WM_COMMAND) {
            return res;
        }
    }
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

DCompOverlay::DCompOverlay() = default;
DCompOverlay::~DCompOverlay() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool DCompOverlay::Initialize(RendererD3D11& renderer, HINSTANCE hInstance, RECT screenRect) {
    renderer_ = &renderer;
    hInstance_ = hInstance;
    screenRect_ = screenRect;
    width_ = screenRect.right - screenRect.left;
    height_ = screenRect.bottom - screenRect.top;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DesktopFlyOverlayWindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST | WS_EX_TRANSPARENT |
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName,
        L"DesktopFly Overlay",
        WS_POPUP | WS_VISIBLE,
        screenRect.left, screenRect.top, width_, height_,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd_) {
        std::cerr << "Failed to create overlay HWND" << std::endl;
        return false;
    }

    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    SetWindowPos(hwnd_, HWND_TOPMOST, screenRect.left, screenRect.top, width_, height_,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);

    return CreateSwapchainAndDComp(renderer);
}

bool DCompOverlay::CreateSwapchainAndDComp(RendererD3D11& renderer) {
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = renderer.GetDevice().As(&dxgiDevice);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIFactory2> dxgiFactory;
    hr = adapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) return false;

    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.Width = width_;
    scDesc.Height = height_;
    scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scDesc.Stereo = FALSE;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = 2;
    scDesc.Scaling = DXGI_SCALING_STRETCH;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = dxgiFactory->CreateSwapChainForComposition(renderer.GetDevice().Get(), &scDesc, nullptr, &swapChain_);
    if (FAILED(hr)) {
        std::cerr << "Failed to create SwapChainForComposition: hr = 0x" << std::hex << hr << std::endl;
        return false;
    }

    // Create RTV
    ComPtr<ID3D11Texture2D> backBuffer;
    hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) return false;

    hr = renderer.GetDevice()->CreateRenderTargetView(backBuffer.Get(), nullptr, &rtv_);
    if (FAILED(hr)) return false;

    // Create Depth Texture & DSV
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width_;
    depthDesc.Height = height_;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = renderer.GetDevice()->CreateTexture2D(&depthDesc, nullptr, &depthTex_);
    if (FAILED(hr)) return false;

    hr = renderer.GetDevice()->CreateDepthStencilView(depthTex_.Get(), nullptr, &dsv_);
    if (FAILED(hr)) return false;

    // DirectComposition
    hr = DCompositionCreateDevice(dxgiDevice.Get(), __uuidof(IDCompositionDevice),
                                  reinterpret_cast<void**>(dcompDevice_.GetAddressOf()));
    if (FAILED(hr)) {
        std::cerr << "Failed to create DirectComposition device: hr = 0x" << std::hex << hr << std::endl;
        return false;
    }

    hr = dcompDevice_->CreateTargetForHwnd(hwnd_, TRUE, &dcompTarget_);
    if (FAILED(hr)) return false;

    hr = dcompDevice_->CreateVisual(&dcompVisual_);
    if (FAILED(hr)) return false;

    hr = dcompVisual_->SetContent(swapChain_.Get());
    if (FAILED(hr)) return false;

    hr = dcompTarget_->SetRoot(dcompVisual_.Get());
    if (FAILED(hr)) return false;

    hr = dcompDevice_->Commit();
    if (FAILED(hr)) return false;

    return true;
}

void DCompOverlay::BeginFrame() {
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    renderer_->GetContext()->ClearRenderTargetView(rtv_.Get(), clearColor);
    renderer_->GetContext()->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(width_);
    vp.Height = static_cast<float>(height_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    renderer_->GetContext()->RSSetViewports(1, &vp);

    renderer_->GetContext()->OMSetRenderTargets(1, rtv_.GetAddressOf(), dsv_.Get());
}

void DCompOverlay::EndFrame() {
    swapChain_->Present(1, 0);
}

void DCompOverlay::Resize(RECT screenRect) {
    screenRect_ = screenRect;
    width_ = screenRect.right - screenRect.left;
    height_ = screenRect.bottom - screenRect.top;

    SetWindowPos(hwnd_, HWND_TOPMOST, screenRect.left, screenRect.top, width_, height_,
                 SWP_NOACTIVATE | SWP_NOZORDER);

    rtv_.Reset();
    dsv_.Reset();
    depthTex_.Reset();

    if (swapChain_) {
        swapChain_->ResizeBuffers(2, width_, height_, DXGI_FORMAT_B8G8R8A8_UNORM, 0);

        ComPtr<ID3D11Texture2D> backBuffer;
        swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        renderer_->GetDevice()->CreateRenderTargetView(backBuffer.Get(), nullptr, &rtv_);

        D3D11_TEXTURE2D_DESC depthDesc = {};
        depthDesc.Width = width_;
        depthDesc.Height = height_;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        renderer_->GetDevice()->CreateTexture2D(&depthDesc, nullptr, &depthTex_);
        renderer_->GetDevice()->CreateDepthStencilView(depthTex_.Get(), nullptr, &dsv_);
    }
}
