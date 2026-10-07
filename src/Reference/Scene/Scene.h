#pragma once

#include "Contracts/Transform.h"
#include <string>

// Reference-scene data only; it has no runtime behavior or mandatory base class.
struct Comment
{
    std::string text;
};

struct Entity
{
    std::string name;
    Transform transform;
};
