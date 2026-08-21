#pragma once

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <memory>
#include "Sim.hpp"
#include "RendererD3D11.hpp"

using Microsoft::WRL::ComPtr;

struct BrainPointVertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT4 color;
    float size;
};

struct FlashNode {
    DirectX::XMFLOAT3 position{0, 0, 0};
    float opacity = 0.0f;
    float maxDuration = 0.28f;
    float timer = 0.0f;
    bool isGF = false;
    bool active = false;
};

class BrainView {
public:
    BrainView();
    ~BrainView();

    bool Initialize(RendererD3D11& renderer, HINSTANCE hInstance, const BrainData& brainData, std::shared_ptr<LIFSim> sim);
    void Show();
    void Hide();
    void Toggle();
    bool IsVisible() const { return isVisible_; }

    void Update(float dt);
    void Render();
    void Move(RECT screenRect);

    void HandleClick(int mouseX, int mouseY);
    void SetHover(bool hovering);

    // Offscreen render for --brainshot
    static int RunBrainshot(const std::string& path);

private:
    bool CreatePipeline(RendererD3D11& renderer);
    bool CreateBuffers(const BrainData& brainData);
    std::string RegionNameFor(const std::vector<int>& picked);

    HWND hwnd_ = nullptr;
    HINSTANCE hInstance_ = nullptr;
    int width_ = 340;
    int height_ = 280;
    bool isVisible_ = false;
    bool isHovered_ = false;

    RendererD3D11* renderer_ = nullptr;
    std::shared_ptr<LIFSim> sim_;

    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID3D11RenderTargetView> rtv_;
    ComPtr<ID3D11DepthStencilView> dsv_;
    ComPtr<ID3D11Texture2D> depthTex_;

    ComPtr<ID3D11VertexShader> brainVS_;
    ComPtr<ID3D11GeometryShader> brainGS_;
    ComPtr<ID3D11PixelShader> brainPS_;
    ComPtr<ID3D11InputLayout> inputLayout_;
    ComPtr<ID3D11Buffer> cbBrainFrame_;

    ComPtr<ID3D11Buffer> somaVB_;
    UINT somaCount_ = 0;

    ComPtr<ID3D11Buffer> circuitVB_;
    UINT circuitCount_ = 0;

    ComPtr<ID3D11Buffer> flashVB_;

    std::vector<FlashNode> flashPool_;
    int flashNext_ = 0;

    float rotationAngle_ = 0.0f;

    // Stimulation ring
    DirectX::XMFLOAT3 stimRingPos_{0, 0, 0};
    float stimRingScale_ = 0.5f;
    float stimRingOpacity_ = 0.0f;

    // Region tooltip
    std::string tooltipText_;
    float tooltipTimer_ = 0.0f;
};
