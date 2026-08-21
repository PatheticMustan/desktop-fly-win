#include "Geometry.hpp"
#include <cmath>

constexpr float PI = 3.14159265358979323846f;

MeshData Geometry::CreateSphere(float radius, int slices, int stacks) {
    MeshData mesh;
    mesh.vertices.reserve((stacks + 1) * (slices + 1));

    for (int i = 0; i <= stacks; ++i) {
        float v = static_cast<float>(i) / static_cast<float>(stacks);
        float phi = v * PI; // 0 to PI
        float y = radius * std::cos(phi);
        float r = radius * std::sin(phi);

        for (int j = 0; j <= slices; ++j) {
            float u = static_cast<float>(j) / static_cast<float>(slices);
            float theta = u * 2.0f * PI; // 0 to 2PI

            float x = r * std::sin(theta);
            float z = r * std::cos(theta);

            Vertex vert;
            vert.position = DirectX::XMFLOAT3(x, y, z);
            vert.normal = DirectX::XMFLOAT3(x / radius, y / radius, z / radius);
            vert.texCoord = DirectX::XMFLOAT2(u, v);
            vert.color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            mesh.vertices.push_back(vert);
        }
    }

    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            uint32_t first = static_cast<uint32_t>(i * (slices + 1) + j);
            uint32_t second = first + static_cast<uint32_t>(slices + 1);

            mesh.indices.push_back(first);
            mesh.indices.push_back(second);
            mesh.indices.push_back(first + 1);

            mesh.indices.push_back(second);
            mesh.indices.push_back(second + 1);
            mesh.indices.push_back(first + 1);
        }
    }

    return mesh;
}

MeshData Geometry::CreateCapsule(float capRadius, float height, int slices, int capStacks) {
    MeshData mesh;
    float cylHalfH = std::max(0.0f, (height - 2.0f * capRadius) * 0.5f);
    int totalRings = capStacks * 2 + 2;
    mesh.vertices.reserve(totalRings * (slices + 1));

    for (int i = 0; i <= capStacks; ++i) {
        float v = static_cast<float>(i) / static_cast<float>(capStacks);
        float phi = v * (PI * 0.5f); // 0 to PI/2
        float y = cylHalfH + capRadius * std::cos(phi);
        float r = capRadius * std::sin(phi);

        for (int j = 0; j <= slices; ++j) {
            float u = static_cast<float>(j) / static_cast<float>(slices);
            float theta = u * 2.0f * PI;
            float x = r * std::sin(theta);
            float z = r * std::cos(theta);

            Vertex vert;
            vert.position = DirectX::XMFLOAT3(x, y, z);
            vert.normal = DirectX::XMFLOAT3(x / capRadius, std::cos(phi), z / capRadius);
            vert.texCoord = DirectX::XMFLOAT2(u, v * 0.25f);
            vert.color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            mesh.vertices.push_back(vert);
        }
    }

    for (int i = 0; i <= capStacks; ++i) {
        float v = static_cast<float>(i) / static_cast<float>(capStacks);
        float phi = (PI * 0.5f) + v * (PI * 0.5f); // PI/2 to PI
        float y = -cylHalfH + capRadius * std::cos(phi);
        float r = capRadius * std::sin(phi);

        for (int j = 0; j <= slices; ++j) {
            float u = static_cast<float>(j) / static_cast<float>(slices);
            float theta = u * 2.0f * PI;
            float x = r * std::sin(theta);
            float z = r * std::cos(theta);

            Vertex vert;
            vert.position = DirectX::XMFLOAT3(x, y, z);
            vert.normal = DirectX::XMFLOAT3(x / capRadius, std::cos(phi), z / capRadius);
            vert.texCoord = DirectX::XMFLOAT2(u, 0.75f + v * 0.25f);
            vert.color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            mesh.vertices.push_back(vert);
        }
    }

    int totalStacks = capStacks * 2 + 1;
    for (int i = 0; i < totalStacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            uint32_t first = static_cast<uint32_t>(i * (slices + 1) + j);
            uint32_t second = first + static_cast<uint32_t>(slices + 1);

            mesh.indices.push_back(first);
            mesh.indices.push_back(second);
            mesh.indices.push_back(first + 1);

            mesh.indices.push_back(second);
            mesh.indices.push_back(second + 1);
            mesh.indices.push_back(first + 1);
        }
    }

    return mesh;
}

MeshData Geometry::CreateCone(float topRadius, float bottomRadius, float height, int slices) {
    MeshData mesh;
    float halfH = height * 0.5f;

    for (int i = 0; i <= 1; ++i) {
        float y = (i == 0) ? halfH : -halfH;
        float r = (i == 0) ? topRadius : bottomRadius;
        float v = static_cast<float>(i);

        for (int j = 0; j <= slices; ++j) {
            float u = static_cast<float>(j) / static_cast<float>(slices);
            float theta = u * 2.0f * PI;
            float x = r * std::sin(theta);
            float z = r * std::cos(theta);

            Vertex vert;
            vert.position = DirectX::XMFLOAT3(x, y, z);
            vert.normal = DirectX::XMFLOAT3(std::sin(theta), 0.0f, std::cos(theta));
            vert.texCoord = DirectX::XMFLOAT2(u, v);
            vert.color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            mesh.vertices.push_back(vert);
        }
    }

    for (int j = 0; j < slices; ++j) {
        uint32_t first = static_cast<uint32_t>(j);
        uint32_t second = first + static_cast<uint32_t>(slices + 1);

        mesh.indices.push_back(first);
        mesh.indices.push_back(second);
        mesh.indices.push_back(first + 1);

        mesh.indices.push_back(second);
        mesh.indices.push_back(second + 1);
        mesh.indices.push_back(first + 1);
    }

    return mesh;
}

MeshData Geometry::CreateExtrudedWing(float width, float height, float depth, int segments) {
    MeshData mesh;
    float rx = width * 0.5f;
    float ry = height * 0.5f;
    float cy = -ry; // Center along Y is at -ry so wing originates at (0, 0, 0)
    float halfD = depth * 0.5f;

    // Top face (Z = +halfD)
    uint32_t topCenterIdx = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({DirectX::XMFLOAT3(0, cy, halfD), DirectX::XMFLOAT3(0, 0, 1), DirectX::XMFLOAT2(0.5f, 0.5f), DirectX::XMFLOAT4(1, 1, 1, 1)});

    for (int i = 0; i <= segments; ++i) {
        float theta = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * PI;
        float x = rx * std::sin(theta);
        float y = cy + ry * std::cos(theta);
        mesh.vertices.push_back({
            DirectX::XMFLOAT3(x, y, halfD),
            DirectX::XMFLOAT3(0, 0, 1),
            DirectX::XMFLOAT2(0.5f + 0.5f * std::sin(theta), 0.5f + 0.5f * std::cos(theta)),
            DirectX::XMFLOAT4(1, 1, 1, 1)
        });
    }

    for (int i = 0; i < segments; ++i) {
        mesh.indices.push_back(topCenterIdx);
        mesh.indices.push_back(topCenterIdx + 1 + i);
        mesh.indices.push_back(topCenterIdx + 1 + i + 1);
    }

    // Bottom face (Z = -halfD)
    uint32_t botCenterIdx = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({DirectX::XMFLOAT3(0, cy, -halfD), DirectX::XMFLOAT3(0, 0, -1), DirectX::XMFLOAT2(0.5f, 0.5f), DirectX::XMFLOAT4(1, 1, 1, 1)});

    for (int i = 0; i <= segments; ++i) {
        float theta = static_cast<float>(i) / static_cast<float>(segments) * 2.0f * PI;
        float x = rx * std::sin(theta);
        float y = cy + ry * std::cos(theta);
        mesh.vertices.push_back({
            DirectX::XMFLOAT3(x, y, -halfD),
            DirectX::XMFLOAT3(0, 0, -1),
            DirectX::XMFLOAT2(0.5f + 0.5f * std::sin(theta), 0.5f + 0.5f * std::cos(theta)),
            DirectX::XMFLOAT4(1, 1, 1, 1)
        });
    }

    for (int i = 0; i < segments; ++i) {
        mesh.indices.push_back(botCenterIdx);
        mesh.indices.push_back(botCenterIdx + 1 + i + 1);
        mesh.indices.push_back(botCenterIdx + 1 + i);
    }

    // Side rim
    for (int i = 0; i < segments; ++i) {
        uint32_t t1 = topCenterIdx + 1 + i;
        uint32_t t2 = topCenterIdx + 1 + i + 1;
        uint32_t b1 = botCenterIdx + 1 + i;
        uint32_t b2 = botCenterIdx + 1 + i + 1;

        mesh.indices.push_back(t1);
        mesh.indices.push_back(b1);
        mesh.indices.push_back(t2);

        mesh.indices.push_back(t2);
        mesh.indices.push_back(b1);
        mesh.indices.push_back(b2);
    }

    return mesh;
}

MeshData Geometry::CreateQuad(float width, float height) {
    MeshData mesh;
    float hw = width * 0.5f;
    float hh = height * 0.5f;

    mesh.vertices.push_back({DirectX::XMFLOAT3(-hw, -hh, 0), DirectX::XMFLOAT3(0, 0, 1), DirectX::XMFLOAT2(0, 1), DirectX::XMFLOAT4(1, 1, 1, 1)});
    mesh.vertices.push_back({DirectX::XMFLOAT3(-hw,  hh, 0), DirectX::XMFLOAT3(0, 0, 1), DirectX::XMFLOAT2(0, 0), DirectX::XMFLOAT4(1, 1, 1, 1)});
    mesh.vertices.push_back({DirectX::XMFLOAT3( hw,  hh, 0), DirectX::XMFLOAT3(0, 0, 1), DirectX::XMFLOAT2(1, 0), DirectX::XMFLOAT4(1, 1, 1, 1)});
    mesh.vertices.push_back({DirectX::XMFLOAT3( hw, -hh, 0), DirectX::XMFLOAT3(0, 0, 1), DirectX::XMFLOAT2(1, 1), DirectX::XMFLOAT4(1, 1, 1, 1)});

    mesh.indices.push_back(0);
    mesh.indices.push_back(1);
    mesh.indices.push_back(2);
    mesh.indices.push_back(0);
    mesh.indices.push_back(2);
    mesh.indices.push_back(3);

    return mesh;
}
