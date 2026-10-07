#include "Tools/ScriptCanvas/ScriptCanvasView.h"
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QMouseEvent>
#include <QtTest>

using namespace ScriptGraph;

namespace {
PinId FirstPin(const Graph& graph, NodeId node) {
    for (const auto& pin : graph.Pins()) if (pin.node == node) return pin.id;
    return 0;
}
QGraphicsItem* Item(ScriptCanvasView& view, const char* kind, std::uint64_t id) {
    for (auto* item : view.scene()->items())
        if (item->data(0).toString() == kind && item->data(1).toULongLong() == id) return item;
    return nullptr;
}
QList<QGraphicsItem*> Items(ScriptCanvasView& view, const char* kind) {
    QList<QGraphicsItem*> result;
    for (auto* item : view.scene()->items()) if (item->data(0).toString() == kind) result.push_back(item);
    return result;
}
void Drag(ScriptCanvasView& view, QPointF from, QPointF to) {
    const auto a = view.mapFromScene(from), b = view.mapFromScene(to);
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, a);
    QMouseEvent move(QEvent::MouseMove, b, view.viewport()->mapToGlobal(b), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view.viewport(), &move);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, b);
}
bool Show(ScriptCanvasView& view) {
    view.resize(900, 600);
    view.show();
    view.activateWindow();
    view.setFocus();
    view.centerOn(0, 0);
    return QTest::qWaitForWindowActive(&view);
}

struct Fixture {
    Graph graph;
    NodeId start = graph.AddNode(NodeType::Start, {-280, -80});
    NodeId print = graph.AddNode(NodeType::Print, {70, -80});
    NodeId otherStart = graph.AddNode(NodeType::Start, {-280, 120});
    NodeId otherPrint = graph.AddNode(NodeType::Print, {70, 140});
    PinId output = FirstPin(graph, start);
    PinId input = FirstPin(graph, print);
    PinId otherOutput = FirstPin(graph, otherStart);
    PinId otherInput = FirstPin(graph, otherPrint);
    PinId extraOutput = graph.AddPin(start, PinDirection::Output);
    PinId extraInput = graph.AddPin(print, PinDirection::Input);
    PinId mixedInput = graph.AddPin(start, PinDirection::Input);
};
}

class ScriptCanvasTests final : public QObject
{
    Q_OBJECT
private slots:
    void oneIncomingExecutionWireAndOutputFanout() {
        Fixture f;
        QVERIFY(f.graph.CanConnect(f.output, f.input));
        QVERIFY(f.graph.Connect(f.output, f.input));
        QVERIFY(!f.graph.CanConnect(f.otherOutput, f.input));
        QVERIFY(!f.graph.Connect(f.otherOutput, f.input));
        QVERIFY(!f.graph.Connect(f.output, f.input));
        QCOMPARE(f.graph.Connections().size(), std::size_t{1});
        QVERIFY(f.graph.Connect(f.output, f.otherInput)); // Fanout is still allowed.
        f.graph.RemoveConnection(f.output, f.input);
        QVERIFY(f.graph.Connect(f.otherOutput, f.input));
        QCOMPARE(f.graph.Connections().size(), std::size_t{2});
    }

    void extraPinsValidateAndDeleteWithTheirNode() {
        Fixture f;
        const auto count = f.graph.Pins().size();
        QVERIFY(!f.graph.AddPin(999, PinDirection::Input));
        QVERIFY(!f.graph.AddPin(f.start, static_cast<PinDirection>(99)));
        QVERIFY(!f.graph.AddPin(f.start, PinDirection::Output, static_cast<PinType>(99)));
        QCOMPARE(f.graph.Pins().size(), count);
        QVERIFY(!f.graph.Connect(f.input, f.extraInput));
        QVERIFY(!f.graph.Connect(f.output, f.extraOutput));
        QVERIFY(!f.graph.Connect(f.output, f.mixedInput)); // No self-node connection.
        QVERIFY(!f.graph.Connect(f.extraOutput, 999));
        QVERIFY(f.graph.Connect(f.output, f.input));
        QVERIFY(f.graph.Connect(f.extraOutput, f.extraInput));
        f.graph.RemoveNode(f.start);
        QVERIFY(f.graph.Connections().empty());
        QVERIFY(!f.graph.FindPin(f.output) && !f.graph.FindPin(f.extraOutput) && !f.graph.FindPin(f.mixedInput));
        QVERIFY(f.graph.FindPin(f.extraInput));
        const auto freshNode = f.graph.AddNode(NodeType::Start, {});
        QVERIFY(freshNode > f.otherPrint && FirstPin(f.graph, freshNode) > f.mixedInput);
        QVERIFY(!f.graph.Connect(f.extraOutput, f.extraInput)); // Deleted ID stays invalid.
    }

    void multiplePinRowsAndConnectionsFollowTheCorrectIds() {
        Fixture f;
        QVERIFY(f.graph.Connect(f.output, f.input));
        ScriptCanvasView view(f.graph);
        QVERIFY(Show(view));
        auto* start = Item(view, "ScriptNode", f.start);
        auto* print = Item(view, "ScriptNode", f.print);
        auto* output = Item(view, "ScriptPin", f.output);
        auto* extraOutput = Item(view, "ScriptPin", f.extraOutput);
        auto* extraInput = Item(view, "ScriptPin", f.extraInput);
        auto* mixedInput = Item(view, "ScriptPin", f.mixedInput);
        QVERIFY(start && print && output && extraOutput && extraInput && mixedInput);
        QCOMPARE(Items(view, "ScriptPin").size(), qsizetype(f.graph.Pins().size()));
        QCOMPARE(output->pos().x(), 180.);
        QCOMPARE(mixedInput->pos().x(), 0.);
        QCOMPARE(extraOutput->pos().y() - output->pos().y(), 24.);
        QVERIFY(start->boundingRect().bottom() > extraOutput->pos().y() + 9);
        // A non-pin child makes child-item order unsuitable for pin lookup.
        auto* decoration = new QGraphicsRectItem(-2, -2, 4, 4, start);
        decoration->setZValue(-10);
        QVERIFY(start->childItems().front()->data(0).toString() != "ScriptPin");
        Drag(view, extraOutput->scenePos(), extraInput->scenePos());
        QCOMPARE(view.GraphData().Connections().size(), std::size_t{2});
        const auto before = print->pos();
        Drag(view, print->scenePos() + QPointF(70, 16), print->scenePos() + QPointF(120, 51));
        QCOMPARE(print->pos(), before + QPointF(50, 35));
        bool stored = false;
        for (const auto& node : view.GraphData().Nodes())
            if (node.id == f.print) stored = node.position == Position{print->pos().x(), print->pos().y()};
        QVERIFY(stored);
        for (auto* item : Items(view, "ScriptConnection")) {
            auto* wire = dynamic_cast<QGraphicsPathItem*>(item);
            auto* from = Item(view, "ScriptPin", item->data(1).toULongLong());
            auto* to = Item(view, "ScriptPin", item->data(2).toULongLong());
            QVERIFY(wire && from && to);
            QCOMPARE(wire->path().elementCount(), 4);
            const auto first = wire->path().elementAt(0);
            QCOMPARE(QPointF(first.x, first.y), from->scenePos());
            QCOMPARE(wire->path().currentPosition(), to->scenePos());
        }
    }

    void occupiedInputRejectsDragAndDeletionFreesIt() {
        Fixture f;
        QVERIFY(f.graph.Connect(f.output, f.input));
        ScriptCanvasView view(f.graph);
        QVERIFY(Show(view));
        auto* otherOutput = Item(view, "ScriptPin", f.otherOutput);
        auto* input = Item(view, "ScriptPin", f.input);
        QVERIFY(otherOutput && input);
        Drag(view, otherOutput->scenePos(), input->scenePos());
        QCOMPARE(view.GraphData().Connections().size(), std::size_t{1});
        QCOMPARE(view.GraphData().Connections().front().from, f.output);
        QCOMPARE(Items(view, "ScriptConnection").size(), qsizetype{1});
        view.scene()->clearSelection();
        Items(view, "ScriptConnection").front()->setSelected(true);
        QTest::keyClick(&view, Qt::Key_Delete);
        QVERIFY(view.GraphData().Connections().empty());
        Drag(view, otherOutput->scenePos(), input->scenePos());
        QCOMPARE(view.GraphData().Connections().size(), std::size_t{1});
        QCOMPARE(view.GraphData().Connections().front().from, f.otherOutput);
        // Removing the source also releases the input.
        view.scene()->clearSelection();
        Item(view, "ScriptNode", f.otherStart)->setSelected(true);
        QTest::keyClick(&view, Qt::Key_Delete);
        QVERIFY(view.GraphData().Connections().empty());
        Drag(view, Item(view, "ScriptPin", f.extraOutput)->scenePos(), input->scenePos());
        QCOMPARE(view.GraphData().Connections().size(), std::size_t{1});
        QCOMPARE(view.GraphData().Connections().front().from, f.extraOutput);
    }

    void deletingMultiPinNodeRemovesEveryPinAndIncidentWire() {
        Fixture f;
        QVERIFY(f.graph.Connect(f.output, f.input));
        QVERIFY(f.graph.Connect(f.extraOutput, f.extraInput));
        QVERIFY(f.graph.Connect(f.otherOutput, f.mixedInput));
        ScriptCanvasView view(f.graph);
        QVERIFY(Show(view));
        auto* start = Item(view, "ScriptNode", f.start);
        QVERIFY(start);
        start->setSelected(true);
        // Deletion during a pending connection cancels the preview first.
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier,
            view.mapFromScene(Item(view, "ScriptPin", f.extraOutput)->scenePos()));
        QTest::keyClick(&view, Qt::Key_Delete);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QCOMPARE(view.GraphData().Nodes().size(), std::size_t{3});
        QVERIFY(view.GraphData().Connections().empty());
        QVERIFY(!Item(view, "ScriptPin", f.output) && !Item(view, "ScriptPin", f.extraOutput)
            && !Item(view, "ScriptPin", f.mixedInput));
        QCOMPARE(Items(view, "ScriptPin").size(), qsizetype{4});
        QCOMPARE(view.scene()->items().size(), qsizetype{7}); // No preview or orphan wire.
    }

    void escapeCancelsExtraPinPreview() {
        Fixture f;
        ScriptCanvasView view(f.graph);
        QVERIFY(Show(view));
        const auto point = view.mapFromScene(Item(view, "ScriptPin", f.extraOutput)->scenePos());
        const auto before = view.scene()->items().size();
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(view.scene()->items().size(), before + 1);
        QTest::keyClick(&view, Qt::Key_Escape);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(view.scene()->items().size(), before);
        QVERIFY(view.GraphData().Connections().empty());
    }
};

QTEST_MAIN(ScriptCanvasTests)
#include "ScriptCanvasTests.moc"
