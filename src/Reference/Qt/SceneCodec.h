#pragma once
#include "Reference/Scene/ReferenceSceneProvider.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QSaveFile>
#include "Editor/Qt/DocumentFiles.h"

namespace SceneCodec {
inline QString Text(const QJsonValue& value) {
    if (!value.isString()) throw std::invalid_argument("Expected text");
    return value.toString();
}
inline std::uint64_t Id(const QJsonValue& value, bool allowZero = false) {
    const auto text = Text(value); bool valid = false;
    const auto id = text.toULongLong(&valid);
    if (!valid || QString::number(id) != text || (!id && !allowZero)) throw std::invalid_argument("Invalid ID");
    return id;
}
inline QJsonArray Vector(Vec3 v) { return {v.x, v.y, v.z}; }
inline Vec3 Vector(const QJsonValue& value) {
    if (!value.isArray() || value.toArray().size() != 3) throw std::invalid_argument("Expected 3 coordinates");
    Vec3 vector;
    for (int i = 0; i < 3; ++i) {
        const auto number = value.toArray()[i];
        if (!number.isDouble()) throw std::invalid_argument("Expected finite coordinate");
        vector[i] = static_cast<float>(number.toDouble());
        if (!std::isfinite(vector[i])) throw std::invalid_argument("Coordinate is out of range");
    }
    return vector;
}
inline QByteArray Encode(const ReferenceSceneProvider::State& state) {
    ReferenceSceneProvider::Validate(state);
    QJsonArray objects;
    for (const auto& [id, object] : state) {
        const auto& t = object.entity.transform;
        QJsonObject data{{"id", QString::number(id)}, {"parent", QString::number(object.parent)},
            {"name", QString::fromStdString(object.entity.name)},
            {"transform", QJsonObject{{"position", Vector(t.position)}, {"rotation", Vector(t.rotation)}, {"scale", Vector(t.scale)}}}};
        if (object.comment) data["comment"] = QJsonObject{{"id", QString::number(object.comment->id)},
            {"text", QString::fromStdString(object.comment->value.text)}};
        if (object.assetLink) data["assetLink"] = QJsonObject{{"id", QString::number(object.assetLink->id)},
            {"sourcePath", QString::fromStdString(object.assetLink->sourcePath)}};
        objects.append(data);
    }
    return QJsonDocument(QJsonObject{{"format", "axl-reference-scene"}, {"version", 1}, {"objects", objects}}).toJson();
}
inline ReferenceSceneProvider::State Decode(const QByteArray& bytes) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) throw std::invalid_argument("Invalid scene JSON");
    const auto root = document.object();
    if (root["format"] != "axl-reference-scene" || !root["version"].isDouble() || root["version"].toDouble() != 1)
        throw std::invalid_argument("Unsupported scene format or version");
    if (!root["objects"].isArray()) throw std::invalid_argument("Missing scene objects");
    ReferenceSceneProvider::State state;
    for (const auto value : root["objects"].toArray()) {
        if (!value.isObject()) throw std::invalid_argument("Invalid scene object");
        const auto data = value.toObject(); const auto id = Id(data["id"]);
        const auto transform = data["transform"].toObject();
        ReferenceSceneProvider::Object object{Entity{Text(data["name"]).toStdString(),
            Transform{Vector(transform["position"]), Vector(transform["rotation"]), Vector(transform["scale"])}}};
        object.parent = Id(data["parent"], true);
        if (data.contains("comment")) {
            const auto comment = data["comment"].toObject();
            object.comment = ReferenceSceneProvider::CommentGroup{Id(comment["id"]), Comment{Text(comment["text"]).toStdString()}};
        }
        if (data.contains("assetLink")) {
            const auto link = data["assetLink"].toObject();
            object.assetLink = ReferenceSceneProvider::AssetLinkGroup{Id(link["id"]), Text(link["sourcePath"]).toStdString()};
        }
        if (!state.emplace(id, std::move(object)).second) throw std::invalid_argument("Duplicate object ID");
    }
    ReferenceSceneProvider::Validate(state);
    return state;
}
inline QByteArray Read(const QString& path) {
    return DocumentFiles::Read(path);
}
inline void Write(const QString& path, const QByteArray& bytes) {
    DocumentFiles::Write(path, bytes);
}
} // namespace SceneCodec
