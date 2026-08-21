#include "BrainView.hpp"
#include <d3dcompiler.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include "../external/stb_image_write.h"

static const DirectX::XMFLOAT4 CLASS_COLORS[] = {
    {0.16f, 0.22f, 0.34f, 1.0f}, // optic
    {0.45f, 0.33f, 0.16f, 1.0f}, // central
    {0.14f, 0.36f, 0.34f, 1.0f}, // sensory
    {0.10f, 0.48f, 0.62f, 1.0f}, // visual_projection
    {0.38f, 0.22f, 0.55f, 1.0f}, // visual_centrifugal
    {0.62f, 0.28f, 0.10f, 1.0f}, // descending
    {0.20f, 0.45f, 0.18f, 1.0f}, // ascending
    {0.55f, 0.14f, 0.14f, 1.0f}, // motor
    {0.50f, 0.25f, 0.40f, 1.0f}, // endocrine
};

struct ConstantBufferPerBrainFrame {
    DirectX::XMMATRIX ViewProj;
    DirectX::XMMATRIX ModelWorld;
    DirectX::XMFLOAT4 FlashParams;
};

static std::string ReadShaderFile(const std::string& filename) {
    std::vector<std::string> paths = {
        filename,
        "shaders/" + filename,
        "../shaders/" + filename,
        "../../shaders/" + filename
    };
    for (const auto& p : paths) {
        std::ifstream file(p);
        if (file.is_open()) {
            std::stringstream ss;
            ss << file.rdbuf();
            return ss.str();
        }
    }
    return "";
}

static LRESULT CALLBACK BrainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* bv = reinterpret_cast<BrainView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_LBUTTONDOWN:
        if (bv) {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            bv->HandleClick(x, y);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (bv) {
            TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            bv->SetHover(true);
        }
        return 0;
    case WM_MOUSELEAVE:
        if (bv) bv->SetHover(false);
        return 0;
    case WM_CLOSE:
        if (bv) bv->Hide();
        return 0;
    case WM_DESTROY:
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

BrainView::BrainView() : flashPool_(48) {}
BrainView::~BrainView() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool BrainView::Initialize(RendererD3D11& renderer, HINSTANCE hInstance, const BrainData& brainData, std::shared_ptr<LIFSim> sim) {
    renderer_ = &renderer;
    hInstance_ = hInstance;
    sim_ = std::move(sim);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = BrainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DesktopFlyBrainWindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc);

    UINT dpi = 96;
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef UINT (WINAPI *GetDpiForSystemProc)();
        auto getDpi = (GetDpiForSystemProc)GetProcAddress(hUser32, "GetDpiForSystem");
        if (getDpi) dpi = getDpi();
    }
    float dpiScale = static_cast<float>(dpi) / 96.0f;
    width_ = static_cast<int>(340.0f * dpiScale);
    height_ = static_cast<int>(280.0f * dpiScale);

    RECT r = {0, 0, width_, height_};
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"Fly Brain — FlyWire v783 (click = stimulate)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hwnd_) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    if (!CreatePipeline(renderer)) return false;
    if (!CreateBuffers(brainData)) return false;

    return true;
}

bool BrainView::CreatePipeline(RendererD3D11& renderer) {
    ComPtr<IDXGIDevice> dxgiDevice;
    renderer.GetDevice().As(&dxgiDevice);

    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(&adapter);

    ComPtr<IDXGIFactory2> dxgiFactory;
    adapter->GetParent(IID_PPV_ARGS(&dxgiFactory));

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

    HRESULT hr = dxgiFactory->CreateSwapChainForHwnd(renderer.GetDevice().Get(), hwnd_, &scDesc, nullptr, nullptr, &swapChain_);
    if (FAILED(hr)) return false;

    ComPtr<ID3D11Texture2D> backBuffer;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    renderer.GetDevice()->CreateRenderTargetView(backBuffer.Get(), nullptr, &rtv_);

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width_;
    depthDesc.Height = height_;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    renderer.GetDevice()->CreateTexture2D(&depthDesc, nullptr, &depthTex_);
    renderer.GetDevice()->CreateDepthStencilView(depthTex_.Get(), nullptr, &dsv_);

    // Compile Brain shaders
    std::string vsSrc = ReadShaderFile("BrainPointVS.hlsl");
    std::string gsSrc = ReadShaderFile("BrainPointGS.hlsl");
    std::string psSrc = ReadShaderFile("BrainPointPS.hlsl");

    ComPtr<ID3DBlob> vsBlob, gsBlob, psBlob, errBlob;
    D3DCompile(vsSrc.data(), vsSrc.size(), "BrainPointVS.hlsl", nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errBlob);
    D3DCompile(gsSrc.data(), gsSrc.size(), "BrainPointGS.hlsl", nullptr, nullptr, "main", "gs_5_0", 0, 0, &gsBlob, &errBlob);
    D3DCompile(psSrc.data(), psSrc.size(), "BrainPointPS.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errBlob);

    renderer.GetDevice()->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &brainVS_);
    renderer.GetDevice()->CreateGeometryShader(gsBlob->GetBufferPointer(), gsBlob->GetBufferSize(), nullptr, &brainGS_);
    renderer.GetDevice()->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &brainPS_);

    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, offsetof(BrainPointVertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(BrainPointVertex, color),    D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"PSIZE",    0, DXGI_FORMAT_R32_FLOAT,          0, offsetof(BrainPointVertex, size),     D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    renderer.GetDevice()->CreateInputLayout(layoutDesc, _countof(layoutDesc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout_);

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(ConstantBufferPerBrainFrame);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    renderer.GetDevice()->CreateBuffer(&cbDesc, nullptr, &cbBrainFrame_);

    return true;
}

bool BrainView::CreateBuffers(const BrainData& brainData) {
    // 1. Somas point cloud
    std::vector<BrainPointVertex> somas;
    somas.reserve(brainData.points.points.size());

    for (const auto& pt : brainData.points.points) {
        DirectX::XMFLOAT4 col = (pt.classIndex >= 0 && pt.classIndex < _countof(CLASS_COLORS))
                                ? CLASS_COLORS[pt.classIndex]
                                : DirectX::XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
        somas.push_back({DirectX::XMFLOAT3(pt.x, pt.y, pt.z), col, 0.035f});
    }
    somaCount_ = static_cast<UINT>(somas.size());

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.ByteWidth = static_cast<UINT>(sizeof(BrainPointVertex) * somas.size());
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA subData = {};
    subData.pSysMem = somas.data();
    renderer_->GetDevice()->CreateBuffer(&vbDesc, &subData, &somaVB_);

    // 2. Circuit neurons overlay
    std::vector<BrainPointVertex> circuitPts;
    circuitPts.reserve(sim_->n);

    for (int i = 0; i < sim_->n; ++i) {
        DirectX::XMFLOAT4 col(0.45f, 0.45f, 0.50f, 1.0f);
        const auto& r = sim_->roles[i];
        if (r == "lc4" || r == "lplc2") col = DirectX::XMFLOAT4(0.15f, 0.85f, 1.0f, 1.0f);
        else if (r == "dna01" || r == "dna02") col = DirectX::XMFLOAT4(1.0f, 0.55f, 0.10f, 1.0f);
        else if (r == "mdn") col = DirectX::XMFLOAT4(1.0f, 0.20f, 0.80f, 1.0f);
        else if (r == "dnp09") col = DirectX::XMFLOAT4(0.25f, 1.0f, 0.35f, 1.0f);
        else if (r == "dng11") col = DirectX::XMFLOAT4(0.75f, 0.55f, 1.0f, 1.0f);
        else if (r == "escw") col = DirectX::XMFLOAT4(1.0f, 0.35f, 0.25f, 1.0f);
        else if (r == "gf") col = DirectX::XMFLOAT4(1.0f, 0.95f, 0.4f, 1.0f);

        circuitPts.push_back({sim_->positions[i], col, 0.065f});
    }
    circuitCount_ = static_cast<UINT>(circuitPts.size());

    vbDesc.ByteWidth = static_cast<UINT>(sizeof(BrainPointVertex) * circuitPts.size());
    subData.pSysMem = circuitPts.data();
    renderer_->GetDevice()->CreateBuffer(&vbDesc, &subData, &circuitVB_);

    // 3. Dynamic Flash Buffer
    vbDesc.Usage = D3D11_USAGE_DYNAMIC;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    vbDesc.ByteWidth = static_cast<UINT>(sizeof(BrainPointVertex) * flashPool_.size());
    renderer_->GetDevice()->CreateBuffer(&vbDesc, nullptr, &flashVB_);

    return true;
}

void BrainView::Show() {
    isVisible_ = true;
    ShowWindow(hwnd_, SW_SHOW);
}

void BrainView::Hide() {
    isVisible_ = false;
    ShowWindow(hwnd_, SW_HIDE);
}

void BrainView::Toggle() {
    if (isVisible_) Hide();
    else Show();
}

void BrainView::SetHover(bool hovering) {
    isHovered_ = hovering;
}

void BrainView::Move(RECT screenRect) {
    int x = screenRect.right - width_ - 18;
    int y = screenRect.bottom - height_ - 50;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void BrainView::Update(float dt) {
    if (!isHovered_) {
        rotationAngle_ += dt * (2.0f * 3.14159265358979323846f / 6.0f); // 6s full rotation
    }

    if (sim_ && sim_->spikeBus) {
        for (const auto& e : sim_->spikeBus->popAll()) {
            if (e.neuron < sim_->n) {
                auto& f = flashPool_[flashNext_];
                flashNext_ = (flashNext_ + 1) % static_cast<int>(flashPool_.size());
                f.position = sim_->positions[e.neuron];
                f.isGF = e.isGF;
                f.maxDuration = e.isGF ? 0.6f : 0.28f;
                f.timer = f.maxDuration;
                f.opacity = e.isGF ? 1.0f : 0.8f;
                f.active = true;
            }
        }
    }

    for (auto& f : flashPool_) {
        if (f.active) {
            f.timer -= dt;
            if (f.timer <= 0.0f) f.active = false;
            else f.opacity = (f.timer / f.maxDuration) * (f.isGF ? 1.0f : 0.8f);
        }
    }

    if (stimRingOpacity_ > 0.01f) {
        stimRingScale_ += dt * 1.5f;
        stimRingOpacity_ -= dt * 1.8f;
    }

    if (tooltipTimer_ > 0.0f) {
        tooltipTimer_ -= dt;
    }
}

void BrainView::Render() {
    if (!isVisible_ || !rtv_) return;

    float clearColor[4] = {0.03f, 0.035f, 0.06f, 1.0f};
    renderer_->GetContext()->ClearRenderTargetView(rtv_.Get(), clearColor);
    renderer_->GetContext()->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(width_);
    vp.Height = static_cast<float>(height_);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    renderer_->GetContext()->RSSetViewports(1, &vp);

    renderer_->GetContext()->OMSetRenderTargets(1, rtv_.GetAddressOf(), dsv_.Get());

    using namespace DirectX;
    XMVECTOR eyePos = XMVectorSet(0.0f, 0.6f, 29.0f, 1.0f);
    XMVECTOR targetPos = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtRH(eyePos, targetPos, up);
    XMMATRIX proj = XMMatrixPerspectiveFovRH(46.0f * 3.14159265358979323846f / 180.0f,
                                            static_cast<float>(width_) / static_cast<float>(height_), 1.0f, 120.0f);
    XMMATRIX viewProj = view * proj;
    XMMATRIX modelWorld = XMMatrixRotationRollPitchYaw(-0.15f, rotationAngle_, 0.0f);

    // Update Constant Buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(renderer_->GetContext()->Map(cbBrainFrame_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        auto* cb = static_cast<ConstantBufferPerBrainFrame*>(mapped.pData);
        cb->ViewProj = XMMatrixTranspose(viewProj);
        cb->ModelWorld = XMMatrixTranspose(modelWorld);
        cb->FlashParams = XMFLOAT4(0, 0, 0, 0);
        renderer_->GetContext()->Unmap(cbBrainFrame_.Get(), 0);
    }

    UINT stride = sizeof(BrainPointVertex);
    UINT offset = 0;

    renderer_->GetContext()->IASetInputLayout(inputLayout_.Get());
    renderer_->GetContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

    renderer_->GetContext()->VSSetShader(brainVS_.Get(), nullptr, 0);
    renderer_->GetContext()->VSSetConstantBuffers(0, 1, cbBrainFrame_.GetAddressOf());

    renderer_->GetContext()->GSSetShader(brainGS_.Get(), nullptr, 0);
    renderer_->GetContext()->GSSetConstantBuffers(0, 1, cbBrainFrame_.GetAddressOf());

    renderer_->GetContext()->PSSetShader(brainPS_.Get(), nullptr, 0);

    // Draw Somas
    renderer_->GetContext()->IASetVertexBuffers(0, 1, somaVB_.GetAddressOf(), &stride, &offset);
    renderer_->GetContext()->Draw(somaCount_, 0);

    // Draw Circuit Neurons
    renderer_->GetContext()->IASetVertexBuffers(0, 1, circuitVB_.GetAddressOf(), &stride, &offset);
    renderer_->GetContext()->Draw(circuitCount_, 0);

    // Draw Flashes
    std::vector<BrainPointVertex> activeFlashes;
    for (const auto& f : flashPool_) {
        if (f.active) {
            XMFLOAT4 col = f.isGF ? XMFLOAT4(1.0f, 0.95f, 0.3f, f.opacity)
                                  : XMFLOAT4(0.75f, 0.95f, 1.0f, f.opacity);
            float sz = f.isGF ? 0.25f : 0.12f;
            activeFlashes.push_back({f.position, col, sz});
        }
    }

    if (!activeFlashes.empty()) {
        if (SUCCEEDED(renderer_->GetContext()->Map(flashVB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            memcpy(mapped.pData, activeFlashes.data(), activeFlashes.size() * sizeof(BrainPointVertex));
            renderer_->GetContext()->Unmap(flashVB_.Get(), 0);
        }
        renderer_->GetContext()->IASetVertexBuffers(0, 1, flashVB_.GetAddressOf(), &stride, &offset);
        renderer_->GetContext()->Draw(static_cast<UINT>(activeFlashes.size()), 0);
    }

    ID3D11GeometryShader* nullGS = nullptr;
    renderer_->GetContext()->GSSetShader(nullGS, nullptr, 0);

    swapChain_->Present(1, 0);
}

void BrainView::HandleClick(int mouseX, int mouseY) {
    if (!sim_) return;

    using namespace DirectX;
    float ndcX = (2.0f * static_cast<float>(mouseX)) / static_cast<float>(width_) - 1.0f;
    float ndcY = 1.0f - (2.0f * static_cast<float>(mouseY)) / static_cast<float>(height_);

    XMVECTOR eyePos = XMVectorSet(0.0f, 0.6f, 29.0f, 1.0f);
    XMVECTOR targetPos = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtRH(eyePos, targetPos, up);
    XMMATRIX proj = XMMatrixPerspectiveFovRH(46.0f * 3.14159265358979323846f / 180.0f,
                                            static_cast<float>(width_) / static_cast<float>(height_), 1.0f, 120.0f);
    XMMATRIX viewProj = view * proj;
    XMMATRIX invVP = XMMatrixInverse(nullptr, viewProj);

    XMVECTOR nearP = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invVP);
    XMVECTOR farP = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invVP);

    XMMATRIX modelWorld = XMMatrixRotationRollPitchYaw(-0.15f, rotationAngle_, 0.0f);
    XMMATRIX invModel = XMMatrixInverse(nullptr, modelWorld);

    XMVECTOR a = XMVector3TransformCoord(nearP, invModel);
    XMVECTOR b = XMVector3TransformCoord(farP, invModel);
    XMVECTOR d = XMVector3Normalize(b - a);

    int best = -1;
    float bestPerp = 1e9f;

    for (int i = 0; i < sim_->n; ++i) {
        XMVECTOR pi = XMVectorSet(sim_->positions[i].x, sim_->positions[i].y, sim_->positions[i].z, 1.0f);
        XMVECTOR ap = pi - a;
        float dotV = XMVectorGetX(XMVector3Dot(ap, d));
        XMVECTOR perpV = ap - d * dotV;
        float perp = XMVectorGetX(XMVector3Length(perpV));
        if (perp < bestPerp) {
            bestPerp = perp;
            best = i;
        }
    }

    if (best < 0) return;

    auto anchor = sim_->positions[best];
    XMVECTOR anchorV = XMVectorSet(anchor.x, anchor.y, anchor.z, 1.0f);

    std::vector<int> picked;
    for (int i = 0; i < sim_->n; ++i) {
        XMVECTOR pi = XMVectorSet(sim_->positions[i].x, sim_->positions[i].y, sim_->positions[i].z, 1.0f);
        float dist = XMVectorGetX(XMVector3Length(pi - anchorV));
        if (dist < 2.2f) {
            picked.push_back(i);
        }
    }

    if (picked.size() < 4) {
        std::vector<std::pair<float, int>> dists;
        for (int i = 0; i < sim_->n; ++i) {
            XMVECTOR pi = XMVectorSet(sim_->positions[i].x, sim_->positions[i].y, sim_->positions[i].z, 1.0f);
            dists.push_back({XMVectorGetX(XMVector3Length(pi - anchorV)), i});
        }
        std::sort(dists.begin(), dists.end());
        picked.clear();
        for (size_t i = 0; i < std::min<size_t>(6, dists.size()); ++i) {
            picked.push_back(dists[i].second);
        }
    } else if (picked.size() > 60) {
        picked.resize(60);
    }

    sim_->stimulate(picked, 0.25f, 400);

    for (size_t i = 0; i < std::min<size_t>(16, picked.size()); ++i) {
        auto& f = flashPool_[flashNext_];
        flashNext_ = (flashNext_ + 1) % static_cast<int>(flashPool_.size());
        f.position = sim_->positions[picked[i]];
        f.isGF = false;
        f.maxDuration = 0.28f;
        f.timer = f.maxDuration;
        f.opacity = 0.8f;
        f.active = true;
    }

    stimRingPos_ = anchor;
    stimRingScale_ = 0.5f;
    stimRingOpacity_ = 1.0f;

    tooltipText_ = RegionNameFor(picked);
    tooltipTimer_ = 2.2f;

    std::wstring title = L"Fly Brain — " + std::wstring(tooltipText_.begin(), tooltipText_.end());
    SetWindowTextW(hwnd_, title.c_str());
}

std::string BrainView::RegionNameFor(const std::vector<int>& picked) {
    std::unordered_map<std::string, int> counts;
    for (int idx : picked) counts[sim_->roles[idx]]++;

    std::string major = "other";
    int maxC = 0;
    for (const auto& [role, count] : counts) {
        if (count > maxC) {
            maxC = count;
            major = role;
        }
    }

    if (major == "lc4" || major == "lplc2") return "Looming detectors (LC4/LPLC2)";
    if (major == "gf") return "Giant Fiber (DNp01) — escape!";
    if (major == "dna01" || major == "dna02") return "Steering neurons (DNa01/02)";
    if (major == "dnp09") return "Walking command (DNp09)";
    if (major == "dng11") return "Grooming command (DNg11)";
    if (major == "escw") return "Escape-wing DNs (DNp02/04/11)";
    if (major == "mdn") return "Moonwalker neurons (MDN)";
    return "Central neurons";
}

int BrainView::RunBrainshot(const std::string& path) {
    auto dataOpt = loadBrainData();
    if (!dataOpt) {
        std::cerr << "no data/ — run etl.py first\n";
        return 1;
    }

    RendererD3D11 renderer;
    if (!renderer.Initialize(false)) {
        std::cerr << "Failed to initialize D3D11 renderer for brainshot\n";
        return 1;
    }

    constexpr int width = 720;
    constexpr int height = 560;

    auto sim = std::make_shared<LIFSim>(dataOpt->circuit, nullptr);
    BrainView bv;
    bv.Initialize(renderer, GetModuleHandle(nullptr), *dataOpt, sim);

    // Decorate with fake spikes for preview
    for (int i = 0; i < 40; ++i) {
        int nr = i % sim->n;
        auto& f = bv.flashPool_[i];
        f.position = sim->positions[nr];
        f.isGF = false;
        f.opacity = 0.8f;
        f.active = true;
    }

    std::vector<uint8_t> rgbaData;
    bool ok = renderer.RenderOffscreen(width, height, [&](RendererD3D11& rndr) {
        using namespace DirectX;
        XMVECTOR eyePos = XMVectorSet(0.0f, 0.6f, 29.0f, 1.0f);
        XMVECTOR targetPos = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
        XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

        XMMATRIX view = XMMatrixLookAtRH(eyePos, targetPos, up);
        XMMATRIX proj = XMMatrixPerspectiveFovRH(46.0f * 3.14159265358979323846f / 180.0f,
                                                static_cast<float>(width) / static_cast<float>(height), 1.0f, 120.0f);
        XMMATRIX viewProj = view * proj;
        XMMATRIX modelWorld = XMMatrixRotationRollPitchYaw(-0.15f, 0.5f, 0.0f);

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(rndr.GetContext()->Map(bv.cbBrainFrame_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            auto* cb = static_cast<ConstantBufferPerBrainFrame*>(mapped.pData);
            cb->ViewProj = XMMatrixTranspose(viewProj);
            cb->ModelWorld = XMMatrixTranspose(modelWorld);
            cb->FlashParams = XMFLOAT4(0, 0, 0, 0);
            rndr.GetContext()->Unmap(bv.cbBrainFrame_.Get(), 0);
        }

        UINT stride = sizeof(BrainPointVertex);
        UINT offset = 0;

        rndr.GetContext()->IASetInputLayout(bv.inputLayout_.Get());
        rndr.GetContext()->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);

        rndr.GetContext()->VSSetShader(bv.brainVS_.Get(), nullptr, 0);
        rndr.GetContext()->VSSetConstantBuffers(0, 1, bv.cbBrainFrame_.GetAddressOf());

        rndr.GetContext()->GSSetShader(bv.brainGS_.Get(), nullptr, 0);
        rndr.GetContext()->GSSetConstantBuffers(0, 1, bv.cbBrainFrame_.GetAddressOf());

        rndr.GetContext()->PSSetShader(bv.brainPS_.Get(), nullptr, 0);

        rndr.GetContext()->IASetVertexBuffers(0, 1, bv.somaVB_.GetAddressOf(), &stride, &offset);
        rndr.GetContext()->Draw(bv.somaCount_, 0);

        rndr.GetContext()->IASetVertexBuffers(0, 1, bv.circuitVB_.GetAddressOf(), &stride, &offset);
        rndr.GetContext()->Draw(bv.circuitCount_, 0);

        std::vector<BrainPointVertex> activeFlashes;
        for (const auto& f : bv.flashPool_) {
            if (f.active) {
                activeFlashes.push_back({f.position, XMFLOAT4(0.75f, 0.95f, 1.0f, f.opacity), 0.16f});
            }
        }
        if (!activeFlashes.empty()) {
            if (SUCCEEDED(rndr.GetContext()->Map(bv.flashVB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                memcpy(mapped.pData, activeFlashes.data(), activeFlashes.size() * sizeof(BrainPointVertex));
                rndr.GetContext()->Unmap(bv.flashVB_.Get(), 0);
            }
            rndr.GetContext()->IASetVertexBuffers(0, 1, bv.flashVB_.GetAddressOf(), &stride, &offset);
            rndr.GetContext()->Draw(static_cast<UINT>(activeFlashes.size()), 0);
        }

        ID3D11GeometryShader* nullGS = nullptr;
        rndr.GetContext()->GSSetShader(nullGS, nullptr, 0);
    }, rgbaData);

    if (!ok) {
        std::cerr << "Brainshot offscreen rendering failed\n";
        return 1;
    }

    if (stbi_write_png(path.c_str(), width, height, 4, rgbaData.data(), width * 4) == 0) {
        std::cerr << "brainshot: failed to encode PNG to " << path << std::endl;
        return 1;
    }

    std::cout << "brainshot written to " << path << std::endl;
    return 0;
}
