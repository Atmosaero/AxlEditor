#pragma once

#include "Contracts/ISceneProvider.h"
#include "Contracts/IObjectProperties.h"
#include "Reference/Scene/Scene.h"
#include <map>
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <set>
#include <cmath>
#include <limits>

// Small Axl reference scene. Only the reference viewport uses its concrete data.
class ReferenceSceneProvider final : public ISceneProvider, public IObjectProperties
{
public:
    struct CommentGroup
    {
        PropertyGroupId id;
        Comment value;
    };

    struct AssetLinkGroup { PropertyGroupId id; std::string sourcePath; };
    struct Object
    {
        Entity entity;
        ObjectId parent = InvalidObjectId;
        std::optional<CommentGroup> comment;
        std::optional<AssetLinkGroup> assetLink;
    };
    using State = std::map<ObjectId, Object>;
    // Snapshots are for documents/history, never a second live scene store.
    static void Validate(const State& state) {
        std::set<PropertyGroupId> groups;
        for (const auto& [id, object] : state) {
            if (!id || id == std::numeric_limits<ObjectId>::max()) throw std::invalid_argument("Invalid object ID");
            for (auto vector : {object.entity.transform.position, object.entity.transform.rotation, object.entity.transform.scale})
                for (int i = 0; i < 3; ++i) if (!std::isfinite(vector[i])) throw std::invalid_argument("Non-finite Transform");
            std::set<ObjectId> visited{id};
            auto parent = object.parent;
            while (parent) {
                const auto found = state.find(parent);
                if (found == state.end()) throw std::invalid_argument("Missing parent");
                if (!visited.insert(parent).second) throw std::invalid_argument("Hierarchy cycle");
                parent = found->second.parent;
            }
            if (object.comment && (!object.comment->id || object.comment->id == std::numeric_limits<PropertyGroupId>::max()
                || !groups.insert(object.comment->id).second)) throw std::invalid_argument("Invalid or duplicate property ID");
            if (object.assetLink && (!object.assetLink->id || object.assetLink->id == std::numeric_limits<PropertyGroupId>::max()
                || !groups.insert(object.assetLink->id).second)) throw std::invalid_argument("Invalid or duplicate property ID");
        }
    }
    void Replace(State state) {
        Validate(state);
        for (const auto& [id, object] : state) {
            nextId_ = std::max(nextId_, id + 1);
            if (object.comment) nextGroupId_ = std::max(nextGroupId_, object.comment->id + 1);
            if (object.assetLink) nextGroupId_ = std::max(nextGroupId_, object.assetLink->id + 1);
        }
        objects_ = std::move(state);
    }

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
        if (nextId_ == InvalidObjectId || nextId_ == std::numeric_limits<ObjectId>::max())
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
        const auto& data = objects_.at(object);
        const auto& comment = data.comment;
        std::vector<PropertyGroupInfo> groups;
        if (comment)
            groups.push_back({comment->id, "Comment", "Comment"});
        if (data.assetLink) groups.push_back({data.assetLink->id, "AssetLink", "Asset Link"});
        return groups;
    }

    bool CanAddPropertyGroup(ObjectId object, const std::string& type) const override
    {
        const auto& data = objects_.at(object);
        return (type == "Comment" && !data.comment) || (type == "AssetLink" && !data.assetLink);
    }

    PropertyGroupId AddPropertyGroup(ObjectId object, const std::string& type) override
    {
        auto& data = objects_.at(object);
        if (type == "AssetLink") {
            if (data.assetLink) return data.assetLink->id;
            if (!nextGroupId_ || nextGroupId_ == std::numeric_limits<PropertyGroupId>::max()) throw std::overflow_error("Property group IDs exhausted");
            data.assetLink = AssetLinkGroup{nextGroupId_++, {}}; return data.assetLink->id;
        }
        if (type != "Comment")
            throw std::invalid_argument("Unsupported reference property group");
        if (data.comment)
            return data.comment->id; // Adding the same type twice never duplicates it.
        if (nextGroupId_ == 0 || nextGroupId_ == std::numeric_limits<PropertyGroupId>::max())
            throw std::overflow_error("Property group IDs exhausted");
        data.comment = CommentGroup{nextGroupId_++, Comment{}};
        return data.comment->id;
    }

    void RemovePropertyGroup(ObjectId object, PropertyGroupId group) override
    {
        auto& link = objects_.at(object).assetLink;
        if (link && link->id == group) link.reset();
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

    std::string GetAssetLink(ObjectId object, PropertyGroupId group) const {
        const auto& link = objects_.at(object).assetLink;
        if (!link || link->id != group) throw std::out_of_range("Asset Link no longer exists");
        return link->sourcePath;
    }
    void SetAssetLink(ObjectId object, PropertyGroupId group, const std::string& path) {
        auto& link = objects_.at(object).assetLink;
        if (!link || link->id != group) throw std::out_of_range("Asset Link no longer exists");
        link->sourcePath = path;
    }
private:
    std::map<ObjectId, Object> objects_;
    ObjectId nextId_ = 1;
    PropertyGroupId nextGroupId_ = 1;
};
