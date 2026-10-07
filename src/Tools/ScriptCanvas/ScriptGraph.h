#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

// Data owned by the Script Canvas tool. No runtime or scene-provider contract.
namespace ScriptGraph {
using NodeId = std::uint64_t;
using PinId = std::uint64_t;
enum class NodeType { Start, Print };
enum class PinDirection { Input, Output };
enum class PinType { Execution };

struct Position {
    double x = 0, y = 0;
    bool operator==(Position other) const { return x == other.x && y == other.y; }
};
struct Node { NodeId id; NodeType type; Position position; };
struct Pin { PinId id; NodeId node; PinDirection direction; PinType type; };
struct Connection { PinId from; PinId to; };

class Graph
{
public:
    const std::vector<Node>& Nodes() const { return nodes_; }
    const std::vector<Pin>& Pins() const { return pins_; }
    const std::vector<Connection>& Connections() const { return connections_; }
    static Graph FromData(std::vector<Node> nodes, std::vector<Pin> pins, std::vector<Connection> connections) {
        Graph result;
        std::set<NodeId> nodeIds; std::set<PinId> pinIds;
        for (const auto& node : nodes) {
            if (!node.id || node.id == std::numeric_limits<NodeId>::max() || !nodeIds.insert(node.id).second
                || (node.type != NodeType::Start && node.type != NodeType::Print)
                || !std::isfinite(node.position.x) || !std::isfinite(node.position.y)
                || std::abs(node.position.x) > 1e7 || std::abs(node.position.y) > 1e7)
                throw std::invalid_argument("Invalid or duplicate graph node");
            result.nextNode_ = std::max(result.nextNode_, node.id + 1);
        }
        for (const auto& pin : pins) {
            if (!pin.id || pin.id == std::numeric_limits<PinId>::max() || !pinIds.insert(pin.id).second
                || !nodeIds.count(pin.node) || pin.type != PinType::Execution
                || (pin.direction != PinDirection::Input && pin.direction != PinDirection::Output))
                throw std::invalid_argument("Invalid or duplicate graph pin");
            result.nextPin_ = std::max(result.nextPin_, pin.id + 1);
        }
        result.nodes_ = std::move(nodes); result.pins_ = std::move(pins);
        for (const auto& connection : connections)
            if (!result.Connect(connection.from, connection.to)) throw std::invalid_argument("Invalid graph connection");
        return result;
    }

    NodeId AddNode(NodeType type, Position position)
    {
        if (type != NodeType::Start && type != NodeType::Print) return 0;
        if (!nextNode_ || !nextPin_ || nextNode_ == std::numeric_limits<NodeId>::max() || nextPin_ == std::numeric_limits<PinId>::max())
            throw std::overflow_error("Graph IDs exhausted");
        ValidatePosition(position);
        const NodeId id = nextNode_++;
        nodes_.push_back({id, type, position});
        AddPin(id, type == NodeType::Start ? PinDirection::Output : PinDirection::Input);
        return id;
    }

    PinId AddPin(NodeId node, PinDirection direction, PinType type = PinType::Execution)
    {
        if ((direction != PinDirection::Input && direction != PinDirection::Output)
            || type != PinType::Execution
            || std::none_of(nodes_.begin(), nodes_.end(), [node](const Node& n) { return n.id == node; }))
            return 0;
        if (!nextPin_ || nextPin_ == std::numeric_limits<PinId>::max()) throw std::overflow_error("Graph pin IDs exhausted");
        const PinId id = nextPin_++;
        pins_.push_back({id, node, direction, type});
        return id;
    }

    const Pin* FindPin(PinId id) const
    {
        const auto it = std::find_if(pins_.begin(), pins_.end(), [id](const Pin& pin) { return pin.id == id; });
        return it == pins_.end() ? nullptr : &*it;
    }

    bool CanConnect(PinId from, PinId to) const
    {
        const auto* output = FindPin(from);
        const auto* input = FindPin(to);
        return output && input && output->node != input->node
            && output->direction == PinDirection::Output && input->direction == PinDirection::Input
            && output->type == input->type && output->type == PinType::Execution
            // One incoming execution wire; outputs may fan out to other inputs.
            && std::none_of(connections_.begin(), connections_.end(), [to](const Connection& c) { return c.to == to; });
    }

    bool Connect(PinId from, PinId to)
    {
        if (!CanConnect(from, to)) return false;
        connections_.push_back({from, to});
        return true;
    }

    void RemoveConnection(PinId from, PinId to)
    {
        connections_.erase(std::remove_if(connections_.begin(), connections_.end(), [=](const Connection& c) {
            return c.from == from && c.to == to;
        }), connections_.end());
    }

    void SetPosition(NodeId id, Position position)
    {
        ValidatePosition(position);
        for (auto& node : nodes_) if (node.id == id) { node.position = position; return; }
    }

    void RemoveNode(NodeId id)
    {
        connections_.erase(std::remove_if(connections_.begin(), connections_.end(), [this, id](const Connection& c) {
            const auto* from = FindPin(c.from);
            const auto* to = FindPin(c.to);
            return (from && from->node == id) || (to && to->node == id);
        }), connections_.end());
        pins_.erase(std::remove_if(pins_.begin(), pins_.end(), [id](const Pin& p) { return p.node == id; }), pins_.end());
        nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [id](const Node& n) { return n.id == id; }), nodes_.end());
    }

private:
    static void ValidatePosition(Position position) {
        if (!std::isfinite(position.x) || !std::isfinite(position.y)
            || std::abs(position.x) > 1e7 || std::abs(position.y) > 1e7) throw std::invalid_argument("Invalid node position");
    }
    NodeId nextNode_ = 1;
    PinId nextPin_ = 1;
    std::vector<Node> nodes_;
    std::vector<Pin> pins_;
    std::vector<Connection> connections_;
};
} // namespace ScriptGraph
