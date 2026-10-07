#pragma once

#include "Contracts/Transform.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using ObjectId = std::uint64_t;
inline constexpr ObjectId InvalidObjectId = 0;

// Called synchronously on the editor thread. IDs are stable and never reused
// during the provider's lifetime; roots have parent 0. Hierarchies are acyclic.
// Name/transform/parent access requires a live ID; invalid IDs throw out_of_range.
// Transforms are local to the parent. Delete removes the object and its subtree.
// A live object may have no Transform (nullopt). SetTransform edits an existing
// Transform; if it is absent, throw logic_error instead of creating one implicitly.
class ISceneProvider
{
public:
    virtual ~ISceneProvider() = default;
    virtual std::vector<ObjectId> GetRootObjects() const = 0;
    virtual ObjectId GetParent(ObjectId id) const = 0;
    virtual std::vector<ObjectId> GetChildren(ObjectId id) const = 0;
    virtual std::string GetName(ObjectId id) const = 0;
    virtual void SetName(ObjectId id, const std::string& name) = 0;
    virtual std::optional<Transform> GetTransform(ObjectId id) const = 0;
    virtual void SetTransform(ObjectId id, const Transform& transform) = 0;
    virtual ObjectId CreateObject(const std::string& name, ObjectId parent = InvalidObjectId) = 0;
    virtual void DeleteObject(ObjectId id) = 0; // Deleting an absent ID is a no-op.
};
