#include "Tools/PythonConsole/PythonConsoleTool.h"
#include "Editor/Qt/EditorContext.h"
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>
#include <QToolBar>
#include <QSplitter>
#include <QVBoxLayout>
#include <QTextCursor>
#include <QCoreApplication>
#include <QFontDatabase>

PythonConsoleTool::PythonConsoleTool(QString interpreter) : interpreter_(std::move(interpreter))
{
#ifdef Q_OS_WIN
    process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= 0x08000000; }); // CREATE_NO_WINDOW
#endif
    connect(&process_, &QProcess::readyReadStandardOutput, this, &PythonConsoleTool::ReadOutput);
    connect(&process_, &QProcess::readyReadStandardError, this, [this] { Append(QString::fromUtf8(process_.readAllStandardError())); });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (stopping_) return;
        Append("Python process: " + process_.errorString() + "\n");
        if (busy_) Finish(false);
    });
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        ReadOutput(); ready_ = false;
        if (busy_) { Append(QString("Python exited (%1). Run again to start a new session.\n").arg(code)); Finish(false); }
    });
}
void PythonConsoleTool::Initialize(EditorContext& context)
{
    if (dock_) return;
    Shutdown();
    auto* dock = new QDockWidget("Python Console");
    dock->setProperty("keepDockTitleBar", true);
    auto* panel = new QWidget(dock); auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(6, 6, 6, 6); layout->setSpacing(6);
    auto* toolbar = new QToolBar(panel); toolbar->setObjectName("PythonConsoleToolbar");
    run_ = toolbar->addAction("Run"); run_->setObjectName("RunPythonAction");
    run_->setShortcut(QKeySequence("Ctrl+Return")); panel->addAction(run_);
    stop_ = toolbar->addAction("Stop"); stop_->setObjectName("StopPythonAction");
    auto* file = toolbar->addAction("Run File..."); file->setObjectName("RunPythonFileAction");
    auto* reset = toolbar->addAction("Reset Session"); reset->setObjectName("ResetPythonSessionAction");
    auto* clear = toolbar->addAction("Clear Output");
    interpreterAction_ = toolbar->addAction("Interpreter...");
    layout->addWidget(toolbar);
    history_ = new QComboBox(panel); history_->setObjectName("PythonHistory"); history_->setPlaceholderText("Recent scripts");
    layout->addWidget(history_);
    auto* splitter = new QSplitter(Qt::Vertical, panel);
    output_ = new QPlainTextEdit(splitter); output_->setObjectName("PythonOutput");
    output_->setReadOnly(true); output_->setMaximumBlockCount(5000);
    input_ = new QPlainTextEdit(splitter); input_->setObjectName("PythonInput");
    input_->setPlaceholderText("Python code (Ctrl+Enter to run)\naxl.commands()\nobj = axl.create('Cube')\naxl.set_position(obj, 1, 2, 3)");
    const auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    input_->setFont(font); output_->setFont(font);
    input_->setTabStopDistance(QFontMetricsF(font).horizontalAdvance(' ') * 4);
    splitter->setStretchFactor(0, 2); splitter->setStretchFactor(1, 1); layout->addWidget(splitter, 1);
    dock->setWidget(panel);
    if (!context.RegisterDock("PythonConsoleDock", dock, Qt::BottomDockWidgetArea)) { delete dock; return; }
    auto* commands = new PythonCommands(dock);
    if (!context.RegisterService<PythonCommands>(*commands)) { delete dock; return; }
    context_ = &context; dock_ = dock; commands_ = commands;
    dock->hide(); dock->setFloating(true); dock->resize(850, 650);
    if (interpreter_.isEmpty()) {
        interpreter_ = qEnvironmentVariable("AXL_PYTHON");
        if (interpreter_.isEmpty()) interpreter_ = QSettings(QCoreApplication::applicationDirPath() + "/python-console.ini", QSettings::IniFormat).value("interpreter").toString();
        if (interpreter_.isEmpty()) interpreter_ = QStandardPaths::findExecutable("python");
        if (interpreter_.isEmpty()) interpreter_ = QStandardPaths::findExecutable("python3");
    }
    connect(dock, &QObject::destroyed, this, [this, commands] {
        Stop(); if (context_) context_->UnregisterService<PythonCommands>(commands);
        context_ = nullptr;
    });
    connect(run_, &QAction::triggered, panel, [this] { Execute(input_->toPlainText()); });
    connect(stop_, &QAction::triggered, panel, [this] { Stop(); });
    connect(clear, &QAction::triggered, panel, [this] { output_->clear(); });
    connect(reset, &QAction::triggered, panel, [this] { Stop(); Append("Python namespace reset.\n"); });
    connect(history_, &QComboBox::activated, panel, [this](int index) { input_->setPlainText(history_->itemData(index).toString()); });
    connect(file, &QAction::triggered, panel, [this] {
        if (busy_) { Append("A script is already running.\n"); return; }
        const auto path = QFileDialog::getOpenFileName(dock_, "Run Python File", {}, "Python (*.py)");
        if (path.isEmpty()) return;
        QFile source(path);
        if (!source.open(QIODevice::ReadOnly)) { Append(source.errorString() + "\n"); return; }
        Execute(QString::fromUtf8(source.readAll()), path);
    });
    connect(interpreterAction_, &QAction::triggered, panel, [this] {
        const auto path = QFileDialog::getOpenFileName(dock_, "Python Interpreter", interpreter_);
        if (path.isEmpty()) return;
        Stop(); interpreter_ = path;
        QSettings(QCoreApplication::applicationDirPath() + "/python-console.ini", QSettings::IniFormat).setValue("interpreter", path);
        Append("Interpreter: " + path + "\n");
    });
    auto* open = new QAction("Python Console", dock); open->setObjectName("OpenPythonConsoleAction");
    connect(open, &QAction::triggered, dock, [this] { dock_->show(); dock_->raise(); input_->setFocus(); });
    context.RegisterAction(open);
    Append("Editor automation. Run axl.commands() to list the API.\n"); UpdateControls();
}
bool PythonConsoleTool::Execute(const QString& source, const QString& filename)
{
    if (!dock_ || !commands_ || busy_ || source.trimmed().isEmpty()) return false;
    if (interpreter_.isEmpty()) { Append("Python was not found. Choose Interpreter... or set AXL_PYTHON.\n"); return false; }
    if (!ready_ && process_.state() != QProcess::NotRunning) Stop();
    source_ = source; filename_ = filename; busy_ = true; ++runId_;
    Append("\n>>> " + source + "\n");
    if (history_) {
        history_->insertItem(0, source.section('\n', 0, 0).left(80), source);
        if (history_->count() > 30) history_->removeItem(30);
    }
    UpdateControls();
    if (ready_ && process_.state() == QProcess::Running) SendExecution();
    else {
        QFile worker(":/python-console/worker.py");
        if (!worker.open(QIODevice::ReadOnly)) { Append("Python worker resource is unavailable.\n"); Finish(false); return false; }
        ready_ = false; buffer_.clear();
        process_.start(interpreter_, {"-X", "utf8", "-u", "-c", QString::fromUtf8(worker.readAll())});
    }
    return true;
}
void PythonConsoleTool::SendExecution()
{
    Send({{"type", "execute"}, {"run", runId_}, {"source", source_}, {"filename", filename_}, {"commands", commands_->GetCommands()}});
}
void PythonConsoleTool::Send(const QJsonObject& message)
{
    if (process_.state() == QProcess::Running) process_.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}
void PythonConsoleTool::ReadOutput()
{
    buffer_ += process_.readAllStandardOutput();
    qsizetype newline;
    while ((newline = buffer_.indexOf('\n')) >= 0) {
        const auto line = buffer_.left(newline); buffer_.remove(0, newline + 1);
        QJsonParseError parse; const auto document = QJsonDocument::fromJson(line, &parse);
        if (parse.error || !document.isObject()) { Append(QString::fromUtf8(line) + "\n"); continue; }
        const auto message = document.object(); const auto type = message["type"].toString();
        if (type == "ready") { ready_ = true; Append("Python " + message["version"].toString() + "\n"); if (busy_) SendExecution(); }
        else if (type == "output") Append(message["text"].toString());
        else if (type == "done" && message["run"].toInt() == runId_ && busy_) Finish(message["ok"].toBool());
        else if (type == "call" && message["run"].toInt() == runId_ && busy_ && commands_) {
            const auto requestRun = runId_;
            QJsonObject reply{{"type", "reply"}, {"id", message["id"]}};
            try {
                reply["value"] = commands_->Invoke(message["name"].toString(), message["args"].toArray(), message["kwargs"].toObject());
                reply["ok"] = true;
            } catch (const std::exception& error) { reply["ok"] = false; reply["error"] = QString::fromUtf8(error.what()); }
            catch (...) { reply["ok"] = false; reply["error"] = "Editor command failed."; }
            if (busy_ && runId_ == requestRun) Send(reply);
        }
    }
}
void PythonConsoleTool::Finish(bool success)
{
    if (!busy_) return;
    busy_ = false; UpdateControls(); emit ExecutionFinished(success);
}
void PythonConsoleTool::Stop()
{
    const bool wasBusy = busy_; busy_ = false; ready_ = false;
    stopping_ = true;
    process_.kill(); if (process_.state() != QProcess::NotRunning) process_.waitForFinished(2000);
    stopping_ = false;
    buffer_.clear(); UpdateControls();
    if (wasBusy) { Append("Python execution stopped.\n"); emit ExecutionFinished(false); }
}
void PythonConsoleTool::Append(const QString& text)
{
    if (!output_) return;
    auto cursor = output_->textCursor(); cursor.movePosition(QTextCursor::End); cursor.insertText(text);
    output_->setTextCursor(cursor); output_->ensureCursorVisible();
}
void PythonConsoleTool::UpdateControls()
{
    if (run_) run_->setEnabled(!busy_); if (stop_) stop_->setEnabled(busy_);
    if (interpreterAction_) interpreterAction_->setEnabled(!busy_);
}
void PythonConsoleTool::Shutdown()
{
    Stop();
    if (context_) context_->UnregisterService<PythonCommands>(commands_.data());
    if (dock_) disconnect(dock_, nullptr, this, nullptr);
    delete dock_.data(); dock_ = nullptr; commands_ = nullptr; context_ = nullptr;
}
