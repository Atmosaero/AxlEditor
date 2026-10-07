#include "Modules/Console/ConsoleModule.h"
#include "Editor/EditorContext.h"
#include "Modules/Console/EditorConsole.h"
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <cmath>
#include <stdexcept>

namespace {
class ConsoleInput final : public QLineEdit
{
public:
    using QLineEdit::QLineEdit;
    QString TakeCommand() {
        const auto command = text();
        if (!command.trimmed().isEmpty() && (history_.isEmpty() || history_.back() != command)) {
            history_.push_back(command);
            if (history_.size() > 100) history_.removeFirst();
        }
        index_ = history_.size();
        draft_.clear();
        clear();
        return command;
    }
protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
            if (index_ == history_.size()) draft_ = text();
            if (event->key() == Qt::Key_Up && index_ > 0) --index_;
            if (event->key() == Qt::Key_Down && index_ < history_.size()) ++index_;
            setText(index_ < history_.size() ? history_[index_] : draft_);
            event->accept();
        } else QLineEdit::keyPressEvent(event);
    }
private:
    QStringList history_;
    qsizetype index_ = 0;
    QString draft_;
};

void Require(bool condition, const char* usage)
{
    if (!condition) throw std::invalid_argument(usage);
}
}

void ConsoleModule::Initialize(EditorContext& context)
{
    if (context_) {
        if (dock_) return;
        Shutdown();
    }
    auto* dock = new QDockWidget("Console");
    auto* panel = new QWidget;
    panel->setObjectName("ConsolePanel");
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    auto* output = new EditorConsole(panel);
    layout->addWidget(output, 1);
    auto* inputRow = new QHBoxLayout;
    inputRow->setSpacing(6);
    inputRow->addWidget(new QLabel(">", panel));
    auto* input = new ConsoleInput(panel);
    input->setObjectName("ConsoleInput");
    input->setPlaceholderText("Enter command (help)");
    input->setClearButtonEnabled(true);
    inputRow->addWidget(input, 1);
    layout->addLayout(inputRow);
    dock->setWidget(panel);
    auto* commands = new ConsoleCommands(dock);
    if (!context.RegisterDock("ConsoleDock", dock, Qt::BottomDockWidgetArea)) {
        delete dock;
        return;
    }
    if (!context.RegisterService<EditorConsole>(*output)) {
        delete dock; // Roll back the dock and its View action; retain the existing service.
        return;
    }
    if (!context.RegisterService<IEditorLog>(*this)) {
        context.UnregisterService<EditorConsole>(output);
        delete dock;
        return;
    }
    if (!context.RegisterService<ConsoleCommands>(*commands)) {
        context.UnregisterService<IEditorLog>(this);
        context.UnregisterService<EditorConsole>(output);
        delete dock;
        return;
    }
    context_ = &context;
    dock_ = dock;
    output_ = output;
    commands_ = commands;
    registrations_.push_back(commands->RegisterCommand("help", "List available commands.",
        [this](const QStringList& args, IEditorLog& log) {
            Require(args.isEmpty(), "Usage: help");
            for (const auto& command : commands_->GetCommands())
                log.Log(ConsoleMessageType::Info, command.name + " - " + command.description);
        }));
    registrations_.push_back(commands->RegisterCommand("echo", "Print text. Usage: echo <text>",
        [](const QStringList& args, IEditorLog& log) { log.Log(ConsoleMessageType::Info, args.join(' ')); }));
    registrations_.push_back(commands->RegisterCommand("add", "Add two numbers. Usage: add <a> <b>",
        [](const QStringList& args, IEditorLog& log) {
            Require(args.size() == 2, "Usage: add <a> <b>");
            bool first = false, second = false;
            const double a = args[0].toDouble(&first), b = args[1].toDouble(&second);
            Require(first && second && std::isfinite(a) && std::isfinite(b) && std::isfinite(a + b),
                "Usage: add <a> <b> (finite numbers)");
            log.Log(ConsoleMessageType::Success, QString::number(a + b, 'g', 15));
        }));
    registrations_.push_back(commands->RegisterCommand("log", "Test message types. Usage: log <info|warning|error|success> <text>",
        [](const QStringList& args, IEditorLog& log) {
            Require(args.size() >= 2, "Usage: log <info|warning|error|success> <text>");
            const auto severity = args[0].toLower();
            ConsoleMessageType type;
            if (severity == "info") type = ConsoleMessageType::Info;
            else if (severity == "warning") type = ConsoleMessageType::Warning;
            else if (severity == "error") type = ConsoleMessageType::Error;
            else if (severity == "success") type = ConsoleMessageType::Success;
            else throw std::invalid_argument("Usage: log <info|warning|error|success> <text>");
            log.Log(type, args.mid(1).join(' '));
        }));
    registrations_.push_back(commands->RegisterCommand("clear", "Clear console output.",
        [this](const QStringList& args, IEditorLog&) {
            Require(args.isEmpty(), "Usage: clear");
            if (output_) output_->clear();
        }));
    QObject::connect(input, &QLineEdit::returnPressed, panel, [this, input] {
        const auto line = input->TakeCommand();
        if (line.trimmed().isEmpty() || !commands_) return;
        Log(ConsoleMessageType::Info, "> " + line);
        commands_->Execute(line, *this);
    });
    output->Log(ConsoleMessageType::Success, "Axl Editor ready.");
    auto* clear = new QAction("Clear Console", dock);
    clear->setObjectName("ClearConsoleAction");
    QObject::connect(clear, &QAction::triggered, output, &QPlainTextEdit::clear);
    context.RegisterAction(clear);
}

void ConsoleModule::Shutdown()
{
    if (!context_) return;
    context_->UnregisterService<IEditorLog>(this);
    if (commands_) context_->UnregisterService<ConsoleCommands>(commands_.data());
    else if (!context_->GetService<ConsoleCommands>()) context_->UnregisterService<ConsoleCommands>();
    registrations_.clear();
    if (output_) context_->UnregisterService<EditorConsole>(output_.data());
    else if (!context_->GetService<EditorConsole>()) context_->UnregisterService<EditorConsole>();
    delete dock_.data(); // Qt also deletes its output and actions; QPointer may already be null.
    dock_ = nullptr;
    output_ = nullptr;
    commands_ = nullptr;
    context_ = nullptr;
}

QDockWidget* ConsoleModule::Dock() const { return dock_.data(); }

void ConsoleModule::Log(ConsoleMessageType type, const QString& message)
{
    if (output_) output_->Log(type, message);
}

void ConsoleModule::Show()
{
    if (dock_) dock_->raise();
}
