#pragma once

#include "Contracts/ISceneProvider.h"

using PropertyGroupId = std::uint64_t;

struct PropertyGroupInfo
{
    PropertyGroupId id;
    std::string type;
    std::string displayName;
};

// Optional editor-facing service. No component base class or field descriptors.
// Called on the editor thread with live ObjectIds. Missing objects throw out_of_range.
// Group IDs are stable within a document. Creation does not recycle deleted IDs;
// Undo may restore them. Callbacks crossing document replacement need a session guard.
// A registered section edits its backend directly; this API only manages groups.
class IObjectProperties
{
public:
    virtual ~IObjectProperties() = default;
    virtual std::vector<PropertyGroupInfo> GetPropertyGroups(ObjectId object) const = 0;
    virtual bool CanAddPropertyGroup(ObjectId object, const std::string& type) const = 0;
    virtual PropertyGroupId AddPropertyGroup(ObjectId object, const std::string& type) = 0;
    virtual void RemovePropertyGroup(ObjectId object, PropertyGroupId group) = 0;
};
