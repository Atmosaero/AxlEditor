#pragma once
#include "Tools/ScriptCanvas/ScriptGraph.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace GraphCodec {
inline std::uint64_t Id(const QJsonValue& value) {
    bool valid = false; const auto text = value.toString(); const auto id = text.toULongLong(&valid);
    if (!value.isString() || !valid || !id || QString::number(id) != text) throw std::invalid_argument("Invalid graph ID");
    return id;
}
inline QByteArray Encode(const ScriptGraph::Graph& graph) {
    (void)ScriptGraph::Graph::FromData(graph.Nodes(), graph.Pins(), graph.Connections());
    QJsonArray nodes, pins, connections;
    for (const auto& node : graph.Nodes()) nodes.append(QJsonObject{{"id", QString::number(node.id)},
        {"type", node.type == ScriptGraph::NodeType::Start ? "Start" : "Print"}, {"x", node.position.x}, {"y", node.position.y}});
    for (const auto& pin : graph.Pins()) pins.append(QJsonObject{{"id", QString::number(pin.id)}, {"node", QString::number(pin.node)},
        {"direction", pin.direction == ScriptGraph::PinDirection::Input ? "Input" : "Output"}, {"type", "Execution"}});
    for (const auto& wire : graph.Connections()) connections.append(QJsonObject{{"from", QString::number(wire.from)}, {"to", QString::number(wire.to)}});
    return QJsonDocument(QJsonObject{{"format", "axl-script-graph"}, {"version", 1}, {"nodes", nodes}, {"pins", pins}, {"connections", connections}}).toJson();
}
inline ScriptGraph::Graph Decode(const QByteArray& bytes) {
    using namespace ScriptGraph;
    QJsonParseError error; const auto doc = QJsonDocument::fromJson(bytes, &error); const auto root = doc.object();
    if (error.error != QJsonParseError::NoError || !doc.isObject() || root["format"] != "axl-script-graph"
        || root["version"].toDouble(-1) != 1 || !root["nodes"].isArray() || !root["pins"].isArray() || !root["connections"].isArray())
        throw std::invalid_argument("Unsupported or invalid graph document");
    std::vector<Node> nodes; std::vector<Pin> pins; std::vector<Connection> connections;
    for (const auto value : root["nodes"].toArray()) {
        const auto data = value.toObject(); const auto type = data["type"].toString();
        if ((type != "Start" && type != "Print") || !data["x"].isDouble() || !data["y"].isDouble()) throw std::invalid_argument("Invalid graph node");
        nodes.push_back({Id(data["id"]), type == "Start" ? NodeType::Start : NodeType::Print, {data["x"].toDouble(), data["y"].toDouble()}});
    }
    for (const auto value : root["pins"].toArray()) {
        const auto data = value.toObject(); const auto direction = data["direction"].toString();
        if ((direction != "Input" && direction != "Output") || data["type"] != "Execution") throw std::invalid_argument("Invalid graph pin");
        pins.push_back({Id(data["id"]), Id(data["node"]), direction == "Input" ? PinDirection::Input : PinDirection::Output, PinType::Execution});
    }
    for (const auto value : root["connections"].toArray()) { const auto data = value.toObject(); connections.push_back({Id(data["from"]), Id(data["to"])}); }
    return Graph::FromData(std::move(nodes), std::move(pins), std::move(connections));
}
}
