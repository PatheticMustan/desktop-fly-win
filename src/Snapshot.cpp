#include "FlyModel.hpp"
#include "RendererD3D11.hpp"
#include <iostream>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../external/stb_image_write.h"

int runSnapshot(const std::string& path) {
    RendererD3D11 renderer;
    if (!renderer.Initialize(false)) {
        std::cerr << "Failed to initialize D3D11 renderer for snapshot\n";
        return 1;
    }

    constexpr int width = 720;
    constexpr int height = 720;

    Fly fly(Point2D{0, 0});
    fly.heading = 3.14159265358979323846f * 0.5f;

    float legAngles[6] = {0.25f, -0.2f, -0.22f, 0.28f, 0.2f, -0.25f};
    float legLifts[6]  = {0.35f,  0.0f,  0.0f,  0.30f, 0.0f,  0.35f};
    for (size_t i = 0; i < fly.model.legs.size() && i < 6; ++i) {
        fly.model.legs[i].angle = legAngles[i];
        fly.model.legs[i].lift = legLifts[i];
    }

    std::vector<uint8_t> rgbaData;
    bool ok = renderer.RenderOffscreen(width, height, [&](RendererD3D11& rndr) {
        using namespace DirectX;

        XMVECTOR eyePos = XMVectorSet(30.0f, -58.0f, 42.0f, 1.0f);
        XMVECTOR targetPos = XMVectorSet(0.0f, 0.0f, 5.0f, 1.0f);
        XMVECTOR up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

        XMMATRIX view = XMMatrixLookAtRH(eyePos, targetPos, up);
        XMMATRIX proj = XMMatrixPerspectiveFovRH(42.0f * 3.14159265358979323846f / 180.0f, 1.0f, 1.0f, 600.0f);
        XMMATRIX viewProj = view * proj;

        XMFLOAT3 lightDir(-0.5f, 0.8f, 0.6f);
        XMFLOAT4 lightColor(1.1f, 1.1f, 1.1f, 1.0f);
        XMFLOAT4 ambientColor(0.5f, 0.5f, 0.5f, 1.0f);
        XMFLOAT3 eyePosF3(30.0f, -58.0f, 42.0f);

        rndr.SetFrameConstants(viewProj, lightDir, lightColor, ambientColor, eyePosF3);
        fly.Render(rndr);
    }, rgbaData);

    if (!ok) {
        std::cerr << "Snapshot offscreen rendering failed\n";
        return 1;
    }

    if (stbi_write_png(path.c_str(), width, height, 4, rgbaData.data(), width * 4) == 0) {
        std::cerr << "snapshot: failed to encode PNG to " << path << std::endl;
        return 1;
    }

    std::cout << "snapshot written to " << path << std::endl;
    return 0;
}
