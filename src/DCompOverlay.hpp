#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <memory>
#include <functional>
#include "RendererD3D11.hpp"

using Microsoft::WRL::ComPtr;

class DCompOverlay {
public:
    using MessageHandler = std::function<LRESULT(HWND, UINT, WPARAM, LPARAM)>;

    DCompOverlay();
    ~DCompOverlay();

    bool Initialize(RendererD3D11& renderer, HINSTANCE hInstance, RECT screenRect);
    void SetCustomHandler(MessageHandler handler) { customHandler_ = std::move(handler); }
    void BeginFrame();
    void EndFrame();
    void Resize(RECT screenRect);

    HWND GetHWND() const { return hwnd_; }
    int GetWidth() const { return width_; }
    int GetHeight() const { return height_; }

    MessageHandler customHandler_;

private:
    bool CreateSwapchainAndDComp(RendererD3D11& renderer);

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    RECT screenRect_{};

    RendererD3D11* renderer_ = nullptr;

    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<ID3D11DepthStencilView> dsv_;
    ComPtr<ID3D11Texture2D> depthTex_;

    ComPtr<IDCompositionDevice> dcompDevice_;
    ComPtr<IDCompositionTarget> dcompTarget_;
    ComPtr<IDCompositionVisual> dcompVisual_;
};
