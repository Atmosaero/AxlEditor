#include "Editor/Qt/ConsoleCommands.h"
#include "Editor/Qt/EditorContext.h"
#include "Tools/Console/ConsoleTool.h"
#include "Tools/Console/EditorConsole.h"
#include <QLineEdit>
#include <QMenuBar>
#include <QRegularExpression>
#include <QtTest>
#include <stdexcept>

namespace {
struct Output final : IEditorLog {
    std::vector<std::pair<ConsoleMessageType, QString>> messages;
    void Log(ConsoleMessageType type, const QString& message) override { messages.emplace_back(type, message); }
};
struct Host {
    QMainWindow window;
    QMenu* view = window.menuBar()->addMenu("View");
    QMenu* tools = window.menuBar()->addMenu("Tools");
    EditorContext context{window, *view, *tools};
};
// An ordinary C++ class can register without inheriting QObject or owning UI.
struct CustomCommand {
    int calls = 0;
    ConsoleCommands::Registration registration;
    explicit CustomCommand(ConsoleCommands& commands) {
        registration = commands.RegisterCommand("custom.ping", "custom.ping - user command",
            [this](const QStringList& args, IEditorLog& output) {
                ++calls;
                output.Log(ConsoleMessageType::Success, args.join('|'));
            });
    }
};
void Submit(QLineEdit& input, const QString& text) {
    input.setText(text);
    QTest::keyClick(&input, Qt::Key_Return);
}
}

class ConsoleCommandTests final : public QObject
{
    Q_OBJECT
private slots:
    void registrationValidationAndOwnerLifetime() {
        ConsoleCommands commands;
        Output output;
        QVERIFY(!commands.RegisterCommand("", "", [](const auto&, auto&) {}));
        QVERIFY(!commands.RegisterCommand("1bad", "", [](const auto&, auto&) {}));
        QVERIFY(!commands.RegisterCommand("bad name", "", [](const auto&, auto&) {}));
        QVERIFY(!commands.RegisterCommand("valid", "", {}));
        {
            CustomCommand owner(commands);
            QVERIFY(owner.registration);
            QVERIFY(!commands.RegisterCommand("CUSTOM.PING", "duplicate", [](const auto&, auto&) {}));
            QVERIFY(commands.Execute("CUSTOM.PING one two", output));
            QCOMPARE(owner.calls, 1);
            QCOMPARE(output.messages.back().second, QString("one|two"));
            QCOMPARE(commands.GetCommands().front().description, QString("custom.ping - user command"));
        }
        QVERIFY(commands.GetCommands().empty());
        QVERIFY(!commands.Execute("custom.ping", output));
        QVERIFY(output.messages.back().second.contains("Unknown command"));
    }

    void tokenization_data() {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("whitespace") << QString("  capture\t one   two  ") << QStringList({"one", "two"});
        QTest::newRow("quotes-paths-escapes")
            << QString::fromUtf8("capture one \"two words\" '' a\"b\" 'x\\'y' C:\\Assets\\file.lua escaped\\ space tail\\")
            << QStringList({"one", "two words", "", "ab", "x'y", "C:\\Assets\\file.lua", "escaped space", "tail\\"});
        QTest::newRow("unicode") << QString::fromUtf8(u8"capture \"Привет мир\" \"\" ")
            << QStringList({QString::fromUtf8(u8"Привет мир"), ""});
        QTest::newRow("escaped-double-quote") << QString::fromUtf8("capture \"a\\\"b\" \"c\\\\d\"")
            << QStringList({"a\"b", "c\\d"});
    }
    void tokenization() {
        QFETCH(QString, line);
        QFETCH(QStringList, expected);
        ConsoleCommands commands;
        Output output;
        QStringList arguments;
        auto registration = commands.RegisterCommand("capture", "", [&](const auto& args, auto&) { arguments = args; });
        QVERIFY(commands.Execute(line, output));
        QCOMPARE(arguments, expected);
        QVERIFY(output.messages.empty());
    }

    void errorsDoNotExecuteAndExceptionsDoNotEscape() {
        ConsoleCommands commands;
        Output output;
        int calls = 0;
        auto valid = commands.RegisterCommand("ok", "", [&](const auto&, auto&) { ++calls; });
        auto failure = commands.RegisterCommand("fail", "", [](const auto&, auto&) { throw std::runtime_error("test failure"); });
        auto unknown = commands.RegisterCommand("throw", "", [](const auto&, auto&) { throw 42; });
        QVERIFY(!commands.Execute(" \t ", output));
        QVERIFY(output.messages.empty());
        QVERIFY(!commands.Execute("ok \"unfinished", output));
        QCOMPARE(calls, 0);
        QCOMPARE(output.messages.back().first, ConsoleMessageType::Error);
        QVERIFY(!commands.Execute("missing", output));
        QVERIFY(!commands.Execute("fail", output));
        QVERIFY(output.messages.back().second.contains("test failure"));
        QVERIFY(!commands.Execute("throw", output));
        QVERIFY(commands.Execute("ok", output));
        QCOMPARE(calls, 1);
    }

    void movingResettingAndRegistryDestruction() {
        Output output;
        auto commands = std::make_unique<ConsoleCommands>();
        auto first = commands->RegisterCommand("ping", "", [](const auto&, auto&) {});
        auto moved = std::move(first);
        QVERIFY(!first && moved);
        first.Reset();
        QVERIFY(commands->Execute("ping", output));
        auto other = commands->RegisterCommand("other", "", [](const auto&, auto&) {});
        other = std::move(moved); // Unregisters its previous command.
        QVERIFY(!moved && other);
        QVERIFY(!commands->Execute("other", output));
        other.Reset();
        auto replacement = commands->RegisterCommand("ping", "replacement", [](const auto&, auto&) {});
        moved.Reset();
        QVERIFY(commands->Execute("ping", output));
        commands.reset();
        QVERIFY(!replacement);
        replacement.Reset(); // Safe when the console dock has already disappeared.
    }

    void callbackCanUnregisterItselfAndRegisterAnotherCommand() {
        ConsoleCommands commands;
        Output output;
        ConsoleCommands::Registration self, next;
        self = commands.RegisterCommand("once", "", [&](const auto&, auto&) {
            self.Reset();
            next = commands.RegisterCommand("next", "", [](const auto&, auto& log) {
                log.Log(ConsoleMessageType::Success, "next called");
            });
        });
        QVERIFY(commands.Execute("once", output));
        QVERIFY(!commands.Execute("once", output));
        QVERIFY(commands.Execute("next", output));
        QCOMPARE(output.messages.back().second, QString("next called"));
    }

    void mutableCallbackStatePersistsBetweenExecutions() {
        ConsoleCommands commands;
        Output output;
        auto registration = commands.RegisterCommand("count", "", [count = 0](const auto&, auto& log) mutable {
            log.Log(ConsoleMessageType::Info, QString::number(++count));
        });
        QVERIFY(commands.Execute("count", output));
        QVERIFY(commands.Execute("count", output));
        QCOMPARE(output.messages[0].second, QString("1"));
        QCOMPARE(output.messages[1].second, QString("2"));
    }

    void enterRunsBuiltinsAndExternalCommands() {
        Host host;
        ConsoleTool tool;
        tool.Initialize(host.context);
        auto* commands = host.context.GetService<ConsoleCommands>();
        auto* input = host.window.findChild<QLineEdit*>("ConsoleInput");
        auto* output = host.context.GetService<EditorConsole>();
        QVERIFY(commands && input && output);
        host.window.resize(640, 480);
        host.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host.window));
        tool.Show();
        input->setFocus();
        Submit(*input, "help");
        QVERIFY(input->text().isEmpty());
        QVERIFY(output->toPlainText().contains("add <a> <b>"));
        Submit(*input, "echo \"hello world\"");
        QVERIFY(output->toPlainText().contains("[Info] hello world"));
        Submit(*input, "add 2.5 -1");
        QVERIFY(output->toPlainText().contains("[Success] 1.5"));
        Submit(*input, "log warning caution");
        QVERIFY(output->toPlainText().contains("[Warning] caution"));
        Submit(*input, "log error problem");
        QVERIFY(output->toPlainText().contains("[Error] problem"));
        Submit(*input, "log success done");
        QVERIFY(output->toPlainText().contains("[Success] done"));
        Submit(*input, "add bad 2");
        QVERIFY(output->toPlainText().contains("[Error] Command add failed"));
        Submit(*input, "add 1e308 1e308");
        QVERIFY(!output->toPlainText().contains("[Success] inf"));
        CustomCommand owner(*commands);
        Submit(*input, "custom.ping external");
        QCOMPARE(owner.calls, 1);
        Submit(*input, "help");
        QVERIFY(output->toPlainText().contains("custom.ping - user command"));
        Submit(*input, "clear");
        QVERIFY(output->toPlainText().isEmpty());
        Submit(*input, "nonexistent");
        QVERIFY(output->toPlainText().contains("[Error] Unknown command: nonexistent"));
        tool.Shutdown();
        QVERIFY(!owner.registration && !host.context.GetService<ConsoleCommands>());
    }

    void historyRestoresDraftAndClearLeavesInputUsable() {
        Host host;
        ConsoleTool tool;
        tool.Initialize(host.context);
        auto* input = host.window.findChild<QLineEdit*>("ConsoleInput");
        Submit(*input, "echo first");
        Submit(*input, "echo second");
        input->setText("draft");
        QTest::keyClick(input, Qt::Key_Up);
        QCOMPARE(input->text(), QString("echo second"));
        QTest::keyClick(input, Qt::Key_Up);
        QCOMPARE(input->text(), QString("echo first"));
        QTest::keyClick(input, Qt::Key_Up);
        QCOMPARE(input->text(), QString("echo first"));
        QTest::keyClick(input, Qt::Key_Down);
        QTest::keyClick(input, Qt::Key_Down);
        QCOMPARE(input->text(), QString("draft"));
        Submit(*input, "clear");
        Submit(*input, "echo after clear");
        QVERIFY(host.context.GetService<EditorConsole>()->toPlainText().contains("after clear"));
    }

    void serviceConflictAndDeletedDockAreSafe() {
        Host host;
        ConsoleCommands existing;
        QVERIFY(host.context.RegisterService(existing));
        ConsoleTool tool;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("^Duplicate editor service:.*"));
        tool.Initialize(host.context);
        QVERIFY(!tool.Dock());
        QVERIFY(!host.context.GetService<IEditorLog>() && !host.context.GetService<EditorConsole>());
        QCOMPARE(host.context.GetService<ConsoleCommands>(), &existing);
        QVERIFY(host.view->actions().isEmpty() && host.tools->actions().isEmpty());
        host.context.UnregisterService(&existing);
        tool.Initialize(host.context);
        auto* commands = host.context.GetService<ConsoleCommands>();
        QVERIFY(commands);
        auto command = commands->RegisterCommand("external", "", [](const auto&, auto&) {});
        delete tool.Dock();
        QVERIFY(!command && !host.context.GetService<ConsoleCommands>());
        QVERIFY(host.context.RegisterService(existing));
        tool.Shutdown();
        QCOMPARE(host.context.GetService<ConsoleCommands>(), &existing);
        host.context.UnregisterService(&existing);
        tool.Initialize(host.context);
        QCOMPARE(host.context.GetService<ConsoleCommands>()->GetCommands().size(), size_t(5));
        tool.Shutdown();
    }
};

QTEST_MAIN(ConsoleCommandTests)
#include "ConsoleCommandTests.moc"
