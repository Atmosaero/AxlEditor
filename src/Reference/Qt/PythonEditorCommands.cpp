#include "Reference/Qt/PythonEditorCommands.h"
#include "Reference/Qt/SceneDocument.h"
#include "Tools/PythonConsole/PythonCommands.h"
#include <QDockWidget>
#include <QRegularExpression>
#include <limits>

namespace {
QString Text(const QJsonValue& value) {
    if (!value.isString()) throw std::invalid_argument("Expected a string");
    return value.toString();
}
float Number(const QJsonValue& value) {
    const auto number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!value.isDouble() || !std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
        throw std::invalid_argument("Expected a finite number in float range");
    return float(number);
}
std::uint64_t Id(const QJsonValue& value) {
    if (value.isObject()) return Id(value.toObject()["$integer"]);
    if (value.isString()) {
        bool ok = false;
        const auto text = value.toString();
        const auto id = text.toULongLong(&ok);
        if (ok && QRegularExpression("^[0-9]+$").match(text).hasMatch()) return id;
    }
    if (value.isDouble()) {
        const double id = value.toDouble();
        if (std::isfinite(id) && id >= 0 && id <= 9007199254740991.0 && std::floor(id) == id) return std::uint64_t(id);
    }
    throw std::invalid_argument("Expected an unsigned ID (use a decimal string for large IDs)");
}
void Require(const EditorOperations::Result& result) {
    if (!result) throw std::runtime_error(result.error);
}
QJsonArray Vector(Vec3 vector) { return {vector.x, vector.y, vector.z}; }
Vec3 Vector(const QJsonValue& value) {
    if (!value.isArray() || value.toArray().size() != 3) throw std::invalid_argument("Expected [x, y, z]");
    const auto array = value.toArray(); return {Number(array[0]), Number(array[1]), Number(array[2])};
}
QJsonObject Asset(const AssetInfo& asset) {
    return {{"id", QString::number(asset.id)}, {"name", QString::fromStdString(asset.name)},
        {"type", QString::fromStdString(asset.type)}, {"source_path", QString::fromStdString(asset.sourcePath)}};
}
}

void RegisterPythonEditorCommands(EditorWindow& window, SceneDocument& document, ReferenceSceneProvider& reference)
{
    auto* commands = window.Context().GetService<PythonCommands>();
    if (!commands) return; // Optional tool; the shell has no Python dependency.
    auto& scene = *window.Context().GetService<ISceneProvider>();
    auto& assets = *window.Context().GetService<IAssetProvider>();
    auto& ops = window.Operations();
    auto object = [&scene, &ops](const QJsonValue& value, bool nullable = false) -> ObjectId {
        if (nullable && value.isNull()) return 0;
        auto idValue = value;
        if (value.isObject() && value.toObject().contains("$object")) {
            const auto handle = value.toObject();
            if (!ops.IsCurrent(Id(handle["generation"]))) throw std::runtime_error("Object belongs to a retired scene session");
            idValue = handle["$object"];
        }
        const auto id = Id(idValue);
        if (id) (void)scene.GetName(id);
        else if (!nullable) throw std::invalid_argument("Expected an object, not an empty selection");
        return id;
    };
    auto handle = [&ops](ObjectId id) -> QJsonValue {
        if (!id) return QJsonValue::Null;
        return QJsonObject{{"$object", QString::number(id)}, {"generation", QString::number(ops.Generation())}};
    };
    auto handles = [handle](const std::vector<ObjectId>& ids) {
        QJsonArray result; for (auto id : ids) result.append(handle(id)); return result;
    };
    auto all = [&scene] {
        std::vector<ObjectId> result;
        std::function<void(ObjectId)> visit = [&](ObjectId id) { result.push_back(id); for (auto child : scene.GetChildren(id)) visit(child); };
        for (auto root : scene.GetRootObjects()) visit(root);
        return result;
    };
    auto transform = [&scene, object](const QJsonObject& args) {
        const auto value = scene.GetTransform(object(args["object"]));
        if (!value) throw std::runtime_error("Object has no Transform");
        return *value;
    };
    auto path = [&document](const QJsonValue& value) {
        const auto text = Text(value);
        if (text.trimmed().isEmpty()) throw std::invalid_argument("Path cannot be empty");
        return QDir(QString::fromStdString(document.Session().projectRoot)).absoluteFilePath(text);
    };
    auto add = [&](const QString& name, const QString& description, QStringList parameters,
                   PythonCommands::Handler callback, QJsonObject defaults = {}) {
        if (!commands->RegisterCommand(name, description, std::move(parameters), std::move(defaults), document, std::move(callback)))
            throw std::runtime_error("Duplicate Python command: " + name.toStdString());
    };
    // Stored callbacks capture helpers by value, never the registration stack.
    add("objects", "List all objects in hierarchy order.", {}, [=](const auto&) { return handles(all()); });
    add("roots", "List root objects.", {}, [handles, &scene](const auto&) { return handles(scene.GetRootObjects()); });
    add("children", "List children of an object.", {"object"}, [object, handles, &scene](const auto& a) { return handles(scene.GetChildren(object(a["object"]))); });
    add("parent", "Return parent or None.", {"object"}, [object, handle, &scene](const auto& a) { return handle(scene.GetParent(object(a["object"]))); });
    add("create", "Create and select an object; parent is optional. Undoable.", {"name", "parent"}, [object, handle, &ops](const auto& a) {
        const auto result = ops.Create(Text(a["name"]).toStdString(), object(a["parent"], true)); Require(result); return handle(result.object);
    }, {{"name", "Object"}, {"parent", QJsonValue::Null}});
    add("delete", "Delete an object and its children. Undoable.", {"object"}, [object, &ops](const auto& a) { Require(ops.Delete(object(a["object"]))); return QJsonValue::Null; });
    add("rename", "Rename an object. Undoable.", {"object", "name"}, [object, &ops](const auto& a) { Require(ops.Rename(object(a["object"]), Text(a["name"]).toStdString())); return QJsonValue::Null; });
    add("name", "Read an object's name.", {"object"}, [object, &scene](const auto& a) { return QString::fromStdString(scene.GetName(object(a["object"]))); });
    add("find", "Find all objects with this exact name.", {"name"}, [all, handles, &scene](const auto& a) {
        const auto name = Text(a["name"]).toStdString(); std::vector<ObjectId> found;
        for (auto id : all()) if (scene.GetName(id) == name) found.push_back(id);
        return handles(found);
    });
    add("select", "Select an object or clear selection with None.", {"object"}, [object, &ops](const auto& a) { Require(ops.Select(object(a["object"], true))); return QJsonValue::Null; }, {{"object", QJsonValue::Null}});
    add("selected", "Return the selected object or None.", {}, [handle, &ops](const auto&) { return handle(ops.Selection()); });
    add("transform", "Read position, rotation (degrees), scale as lists.", {"object"}, [transform](const auto& a) {
        const auto value = transform(a); return QJsonObject{{"position", Vector(value.position)}, {"rotation", Vector(value.rotation)}, {"scale", Vector(value.scale)}};
    });
    add("set_transform", "Set any combination of position, rotation and scale. Undoable.", {"object", "position", "rotation", "scale"}, [object, transform, &ops](const auto& a) {
        auto value = transform(a);
        if (!a["position"].isNull()) value.position = Vector(a["position"]);
        if (!a["rotation"].isNull()) value.rotation = Vector(a["rotation"]);
        if (!a["scale"].isNull()) value.scale = Vector(a["scale"]);
        Require(ops.SetTransform(object(a["object"]), value)); return QJsonValue::Null;
    }, {{"position", QJsonValue::Null}, {"rotation", QJsonValue::Null}, {"scale", QJsonValue::Null}});
    for (const auto& field : {QString("position"), QString("rotation"), QString("scale")}) {
        auto member = field == "position" ? &Transform::position : field == "rotation" ? &Transform::rotation : &Transform::scale;
        add(field, "Read " + field + " as [x, y, z].", {"object"}, [transform, member](const auto& a) { return Vector(transform(a).*member); });
        add("set_" + field, "Set " + field + ". Rotation uses degrees. Undoable.", {"object", "x", "y", "z"}, [object, transform, member, &ops](const auto& a) {
            auto value = transform(a); value.*member = {Number(a["x"]), Number(a["y"]), Number(a["z"])};
            Require(ops.SetTransform(object(a["object"]), value)); return QJsonValue::Null;
        });
    }
    add("translate", "Add a local-space offset to position. Undoable.", {"object", "x", "y", "z"}, [object, transform, &ops](const auto& a) {
        auto value = transform(a); value.position.x += Number(a["x"]); value.position.y += Number(a["y"]); value.position.z += Number(a["z"]);
        Require(ops.SetTransform(object(a["object"]), value)); return QJsonValue::Null;
    });
    // These three commands belong to the reference integration, not the scene contract.
    auto comment = [object, &reference](const QJsonObject& a) -> PropertyGroupId {
        for (const auto& group : reference.GetPropertyGroups(object(a["object"]))) if (group.type == "Comment") return group.id;
        return 0;
    };
    add("comment", "Read reference Comment text, or None if absent.", {"object"}, [object, comment, &reference](const auto& a) -> QJsonValue {
        const auto group = comment(a); return group ? QJsonValue(QString::fromStdString(reference.GetCommentText(object(a["object"]), group))) : QJsonValue::Null;
    });
    add("set_comment", "Add or update reference Comment text. Undoable.", {"object", "text"}, [object, &reference, &ops](const auto& a) {
        const auto id = object(a["object"]); const auto text = Text(a["text"]).toStdString();
        Require(ops.Edit("Comment", [&] { const auto group = reference.AddPropertyGroup(id, "Comment"); reference.SetCommentText(id, group, text); })); return QJsonValue::Null;
    });
    add("remove_comment", "Remove reference Comment if present. Undoable.", {"object"}, [object, comment, &reference, &ops](const auto& a) {
        const auto id = object(a["object"]); const auto group = comment(a);
        if (group) Require(ops.Edit("Remove Comment", [&] { reference.RemovePropertyGroup(id, group); })); return QJsonValue::Null;
    });
    auto checkDocument = [&document](bool success) {
        if (!success) throw std::runtime_error(document.lastError.isEmpty() ? "Document operation cancelled or path is invalid" : document.lastError.toStdString());
        return QJsonValue(true);
    };
    add("scene_new", "New scene, with the usual unsaved-changes prompt.", {}, [checkDocument, &document](const auto&) { return checkDocument(document.NewScene()); });
    add("scene_open", "Open scene; relative paths use the project root. May prompt to save.", {"path"}, [path, checkDocument, &document](const auto& a) { return checkDocument(document.Open(path(a["path"]))); });
    add("scene_save", "Save scene. An untitled scene requires path; optional path performs Save As.", {"path"}, [path, checkDocument, &document](const auto& a) {
        if (a["path"].isNull() && document.Session().scenePath.empty()) throw std::runtime_error("Untitled scene requires a path");
        return checkDocument(a["path"].isNull() ? document.Save() : document.SaveAs(path(a["path"])));
    }, {{"path", QJsonValue::Null}});
    add("project_open", "Open an existing project folder. May prompt to save.", {"path"}, [checkDocument, &document](const auto& a) { return checkDocument(document.OpenProject(Text(a["path"]))); });
    add("project_info", "Read current project, scene path and scene dirty state.", {}, [&document](const auto&) {
        const auto& session = document.Session(); return QJsonObject{{"root", QString::fromStdString(session.projectRoot)}, {"name", QString::fromStdString(session.projectName)},
            {"scene_path", QString::fromStdString(session.scenePath)}, {"scene_name", QString::fromStdString(session.sceneName)}, {"dirty", document.IsDirty()}};
    });
    add("assets", "List asset metadata through IAssetProvider; IDs are decimal strings.", {}, [&assets](const auto&) { QJsonArray result; for (const auto& asset : assets.GetAssets()) result.append(Asset(asset)); return result; });
    add("asset", "Read asset metadata, or None if unavailable.", {"id"}, [&assets](const auto& a) -> QJsonValue { const auto asset = assets.GetAsset(Id(a["id"])); return asset ? QJsonValue(Asset(*asset)) : QJsonValue::Null; });
    add("assets_refresh", "Refresh provider and Assets panel.", {}, [&window](const auto&) { window.RefreshAssets(); return QJsonValue::Null; });
    add("asset_open", "Open an asset through the registered editor handler; return whether handled.", {"id"}, [&assets, &window](const auto& a) {
        const auto asset = assets.GetAsset(Id(a["id"])); if (!asset) throw std::runtime_error("Asset is unavailable");
        auto* handlers = window.Context().GetService<AssetOpenHandlers>(); return handlers && handlers->Open(*asset);
    });
    add("undo", "Undo one scene edit (independent of graph focus).", {}, [&ops, &document](const auto&) { ops.CommitTransformGesture(); const auto result = document.History().canUndo(); document.History().undo(); return result; });
    add("redo", "Redo one scene edit (independent of graph focus).", {}, [&ops, &document](const auto&) { ops.CommitTransformGesture(); const auto result = document.History().canRedo(); document.History().redo(); return result; });
    add("history", "Read scene undo/redo availability and labels.", {}, [&document](const auto&) { auto& history = document.History(); return QJsonObject{{"can_undo", history.canUndo()}, {"can_redo", history.canRedo()}, {"undo", history.undoText()}, {"redo", history.redoText()}}; });
    auto viewport = [&window]() -> IEditorViewport& {
        auto* result = dynamic_cast<IEditorViewport*>(&window.ViewportWidget()); if (!result) throw std::runtime_error("This viewport has no camera controls"); return *result;
    };
    add("frame_selected", "Frame the selected object in the viewport.", {}, [viewport](const auto&) { viewport().FrameSelected(); return QJsonValue::Null; });
    add("viewport_mode", "Read camera mode, or set '2d'/'3d' without changing scene data.", {"mode"}, [viewport](const auto& a) {
        if (!a["mode"].isNull()) { const auto mode = Text(a["mode"]).toLower(); if (mode != "2d" && mode != "3d") throw std::invalid_argument("Mode must be '2d' or '3d'"); viewport().Set2DMode(mode == "2d"); }
        return viewport().Is2DMode() ? "2d" : "3d";
    }, {{"mode", QJsonValue::Null}});
    for (const auto& verb : {QString("show"), QString("hide"), QString("float")})
        add("dock_" + verb, "Control a dock using its stable object name (see docks()).", verb == "float" ? QStringList{"name", "floating"} : QStringList{"name"}, [&window, verb](const auto& a) {
            auto* dock = window.findChild<QDockWidget*>(Text(a["name"])); if (!dock) throw std::runtime_error("Dock is unavailable");
            if (verb == "show") { dock->show(); dock->raise(); } else if (verb == "hide") dock->hide();
            else { if (!a["floating"].isBool()) throw std::invalid_argument("floating must be a bool"); dock->setFloating(a["floating"].toBool()); }
            return QJsonValue::Null;
        }, verb == "float" ? QJsonObject{{"floating", true}} : QJsonObject{});
    add("docks", "List dock names, titles, visibility and floating state.", {}, [&window](const auto&) {
        QJsonArray result; for (auto* dock : window.findChildren<QDockWidget*>()) result.append(QJsonObject{{"name", dock->objectName()}, {"title", dock->windowTitle()}, {"visible", dock->isVisible()}, {"floating", dock->isFloating()}}); return result;
    });
    for (const auto& verb : {QString("save"), QString("reset")})
        add("layout_" + verb, verb + " the editor dock layout.", {}, [&window, verb](const auto&) {
            auto* action = window.findChild<QAction*>(verb == "save" ? "SaveLayoutAction" : "ResetLayoutAction");
            if (!action) throw std::runtime_error("Layout action is unavailable"); action->trigger(); return QJsonValue::Null;
        });
    add("log", "Write to editor Console: info, warning, error or success.", {"message", "level"}, [&window](const auto& a) {
        const auto text = Text(a["message"]); const auto level = Text(a["level"]);
        const std::map<QString, ConsoleMessageType> levels{{"info", ConsoleMessageType::Info}, {"warning", ConsoleMessageType::Warning}, {"error", ConsoleMessageType::Error}, {"success", ConsoleMessageType::Success}};
        const auto found = levels.find(level); if (found == levels.end()) throw std::invalid_argument("Unknown log level");
        auto* log = window.Context().GetService<IEditorLog>(); if (!log) throw std::runtime_error("Editor Console is unavailable"); log->Log(found->second, text); return QJsonValue::Null;
    }, {{"level", "info"}});
}
