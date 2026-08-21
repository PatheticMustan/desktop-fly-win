#pragma once

#include <vector>
#include <cstdint>
#include <DirectXMath.h>

struct Vertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 texCoord;
    DirectX::XMFLOAT4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

class Geometry {
public:
    static MeshData CreateSphere(float radius, int slices = 20, int stacks = 14);
    static MeshData CreateCapsule(float capRadius, float height, int slices = 16, int capStacks = 6);
    static MeshData CreateCone(float topRadius, float bottomRadius, float height, int slices = 16);
    static MeshData CreateExtrudedWing(float width = 5.2f, float height = 16.5f, float depth = 0.12f, int segments = 32);
    static MeshData CreateQuad(float width, float height);
};
