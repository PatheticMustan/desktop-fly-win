#include "RendererD3D11.hpp"
#include <d3dcompiler.h>
#include <iostream>
#include <fstream>
#include <sstream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

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

RendererD3D11::RendererD3D11() = default;
RendererD3D11::~RendererD3D11() = default;

bool RendererD3D11::Initialize(bool forceWarp) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_DRIVER_TYPE driverType = forceWarp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE;
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1
    };
    D3D_FEATURE_LEVEL featureLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr, driverType, nullptr, flags,
        featureLevels, _countof(featureLevels),
        D3D11_SDK_VERSION,
        &device_, &featureLevel, &context_
    );

    if (FAILED(hr) && !forceWarp) {
        // Fallback to WARP
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
            featureLevels, _countof(featureLevels),
            D3D11_SDK_VERSION,
            &device_, &featureLevel, &context_
        );
    }

    if (FAILED(hr)) {
        std::cerr << "Failed to create Direct3D 11 device: hr = 0x" << std::hex << hr << std::endl;
        return false;
    }

    if (!CreateShaders()) return false;
    if (!CreateAbdomenTexture()) return false;
    if (!CreateRenderStates()) return false;

    // Create standard meshes
    sphereMesh = CreateMeshBuffer(Geometry::CreateSphere(1.0f, 20, 14));
    capsuleMesh = CreateMeshBuffer(Geometry::CreateCapsule(1.0f, 2.0f, 16, 6));
    coneMesh = CreateMeshBuffer(Geometry::CreateCone(1.0f, 0.5f, 2.0f, 16));
    wingMesh = CreateMeshBuffer(Geometry::CreateExtrudedWing(5.2f, 16.5f, 0.12f, 32));
    quadMesh = CreateMeshBuffer(Geometry::CreateQuad(1.0f, 1.0f));

    return true;
}

bool RendererD3D11::CreateShaders() {
    std::string vsSrc = ReadShaderFile("FlyVS.hlsl");
    std::string psSrc = ReadShaderFile("FlyPS.hlsl");

    if (vsSrc.empty() || psSrc.empty()) {
        std::cerr << "Failed to locate FlyVS.hlsl or FlyPS.hlsl" << std::endl;
        return false;
    }

    ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
    HRESULT hr = D3DCompile(vsSrc.data(), vsSrc.size(), "FlyVS.hlsl", nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) std::cerr << "VS Compile Error: " << (char*)errBlob->GetBufferPointer() << std::endl;
        return false;
    }

    hr = D3DCompile(psSrc.data(), psSrc.size(), "FlyPS.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &errBlob);
    if (FAILED(hr)) {
        if (errBlob) std::cerr << "PS Compile Error: " << (char*)errBlob->GetBufferPointer() << std::endl;
        return false;
    }

    hr = device_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &flyVS_);
    if (FAILED(hr)) return false;

    hr = device_->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &flyPS_);
    if (FAILED(hr)) return false;

    D3D11_INPUT_ELEMENT_DESC layoutDesc[] = {
        // Vertex data (Slot 0)
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal),   D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(Vertex, texCoord), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},

        // Instance data (Slot 1)
        {"INSTANCE_WORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, static_cast<UINT>(offsetof(InstanceData, World) + 0),  D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE_WORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, static_cast<UINT>(offsetof(InstanceData, World) + 16), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE_WORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, static_cast<UINT>(offsetof(InstanceData, World) + 32), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE_WORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, static_cast<UINT>(offsetof(InstanceData, World) + 48), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE_DIFFUSE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, static_cast<UINT>(offsetof(InstanceData, DiffuseColor)), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE_SPECULAR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, static_cast<UINT>(offsetof(InstanceData, SpecularColor)), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE_SHININESS", 0, DXGI_FORMAT_R32_FLOAT, 1, static_cast<UINT>(offsetof(InstanceData, Shininess)), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE_USE_TEXTURE", 0, DXGI_FORMAT_R32_FLOAT, 1, static_cast<UINT>(offsetof(InstanceData, UseTexture)), D3D11_INPUT_PER_INSTANCE_DATA, 1},
        {"INSTANCE_PAD", 0, DXGI_FORMAT_R32G32_FLOAT, 1, static_cast<UINT>(offsetof(InstanceData, pad)), D3D11_INPUT_PER_INSTANCE_DATA, 1}
    };

    hr = device_->CreateInputLayout(layoutDesc, _countof(layoutDesc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &inputLayout_);
    if (FAILED(hr)) return false;

    // Constant buffers
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.ByteWidth = sizeof(ConstantBufferPerFrame);
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = device_->CreateBuffer(&cbDesc, nullptr, &cbPerFrame_);
    if (FAILED(hr)) return false;

    EnsureInstanceBufferSize(512);

    return true;
}

bool RendererD3D11::CreateAbdomenTexture() {
    constexpr UINT width = 64;
    constexpr UINT height = 128;
    std::vector<uint32_t> pixels(width * height);

    uint32_t baseCol = 0xFF528CB8; // B8 8C 52 FF (RGBA: 0.72, 0.55, 0.32, 1) -> 0xAABBGGRR
    uint32_t darkCol = 0xFF172638; // 38 26 17 FF (RGBA: 0.22, 0.15, 0.09, 1)

    for (UINT y = 0; y < height; ++y) {
        bool isDark = (y <= 25) || (y >= 38 && y <= 47) || (y >= 60 && y <= 69) || (y >= 82 && y <= 90);
        uint32_t col = isDark ? darkCol : baseCol;
        for (UINT x = 0; x < width; ++x) {
            pixels[y * width + x] = col;
        }
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subData = {};
    subData.pSysMem = pixels.data();
    subData.SysMemPitch = width * sizeof(uint32_t);

    HRESULT hr = device_->CreateTexture2D(&texDesc, &subData, &abdomenTexture_);
    if (FAILED(hr)) return false;

    hr = device_->CreateShaderResourceView(abdomenTexture_.Get(), nullptr, &abdomenSRV_);
    if (FAILED(hr)) return false;

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    hr = device_->CreateSamplerState(&sampDesc, &linearSampler_);
    return SUCCEEDED(hr);
}

bool RendererD3D11::CreateRenderStates() {
    // Blend states
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    HRESULT hr = device_->CreateBlendState(&blendDesc, &blendOpaque_);
    if (FAILED(hr)) return false;

    // Premultiplied alpha blend state (for DirectComposition)
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    hr = device_->CreateBlendState(&blendDesc, &blendAlpha_);
    if (FAILED(hr)) return false;

    // Additive blend state (for brain point flashes)
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    hr = device_->CreateBlendState(&blendDesc, &blendAdditive_);
    if (FAILED(hr)) return false;

    // Depth Stencil states
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    hr = device_->CreateDepthStencilState(&dsDesc, &depthStateReadWrite_);
    if (FAILED(hr)) return false;

    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    hr = device_->CreateDepthStencilState(&dsDesc, &depthStateReadOnly_);
    if (FAILED(hr)) return false;

    dsDesc.DepthEnable = FALSE;
    hr = device_->CreateDepthStencilState(&dsDesc, &depthStateDisabled_);
    if (FAILED(hr)) return false;

    // Rasterizer states
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_BACK;
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.DepthClipEnable = TRUE;
    hr = device_->CreateRasterizerState(&rastDesc, &rasterCullBack_);
    if (FAILED(hr)) return false;

    rastDesc.CullMode = D3D11_CULL_NONE;
    hr = device_->CreateRasterizerState(&rastDesc, &rasterCullNone_);
    return SUCCEEDED(hr);
}

MeshBuffer RendererD3D11::CreateMeshBuffer(const MeshData& mesh) {
    MeshBuffer buffer;
    buffer.indexCount = static_cast<UINT>(mesh.indices.size());

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * mesh.vertices.size());
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA vbData = {};
    vbData.pSysMem = mesh.vertices.data();
    device_->CreateBuffer(&vbDesc, &vbData, &buffer.vertexBuffer);

    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.ByteWidth = static_cast<UINT>(sizeof(uint32_t) * mesh.indices.size());
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA ibData = {};
    ibData.pSysMem = mesh.indices.data();
    device_->CreateBuffer(&ibDesc, &ibData, &buffer.indexBuffer);

    return buffer;
}

void RendererD3D11::SetFrameConstants(const DirectX::XMMATRIX& viewProj,
                                      const DirectX::XMFLOAT3& lightDir,
                                      const DirectX::XMFLOAT4& lightColor,
                                      const DirectX::XMFLOAT4& ambientColor,
                                      const DirectX::XMFLOAT3& eyePos) {
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context_->Map(cbPerFrame_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        auto* cb = static_cast<ConstantBufferPerFrame*>(mapped.pData);
        cb->ViewProj = DirectX::XMMatrixTranspose(viewProj);
        cb->LightDir = lightDir;
        cb->LightColor = lightColor;
        cb->AmbientColor = ambientColor;
        cb->EyePos = eyePos;
        context_->Unmap(cbPerFrame_.Get(), 0);
    }
}

bool RendererD3D11::EnsureInstanceBufferSize(size_t requiredCount) {
    if (instanceBuffer_ && instanceBufferCapacity_ >= requiredCount) {
        return true;
    }
    size_t newCap = std::max<size_t>(requiredCount, instanceBufferCapacity_ == 0 ? 512 : instanceBufferCapacity_ * 2);
    D3D11_BUFFER_DESC desc = {};
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.ByteWidth = static_cast<UINT>(sizeof(InstanceData) * newCap);
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    ComPtr<ID3D11Buffer> newBuf;
    HRESULT hr = device_->CreateBuffer(&desc, nullptr, &newBuf);
    if (FAILED(hr)) return false;

    instanceBuffer_ = newBuf;
    instanceBufferCapacity_ = newCap;
    return true;
}

void RendererD3D11::SetObjectConstants(const DirectX::XMMATRIX& world,
                                       const DirectX::XMFLOAT4& diffuseColor,
                                       const DirectX::XMFLOAT4& specularColor,
                                       float shininess,
                                       bool useTexture) {
    DirectX::XMStoreFloat4x4(&singleInstanceObj_.World, world);
    singleInstanceObj_.DiffuseColor = diffuseColor;
    singleInstanceObj_.SpecularColor = specularColor;
    singleInstanceObj_.Shininess = shininess;
    singleInstanceObj_.UseTexture = useTexture ? 1.0f : 0.0f;
    singleInstanceObj_.pad = DirectX::XMFLOAT2(0, 0);
}

void RendererD3D11::DrawMesh(const MeshBuffer& buffer, bool doubleSided, bool alphaBlend) {
    DrawMeshInstanced(buffer, &singleInstanceObj_, 1, doubleSided, alphaBlend, singleInstanceObj_.UseTexture > 0.5f);
}

void RendererD3D11::DrawMeshInstanced(const MeshBuffer& buffer, const InstanceData* instances, size_t count,
                                      bool doubleSided, bool alphaBlend, bool useTexture) {
    if (count == 0 || !instances) return;
    if (!EnsureInstanceBufferSize(count)) return;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(context_->Map(instanceBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, instances, sizeof(InstanceData) * count);
        context_->Unmap(instanceBuffer_.Get(), 0);
    } else {
        return;
    }

    UINT strides[2] = { sizeof(Vertex), sizeof(InstanceData) };
    UINT offsets[2] = { 0, 0 };
    ID3D11Buffer* vbs[2] = { buffer.vertexBuffer.Get(), instanceBuffer_.Get() };

    context_->IASetInputLayout(inputLayout_.Get());
    context_->IASetVertexBuffers(0, 2, vbs, strides, offsets);
    context_->IASetIndexBuffer(buffer.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    context_->VSSetShader(flyVS_.Get(), nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, cbPerFrame_.GetAddressOf());

    context_->PSSetShader(flyPS_.Get(), nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, cbPerFrame_.GetAddressOf());

    if (useTexture) {
        context_->PSSetShaderResources(0, 1, abdomenSRV_.GetAddressOf());
        context_->PSSetSamplers(0, 1, linearSampler_.GetAddressOf());
    }

    context_->RSSetState(doubleSided ? rasterCullNone_.Get() : rasterCullBack_.Get());
    context_->OMSetBlendState(alphaBlend ? blendAlpha_.Get() : blendOpaque_.Get(), nullptr, 0xFFFFFFFF);
    context_->OMSetDepthStencilState(depthStateReadWrite_.Get(), 0);

    context_->DrawIndexedInstanced(buffer.indexCount, static_cast<UINT>(count), 0, 0, 0);

    if (useTexture) {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        context_->PSSetShaderResources(0, 1, &nullSRV);
    }
}

void RendererD3D11::DrawAbdomen(const MeshBuffer& buffer, const DirectX::XMMATRIX& world) {
    SetObjectConstants(world, DirectX::XMFLOAT4(1, 1, 1, 1), DirectX::XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f), 0.35f, true);
    DrawMesh(buffer, false, false);
}

bool RendererD3D11::RenderOffscreen(int width, int height,
                                    const std::function<void(RendererD3D11&)>& renderFunc,
                                    std::vector<uint8_t>& outRgba) {
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

    ComPtr<ID3D11Texture2D> renderTex;
    HRESULT hr = device_->CreateTexture2D(&texDesc, nullptr, &renderTex);
    if (FAILED(hr)) return false;

    ComPtr<ID3D11RenderTargetView> rtv;
    hr = device_->CreateRenderTargetView(renderTex.Get(), nullptr, &rtv);
    if (FAILED(hr)) return false;

    // Depth buffer
    texDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depthTex;
    hr = device_->CreateTexture2D(&texDesc, nullptr, &depthTex);
    if (FAILED(hr)) return false;

    ComPtr<ID3D11DepthStencilView> dsv;
    hr = device_->CreateDepthStencilView(depthTex.Get(), nullptr, &dsv);
    if (FAILED(hr)) return false;

    // Set viewport
    D3D11_VIEWPORT vp = {};
    vp.Width = static_cast<float>(width);
    vp.Height = static_cast<float>(height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    context_->RSSetViewports(1, &vp);

    // Clear
    float clearColor[4] = {0.94f, 0.94f, 0.94f, 1.0f};
    context_->ClearRenderTargetView(rtv.Get(), clearColor);
    context_->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    context_->OMSetRenderTargets(1, rtv.GetAddressOf(), dsv.Get());

    // Render
    renderFunc(*this);

    // Readback
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.BindFlags = 0;
    texDesc.Usage = D3D11_USAGE_STAGING;
    texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Texture2D> stagingTex;
    hr = device_->CreateTexture2D(&texDesc, nullptr, &stagingTex);
    if (FAILED(hr)) return false;

    context_->CopyResource(stagingTex.Get(), renderTex.Get());

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context_->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    outRgba.resize(width * height * 4);
    const auto* src = static_cast<const uint8_t*>(mapped.pData);
    for (int y = 0; y < height; ++y) {
        memcpy(&outRgba[y * width * 4], src + y * mapped.RowPitch, width * 4);
    }
    context_->Unmap(stagingTex.Get(), 0);

    return true;
}
