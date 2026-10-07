#include "Tools/ScriptCanvas/ScriptCanvasView.h"
#include "Tools/ScriptCanvas/ScriptGraph.h"
#include "Tools/ScriptCanvas/GraphCodec.h"
#include <QMenu>
#include "Editor/Qt/EditorTheme.h"

#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <cmath>
#include <map>
#include <utility>

namespace ScriptCanvasDetail {
using namespace ScriptGraph;

QPainterPath ConnectionPath(QPointF from, QPointF to)
{
    const qreal reach = std::max(45.0, std::abs(to.x() - from.x()) * .5);
    QPainterPath path(from);
    path.cubicTo(from + QPointF(reach, 0), to - QPointF(reach, 0), to);
    return path;
}

class PinItem final : public QGraphicsItem
{
public:
    explicit PinItem(Pin pin, QGraphicsItem* parent) : QGraphicsItem(parent), pin_(pin)
    {
        setData(0, "ScriptPin");
        setData(1, QVariant::fromValue<qulonglong>(pin.id));
        setAcceptedMouseButtons(Qt::NoButton);
        setAcceptHoverEvents(true);
        setCursor(Qt::CrossCursor);
        setZValue(2);
    }
    const Pin& Data() const { return pin_; }
    QRectF boundingRect() const override { return {-9, -9, 18, 18}; }
    QPainterPath shape() const override { QPainterPath path; path.addEllipse(boundingRect()); return path; }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        const auto& theme = DarkTheme();
        painter->setPen(QPen(theme.graphPinBorder, 1.5));
        painter->setBrush(isUnderMouse() ? theme.graphPinHover : theme.accent);
        painter->drawEllipse(QRectF(-5, -5, 10, 10));
    }
private:
    Pin pin_;
};

class ConnectionItem final : public QGraphicsPathItem
{
public:
    explicit ConnectionItem(Connection connection) : connection_(connection)
    {
        setData(0, "ScriptConnection");
        setData(1, QVariant::fromValue<qulonglong>(connection.from));
        setData(2, QVariant::fromValue<qulonglong>(connection.to));
        setFlag(ItemIsSelectable);
        setZValue(-1);
        setPen(QPen(DarkTheme().graphConnection, 2));
    }
    const Connection& Data() const { return connection_; }
    QPainterPath shape() const override
    {
        QPainterPathStroker stroker;
        stroker.setWidth(12); // An easy click target, independent of the thin visible wire.
        return stroker.createStroke(path());
    }
    QRectF boundingRect() const override { return shape().boundingRect(); }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override
    {
        const auto& theme = DarkTheme();
        painter->setPen(QPen(isSelected() ? theme.graphConnectionSelected : theme.graphConnection, isSelected() ? 3 : 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path());
    }
private:
    Connection connection_;
};

class GraphScene;
class NodeItem final : public QGraphicsItem
{
public:
    NodeItem(Node node, const std::vector<Pin>& pins, GraphScene& graph);
    const std::vector<PinItem*>& Pins() const { return pins_; }
    QRectF boundingRect() const override { return {-1, -1, 182, height_ + 2}; }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;
protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
private:
    NodeId id_;
    NodeType type_;
    GraphScene& graph_;
    std::vector<PinItem*> pins_; // Qt owns these children; order is explicit.
    qreal height_ = 86;
};

class GraphScene final : public QGraphicsScene
{
public:
    GraphScene(Graph graph, QObject* parent) : QGraphicsScene(parent), graph_(std::move(graph))
    {
        setSceneRect(-4000, -4000, 8000, 8000);
        for (const auto& node : graph_.Nodes()) AddNodeItem(node);
        RebuildConnections();
    }

    const Graph& Data() const { return graph_; }
    PinItem* FindPinItem(PinId id) const
    {
        const auto found = pins_.find(id);
        return found == pins_.end() ? nullptr : found->second;
    }

    void AddNode(NodeType type, QPointF position)
    {
        const NodeId id = graph_.AddNode(type, {position.x(), position.y()});
        if (!id) return;
        auto* item = AddNodeItem(graph_.Nodes().back());
        clearSelection();
        item->setSelected(true);
    }

    PinItem* PinAt(QPointF position) const
    {
        for (auto* item : items(position)) if (auto* pin = dynamic_cast<PinItem*>(item)) return pin;
        return nullptr;
    }

    void Connect(PinId from, PinId to)
    {
        if (graph_.Connect(from, to)) RebuildConnections();
    }

    void MoveNode(NodeId id, QPointF position)
    {
        graph_.SetPosition(id, {position.x(), position.y()});
        UpdateConnections();
    }

    void DeleteSelection()
    {
        // Collect IDs first: deleting a node also removes its pins and incident wires.
        std::vector<NodeId> nodes;
        for (auto* item : selectedItems()) {
            if (auto* wire = dynamic_cast<ConnectionItem*>(item))
                graph_.RemoveConnection(wire->Data().from, wire->Data().to);
            else if (auto* node = dynamic_cast<NodeItem*>(item))
                nodes.push_back(node->data(1).toULongLong());
        }
        for (NodeId id : nodes) {
            for (const auto& pin : graph_.Pins()) if (pin.node == id) pins_.erase(pin.id);
            graph_.RemoveNode(id);
            delete nodes_.at(id);
            nodes_.erase(id);
        }
        RebuildConnections();
    }

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override
    {
        const auto& theme = DarkTheme();
        painter->fillRect(rect, theme.graphBackground);
        for (int spacing : {24, 120}) {
            QPen pen(spacing == 24 ? theme.graphGridMinor : theme.graphGridMajor);
            pen.setCosmetic(true);
            painter->setPen(pen);
            const qreal left = std::floor(rect.left() / spacing) * spacing;
            const qreal top = std::floor(rect.top() / spacing) * spacing;
            for (qreal x = left; x < rect.right(); x += spacing) painter->drawLine(QLineF(x, rect.top(), x, rect.bottom()));
            for (qreal y = top; y < rect.bottom(); y += spacing) painter->drawLine(QLineF(rect.left(), y, rect.right(), y));
        }
    }

private:
    NodeItem* AddNodeItem(const Node& node)
    {
        std::vector<Pin> pins;
        for (const auto& pin : graph_.Pins()) if (pin.node == node.id) pins.push_back(pin);
        auto* item = new NodeItem(node, pins, *this);
        addItem(item);
        nodes_.emplace(node.id, item);
        for (auto* pin : item->Pins()) pins_.emplace(pin->Data().id, pin);
        return item;
    }
    void RebuildConnections()
    {
        for (auto* connection : connections_) delete connection;
        connections_.clear();
        for (const auto& connection : graph_.Connections()) {
            auto* item = new ConnectionItem(connection);
            addItem(item);
            connections_.push_back(item);
        }
        UpdateConnections();
    }
    void UpdateConnections()
    {
        for (auto* item : connections_) {
            const auto& connection = item->Data();
            item->setPath(ConnectionPath(pins_.at(connection.from)->scenePos(), pins_.at(connection.to)->scenePos()));
        }
    }
    Graph graph_;
    std::map<NodeId, NodeItem*> nodes_;
    std::map<PinId, PinItem*> pins_;
    std::vector<ConnectionItem*> connections_;
};

NodeItem::NodeItem(Node node, const std::vector<Pin>& pins, GraphScene& graph) : id_(node.id), type_(node.type), graph_(graph)
{
    setData(0, "ScriptNode");
    setData(1, QVariant::fromValue<qulonglong>(id_));
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setCursor(Qt::OpenHandCursor);
    int inputs = 0, outputs = 0;
    for (const auto& pin : pins) {
        auto* handle = new PinItem(pin, this);
        const bool input = pin.direction == PinDirection::Input;
        const int row = input ? inputs++ : outputs++;
        handle->setPos(input ? 0 : 180, 60 + row * 24);
        pins_.push_back(handle);
    }
    height_ += std::max(0, std::max(inputs, outputs) - 1) * 24;
    setPos(node.position.x, node.position.y);
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    const auto& theme = DarkTheme();
    painter->setPen(QPen(isSelected() ? theme.graphNodeSelected : theme.graphNodeBorder, isSelected() ? 2 : 1));
    painter->setBrush(theme.graphNode);
    painter->drawRoundedRect(QRectF(0, 0, 180, height_), 5, 5);
    painter->setPen(QPen(theme.graphNodeDivider, 1));
    painter->drawLine(1, 32, 179, 32);
    painter->setPen(theme.strongText);
    QFont font = painter->font();
    font.setBold(true);
    painter->setFont(font);
    painter->drawText(QRectF(12, 0, 156, 32), Qt::AlignVCenter, type_ == NodeType::Start ? "Start" : "Print");
    font.setBold(false);
    painter->setFont(font);
    painter->setPen(theme.graphLabel);
    for (auto* pin : pins_) {
        const bool input = pin->Data().direction == PinDirection::Input;
        painter->drawText(QRectF(input ? 14 : 96, pin->pos().y() - 18, 70, 36),
            Qt::AlignVCenter | (input ? Qt::AlignLeft : Qt::AlignRight), "Exec");
    }
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionHasChanged) graph_.MoveNode(id_, value.toPointF());
    return QGraphicsItem::itemChange(change, value);
}

} // namespace ScriptCanvasDetail

using namespace ScriptCanvasDetail;

ScriptCanvasView::ScriptCanvasView(QWidget* parent) : ScriptCanvasView(ScriptGraph::Graph{}, parent) {}

ScriptCanvasView::ScriptCanvasView(ScriptGraph::Graph graph, QWidget* parent)
    : QGraphicsView(parent), graph_(new GraphScene(std::move(graph), this))
{
    setObjectName("ScriptCanvasView");
    setScene(graph_);
    setFrameShape(QFrame::NoFrame);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(RubberBandDrag);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(280, 120);
    setToolTip("RMB: Add Node\nDrag an output pin to an input pin\nDelete: remove selected nodes or connections");
}

const ScriptGraph::Graph& ScriptCanvasView::GraphData() const { return graph_->Data(); }
void ScriptCanvasView::SetGraph(ScriptGraph::Graph graph) {
    CancelConnection(); gesture_.reset();
    auto* previous = graph_;
    graph_ = new GraphScene(std::move(graph), this); setScene(graph_); delete previous;
}
void ScriptCanvasView::Record(const ScriptGraph::Graph& before, const QString& label) {
    if (edited && GraphCodec::Encode(before) != GraphCodec::Encode(GraphData())) edited(before, GraphData(), label);
}
void ScriptCanvasView::EditGraph(const QString& label, const std::function<void(ScriptGraph::Graph&)>& edit) {
    FinishGesture(); const auto before = GraphData(); auto after = before; edit(after);
    SetGraph(std::move(after)); Record(before, label);
}
void ScriptCanvasView::FinishGesture(bool cancel) {
    if (!gesture_) return;
    const auto before = *gesture_; gesture_.reset();
    if (cancel) SetGraph(before); else Record(before, "Move nodes");
}

void ScriptCanvasView::contextMenuEvent(QContextMenuEvent* event)
{
    CancelConnection();
    const QPointF position = mapToScene(event->pos());
    QMenu menu(this);
    auto* add = menu.addMenu("Add Node");
    auto* start = add->addAction("Start");
    auto* print = add->addAction("Print");
    menu.addSeparator();
    auto* remove = menu.addAction("Delete");
    remove->setEnabled(!graph_->selectedItems().isEmpty());
    auto* choice = menu.exec(event->globalPos());
    const auto before = GraphData();
    if (choice == start) graph_->AddNode(NodeType::Start, position);
    else if (choice == print) graph_->AddNode(NodeType::Print, position);
    else if (choice == remove) graph_->DeleteSelection();
    Record(before, choice == remove ? "Delete graph selection" : "Add node");
    event->accept();
}
void ScriptCanvasView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (auto* pin = graph_->PinAt(mapToScene(event->position().toPoint()))) {
            if (pin->Data().direction == PinDirection::Output) {
                CancelConnection();
                from_ = pin->Data().id;
                preview_ = graph_->addPath(ConnectionPath(pin->scenePos(), pin->scenePos()),
                    QPen(DarkTheme().graphConnectionPreview, 2, Qt::DashLine));
                preview_->setZValue(-.5);
                preview_->setAcceptedMouseButtons(Qt::NoButton);
            }
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton) { FinishGesture(); gesture_ = GraphData(); }
    QGraphicsView::mousePressEvent(event);
}
void ScriptCanvasView::mouseMoveEvent(QMouseEvent* event)
{
    if (preview_) {
        if (auto* pin = graph_->FindPinItem(from_))
            preview_->setPath(ConnectionPath(pin->scenePos(), mapToScene(event->position().toPoint())));
        else CancelConnection();
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}
void ScriptCanvasView::mouseReleaseEvent(QMouseEvent* event)
{
    if (preview_ && event->button() == Qt::LeftButton) {
        const auto before = GraphData();
        if (auto* pin = graph_->PinAt(mapToScene(event->position().toPoint()))) graph_->Connect(from_, pin->Data().id);
        CancelConnection();
        Record(before, "Connect pins");
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton) FinishGesture();
}
void ScriptCanvasView::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        FinishGesture(); const auto before = GraphData();
        CancelConnection();
        graph_->DeleteSelection();
        Record(before, "Delete graph selection");
        event->accept();
    } else if (event->key() == Qt::Key_Escape) {
        CancelConnection();
        FinishGesture(true);
        event->accept();
    } else QGraphicsView::keyPressEvent(event);
}
void ScriptCanvasView::hideEvent(QHideEvent* event) { CancelConnection(); FinishGesture(); QGraphicsView::hideEvent(event); }
void ScriptCanvasView::focusOutEvent(QFocusEvent* event) { CancelConnection(); FinishGesture(); QGraphicsView::focusOutEvent(event); }

void ScriptCanvasView::CancelConnection() { delete preview_; preview_ = nullptr; from_ = 0; }
