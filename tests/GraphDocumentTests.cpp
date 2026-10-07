#include "Tools/ScriptCanvas/GraphDocument.h"
#include <QTemporaryDir>
#include <QMouseEvent>
#include <QtTest>

class GraphDocumentTests final : public QObject {
    Q_OBJECT
private slots:
    void includedExampleGraphLoads() {
        const auto path = QFINDTESTDATA("../Example/Assets/flow.axlgraph"); QVERIFY(!path.isEmpty());
        const auto graph = GraphCodec::Decode(DocumentFiles::Read(path));
        QCOMPARE(graph.Nodes().size(), std::size_t{2}); QCOMPARE(graph.Pins().size(), std::size_t{2});
        QCOMPARE(graph.Connections().size(), std::size_t{1}); QVERIFY((graph.Nodes()[1].position == ScriptGraph::Position{80, -80}));
    }
    void savesValidatesAndUndoesGraphWithoutScene() {
        QTemporaryDir dir;
        QMainWindow window; QMenu viewMenu, toolsMenu; EditorContext context(window, viewMenu, toolsMenu);
        auto* view = new ScriptCanvasView(&window); window.setCentralWidget(view);
        QLabel title; GraphDocument document(*view, context, title);
        document.decide = [] { return GraphDocument::Decision::Discard; };
        using namespace ScriptGraph;
        view->EditGraph("Add nodes", [](Graph& graph) {
            graph.AddNode(NodeType::Start, {-240, -80}); graph.AddNode(NodeType::Print, {80, -80});
            graph.Connect(graph.Pins()[0].id, graph.Pins()[1].id);
        });
        QVERIFY(document.IsDirty()); QCOMPARE(document.History().count(), 1);
        const auto saved = GraphCodec::Encode(view->GraphData());
        QVERIFY(document.SaveAs(dir.filePath("flow.axlgraph"))); QVERIFY(!document.IsDirty());
        view->EditGraph("Delete", [](Graph& graph) { graph.RemoveNode(graph.Nodes()[0].id); });
        QVERIFY(document.IsDirty()); QVERIFY(view->GraphData().Connections().empty());
        document.History().undo(); QCOMPARE(GraphCodec::Encode(view->GraphData()), saved); QVERIFY(!document.IsDirty());
        document.History().redo(); QCOMPARE(view->GraphData().Nodes().size(), std::size_t{1});
        document.decide = [] { return GraphDocument::Decision::Cancel; };
        QVERIFY(!document.New()); QVERIFY(document.IsDirty());
        QVERIFY(!document.SaveAs(dir.filePath("missing/fail.axlgraph"))); QVERIFY(document.IsDirty());
        const auto current = GraphCodec::Encode(view->GraphData());
        auto corrupt = QJsonDocument::fromJson(saved).object(); corrupt["version"] = 2;
        DocumentFiles::Write(dir.filePath("bad.axlgraph"), QJsonDocument(corrupt).toJson());
        QVERIFY(!document.Open(dir.filePath("bad.axlgraph"))); QCOMPARE(GraphCodec::Encode(view->GraphData()), current);
        document.decide = [] { return GraphDocument::Decision::Discard; };
        QVERIFY(document.New()); QVERIFY(view->GraphData().Nodes().empty());
        QVERIFY(document.Open(dir.filePath("flow.axlgraph"))); QCOMPARE(GraphCodec::Encode(view->GraphData()), saved);
        QCOMPARE(document.History().count(), 0); QVERIFY(!document.IsDirty());
        auto duplicate = QJsonDocument::fromJson(saved).object(); auto nodes = duplicate["nodes"].toArray(); nodes.append(nodes.first()); duplicate["nodes"] = nodes;
        QVERIFY_EXCEPTION_THROWN(GraphCodec::Decode(QJsonDocument(duplicate).toJson()), std::invalid_argument);
        auto badPin = QJsonDocument::fromJson(saved).object(); auto pins = badPin["pins"].toArray(); auto pin = pins[0].toObject(); pin["node"] = "999"; pins[0] = pin; badPin["pins"] = pins;
        QVERIFY_EXCEPTION_THROWN(GraphCodec::Decode(QJsonDocument(badPin).toJson()), std::invalid_argument);
        auto badWire = QJsonDocument::fromJson(saved).object(); badWire["connections"] = QJsonArray{QJsonObject{{"from", "2"}, {"to", "1"}}};
        QVERIFY_EXCEPTION_THROWN(GraphCodec::Decode(QJsonDocument(badWire).toJson()), std::invalid_argument);
    }
    void nativeNodeDragCommitsOnceAndEscapeRestores() {
        QMainWindow window; QMenu viewMenu, toolsMenu; EditorContext context(window, viewMenu, toolsMenu);
        auto* view = new ScriptCanvasView(&window); window.setCentralWidget(view); QLabel title;
        GraphDocument document(*view, context, title);
        view->EditGraph("Add", [](ScriptGraph::Graph& graph) { graph.AddNode(ScriptGraph::NodeType::Start, {-80, -60}); });
        window.resize(900, 600); window.show(); window.activateWindow(); view->setFocus();
        QVERIFY(QTest::qWaitForWindowActive(&window)); view->centerOn(0, 0);
        const auto move = [&](QPointF from, QPointF to) {
            const auto a = view->mapFromScene(from), b = view->mapFromScene(to);
            QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, a);
            QMouseEvent event(QEvent::MouseMove, b, view->viewport()->mapToGlobal(b), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view->viewport(), &event);
        };
        const auto before = view->GraphData().Nodes()[0].position;
        move({-10, -44}, {40, -14});
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->mapFromScene(QPointF(40, -14)));
        QCOMPARE(document.History().count(), 2);
        QVERIFY(!(view->GraphData().Nodes()[0].position == before));
        document.History().undo(); QVERIFY(view->GraphData().Nodes()[0].position == before);
        move({-10, -44}, {50, -14}); QTest::keyClick(view, Qt::Key_Escape);
        QVERIFY(view->GraphData().Nodes()[0].position == before); QCOMPARE(document.History().index(), 1);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton);
    }
};
QTEST_MAIN(GraphDocumentTests)
#include "GraphDocumentTests.moc"
