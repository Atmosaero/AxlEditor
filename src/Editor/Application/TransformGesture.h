#pragma once
#include "Contracts/Transform.h"
#include <stdexcept>

enum class TransformTool { Move, Rotate, Scale };

// Qt converts a mouse gesture to a parent-local offset or scalar amount.
// This computes the edit only; no provider, widget or renderer is accessed.
inline Transform ApplyTransformGesture(const Transform& start, Transform current,
    TransformTool tool, int axis, float amount, Vec3 localOffset, bool twoD)
{
    if (tool == TransformTool::Move) {
        for (int i = 0; i < 3; ++i) current.position[i] = start.position[i] + localOffset[i];
    } else if (tool == TransformTool::Rotate) {
        if (axis < 0 || axis > 2) throw std::invalid_argument("Invalid rotation axis");
        current.rotation[axis] = start.rotation[axis] + amount;
    } else if (tool == TransformTool::Scale) {
        if (axis < 0 || axis > 3) throw std::invalid_argument("Invalid scale axis");
        if (axis < 3) current.scale[axis] = start.scale[axis] + amount;
        else for (int i = 0; i < (twoD ? 2 : 3); ++i) current.scale[i] = start.scale[i] * (1 + amount);
    }
    return current;
}
