#include "Reference/Qt/SceneDocument.h"
#include "Reference/Qt/CommentSection.h"
#include "Reference/Qt/AssetLinkSection.h"
#include "Reference/Qt/Viewport/ReferenceViewport.h"
#include "Tools/Console/ConsoleTool.h"
#include "Tools/ScriptCanvas/ScriptCanvasTool.h"
#include "Tools/ScriptCanvas/GraphDocument.h"
#include <QTemporaryDir>
#include <QDoubleSpinBox>
#include <QSurfaceFormat>
#include <QtTest>

namespace {
struct Editor {
    ReferenceSceneProvider scene;
    ReferenceAssetProvider assets{""};
    ReferenceViewport* viewport = new ReferenceViewport(scene);
    std::unique_ptr<EditorWindow> window;
    std::unique_ptr<SceneDocument> document;
    Editor() {
        EditorWindow::ToolList tools;
        tools.push_back(std::make_unique<ConsoleTool>()); tools.push_back(std::make_unique<ScriptCanvasTool>());
        window = std::make_unique<EditorWindow>(scene, assets, viewport, &scene, std::move(tools));
        RegisterCommentSection(window->PropertySections(), scene);
        document = std::make_unique<SceneDocument>(*window, scene, assets);
        RegisterAssetLinkSection(window->PropertySections(), scene, assets, document->Session());
        document->decide = [] { return SceneDocument::Decision::Discard; };
        Graph()->decide = [] { return GraphDocument::Decision::Discard; };
    }
    GraphDocument* Graph() { return window->Context().GetService<GraphDocument>(); }
    ScriptCanvasView* Canvas() { return dynamic_cast<ScriptCanvasView*>(window->findChild<QGraphicsView*>("ScriptCanvasView")); }
};
}
class WorkflowTests final : public QObject {
    Q_OBJECT
private slots:
    void textFocusAndClosingDuringViewportDrag() {
        QTemporaryDir project; Editor editor; auto& window = *editor.window;
        window.show(); window.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&window));
        const auto id = window.Operations().Create("Cube").object;
        QVERIFY(window.Operations().Edit("Add Comment", [&] { editor.scene.AddPropertyGroup(id, "Comment"); }).success);
        window.PropertySections().Refresh();
        auto* text = window.findChild<QPlainTextEdit*>("CommentText"); QVERIFY(text);
        text->setFocus(); const auto camera = editor.viewport->grabFramebuffer();
        QTest::keyClicks(text, "WASD"); QTest::qWait(80);
        QCOMPARE(editor.viewport->grabFramebuffer(), camera);
        text->selectAll(); QTest::keyClick(text, Qt::Key_Delete);
        QCOMPARE(editor.scene.GetObjects().size(), std::size_t{1});
        const auto path = project.filePath("close.axl"); QVERIFY(editor.document->SaveAs(path));
        editor.viewport->Set2DMode(true); editor.viewport->setFocus();
        QVERIFY(!editor.document->IsDirty()); // Camera and layout are not scene edits.
        const QPoint handle(editor.viewport->width() / 2 + 55, editor.viewport->height() / 2);
        QTest::mousePress(editor.viewport, Qt::LeftButton, Qt::NoModifier, handle);
        QCOMPARE(QWidget::mouseGrabber(), editor.viewport);
        const auto end = handle + QPoint(30, 0);
        QMouseEvent move(QEvent::MouseMove, end, editor.viewport->mapToGlobal(end), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport, &move);
        QVERIFY(editor.scene.GetTransform(id)->position.x > 0);
        editor.document->decide = [] { return SceneDocument::Decision::Save; };
        QVERIFY(window.close()); QVERIFY(QWidget::mouseGrabber() != editor.viewport);
        QVERIFY(!editor.document->IsDirty());
        QCOMPARE(SceneCodec::Encode(editor.scene.GetObjects()), DocumentFiles::Read(path));
    }
    void completeWorkflowClosesAndReopensBothDocuments() {
        QTemporaryDir project; QVERIFY(QDir(project.path()).mkpath("Assets"));
        DocumentFiles::Write(project.filePath("Assets/demo.lua"), "print('asset only')\n");
        QByteArray sceneBytes, graphBytes; ObjectId id = 0;
        const auto scenePath = project.filePath("Assets/example.axl"), graphPath = project.filePath("Assets/flow.axlgraph");
        {
            Editor editor; auto& window = *editor.window;
            window.show(); window.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&window));
            QVERIFY(editor.document->OpenProject(project.path())); QVERIFY(editor.scene.GetObjects().empty());
            window.findChild<QAction*>("CreateObjectAction")->trigger(); id = window.Operations().Selection(); QVERIFY(id);
            auto* name = window.findChild<QLineEdit*>("ObjectName"); name->setText(QString::fromUtf8("Куб")); QTest::keyClick(name, Qt::Key_Return);
            window.findChild<QDoubleSpinBox*>("PositionX")->setValue(2.5);
            window.findChild<QDoubleSpinBox*>("RotationZ")->setValue(25);
            window.findChild<QDoubleSpinBox*>("ScaleY")->setValue(1.5);
            auto* add = window.findChild<QPushButton*>("AddComponentButton"); QVERIFY(add);
            const auto addType = [&](const QString& type) {
                emit add->menu()->aboutToShow();
                for (auto* action : add->menu()->actions()) if (action->text() == type) { action->trigger(); break; }
                QCoreApplication::processEvents();
            };
            addType("Comment");
            auto* text = window.findChild<QPlainTextEdit*>("CommentText"); QVERIFY(text);
            text->setPlainText(QString::fromUtf8("Сохранённый комментарий\nSecond line"));
            addType("Asset Link");
            auto* choose = window.findChild<QPushButton*>("ChooseAssetButton"); QVERIFY(choose);
            QTimer::singleShot(0, &window, [&window] {
                AssetPicker* picker = nullptr;
                for (auto* dialog : window.findChildren<QDialog*>()) if (auto* candidate = dynamic_cast<AssetPicker*>(dialog)) picker = candidate;
                if (!picker) qFatal("Asset Picker did not open");
                auto* tree = picker->findChild<QTreeWidget*>("AssetPickerTree"); QVERIFY(tree->topLevelItemCount());
                tree->setCurrentItem(tree->topLevelItem(0)); picker->accept();
            });
            choose->click();
            const auto groups = editor.scene.GetPropertyGroups(id); QCOMPARE(groups.size(), std::size_t{2});
            QCOMPARE(editor.scene.GetAssetLink(id, groups[1].id), std::string("Assets/demo.lua"));
            name->setFocus(); QCoreApplication::processEvents();
            const auto withLink = SceneCodec::Encode(editor.scene.GetObjects());
            window.findChild<QAction*>("UndoAction")->trigger();
            QCOMPARE(editor.scene.GetAssetLink(id, groups[1].id), std::string{});
            window.findChild<QAction*>("RedoAction")->trigger(); QCOMPARE(SceneCodec::Encode(editor.scene.GetObjects()), withLink);
            QVERIFY(editor.document->SaveAs(scenePath)); sceneBytes = DocumentFiles::Read(scenePath);
            window.findChild<QAction*>("OpenScriptCanvasAction")->trigger();
            auto* canvas = editor.Canvas(); canvas->setFocus(); QCoreApplication::processEvents();
            const auto sceneIndex = editor.document->History().index();
            canvas->EditGraph("Add Start", [](ScriptGraph::Graph& graph) { graph.AddNode(ScriptGraph::NodeType::Start, {-240, -80}); });
            canvas->EditGraph("Add Print", [](ScriptGraph::Graph& graph) { graph.AddNode(ScriptGraph::NodeType::Print, {80, -80}); });
            canvas->EditGraph("Connect", [](ScriptGraph::Graph& graph) { graph.Connect(graph.Pins()[0].id, graph.Pins()[1].id); });
            window.findChild<QAction*>("UndoAction")->trigger(); QVERIFY(canvas->GraphData().Connections().empty());
            QCOMPARE(editor.document->History().index(), sceneIndex);
            window.findChild<QAction*>("RedoAction")->trigger(); QCOMPARE(canvas->GraphData().Connections().size(), std::size_t{1});
            QVERIFY(editor.Graph()->SaveAs(graphPath));
            canvas->EditGraph("Move", [](ScriptGraph::Graph& graph) { graph.SetPosition(graph.Nodes()[1].id, {160, 20}); });
            window.findChild<QAction*>("SaveAction")->trigger(); QVERIFY(!editor.Graph()->IsDirty());
            graphBytes = DocumentFiles::Read(graphPath);
            name->setFocus(); QCoreApplication::processEvents();
            QVERIFY(window.Operations().Rename(id, "Unsaved scene").success);
            editor.document->decide = [] { return SceneDocument::Decision::Cancel; };
            QVERIFY(!window.close()); QVERIFY(window.isVisible());
            QVERIFY(editor.document->IsDirty());
            editor.document->History().undo(); QVERIFY(!editor.document->IsDirty());
            canvas->EditGraph("Dirty graph", [](ScriptGraph::Graph& graph) { graph.AddNode(ScriptGraph::NodeType::Print, {100, 160}); });
            editor.Graph()->decide = [] { return GraphDocument::Decision::Cancel; };
            QVERIFY(!window.close()); QVERIFY(window.isVisible());
            editor.Graph()->History().undo(); QVERIFY(!editor.Graph()->IsDirty());
            QVERIFY(window.close());
        }
        {
            Editor editor; QVERIFY(editor.document->OpenProject(project.path())); QVERIFY(editor.document->Open(scenePath));
            QVERIFY(editor.Graph()->Open(graphPath));
            QCOMPARE(SceneCodec::Encode(editor.scene.GetObjects()), sceneBytes);
            QCOMPARE(GraphCodec::Encode(editor.Canvas()->GraphData()), graphBytes);
            QCOMPARE(editor.scene.GetPropertyGroups(id).size(), std::size_t{2});
            QCOMPARE(editor.scene.GetTransform(id)->position.x, 2.5f);
            QVERIFY(!editor.document->IsDirty()); QVERIFY(!editor.Graph()->IsDirty());
        }
    }
};
int main(int argc, char** argv) {
    QSurfaceFormat format; format.setVersion(3, 3); format.setProfile(QSurfaceFormat::CoreProfile); format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv); WorkflowTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "WorkflowTests.moc"
