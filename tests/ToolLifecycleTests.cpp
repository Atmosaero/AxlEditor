#include "Tools/Console/ConsoleTool.h"
#include "Tools/Console/EditorConsole.h"
#include "Editor/Qt/EditorContext.h"
#include "Tools/ScriptCanvas/ScriptCanvasTool.h"
#include <QMenuBar>
#include <QRegularExpression>
#include <QToolButton>
#include <QtTest>

namespace {
struct Host
{
    QMainWindow window;
    QMenu* view = window.menuBar()->addMenu("View");
    QMenu* tools = window.menuBar()->addMenu("Tools");
    EditorContext context{window, *view, *tools};
};
struct BorrowedService { int value; };
void ExpectDuplicateService()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^Duplicate editor service:.*"));
}
void ExpectDuplicateDock()
{
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^Invalid or duplicate editor dock:.*"));
}
}

class ToolLifecycleTests final : public QObject
{
    Q_OBJECT
private slots:
    void borrowedServiceIdentity()
    {
        Host host;
        BorrowedService first{1}, other{2};
        QVERIFY(host.context.RegisterService(first));
        ExpectDuplicateService();
        QVERIFY(!host.context.RegisterService(other));
        QCOMPARE(host.context.GetService<BorrowedService>(), &first);
        host.context.UnregisterService(&other);
        QCOMPARE(host.context.GetService<BorrowedService>(), &first);
        host.context.UnregisterService(&first);
        QVERIFY(!host.context.GetService<BorrowedService>());
    }

    void deletedQObjectService()
    {
        Host host;
        auto* original = new QObject;
        QVERIFY(host.context.RegisterService(*original));
        delete original;
        QVERIFY(!host.context.GetService<QObject>());
        QObject replacement;
        QVERIFY(host.context.RegisterService(replacement));
        QCOMPARE(host.context.GetService<QObject>(), &replacement);
        host.context.UnregisterService(&replacement);
    }

    void consoleRepeatedLifecycle()
    {
        Host host;
        ConsoleTool tool;
        tool.Initialize(host.context);
        auto* firstDock = tool.Dock();
        QVERIFY(firstDock);
        QCOMPARE(firstDock->objectName(), QString("ConsoleDock"));
        auto* console = host.context.GetService<EditorConsole>();
        QVERIFY(console && console->toPlainText().contains("Axl Editor ready."));
        QCOMPARE(host.context.GetService<IEditorLog>(), static_cast<IEditorLog*>(&tool));
        host.context.GetService<IEditorLog>()->Log(ConsoleMessageType::Warning, "Interface message");
        QVERIFY(console->toPlainText().contains("[Warning] Interface message"));
        tool.Initialize(host.context);
        QCOMPARE(tool.Dock(), firstDock);
        QCOMPARE(host.window.findChildren<QDockWidget*>().size(), 1);
        QCOMPARE(host.view->actions().size(), 1);
        QVERIFY(host.tools->actions().isEmpty());
        auto* clearButton = firstDock->findChild<QToolButton*>("ClearConsoleButton");
        QVERIFY(clearButton && !clearButton->icon().isNull());
        QCOMPARE(clearButton->toolButtonStyle(), Qt::ToolButtonIconOnly);
        console->Log(ConsoleMessageType::Info, "test");
        clearButton->click();
        QVERIFY(console->toPlainText().isEmpty());
        QPointer<QDockWidget> dockGuard = firstDock;
        QPointer<QAction> actionGuard = clearButton->defaultAction();
        tool.Shutdown(); tool.Shutdown();
        QVERIFY(!dockGuard && !actionGuard && !tool.Dock());
        QVERIFY(!host.context.GetService<EditorConsole>());
        QVERIFY(!host.context.GetService<IEditorLog>());
        QVERIFY(host.view->actions().isEmpty() && host.tools->actions().isEmpty());
        tool.Initialize(host.context);
        QVERIFY(tool.Dock() && host.context.GetService<EditorConsole>());
    }

    void consoleDockConflictRollsBack()
    {
        Host host;
        auto* existing = new QDockWidget;
        QVERIFY(host.context.RegisterDock("ConsoleDock", existing, Qt::BottomDockWidgetArea));
        ConsoleTool tool;
        ExpectDuplicateDock();
        tool.Initialize(host.context);
        QVERIFY(!tool.Dock() && !host.context.GetService<EditorConsole>());
        QCOMPARE(host.window.findChildren<QDockWidget*>().size(), 1);
        QCOMPARE(host.view->actions().size(), 1);
        QVERIFY(host.tools->actions().isEmpty());
        tool.Shutdown();
        QCOMPARE(host.window.findChild<QDockWidget*>("ConsoleDock"), existing);
        delete existing;
        tool.Initialize(host.context);
        QVERIFY(tool.Dock());
    }

    void consoleServiceConflictKeepsExisting()
    {
        Host host;
        EditorConsole existing;
        QVERIFY(host.context.RegisterService(existing));
        ConsoleTool tool;
        ExpectDuplicateService();
        tool.Initialize(host.context);
        QVERIFY(!tool.Dock());
        QVERIFY(host.window.findChildren<QDockWidget*>().isEmpty());
        QVERIFY(host.view->actions().isEmpty() && host.tools->actions().isEmpty());
        tool.Shutdown();
        QCOMPARE(host.context.GetService<EditorConsole>(), &existing);
        host.context.UnregisterService(&existing);
        tool.Initialize(host.context);
        QVERIFY(tool.Dock());
    }

    void consoleExternallyDeletedDock()
    {
        Host host;
        ConsoleTool tool;
        tool.Initialize(host.context);
        delete tool.Dock();
        QVERIFY(!tool.Dock() && !host.context.GetService<EditorConsole>());
        QVERIFY(host.view->actions().isEmpty() && host.tools->actions().isEmpty());
        tool.Initialize(host.context);
        QVERIFY(tool.Dock() && host.context.GetService<EditorConsole>());
        delete tool.Dock();
        tool.Shutdown(); tool.Shutdown();
    }

    void loggingServiceConflictRollsBackConsole()
    {
        struct ExistingLog final : IEditorLog {
            void Log(ConsoleMessageType, const QString&) override {}
        } existing;
        Host host;
        QVERIFY(host.context.RegisterService<IEditorLog>(existing));
        ConsoleTool tool;
        ExpectDuplicateService();
        tool.Initialize(host.context);
        QVERIFY(!tool.Dock() && !host.context.GetService<EditorConsole>());
        QCOMPARE(host.context.GetService<IEditorLog>(), &existing);
        QVERIFY(host.window.findChildren<QDockWidget*>().isEmpty());
        QVERIFY(host.view->actions().isEmpty() && host.tools->actions().isEmpty());
        tool.Shutdown();
        QCOMPARE(host.context.GetService<IEditorLog>(), &existing);
        host.context.UnregisterService<IEditorLog>(&existing);
    }

    void consoleShutdownKeepsReplacementService()
    {
        Host host;
        ConsoleTool tool;
        tool.Initialize(host.context);
        delete tool.Dock();
        EditorConsole replacement;
        QVERIFY(host.context.RegisterService(replacement));
        tool.Shutdown();
        QCOMPARE(host.context.GetService<EditorConsole>(), &replacement);
        host.context.UnregisterService(&replacement);
    }

    void scriptCanvasRepeatedAndExternalDeletion()
    {
        Host host;
        ScriptCanvasTool tool;
        tool.Initialize(host.context);
        auto* dock = tool.Dock();
        QVERIFY(dock && dock->isHidden());
        QCOMPARE(dock->objectName(), QString("ScriptCanvasDock"));
        QVERIFY(dock->isFloating());
        QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetFloatable));
        QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetMovable));
        tool.Initialize(host.context);
        QCOMPARE(tool.Dock(), dock);
        QCOMPARE(host.tools->actions().size(), 1);
        delete dock;
        QVERIFY(!tool.Dock());
        QVERIFY(host.view->actions().isEmpty() && host.tools->actions().isEmpty());
        tool.Shutdown(); tool.Shutdown();
        tool.Initialize(host.context);
        QVERIFY(tool.Dock());
    }

    void scriptCanvasDockConflict()
    {
        Host host;
        auto* existing = new QDockWidget;
        QVERIFY(host.context.RegisterDock("ScriptCanvasDock", existing, Qt::BottomDockWidgetArea));
        ScriptCanvasTool tool;
        ExpectDuplicateDock();
        tool.Initialize(host.context);
        QVERIFY(!tool.Dock());
        QCOMPARE(host.window.findChildren<QDockWidget*>().size(), 1);
        QCOMPARE(host.view->actions().size(), 1);
        QVERIFY(host.tools->actions().isEmpty());
        tool.Shutdown();
        QCOMPARE(host.window.findChild<QDockWidget*>("ScriptCanvasDock"), existing);
    }
};

QTEST_MAIN(ToolLifecycleTests)
#include "ToolLifecycleTests.moc"
