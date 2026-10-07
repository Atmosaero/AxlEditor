#pragma once

#include "Contracts/ISceneProvider.h"
#include "Contracts/IObjectProperties.h"
#include "Reference/Scene/Scene.h"
#include <map>
#include <optional>
#include <stdexcept>

// Small Axl reference scene. Only the reference viewport uses its concrete data.
class ReferenceSceneProvider final : public ISceneProvider, public IObjectProperties
{
public:
    struct CommentGroup
    {
        PropertyGroupId id;
        Comment value;
    };

    struct Object
    {
        Entity entity;
        ObjectId parent = InvalidObjectId;
        std::optional<CommentGroup> comment;
    };

    std::vector<ObjectId> GetRootObjects() const override
    {
        std::vector<ObjectId> roots;
        for (const auto& [id, object] : objects_)
            if (object.parent == InvalidObjectId)
                roots.push_back(id);
        return roots;
    }

    ObjectId GetParent(ObjectId id) const override { return objects_.at(id).parent; }

    std::vector<ObjectId> GetChildren(ObjectId id) const override
    {
        (void)objects_.at(id); // Validate the parent ID.
        std::vector<ObjectId> children;
        for (const auto& [childId, object] : objects_)
            if (object.parent == id)
                children.push_back(childId);
        return children;
    }

    std::string GetName(ObjectId id) const override { return objects_.at(id).entity.name; }
    void SetName(ObjectId id, const std::string& name) override { objects_.at(id).entity.name = name; }
    std::optional<Transform> GetTransform(ObjectId id) const override { return objects_.at(id).entity.transform; }
    void SetTransform(ObjectId id, const Transform& transform) override { objects_.at(id).entity.transform = transform; }

    ObjectId CreateObject(const std::string& name, ObjectId parent = InvalidObjectId) override
    {
        if (parent != InvalidObjectId)
            (void)objects_.at(parent);
        if (nextId_ == InvalidObjectId)
            throw std::overflow_error("Scene object IDs exhausted");
        const ObjectId id = nextId_;
        objects_.emplace(id, Object{Entity{name, Transform{}}, parent});
        ++nextId_;
        return id;
    }

    void DeleteObject(ObjectId id) override
    {
        if (objects_.find(id) == objects_.end())
            return;
        for (ObjectId child : GetChildren(id))
            DeleteObject(child);
        objects_.erase(id);
    }

    const std::map<ObjectId, Object>& GetObjects() const { return objects_; }

    std::vector<PropertyGroupInfo> GetPropertyGroups(ObjectId object) const override
    {
        const auto& comment = objects_.at(object).comment;
        if (comment)
            return {{comment->id, "Comment", "Comment"}};
        return {};
    }

    bool CanAddPropertyGroup(ObjectId object, const std::string& type) const override
    {
        const auto& data = objects_.at(object);
        return type == "Comment" && !data.comment;
    }

    PropertyGroupId AddPropertyGroup(ObjectId object, const std::string& type) override
    {
        auto& data = objects_.at(object);
        if (type != "Comment")
            throw std::invalid_argument("Unsupported reference property group");
        if (data.comment)
            return data.comment->id; // Adding the same type twice never duplicates it.
        if (nextGroupId_ == 0)
            throw std::overflow_error("Property group IDs exhausted");
        data.comment = CommentGroup{nextGroupId_++, Comment{}};
        return data.comment->id;
    }

    void RemovePropertyGroup(ObjectId object, PropertyGroupId group) override
    {
        auto& comment = objects_.at(object).comment;
        if (comment && comment->id == group)
            comment.reset();
    }

    std::string GetCommentText(ObjectId object, PropertyGroupId group) const
    {
        const auto& comment = objects_.at(object).comment;
        if (!comment || comment->id != group)
            throw std::out_of_range("Comment group no longer exists");
        return comment->value.text;
    }

    void SetCommentText(ObjectId object, PropertyGroupId group, const std::string& text)
    {
        auto& comment = objects_.at(object).comment;
        if (!comment || comment->id != group)
            throw std::out_of_range("Comment group no longer exists");
        comment->value.text = text;
    }

private:
    std::map<ObjectId, Object> objects_;
    ObjectId nextId_ = 1;
    PropertyGroupId nextGroupId_ = 1;
};
