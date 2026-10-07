#pragma once

// Editor-facing data; no dependency on Qt or an engine math library.
struct Vec3
{
    float x = 0, y = 0, z = 0;

    float& operator[](int axis) { return axis == 0 ? x : axis == 1 ? y : z; }
    float operator[](int axis) const { return axis == 0 ? x : axis == 1 ? y : z; }
    bool operator==(const Vec3& other) const { return x == other.x && y == other.y && z == other.z; }
};

// Shared editor data; no dependency on Entity or a particular scene/runtime.
struct Transform
{
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f}; // Degrees, local X then Y then Z.
    Vec3 scale{1.0f, 1.0f, 1.0f};
};
