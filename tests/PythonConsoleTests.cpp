#include "Tools/PythonConsole/PythonConsoleTool.h"
#include "Reference/Qt/PythonEditorCommands.h"
#include "Reference/Qt/SceneDocument.h"
#include "Reference/Qt/CommentSection.h"
#include "Tools/Console/ConsoleTool.h"
#include "Tools/Console/EditorConsole.h"
#include <QDoubleSpinBox>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#ifndef AXL_TEST_PYTHON
#define AXL_TEST_PYTHON ""
#endif

namespace {
class Viewport final : public QWidget, public IEditorViewport {
public:
    EditorViewportEvents events;
    bool twoD = false;
    ObjectId selection = 0;
    int frames = 0;
    EditorViewportEvents& Events() override { return events; }
    void SetSelectedObject(ObjectId id) override { selection = id; }
    bool Is2DMode() const override { return twoD; }
    void Set2DMode(bool value) override { twoD = value; emit events.CameraModeChanged(value); }
    float NavigationSpeed() const override { return 5; }
    void SetNavigationSpeed(float) override {}
    void FrameSelected() override { ++frames; }
};
struct Editor {
    ReferenceSceneProvider scene;
    ReferenceAssetProvider assets{""};
    Viewport* viewport = new Viewport;
    PythonConsoleTool* python;
    std::unique_ptr<EditorWindow> window;
    std::unique_ptr<SceneDocument> document;
    Editor(QString interpreter = AXL_TEST_PYTHON) {
        EditorWindow::ToolList tools;
        tools.push_back(std::make_unique<ConsoleTool>());
        auto tool = std::make_unique<PythonConsoleTool>(interpreter); python = tool.get(); tools.push_back(std::move(tool));
        window = std::make_unique<EditorWindow>(scene, assets, viewport, &scene, std::move(tools));
        RegisterCommentSection(window->PropertySections(), scene);
        document = std::make_unique<SceneDocument>(*window, scene, assets);
        document->decide = [] { return SceneDocument::Decision::Discard; };
        RegisterPythonEditorCommands(*window, *document, scene);
    }
    QString Output() { return python->Dock()->findChild<QPlainTextEdit*>("PythonOutput")->toPlainText(); }
    bool Run(const QString& code, const QString& filename = "<test>") {
        QSignalSpy finished(python, &PythonConsoleTool::ExecutionFinished);
        if (!python->Execute(code, filename)) return false;
        if (finished.isEmpty() && !finished.wait(15000)) return false;
        return finished.count() == 1 && finished[0][0].toBool();
    }
};
QString Literal(const QString& text) {
    const auto array = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(array.mid(1, array.size() - 2));
}
}

class PythonConsoleTests final : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QFile::remove(QCoreApplication::applicationDirPath() + "/editor-layout.ini");
    }
    void registryLifetimeAndArguments() {
        PythonCommands registry; auto owner = std::make_unique<QObject>(); int calls = 0;
        QVERIFY(registry.RegisterCommand("custom", "test", {"first", "second"}, {{"second", 5}}, *owner,
            [&](const auto& a) { ++calls; return a["first"].toInt() + a["second"].toInt(); }));
        QVERIFY(!registry.RegisterCommand("custom", "duplicate", {}, {}, *owner, [](const auto&) { return true; }));
        QVERIFY(!registry.RegisterCommand("commands", "reserved", {}, {}, *owner, [](const auto&) { return true; }));
        QVERIFY(!registry.RegisterCommand("bad name", "", {}, {}, *owner, [](const auto&) { return true; }));
        QVERIFY(!registry.RegisterCommand("bad_args", "", {"x", "x"}, {}, *owner, [](const auto&) { return true; }));
        QVERIFY(!registry.RegisterCommand("bad_defaults", "", {"x"}, {{"y", 1}}, *owner, [](const auto&) { return true; }));
        QCOMPARE(registry.Invoke("custom", {2}).toInt(), 7);
        QCOMPARE(registry.Invoke("custom", {}, {{"first", 4}, {"second", 3}}).toInt(), 7);
        QVERIFY_EXCEPTION_THROWN(registry.Invoke("custom"), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(registry.Invoke("custom", {1, 2, 3}), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(registry.Invoke("custom", {1}, {{"first", 1}}), std::invalid_argument);
        QVERIFY_EXCEPTION_THROWN(registry.Invoke("custom", {1}, {{"typo", 1}}), std::invalid_argument);
        QCOMPARE(calls, 2);
        owner.reset(); QVERIFY(registry.GetCommands().isEmpty());
        QVERIFY_EXCEPTION_THROWN(registry.Invoke("custom", {1}), std::runtime_error);
        QObject replacement;
        QVERIFY(registry.RegisterCommand("custom", "new", {}, {}, replacement, [](const auto&) { return true; }));
        registry.Unregister(replacement); QVERIFY(registry.GetCommands().isEmpty());
    }
    void toolLifecycleAndMissingInterpreter() {
        Editor editor("missing-python-executable-for-test");
        auto& tool = *editor.python; auto* dock = tool.Dock(); QVERIFY(dock); QVERIFY(dock->isFloating());
        tool.Initialize(editor.window->Context()); QCOMPARE(tool.Dock(), dock);
        QVERIFY(!editor.Run("print('cannot run')"));
        QVERIFY(editor.Output().contains("Python process:")); QVERIFY(!tool.IsBusy());
        tool.Shutdown(); tool.Shutdown();
        QVERIFY(!editor.window->Context().GetService<PythonCommands>());
        tool.Initialize(editor.window->Context()); QVERIFY(tool.Dock());
        delete tool.Dock(); QVERIFY(!tool.Dock()); QVERIFY(!editor.window->Context().GetService<PythonCommands>());
        tool.Initialize(editor.window->Context()); QVERIFY(tool.Dock());
        editor.document.reset(); // Registered callbacks retire without dereferencing the document.
        QVERIFY(tool.Commands()->GetCommands().isEmpty());
    }
    void realPythonAndPersistentNamespace() {
        if (QString(AXL_TEST_PYTHON).isEmpty()) QSKIP("Python interpreter unavailable");
        Editor editor;
        QVERIFY2(editor.Run("import math, sys\nvalues = [math.sqrt(i*i) for i in range(6)]\nprint('stdout marker')\nprint('stderr marker', file=sys.stderr)\nsum(values)"), qPrintable(editor.Output()));
        QVERIFY(editor.Output().contains("stdout marker")); QVERIFY(editor.Output().contains("stderr marker"));
        QVERIFY(editor.Output().contains("15.0"));
        QVERIFY(editor.Run("import axl, inspect\nassert len(axl.commands()) >= 20\nassert str(inspect.signature(axl.create)) == \"(name='Object', parent=None)\"\nassert len(values) == 6\nprint('persistent OK')"));
        QVERIFY(editor.Output().contains("persistent OK"));
        QVERIFY(!editor.Run("1 / 0", "example.py"));
        QVERIFY(editor.Output().contains("ZeroDivisionError")); QVERIFY(editor.Output().contains("example.py"));
        QVERIFY(editor.Run("assert len(values) == 6"));
        QVERIFY(!editor.Run("raise SystemExit(7)")); QVERIFY(editor.Run("print('still alive')"));
    }
    void sceneAutomationAndInspector() {
        if (QString(AXL_TEST_PYTHON).isEmpty()) QSKIP("Python interpreter unavailable");
        Editor editor;
        const auto code = QString::fromUtf8(R"PY(
import axl
root = axl.create('Root')
cube = axl.create('Куб', parent=root)
assert axl.roots() == [root]
assert axl.children(root) == [cube]
assert axl.parent(cube) == root
assert axl.objects() == [root, cube]
assert axl.selected() == cube
axl.rename(cube, 'Cube')
assert axl.name(cube) == 'Cube'
assert axl.find('Cube') == [cube]
axl.set_position(cube, x=1, y=2, z=3)
axl.set_rotation(cube, 10, 20, 30)
axl.set_scale(cube, 2, 2, 2)
assert axl.rotation(cube) == [10, 20, 30]
assert axl.scale(cube) == [2, 2, 2]
axl.translate(cube, 2, 0, 0)
assert axl.position(cube) == [3, 2, 3]
axl.set_transform(cube, position=[4, 5, 6], scale=[1, 2, 3])
assert axl.transform(cube)['position'] == [4, 5, 6]
assert axl.comment(cube) is None
axl.set_comment(cube, 'Line 1\nКомментарий')
assert axl.comment(cube) == 'Line 1\nКомментарий'
assert axl.history()['can_undo']
assert axl.undo()
assert axl.comment(cube) is None
assert axl.redo()
assert axl.comment(cube) == 'Line 1\nКомментарий'
axl.remove_comment(cube)
assert axl.comment(cube) is None
axl.undo()
axl.select()
assert axl.selected() is None
axl.select(cube)
axl.frame_selected()
assert axl.viewport_mode() == '3d'
assert axl.viewport_mode('2d') == '2d'
axl.log('Automation complete', level='success')
)PY");
        QVERIFY2(editor.Run(code), qPrintable(editor.Output()));
        const auto id = editor.window->Operations().Selection(); QVERIFY(id);
        QCOMPARE(editor.scene.GetName(id), std::string("Cube"));
        QCOMPARE(editor.viewport->selection, id); QCOMPARE(editor.viewport->frames, 1); QVERIFY(editor.viewport->twoD);
        QCOMPARE(editor.window->findChild<QDoubleSpinBox*>("PositionX")->value(), 4.0);
        QCOMPARE(editor.window->findChild<QPlainTextEdit*>("CommentText")->toPlainText(), QString::fromUtf8("Line 1\nКомментарий"));
        QVERIFY(editor.window->Context().GetService<EditorConsole>()->toPlainText().contains("[Success] Automation complete"));
        auto* tree = editor.window->findChild<QTreeWidget*>("SceneTree");
        QCOMPARE(tree->topLevelItem(0)->child(0)->child(0)->text(0), QString("Cube"));
        QVERIFY(editor.document->IsDirty());
        QVERIFY(editor.Run("axl.delete(root)\nassert axl.objects() == []\nassert axl.undo()\nassert axl.name(cube) == 'Cube'"));
        QVERIFY(editor.Run("for i in range(10):\n    obj = axl.create(f'Batch {i}')\n    axl.set_position(obj, i, 0, 0)\nassert len(axl.objects()) == 12"));
    }
    void documentsAssetsAndStaleHandles() {
        if (QString(AXL_TEST_PYTHON).isEmpty()) QSKIP("Python interpreter unavailable");
        QTemporaryDir project; QVERIFY(QDir(project.path()).mkpath("Assets"));
        DocumentFiles::Write(project.filePath("Assets/test.lua"), "print('metadata')");
        Editor editor;
        const QString code = "axl.project_open(" + Literal(project.path()) + R"PY()
assert axl.project_info()['root']
axl.assets_refresh()
items = axl.assets()
assert len(items) == 1
assert axl.asset(items[0]['id'])['type'] == 'Script'
assert axl.asset(0) is None
assert axl.asset_open(items[0]['id']) is False
old = axl.create('Saved')
axl.scene_save('Assets/test.axl')
assert not axl.project_info()['dirty']
axl.set_position(old, 1, 2, 3)
axl.scene_save()
axl.scene_new()
assert axl.objects() == []
axl.scene_open('Assets/test.axl')
obj = axl.find('Saved')[0]
assert axl.position(obj) == [1, 2, 3]
try:
    axl.rename(old, 'stale')
    raise AssertionError('stale handle accepted')
except axl.EditorError as e:
    assert 'retired' in str(e)
assert axl.asset_open(next(a['id'] for a in axl.assets() if a['type'] == 'Scene'))
)PY";
        QVERIFY2(editor.Run(code), qPrintable(editor.Output()));
        QVERIFY(QFileInfo::exists(project.filePath("Assets/test.axl")));
        QCOMPARE(editor.window->findChild<QTreeWidget*>("AssetsTree")->topLevelItemCount(), 2);
        editor.document->decide = [] { return SceneDocument::Decision::Cancel; };
        QVERIFY(!editor.Run("axl.create('Dirty')\naxl.scene_new()"));
        QVERIFY(editor.document->IsDirty()); QCOMPARE(editor.scene.GetObjects().size(), std::size_t{2});
    }
    void invalidCallsAreRejected_data() {
        QTest::addColumn<QString>("code");
        QTest::newRow("missing") << "axl.set_position(obj, 1, 2)";
        QTest::newRow("extra") << "axl.set_position(obj, 1, 2, 3, 4)";
        QTest::newRow("keyword") << "axl.set_position(obj, 1, 2, 3, typo=1)";
        QTest::newRow("duplicate") << "axl.rename(obj, 'name', name='again')";
        QTest::newRow("type") << "axl.set_position(obj, 'x', 0, 0)";
        QTest::newRow("bool") << "axl.set_position(obj, True, 0, 0)";
        QTest::newRow("infinity") << "axl.set_position(obj, float('inf'), 0, 0)";
        QTest::newRow("float overflow") << "axl.set_position(obj, 1e100, 0, 0)";
        QTest::newRow("vector") << "axl.set_transform(obj, position=[1, 2])";
        QTest::newRow("partial invalid transform") << "axl.set_transform(obj, position=[1, 2, 3], scale=['bad', 1, 1])";
        QTest::newRow("unknown object") << "axl.rename(999, 'missing')";
        QTest::newRow("wrong camera mode") << "axl.viewport_mode('4d')";
        QTest::newRow("unknown dock") << "axl.dock_show('MissingDock')";
        QTest::newRow("invalid level") << "axl.log('text', 'debug')";
        QTest::newRow("untitled save") << "axl.scene_save()";
    }
    void invalidCallsAreRejected() {
        if (QString(AXL_TEST_PYTHON).isEmpty()) QSKIP("Python interpreter unavailable");
        QFETCH(QString, code); Editor editor;
        QVERIFY(editor.Run("obj = axl.create('Cube')"));
        const auto before = SceneCodec::Encode(editor.scene.GetObjects());
        const auto index = editor.document->History().index();
        QVERIFY2(!editor.Run(code), qPrintable(editor.Output()));
        QCOMPARE(SceneCodec::Encode(editor.scene.GetObjects()), before);
        QCOMPARE(editor.document->History().index(), index);
        QVERIFY(editor.Run("assert axl.name(obj) == 'Cube'"));
    }
    void largeIdsAndExtensionCommands() {
        if (QString(AXL_TEST_PYTHON).isEmpty()) QSKIP("Python interpreter unavailable");
        Editor editor; const ObjectId id = 9007199254741027ULL;
        editor.scene.Replace({{id, {Entity{"Large", {}}}}}); editor.window->RefreshScene();
        QObject owner;
        QVERIFY(editor.python->Commands()->RegisterCommand("custom", "Custom class command", {"value"}, {}, owner, [](const auto& a) { return a["value"]; }));
        QVERIFY2(editor.Run("obj = axl.objects()[0]\nassert obj.id == 9007199254741027\naxl.rename(obj.id, 'Exact')\nassert axl.name(obj) == 'Exact'\nassert axl.custom({'nested': [1, 2]}) == {'nested': [1, 2]}"), qPrintable(editor.Output()));
        editor.python->Commands()->Unregister(owner);
        QVERIFY(editor.Run("assert 'custom' not in axl.commands()\nassert not hasattr(axl, 'custom')"));
    }
    void dockingAndLayout() {
        if (QString(AXL_TEST_PYTHON).isEmpty()) QSKIP("Python interpreter unavailable");
        Editor editor; editor.window->show();
        QVERIFY(editor.Run("assert any(d['name'] == 'PythonConsoleDock' for d in axl.docks())\naxl.dock_show('PythonConsoleDock')\naxl.dock_float('PythonConsoleDock', False)\naxl.layout_save()\naxl.dock_hide('PythonConsoleDock')\naxl.layout_reset()"));
        QVERIFY(editor.python->Dock()->isFloating());
        QVERIFY(QFileInfo::exists(QCoreApplication::applicationDirPath() + "/editor-layout.ini"));
    }
    void stopResponsiveRestartAndOwnerDeletion() {
        if (QString(AXL_TEST_PYTHON).isEmpty()) QSKIP("Python interpreter unavailable");
        Editor editor; QSignalSpy finished(editor.python, &PythonConsoleTool::ExecutionFinished);
        int ticks = 0; QTimer timer; connect(&timer, &QTimer::timeout, this, [&] { ++ticks; }); timer.start(10);
        QVERIFY(editor.python->Execute("while True:\n    pass"));
        QVERIFY(!editor.python->Execute("print('second run')"));
        QTRY_VERIFY_WITH_TIMEOUT(ticks >= 10, 3000);
        QVERIFY(editor.python->IsBusy()); editor.python->Stop();
        QCOMPARE(finished.count(), 1); QVERIFY(!finished[0][0].toBool()); QVERIFY(!editor.python->IsBusy());
        QVERIFY(editor.Run("assert 'obj' not in globals()\nobj = axl.create('After stop')"));
        QVERIFY(editor.python->Execute("while True:\n    pass")); QTest::qWait(100);
        delete editor.python->Dock(); QVERIFY(!editor.python->IsBusy());
        QVERIFY(!editor.window->Context().GetService<PythonCommands>());
        editor.python->Initialize(editor.window->Context());
        RegisterPythonEditorCommands(*editor.window, *editor.document, editor.scene);
        QVERIFY(editor.Run("assert axl.name(axl.selected()) == 'After stop'"));
    }
    void keyboardExecutionAndFocus() {
        if (QString(AXL_TEST_PYTHON).isEmpty()) QSKIP("Python interpreter unavailable");
        Editor editor; editor.window->show();
        editor.window->findChild<QAction*>("OpenPythonConsoleAction")->trigger();
        auto* dock = editor.python->Dock(); dock->activateWindow(); QVERIFY(QTest::qWaitForWindowActive(dock));
        auto* input = dock->findChild<QPlainTextEdit*>("PythonInput"); input->setFocus();
        input->setPlainText("obj = axl.create('Keyboard')");
        QSignalSpy finished(editor.python, &PythonConsoleTool::ExecutionFinished);
        QTest::keyClick(input, Qt::Key_Return, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000); QVERIFY(finished[0][0].toBool());
        input->setFocus(); input->selectAll(); QTest::keyClick(input, Qt::Key_Delete);
        QCOMPARE(editor.scene.GetObjects().size(), std::size_t{1});
        auto* history = dock->findChild<QComboBox*>("PythonHistory"); QCOMPARE(history->count(), 1);
        emit history->activated(0); QCOMPARE(input->toPlainText(), QString("obj = axl.create('Keyboard')"));
    }
};
QTEST_MAIN(PythonConsoleTests)
#include "PythonConsoleTests.moc"
