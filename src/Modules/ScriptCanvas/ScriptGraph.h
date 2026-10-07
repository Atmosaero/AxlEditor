#pragma once

#include <QPointF>
#include <algorithm>
#include <cstdint>
#include <vector>

// Data owned by the Script Canvas tool. No runtime or scene-provider contract.
namespace ScriptGraph {
using NodeId = std::uint64_t;
using PinId = std::uint64_t;
enum class NodeType { Start, Print };
enum class PinDirection { Input, Output };
enum class PinType { Execution };

struct Node { NodeId id; NodeType type; QPointF position; };
struct Pin { PinId id; NodeId node; PinDirection direction; PinType type; };
struct Connection { PinId from; PinId to; };

class Graph
{
public:
    const std::vector<Node>& Nodes() const { return nodes_; }
    const std::vector<Pin>& Pins() const { return pins_; }
    const std::vector<Connection>& Connections() const { return connections_; }

    NodeId AddNode(NodeType type, QPointF position)
    {
        if (type != NodeType::Start && type != NodeType::Print) return 0;
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

    void SetPosition(NodeId id, QPointF position)
    {
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
    NodeId nextNode_ = 1;
    PinId nextPin_ = 1;
    std::vector<Node> nodes_;
    std::vector<Pin> pins_;
    std::vector<Connection> connections_;
};
} // namespace ScriptGraph
