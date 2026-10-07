#pragma once
#include "Contracts/ISceneProvider.h"
#include <cmath>
#include <functional>
#include <string>
#include <utility>

// Typed editing entry point. The provider remains the only scene model.
class EditorOperations
{
public:
    enum class Change { Selection, Values, Structure, Session };
    struct Result {
        bool success = false;
        ObjectId object = InvalidObjectId;
        std::string error;
        explicit operator bool() const { return success; }
    };
    explicit EditorOperations(ISceneProvider& scene) : scene_(scene) {}
    ObjectId Selection() const { return selected_; }
    std::uint64_t Generation() const { return generation_; }
    bool IsCurrent(std::uint64_t generation) const { return generation == generation_; }
    // UI refresh and history hooks are installed by adapters, never required.
    std::function<void(Change)> changed;
    std::function<void()> beforeEdit;
    std::function<void(const std::string&)> committed;

    Result Select(ObjectId id) {
        try {
            if (id) (void)scene_.GetName(id);
            if (id == selected_) return {true, id, {}};
            CancelTransformGesture();
            selected_ = id;
            Notify(Change::Selection);
            return {true, id, {}};
        } catch (const std::exception& e) { return {false, id, e.what()}; }
    }
    Result Create(const std::string& name, ObjectId parent = 0) {
        ObjectId id = 0;
        auto result = Edit("Create object", [&] { id = scene_.CreateObject(name, parent); selected_ = id; }, Change::Structure);
        result.object = id;
        return result;
    }
    Result Delete(ObjectId id) {
        if (!id) return {false, id, "No object selected"};
        return Edit("Delete object", [&] {
            const auto parent = scene_.GetParent(id);
            ObjectId cursor = selected_;
            while (cursor && cursor != id) cursor = scene_.GetParent(cursor);
            scene_.DeleteObject(id);
            if (cursor == id) selected_ = parent;
        }, Change::Structure);
    }
    Result Rename(ObjectId id, const std::string& name) {
        try {
            if (scene_.GetName(id) == name) return {true, id, {}};
            return Edit("Rename object", [&] { scene_.SetName(id, name); }, Change::Structure);
        } catch (const std::exception& e) { return {false, id, e.what()}; }
    }
    Result SetTransform(ObjectId id, const Transform& value) {
        for (auto vector : {value.position, value.rotation, value.scale})
            for (int i = 0; i < 3; ++i) if (!std::isfinite(vector[i])) return {false, id, "Transform must be finite"};
        try {
            const auto old = scene_.GetTransform(id);
            if (!old) return {false, id, "Object has no Transform"};
            if (old->position == value.position && old->rotation == value.rotation && old->scale == value.scale)
                return {true, id, {}};
            return Edit("Transform", [&] { scene_.SetTransform(id, value); }, Change::Values, gesture_ && gestureObject_ == id);
        } catch (const std::exception& e) { return {false, id, e.what()}; }
    }
    Result Edit(const std::string& label, const std::function<void()>& operation, Change change = Change::Values, bool preview = false) {
        try {
            if (gesture_ && !preview) CommitTransformGesture();
            if (!gesture_ && beforeEdit) beforeEdit();
            operation();
            if (!gesture_ && committed) committed(label);
            Notify(change);
            return {true, selected_, {}};
        } catch (const std::exception& e) { return {false, selected_, e.what()}; }
    }
    bool BeginTransformGesture(ObjectId id) {
        CancelTransformGesture();
        try {
            const auto start = scene_.GetTransform(id);
            if (!start) return false;
            if (beforeEdit) beforeEdit();
            gesture_ = true; gestureObject_ = id; gestureStart_ = *start;
            return true;
        } catch (const std::exception&) { return false; }
    }
    bool HasTransformGesture() const { return gesture_; }
    void CommitTransformGesture() {
        if (!std::exchange(gesture_, false)) return;
        if (committed) committed("Transform");
    }
    void CancelTransformGesture() {
        if (!std::exchange(gesture_, false)) return;
        try { scene_.SetTransform(gestureObject_, gestureStart_); } catch (const std::exception&) {}
        Notify(Change::Values);
    }
    // Call before replacing data in a still-live provider. Retired callbacks use Generation.
    void ResetSession() {
        CancelTransformGesture(); selected_ = 0; ++generation_; Notify(Change::Session);
    }
    void Notify(Change change = Change::Values) { if (changed) changed(change); }
private:
    ISceneProvider& scene_;
    ObjectId selected_ = 0, gestureObject_ = 0;
    std::uint64_t generation_ = 1;
    bool gesture_ = false;
    Transform gestureStart_;
};
