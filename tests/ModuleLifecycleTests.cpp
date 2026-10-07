#include "Modules/Console/ConsoleModule.h"
#include "Modules/Console/EditorConsole.h"
#include "Editor/EditorContext.h"
#include "Modules/ScriptCanvas/ScriptCanvasModule.h"
#include <QMenuBar>
#include <QRegularExpression>
#include <QtTest>

namespace {
struct Host
{
    QMainWindow window;
    QMenu* view = window.menuBar()->addMenu("View");
    QMenu* modules = window.menuBar()->addMenu("Modules");
    EditorContext context{window, *view, *modules};
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

class ModuleLifecycleTests final : public QObject
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
        ConsoleModule module;
        module.Initialize(host.context);
        auto* firstDock = module.Dock();
        QVERIFY(firstDock);
        QCOMPARE(firstDock->objectName(), QString("ConsoleDock"));
        auto* console = host.context.GetService<EditorConsole>();
        QVERIFY(console && console->toPlainText().contains("Axl Editor ready."));
        QCOMPARE(host.context.GetService<IEditorLog>(), static_cast<IEditorLog*>(&module));
        host.context.GetService<IEditorLog>()->Log(ConsoleMessageType::Warning, "Interface message");
        QVERIFY(console->toPlainText().contains("[Warning] Interface message"));
        module.Initialize(host.context);
        QCOMPARE(module.Dock(), firstDock);
        QCOMPARE(host.window.findChildren<QDockWidget*>().size(), 1);
        QCOMPARE(host.view->actions().size(), 1);
        QCOMPARE(host.modules->actions().size(), 1);
        console->Log(ConsoleMessageType::Info, "test");
        host.modules->actions().front()->trigger();
        QVERIFY(console->toPlainText().isEmpty());
        QPointer<QDockWidget> dockGuard = firstDock;
        QPointer<QAction> actionGuard = host.modules->actions().front();
        module.Shutdown(); module.Shutdown();
        QVERIFY(!dockGuard && !actionGuard && !module.Dock());
        QVERIFY(!host.context.GetService<EditorConsole>());
        QVERIFY(!host.context.GetService<IEditorLog>());
        QVERIFY(host.view->actions().isEmpty() && host.modules->actions().isEmpty());
        module.Initialize(host.context);
        QVERIFY(module.Dock() && host.context.GetService<EditorConsole>());
    }

    void consoleDockConflictRollsBack()
    {
        Host host;
        auto* existing = new QDockWidget;
        QVERIFY(host.context.RegisterDock("ConsoleDock", existing, Qt::BottomDockWidgetArea));
        ConsoleModule module;
        ExpectDuplicateDock();
        module.Initialize(host.context);
        QVERIFY(!module.Dock() && !host.context.GetService<EditorConsole>());
        QCOMPARE(host.window.findChildren<QDockWidget*>().size(), 1);
        QCOMPARE(host.view->actions().size(), 1);
        QVERIFY(host.modules->actions().isEmpty());
        module.Shutdown();
        QCOMPARE(host.window.findChild<QDockWidget*>("ConsoleDock"), existing);
        delete existing;
        module.Initialize(host.context);
        QVERIFY(module.Dock());
    }

    void consoleServiceConflictKeepsExisting()
    {
        Host host;
        EditorConsole existing;
        QVERIFY(host.context.RegisterService(existing));
        ConsoleModule module;
        ExpectDuplicateService();
        module.Initialize(host.context);
        QVERIFY(!module.Dock());
        QVERIFY(host.window.findChildren<QDockWidget*>().isEmpty());
        QVERIFY(host.view->actions().isEmpty() && host.modules->actions().isEmpty());
        module.Shutdown();
        QCOMPARE(host.context.GetService<EditorConsole>(), &existing);
        host.context.UnregisterService(&existing);
        module.Initialize(host.context);
        QVERIFY(module.Dock());
    }

    void consoleExternallyDeletedDock()
    {
        Host host;
        ConsoleModule module;
        module.Initialize(host.context);
        delete module.Dock();
        QVERIFY(!module.Dock() && !host.context.GetService<EditorConsole>());
        QVERIFY(host.view->actions().isEmpty() && host.modules->actions().isEmpty());
        module.Initialize(host.context);
        QVERIFY(module.Dock() && host.context.GetService<EditorConsole>());
        delete module.Dock();
        module.Shutdown(); module.Shutdown();
    }

    void loggingServiceConflictRollsBackConsole()
    {
        struct ExistingLog final : IEditorLog {
            void Log(ConsoleMessageType, const QString&) override {}
        } existing;
        Host host;
        QVERIFY(host.context.RegisterService<IEditorLog>(existing));
        ConsoleModule module;
        ExpectDuplicateService();
        module.Initialize(host.context);
        QVERIFY(!module.Dock() && !host.context.GetService<EditorConsole>());
        QCOMPARE(host.context.GetService<IEditorLog>(), &existing);
        QVERIFY(host.window.findChildren<QDockWidget*>().isEmpty());
        QVERIFY(host.view->actions().isEmpty() && host.modules->actions().isEmpty());
        module.Shutdown();
        QCOMPARE(host.context.GetService<IEditorLog>(), &existing);
        host.context.UnregisterService<IEditorLog>(&existing);
    }

    void consoleShutdownKeepsReplacementService()
    {
        Host host;
        ConsoleModule module;
        module.Initialize(host.context);
        delete module.Dock();
        EditorConsole replacement;
        QVERIFY(host.context.RegisterService(replacement));
        module.Shutdown();
        QCOMPARE(host.context.GetService<EditorConsole>(), &replacement);
        host.context.UnregisterService(&replacement);
    }

    void scriptCanvasRepeatedAndExternalDeletion()
    {
        Host host;
        ScriptCanvasModule module;
        module.Initialize(host.context);
        auto* dock = module.Dock();
        QVERIFY(dock && dock->isHidden());
        QCOMPARE(dock->objectName(), QString("ScriptCanvasDock"));
        module.Initialize(host.context);
        QCOMPARE(module.Dock(), dock);
        QCOMPARE(host.modules->actions().size(), 1);
        delete dock;
        QVERIFY(!module.Dock());
        QVERIFY(host.view->actions().isEmpty() && host.modules->actions().isEmpty());
        module.Shutdown(); module.Shutdown();
        module.Initialize(host.context);
        QVERIFY(module.Dock());
    }

    void scriptCanvasDockConflict()
    {
        Host host;
        auto* existing = new QDockWidget;
        QVERIFY(host.context.RegisterDock("ScriptCanvasDock", existing, Qt::BottomDockWidgetArea));
        ScriptCanvasModule module;
        ExpectDuplicateDock();
        module.Initialize(host.context);
        QVERIFY(!module.Dock());
        QCOMPARE(host.window.findChildren<QDockWidget*>().size(), 1);
        QCOMPARE(host.view->actions().size(), 1);
        QVERIFY(host.modules->actions().isEmpty());
        module.Shutdown();
        QCOMPARE(host.window.findChild<QDockWidget*>("ScriptCanvasDock"), existing);
    }
};

QTEST_MAIN(ModuleLifecycleTests)
#include "ModuleLifecycleTests.moc"
