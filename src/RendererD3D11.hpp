#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "Geometry.hpp"
#include "Sim.hpp"

using Microsoft::WRL::ComPtr;

struct ConstantBufferPerFrame {
    DirectX::XMMATRIX ViewProj;
    DirectX::XMFLOAT3 LightDir;
    float pad0;
    DirectX::XMFLOAT4 LightColor;
    DirectX::XMFLOAT4 AmbientColor;
    DirectX::XMFLOAT3 EyePos;
    float pad1;
};

struct ConstantBufferPerObject {
    DirectX::XMMATRIX World;
    DirectX::XMFLOAT4 DiffuseColor;
    DirectX::XMFLOAT4 SpecularColor;
    float Shininess;
    float UseTexture;
    DirectX::XMFLOAT2 pad2;
};

struct MeshBuffer {
    ComPtr<ID3D11Buffer> vertexBuffer;
    ComPtr<ID3D11Buffer> indexBuffer;
    UINT indexCount = 0;
};

class RendererD3D11 {
public:
    RendererD3D11();
    ~RendererD3D11();

    bool Initialize(bool forceWarp = false);

    ComPtr<ID3D11Device> GetDevice() const { return device_; }
    ComPtr<ID3D11DeviceContext> GetContext() const { return context_; }

    MeshBuffer CreateMeshBuffer(const MeshData& mesh);

    void SetFrameConstants(const DirectX::XMMATRIX& viewProj,
                           const DirectX::XMFLOAT3& lightDir,
                           const DirectX::XMFLOAT4& lightColor,
                           const DirectX::XMFLOAT4& ambientColor,
                           const DirectX::XMFLOAT3& eyePos);

    void SetObjectConstants(const DirectX::XMMATRIX& world,
                            const DirectX::XMFLOAT4& diffuseColor,
                            const DirectX::XMFLOAT4& specularColor,
                            float shininess,
                            bool useTexture = false);

    void DrawMesh(const MeshBuffer& buffer, bool doubleSided = false, bool alphaBlend = false);
    void DrawAbdomen(const MeshBuffer& buffer, const DirectX::XMMATRIX& world);

    // Offscreen render & snapshot
    bool RenderOffscreen(int width, int height,
                         const std::function<void(RendererD3D11&)>& renderFunc,
                         std::vector<uint8_t>& outRgba);

    // Standard cached primitive mesh buffers
    MeshBuffer sphereMesh;
    MeshBuffer capsuleMesh;
    MeshBuffer coneMesh;
    MeshBuffer wingMesh;
    MeshBuffer quadMesh;

private:
    bool CreateShaders();
    bool CreateAbdomenTexture();
    bool CreateRenderStates();

    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;

    ComPtr<ID3D11VertexShader> flyVS_;
    ComPtr<ID3D11PixelShader> flyPS_;
    ComPtr<ID3D11InputLayout> inputLayout_;

    ComPtr<ID3D11Buffer> cbPerFrame_;
    ComPtr<ID3D11Buffer> cbPerObject_;

    ComPtr<ID3D11Texture2D> abdomenTexture_;
    ComPtr<ID3D11ShaderResourceView> abdomenSRV_;
    ComPtr<ID3D11SamplerState> linearSampler_;

    ComPtr<ID3D11BlendState> blendOpaque_;
    ComPtr<ID3D11BlendState> blendAlpha_;
    ComPtr<ID3D11BlendState> blendAdditive_;

    ComPtr<ID3D11DepthStencilState> depthStateReadWrite_;
    ComPtr<ID3D11DepthStencilState> depthStateReadOnly_;
    ComPtr<ID3D11DepthStencilState> depthStateDisabled_;

    ComPtr<ID3D11RasterizerState> rasterCullBack_;
    ComPtr<ID3D11RasterizerState> rasterCullNone_;
};
